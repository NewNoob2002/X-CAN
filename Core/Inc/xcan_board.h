/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "stm32g4xx_hal.h"
void xcan_board_safe(void);
void xcan_board_init(void);
void xcan_start(void);
void xcan_log(const char *line);
void xcan_fatal(void) __attribute__((noreturn));
