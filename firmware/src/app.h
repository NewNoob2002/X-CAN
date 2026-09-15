#pragma once
#include <stdbool.h>
#include <stdint.h>
void usb_wake(void);
void usb_lock(void);
void usb_unlock(void);
uint32_t board_id(void);
uint32_t uptime_ms(void);
int termination_get(void);
int xcan_boot_request_test(void);
int xcan_boot_confirm(void);
bool xcan_update_ready(void);
void xcan_reboot(void);
void usb_work(void);
int usb_init(const uint32_t uid[3]);
