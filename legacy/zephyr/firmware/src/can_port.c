#include "can_port.h"
#include <errno.h>
#include <string.h>
#include <soc.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

static const struct device *const can = DEVICE_DT_GET(DT_NODELABEL(fdcan1));
static const struct gpio_dt_spec stb = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), stb_gpios);
static const struct gpio_dt_spec arm = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), arm_gpios);
K_MSGQ_DEFINE(rx_queue, sizeof(struct xcan_v2_can_frame), 32, 4);
static struct k_spinlock lock;
static atomic_t running;
static struct xcan_can_status counters;
static uint32_t generation, rx_sequence, last_overrun, tx_token;
static bool fd_enabled, config_valid, tx_pending, tx_ready, forced_timeout;
static struct xcan_tx_result tx_result;
static uint64_t tx_deadline;
extern int xcan_termination_get(void);

static uint64_t add(uint64_t a, uint64_t b) { return UINT64_MAX-a < b ? UINT64_MAX : a+b; }
static uint64_t now_us(void) { return k_ticks_to_us_floor64(k_uptime_ticks()); }

void xcan_can_gate_off(void)
{
    /* STM32 GPIO operations are nonblocking and available from USB callbacks. */
    (void)gpio_pin_set_dt(&arm, 0);
    (void)gpio_pin_set_dt(&stb, 1);
}
static void rx_callback(const struct device *dev, struct can_frame *frame, void *arg)
{
    ARG_UNUSED(dev); ARG_UNUSED(arg);
    if (!atomic_get(&running)) { return; }
    struct xcan_v2_can_frame f = { .id = frame->id, .flags = frame->flags,
        .dlc = frame->dlc, .timestamp_us = now_us() };
    f.length = (frame->flags & CAN_FRAME_RTR) ? 0 : can_dlc_to_bytes(frame->dlc);
    k_spinlock_key_t key = k_spin_lock(&lock);
    f.sequence = rx_sequence++;
    counters.received = add(counters.received, 1);
    k_spin_unlock(&lock, key);
    /* Noncanonical Classical DLC values cannot be silently normalized. */
    if ((!(frame->flags & CAN_FRAME_FDF) && frame->dlc > 8) || f.length > 64) { goto dropped; }
    memcpy(f.data, frame->data, f.length);
    if (k_msgq_put(&rx_queue, &f, K_NO_WAIT) == 0) { return; }
dropped:
    key = k_spin_lock(&lock); counters.dropped = add(counters.dropped, 1); k_spin_unlock(&lock, key);
}
static void tx_callback(const struct device *dev, int err, void *arg)
{
    ARG_UNUSED(dev);
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (tx_pending && !tx_ready && (uint32_t)(uintptr_t)arg == tx_token) {
        tx_result.result = err == 0 ? 0 : err == -ENETUNREACH ? 2 :
                           err == -ENETDOWN && !forced_timeout ? 1 : 3;
        tx_result.timestamp = now_us(); tx_ready = true;
    }
    k_spin_unlock(&lock, key);
}
static void sample_overruns(void)
{
    uint32_t value = can_stats_get_rx_overruns(can);
    counters.overruns = add(counters.overruns, (uint32_t)(value-last_overrun));
    last_overrun = value;
}
int xcan_can_init(void)
{
    if (!device_is_ready(can)) { return -ENODEV; }
    can_mode_t caps;
    int err = can_get_capabilities(can, &caps);
    if (err || (caps & (CAN_MODE_FD | CAN_MODE_ONE_SHOT | CAN_MODE_MANUAL_RECOVERY)) !=
                       (CAN_MODE_FD | CAN_MODE_ONE_SHOT | CAN_MODE_MANUAL_RECOVERY)) { return -ENOTSUP; }
    const struct can_filter std = {.id = 0, .mask = 0, .flags = 0};
    const struct can_filter ext = {.id = 0, .mask = 0, .flags = CAN_FILTER_IDE};
    int first = can_add_rx_filter(can, rx_callback, NULL, &std);
    if (first < 0) { return first; }
    int second = can_add_rx_filter(can, rx_callback, NULL, &ext);
    if (second < 0) { can_remove_rx_filter(can, first); return second; }
    return 0;
}
int xcan_can_stop(void)
{
    xcan_can_gate_off(); atomic_clear(&running);
    int err = can_stop(can);
    sample_overruns();
    struct xcan_v2_can_frame f;
    uint32_t discarded = 0;
    while (k_msgq_get(&rx_queue, &f, K_NO_WAIT) == 0) { discarded++; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    counters.dropped = add(counters.dropped, discarded);
    k_spin_unlock(&lock, key);
    return err == -EALREADY ? 0 : err;
}
void xcan_can_reset(uint32_t new_generation)
{
    int err = xcan_can_stop();
    if (err) { k_panic(); } /* Gate remains off; cannot safely reuse a failed controller. */
    k_spinlock_key_t key = k_spin_lock(&lock);
    memset(&counters, 0, sizeof(counters));
    rx_sequence = 0; tx_pending = tx_ready = false; forced_timeout = false;
    k_spin_unlock(&lock, key);
    generation = new_generation; config_valid = fd_enabled = false;
    last_overrun = can_stats_get_rx_overruns(can);
}
int xcan_can_configure(uint32_t nominal, uint32_t data, uint8_t mode, bool fd)
{
    if (atomic_get(&running) || tx_pending) { return -EBUSY; }
    struct can_timing arbitration, fast;
    if (!nominal || mode > 1 || (fd ? !data : data != 0)) { return -EINVAL; }
    int err = can_calc_timing(can, &arbitration, nominal, 0);
    if (err < 0) { return -EINVAL; }
    if (fd && can_calc_timing_data(can, &fast, data, 0) < 0) { return -EINVAL; }
    can_mode_t flags = CAN_MODE_MANUAL_RECOVERY | CAN_MODE_ONE_SHOT;
    if (!mode) { flags |= CAN_MODE_LISTENONLY; }
    if (fd) { flags |= CAN_MODE_FD; }
    if (IS_ENABLED(CONFIG_XCAN_INTERNAL_LOOPBACK)) { flags |= CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY; }
    config_valid = false;
    err = can_set_mode(can, flags);
    if (!err) { err = can_set_timing(can, &arbitration); }
    if (!err && fd) { err = can_set_timing_data(can, &fast); }
    if (err) { return err; }
    /* The project M_CAN patch sets DAR and keeps ES0431 EFBI disabled. */
    if ((FDCAN1->CCCR & (FDCAN_CCCR_DAR | FDCAN_CCCR_EFBI)) != FDCAN_CCCR_DAR) { return -EIO; }
    counters.nominal = nominal; counters.data = data; counters.mode = mode;
    fd_enabled = fd; config_valid = true;
    return 0;
}
int xcan_can_start(void)
{
    if (!config_valid) { return -ENETDOWN; }
    if (atomic_get(&running)) { return -EALREADY; }
    if (tx_pending) { return -EBUSY; }
    if (!xcan_usb_active(generation)) { return -ENETDOWN; }
    xcan_can_gate_off();
    sample_overruns();
    int err = can_start(can);
    if (err) { return err; }
    last_overrun = 0; /* Driver resets its stats on successful start. */
    unsigned int key = irq_lock();
    if (xcan_usb_active(generation)) {
        atomic_set(&running, 1);
        if (!IS_ENABLED(CONFIG_XCAN_INTERNAL_LOOPBACK)) { err = gpio_pin_set_dt(&stb, 0); }
    } else { err = -ENETDOWN; }
    irq_unlock(key);
    if (err) { (void)xcan_can_stop(); }
    return err;
}
int xcan_can_send(uint32_t id, const struct xcan_v2_can_frame *f)
{
    if (!atomic_get(&running) || !xcan_usb_active(generation) || counters.mode != 1) { return -ENETDOWN; }
    if ((f->flags & CAN_FRAME_FDF) && !fd_enabled) { return -ENOTSUP; }
    if (tx_pending) { return -EBUSY; }
    struct can_frame frame = {.id = f->id, .flags = f->flags, .dlc = f->dlc};
    memcpy(frame.data, f->data, f->length);
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (++tx_token == 0) { ++tx_token; }
    tx_result.id = id; tx_pending = true; tx_ready = false; forced_timeout = false;
    tx_deadline = k_uptime_get() + 1000;
    k_spin_unlock(&lock, key);
    int err = can_send(can, &frame, K_NO_WAIT, tx_callback, (void *)(uintptr_t)tx_token);
    if (err) { key = k_spin_lock(&lock); tx_pending = tx_ready = false; k_spin_unlock(&lock, key); }
    return err;
}
void xcan_can_poll(uint64_t now_ms)
{
    sample_overruns();
    k_spinlock_key_t key = k_spin_lock(&lock);
    bool timed_out = tx_pending && !tx_ready && now_ms >= tx_deadline;
    if (timed_out) { forced_timeout = true; }
    k_spin_unlock(&lock, key);
    if (timed_out) { (void)xcan_can_stop(); }
}
void xcan_can_status(struct xcan_can_status *s)
{
    enum can_state state;
    k_spinlock_key_t key = k_spin_lock(&lock); *s = counters; k_spin_unlock(&lock, key);
    int term = xcan_termination_get(); s->term = term < 0 ? 255 : term;
    s->state = 0;
    if (atomic_get(&running) && can_get_state(can, &state, NULL) == 0) {
        switch (state) {
        case CAN_STATE_ERROR_ACTIVE: s->state = 1; break;
        case CAN_STATE_ERROR_WARNING: s->state = 2; break;
        case CAN_STATE_ERROR_PASSIVE: s->state = 3; break;
        case CAN_STATE_BUS_OFF: s->state = 4; break;
        default: break;
        }
    }
}
bool xcan_can_pop(struct xcan_v2_can_frame *f) { return k_msgq_get(&rx_queue, f, K_NO_WAIT) == 0; }
bool xcan_can_tx_result(struct xcan_tx_result *r)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    bool ready = tx_pending && tx_ready;
    if (ready) { *r = tx_result; }
    k_spin_unlock(&lock, key); return ready;
}
void xcan_can_tx_delivered(void)
{
    k_spinlock_key_t key = k_spin_lock(&lock); tx_pending = tx_ready = false; k_spin_unlock(&lock, key);
}
