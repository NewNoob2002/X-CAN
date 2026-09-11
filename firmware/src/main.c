/* SPDX-License-Identifier: Apache-2.0 */
#include <soc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

static atomic_t termination = ATOMIC_INIT(-1);
int xcan_termination_get(void) { return (int)atomic_get(&termination); }
#ifdef CONFIG_XCAN_USB
int xcan_usb_init(void);
#endif

#define USER DT_PATH(zephyr_user)
static const struct gpio_dt_spec stb = GPIO_DT_SPEC_GET(USER, stb_gpios);
static const struct gpio_dt_spec arm = GPIO_DT_SPEC_GET(USER, arm_gpios);
static const struct gpio_dt_spec injection =
    GPIO_DT_SPEC_GET(USER, injection_gpios);
static const struct gpio_dt_spec tx = GPIO_DT_SPEC_GET(USER, can_tx_gpios);
static const struct gpio_dt_spec term =
    GPIO_DT_SPEC_GET(USER, termination_gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void configure(const struct gpio_dt_spec *pin, gpio_flags_t flags) {
  if (!gpio_is_ready_dt(pin) || gpio_pin_configure_dt(pin, flags) != 0) {
    printk("FATAL gpio init\n");
    k_panic();
  }
}

int main(void) {
  /* Configure the output latch before enabling each output. */
  configure(&arm, GPIO_OUTPUT_INACTIVE);
  configure(&stb, GPIO_OUTPUT_ACTIVE);
  configure(&injection, GPIO_OUTPUT_INACTIVE);
  configure(&tx, GPIO_OUTPUT_ACTIVE);
  configure(&term,
            GPIO_INPUT); /* External R14 pull-down, manual declaration. */
  configure(&led, GPIO_OUTPUT_INACTIVE);
  printk("X-CAN bringup sysclk=%u idcode=%08x optr=%08x SAFE\n",
         SystemCoreClock, DBGMCU->IDCODE, FLASH->OPTR);
#ifdef CONFIG_XCAN_USB
  int usb_err = xcan_usb_init();
  printk("USB init=%d vid=%04x pid=%04x\n", usb_err, CONFIG_XCAN_USB_VID,
         CONFIG_XCAN_USB_PID);
  if (usb_err != 0) {
    k_panic();
  }
#endif

  int stable = -1, candidate = -1;
  unsigned int samples = 0;
  int64_t heartbeat = 0;
  while (true) {
    int value = gpio_pin_get_dt(&term);
    if (value < 0) {
      printk("FATAL termination read=%d\n", value);
      k_panic();
    }
    if (value != candidate) {
      candidate = value;
      samples = 1;
    } else if (samples < 4) {
      samples++;
    }
    if (samples == 4 && stable != candidate) {
      stable = candidate;
      atomic_set(&termination, stable);
      printk("termination_declared=%d (manual switch)\n", stable);
    }
    if (k_uptime_get() >= heartbeat) {
      printk("alive ms=%lld termination_declared=%d SAFE\n",
             (long long)k_uptime_get(), stable);
      if (gpio_pin_toggle_dt(&led) != 0) {
        k_panic();
      }
      heartbeat = k_uptime_get() + 1000;
    }
    k_msleep(10);
  }
  return 0;
}
