/* SPDX-License-Identifier: Apache-2.0
 * Based on Zephyr samples/subsys/usb/webusb/src/sfunc.c
 * Copyright (c) 2024 Nordic Semiconductor ASA
 */
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <soc.h>
#include "protocol.h"

extern int xcan_termination_get(void);

static struct usb_if_descriptor interface = {
    .bLength = sizeof(struct usb_if_descriptor), .bDescriptorType = USB_DESC_INTERFACE,
    .bNumEndpoints = 2, .bInterfaceClass = USB_BCC_VENDOR,
};
static struct usb_ep_descriptor ep_out = {
    .bLength = sizeof(struct usb_ep_descriptor), .bDescriptorType = USB_DESC_ENDPOINT,
    .bEndpointAddress = 0x01, .bmAttributes = USB_EP_TYPE_BULK,
    .wMaxPacketSize = sys_cpu_to_le16(64),
};
static struct usb_ep_descriptor ep_in = {
    .bLength = sizeof(struct usb_ep_descriptor), .bDescriptorType = USB_DESC_ENDPOINT,
    .bEndpointAddress = 0x81, .bmAttributes = USB_EP_TYPE_BULK,
    .wMaxPacketSize = sys_cpu_to_le16(64),
};
static struct usb_desc_header end;
static const struct usb_desc_header *descriptors[] = {
    (void *)&interface, (void *)&ep_out, (void *)&ep_in, &end,
};

/* First member must be udc_buf_info. Epoch rejects callbacks from old configs. */
struct transfer_info { struct udc_buf_info udc; uint32_t epoch; };
NET_BUF_POOL_FIXED_DEFINE(transfer_pool, 2, 64, sizeof(struct transfer_info), NULL);
static bool enabled; /* All class callbacks run in the USBD thread. */
static uint32_t epoch;
static uint8_t request[64];
static size_t received;

static void queue(struct usbd_class_data *cls, struct net_buf *buf, uint8_t ep)
{
    struct transfer_info *info = net_buf_user_data(buf);
    memset(&info->udc, 0, sizeof(info->udc));
    info->udc.ep = ep;
    int err = usbd_ep_enqueue(cls, buf);
    if (err) {
        enabled = false;
        net_buf_unref(buf);
        printk("USB enqueue error=%d; reconfigure required\n", err);
    }
}

static int complete(struct usbd_class_data *cls, struct net_buf *buf, int err)
{
    struct transfer_info *info = net_buf_user_data(buf);
    if (!enabled || info->epoch != epoch || err) {
        if (err && info->epoch == epoch) {
            enabled = false;
            received = 0;
            printk("USB transfer error=%d; reconfigure required\n", err);
        }
        net_buf_unref(buf);
        return 0;
    }
    if (info->udc.ep == ep_in.bEndpointAddress) {
        net_buf_reset(buf);
        queue(cls, buf, ep_out.bEndpointAddress);
        return 0;
    }
    if (buf->len > sizeof(request) - received) {
        /* Single in-flight 64-byte record; excess bytes violate framing. */
        received = 0;
        memset(request, 0, sizeof(request));
        request[5] = 0xff; /* Produce an explicit malformed-header response. */
    } else {
        memcpy(request + received, buf->data, buf->len);
        received += buf->len;
        if (received < sizeof(request)) {
            net_buf_reset(buf);
            queue(cls, buf, ep_out.bEndpointAddress);
            return 0;
        }
        received = 0;
    }
    net_buf_reset(buf);
    xcan_reply(request, net_buf_add(buf, 64), SystemCoreClock, DBGMCU->IDCODE,
               k_uptime_get_32(), xcan_termination_get());
    queue(cls, buf, ep_in.bEndpointAddress);
    return 0;
}

static void *get_desc(struct usbd_class_data *cls, enum usbd_speed speed)
{
    ARG_UNUSED(cls); ARG_UNUSED(speed);
    return descriptors;
}
static void enable(struct usbd_class_data *cls)
{
    epoch++;
    received = 0;
    struct net_buf *buf = net_buf_alloc(&transfer_pool, K_NO_WAIT);
    if (!buf) { enabled = false; printk("USB no buffer\n"); return; }
    ((struct transfer_info *)net_buf_user_data(buf))->epoch = epoch;
    enabled = true;
    queue(cls, buf, ep_out.bEndpointAddress);
}
static void disable(struct usbd_class_data *cls)
{
    ARG_UNUSED(cls);
    enabled = false;
    received = 0;
    epoch++;
}
static int init(struct usbd_class_data *cls)
{
    ARG_UNUSED(cls);
    return 0;
}
static const struct usbd_class_api api = {
    .request = complete, .get_desc = get_desc, .enable = enable, .disable = disable,
    .init = init,
};
USBD_DEFINE_CLASS(xcan_vendor, &api, NULL, NULL);

USBD_DEVICE_DEFINE(xcan_usb, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   CONFIG_XCAN_USB_VID, CONFIG_XCAN_USB_PID);
USBD_DESC_LANG_DEFINE(language);
USBD_DESC_MANUFACTURER_DEFINE(manufacturer, "X-CAN");
USBD_DESC_PRODUCT_DEFINE(product, "X-CAN bringup");
USBD_DESC_SERIAL_NUMBER_DEFINE(serial_number);
USBD_DESC_CONFIG_DEFINE(config_string, "Vendor USB");
USBD_CONFIGURATION_DEFINE(config, 0, 50, &config_string); /* 100 mA, no remote wakeup. */

#include "msos.h"

int xcan_usb_init(void)
{
    struct usbd_desc_node *strings[] = {
        &language, &manufacturer, &product, &serial_number, &bos_msos,
    };
    int err;
    for (size_t i = 0; i < ARRAY_SIZE(strings); ++i) {
        err = usbd_add_descriptor(&xcan_usb, strings[i]);
        if (err) { return err; }
    }
    err = usbd_device_set_bcd_usb(&xcan_usb, USBD_SPEED_FS, 0x0210);
    if (err) { return err; }
    err = usbd_add_configuration(&xcan_usb, USBD_SPEED_FS, &config);
    if (err) { return err; }
    err = usbd_register_class(&xcan_usb, "xcan_vendor", USBD_SPEED_FS, 1);
    if (err) { return err; }
    err = usbd_init(&xcan_usb);
    return err ? err : usbd_enable(&xcan_usb);
}
