/* SPDX-License-Identifier: Apache-2.0 */
#include "usbd_core.h"
#include "app.h"
#include "fw_update.h"
#include "protocol.h"
#include "protocol_v2.h"
#include <string.h>

static const uint8_t device[] = {18,1,0x10,2,0,0,0,64,0xca,0xc0,0x13,3,0,1,1,2,3,1};
static const uint8_t config[] = {
    9,2,32,0,1,1,4,0x80,50, 9,4,0,0,2,0xff,0,0,0,
    7,5,1,2,64,0,0, 7,5,0x81,2,64,0,0
};
static const uint8_t bos_bytes[] = {
    5,15,33,0,1, 28,16,5,0,
    0xdf,0x60,0xdd,0xd8,0x89,0x45,0xc7,0x4c,0x9c,0xd2,0x65,0x9d,0x9e,0x64,0x8a,0x9f,
    0,0,0,0x0a,162,0,2,0
};
#include "msos_bytes.h"
static const struct usb_bos_descriptor bos = {bos_bytes, sizeof(bos_bytes)};
static char serial[25];
static const uint8_t *device_desc(uint8_t speed) { return device; }
static const uint8_t *config_desc(uint8_t speed) { return config; }
static const char *string_desc(uint8_t speed, uint8_t index)
{
    static const char lang[] = {9,4};
    const char *strings[] = {lang, "X-CAN", "X-CAN bringup", serial, "Vendor USB"};
    return index < 5 ? strings[index] : NULL;
}
static const struct usb_descriptor descriptors = {
    .device_descriptor_callback = device_desc,
    .config_descriptor_callback = config_desc,
    .string_descriptor_callback = string_desc,
    .bos_descriptor = &bos,
};
static int vendor(uint8_t busid, struct usb_setup_packet *s, uint8_t **data, uint32_t *len)
{
    static const uint8_t version[] = {'X','C','A','N',2,0,0x20,0x02};
    if (s->bmRequestType != 0xc0 || s->wValue != 0) return -1;
    if (s->bRequest == 2 && s->wIndex == 7) {
        *data = (uint8_t *)msos_bytes; *len = sizeof(msos_bytes); return 0;
    }
    if (s->bRequest == 3 && s->wIndex == 0) {
        *data = (uint8_t *)version; *len = sizeof(version); return 0;
    }
    return -1;
}
static struct usbd_interface interface = {.vendor_handler = vendor};
static uint8_t packet[64] __attribute__((aligned(4)));
static uint8_t request[XCAN_V2_MAX_MESSAGE] __attribute__((aligned(4)));
static uint8_t work_request[XCAN_V2_MAX_MESSAGE] __attribute__((aligned(4)));
static uint8_t response[XCAN_V2_MAX_MESSAGE] __attribute__((aligned(4)));
static struct xcan_v2_message v2_request, v2_response;
static unsigned received, expected;
static uint32_t session_generation, session, last_request_id;
static volatile uint32_t connection_generation;
static volatile bool enabled, ready, reboot_after_tx;

static uint16_t get16(const uint8_t *p)
{ return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static void put16(uint8_t *p, uint16_t value)
{ p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); }
static void put32(uint8_t *p, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i)); }

static void arm_out(void)
{
    if (usbd_ep_start_read(0, 1, packet, sizeof(packet))) {
        enabled = false; received = expected = 0;
    }
}

static bool set_expected(void)
{
    if (expected || received < 5) return true;
    if (request[4] == 1) { expected = 64; return true; }
    if (request[4] != 2) { expected = 64; return true; }
    if (received < XCAN_V2_HEADER) return true;
    uint16_t length = get16(request + 8);
    if (length > XCAN_V2_MAX_PAYLOAD) return false;
    expected = XCAN_V2_HEADER + length;
    return true;
}

static void out_done(uint8_t busid, uint8_t ep, uint32_t n)
{
    if (!enabled || ready || !usb_device_is_configured(0)) return;
    if (n > sizeof(request) - received || (expected && n > expected - received)) {
        memset(request, 0, 64); request[4] = 1; request[5] = 0xff;
        received = expected = 64;
    } else {
        memcpy(request + received, packet, n); received += n;
        if (!set_expected()) { enabled = false; received = expected = 0; return; }
        if (!expected || received < expected) { arm_out(); return; }
        if (received != expected) { enabled = false; received = expected = 0; return; }
    }
    ready = true;
    usb_wake();
}

static void in_done(uint8_t busid, uint8_t ep, uint32_t n)
{
    if (reboot_after_tx) { reboot_after_tx = false; xcan_reboot(); }
    if (enabled && usb_device_is_configured(0)) arm_out();
}
static struct usbd_endpoint out_ep = {.ep_addr = 1, .ep_cb = out_done};
static struct usbd_endpoint in_ep = {.ep_addr = 0x81, .ep_cb = in_done};
static void event(uint8_t busid, uint8_t e)
{
    if (e == USBD_EVENT_RESET || e == USBD_EVENT_DISCONNECTED || e == USBD_EVENT_ERROR) {
        connection_generation++;
        enabled = ready = reboot_after_tx = false;
        received = expected = session = last_request_id = 0;
    } else if (e == USBD_EVENT_CONFIGURED) {
        connection_generation++;
        received = expected = session = last_request_id = 0;
        ready = reboot_after_tx = false; enabled = true; arm_out();
    }
}

