/* SPDX-License-Identifier: Apache-2.0
 * Adapted from Zephyr webusb/msosv2.h, Copyright (c) 2016-2019 Intel Corporation,
 * Copyright (c) 2023-2024 Nordic Semiconductor ASA.
 */
#include <zephyr/usb/msos_desc.h>
static const struct {
    struct msosv2_descriptor_set_header header;
    struct msosv2_compatible_id compatible;
    struct msosv2_guids_property property;
} __packed msos_set = {
    .header = { .wLength = 10, .wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
        .dwWindowsVersion = sys_cpu_to_le32(0x0A000000), .wTotalLength = 162 },
    .compatible = { .wLength = 20, .wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
        .CompatibleID = {'W','I','N','U','S','B',0,0} },
    .property = { .wLength = 132, .wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY,
        .wPropertyDataType = MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ,
        .wPropertyNameLength = 42, .PropertyName = {DEVICE_INTERFACE_GUIDS_PROPERTY_NAME},
        .wPropertyDataLength = 80, .bPropertyData = {0x7b, 0x00, 0x44, 0x00, 0x39, 0x00, 0x45, 0x00, 0x37, 0x00, 0x31, 0x00, 0x44, 0x00, 0x41, 0x00, 0x38, 0x00, 0x2d, 0x00, 0x33, 0x00, 0x43, 0x00, 0x42, 0x00, 0x35, 0x00, 0x2d, 0x00, 0x34, 0x00, 0x45, 0x00, 0x33, 0x00, 0x42, 0x00, 0x2d, 0x00, 0x38, 0x00, 0x41, 0x00, 0x35, 0x00, 0x33, 0x00, 0x2d, 0x00, 0x35, 0x00, 0x38, 0x00, 0x34, 0x00, 0x33, 0x00, 0x34, 0x00, 0x31, 0x00, 0x34, 0x00, 0x45, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x31, 0x00, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x00} },
};
BUILD_ASSERT(sizeof(msos_set) == 162);
static const struct {
    struct usb_bos_platform_descriptor platform;
    struct usb_bos_capability_msos capability;
} __packed msos_cap = {
    .platform = { .bLength = 28, .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
        .bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
        .PlatformCapabilityUUID = {0xdf,0x60,0xdd,0xd8,0x89,0x45,0xc7,0x4c,
                                   0x9c,0xd2,0x65,0x9d,0x9e,0x64,0x8a,0x9f} },
    .capability = { .dwWindowsVersion = sys_cpu_to_le32(0x0A000000),
        .wMSOSDescriptorSetTotalLength = sys_cpu_to_le16(sizeof(msos_set)),
        .bMS_VendorCode = 2 },
};
static int msos_request(const struct usbd_context *ctx,
                        const struct usb_setup_packet *setup, struct net_buf *buf)
{
    ARG_UNUSED(ctx);
    if (setup->bmRequestType != 0xc0 || setup->bRequest != 2 ||
        setup->wIndex != MS_OS_20_DESCRIPTOR_INDEX || setup->wValue != 0) {
        return -ENOTSUP;
    }
    net_buf_add_mem(buf, &msos_set, MIN(net_buf_tailroom(buf), sizeof(msos_set)));
    return 0;
}
USBD_DESC_BOS_VREQ_DEFINE(bos_msos, sizeof(msos_cap), &msos_cap, 2, msos_request, NULL);