static uint16_t fw_status(int result)
{
    if (result == 0) return 0;
    switch (-result) {
    case XCAN_FW_ERROR_ARGUMENT: case XCAN_FW_ERROR_MAGIC: case XCAN_FW_ERROR_HASH: return 1;
    case XCAN_FW_ERROR_STATE: return 3;
    default: return 6;
    }
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool prepare_v2(void)
{
    memset(&v2_response, 0, sizeof(v2_response));
    v2_response.kind = XCAN_V2_RESPONSE;
    v2_response.opcode = v2_request.opcode;
    v2_response.id = v2_request.id;
    v2_response.session = v2_request.session;
    if (v2_request.opcode == XCAN_V2_HELLO) {
        if (session != 0) { v2_response.status = 3; return false; }
        session = ++session_generation;
        if (session == 0) session = ++session_generation;
        last_request_id = v2_request.id;
        v2_response.session = session;
        v2_response.length = 16;
        put32(v2_response.payload, 1u << 4);
        put16(v2_response.payload + 4, XCAN_V2_MAX_MESSAGE);
        v2_response.payload[6] = 1;
        v2_response.payload[7] = 2;
        put32(v2_response.payload + 8, 1000000);
        return false;
    }
    if (session == 0 || v2_request.session != session) {
        v2_response.status = 5; return false;
    }
    if (v2_request.id <= last_request_id) {
        v2_response.status = 3; return false;
    }
    last_request_id = v2_request.id;
    return true;
}

static bool dispatch_v2(void)
{
    bool reboot = false;
    switch (v2_request.opcode) {
    case XCAN_V2_FW_BEGIN:
        if (!xcan_update_ready()) v2_response.status = 4;
        else v2_response.status = fw_status(xcan_fw_begin(get32(v2_request.payload),
                                                          v2_request.payload + 4));
        break;
    case XCAN_V2_FW_STATUS: {
        xcan_fw_step();
        struct xcan_fw_status status;
        xcan_fw_get_status(&status);
        v2_response.length = 16;
        v2_response.payload[0] = status.state;
        v2_response.payload[1] = status.error;
        put32(v2_response.payload + 4, status.image_size);
        put32(v2_response.payload + 8, status.next_offset);
        put32(v2_response.payload + 12, status.erase_offset);
        break;
    }
    case XCAN_V2_FW_WRITE: {
        struct xcan_fw_status status;
        xcan_fw_get_status(&status);
        if (status.state == XCAN_FW_ERASING) { v2_response.status = 4; break; }
        v2_response.status = fw_status(xcan_fw_write(
            get32(v2_request.payload), v2_request.payload + 8,
            get16(v2_request.payload + 4)));
        break;
    }
    case XCAN_V2_FW_FINISH:
        v2_response.status = fw_status(xcan_fw_finish());
        break;
    case XCAN_V2_FW_ABORT:
        v2_response.status = fw_status(xcan_fw_abort());
        break;
    case XCAN_V2_FW_REBOOT:
        if (!xcan_fw_complete()) v2_response.status = 3;
        else reboot = true;
        break;
    default:
        v2_response.status = 2;
        break;
    }
    return reboot;
}

void usb_work(void)
{
    unsigned request_length;
    uint32_t generation;
    usb_lock();
    if (!enabled || !ready || !usb_device_is_configured(0)) {
        usb_unlock();
        return;
    }
    ready = false;
    request_length = received;
    memcpy(work_request, request, request_length);
    received = expected = 0;
    generation = connection_generation;
    usb_unlock();

    unsigned response_length = 0;
    bool reboot = false;
    if (work_request[4] == 1) {
        xcan_reply(work_request, response, 144000000, board_id(), uptime_ms(), termination_get());
        response_length = 64;
    } else if (xcan_v2_decode(work_request, request_length, &v2_request)) {
        usb_lock();
        bool connected = enabled && usb_device_is_configured(0) &&
                         generation == connection_generation;
        bool execute = connected && prepare_v2();
        usb_unlock();
        if (!connected) return;
        if (execute) reboot = dispatch_v2();
        response_length = xcan_v2_encode(&v2_response, response, sizeof(response));
    }

    usb_lock();
    if (!response_length) {
        if (generation == connection_generation) enabled = false;
    } else if (enabled && usb_device_is_configured(0) &&
               generation == connection_generation) {
        reboot_after_tx = reboot;
        if (usbd_ep_start_write(0, 0x81, response, response_length)) {
            reboot_after_tx = false;
            enabled = false;
        }
    }
    usb_unlock();
}

int usb_init(const uint32_t uid[3])
{
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < 24; i++) serial[i] = hex[(uid[2-i/8] >> (28 - (i%8)*4)) & 15];
    serial[24] = 0;
    xcan_fw_reset();
    usbd_desc_register(0, &descriptors);
    usbd_add_interface(0, &interface);
    usbd_add_endpoint(0, &out_ep); usbd_add_endpoint(0, &in_ep);
    return usbd_initialize(0, 0x40005c00, event);
}
