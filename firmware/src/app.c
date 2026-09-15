/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include "FreeRTOS.h"
#include "bootutil/bootutil_public.h"
#include "main.h"
#include "task.h"
#include "xcan_board.h"
#include <stdio.h>

#ifndef XCAN_CONFIRM_DELAY_MS
#define XCAN_CONFIRM_DELAY_MS 1000U
#endif

static volatile int termination = -1;
static volatile bool update_ready;
static TaskHandle_t usb_task;
static StaticTask_t main_tcb, usb_tcb, idle_tcb;
static StackType_t main_stack[512], usb_stack[384], idle_stack[128];

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
  (void)task;
  (void)name;
  xcan_fatal();
}
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   uint32_t *size) {
  *tcb = &idle_tcb;
  *stack = idle_stack;
  *size = 128;
}
void usb_lock(void) { taskENTER_CRITICAL(); }
void usb_unlock(void) { taskEXIT_CRITICAL(); }
void usb_wake(void) {
  BaseType_t wake = pdFALSE;
  vTaskNotifyGiveFromISR(usb_task, &wake);
  portYIELD_FROM_ISR(wake);
}
uint32_t uptime_ms(void) { return xTaskGetTickCount(); }
int termination_get(void) { return termination; }
int xcan_boot_request_test(void) { return boot_set_pending_multi(0, 0); }
int xcan_boot_confirm(void) { return boot_set_confirmed_multi(0); }
bool xcan_update_ready(void) { return update_ready; }
void xcan_reboot(void) {
  NVIC_SystemReset();
  for (;;) {
  }
}
static void usb_thread(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    usb_work();
  }
}
static void main_thread(void *arg) {
  (void)arg;
  char line[128];
  snprintf(line, sizeof(line),
           "X-CAN HAL+FreeRTOS sysclk=%lu idcode=%08lx optr=%08lx SAFE\n",
           (unsigned long)SystemCoreClock, (unsigned long)board_id(),
           (unsigned long)FLASH->OPTR);
  xcan_log(line);
  uint32_t uid[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
  int err = usb_init(uid);
  snprintf(line, sizeof(line), "USB init=%d vid=c0ca pid=0313\n", err);
  xcan_log(line);
  if (err)
    xcan_fatal();
  int candidate = -1;
  unsigned samples = 0;
  uint32_t heartbeat = 0;
  int image_confirmed = 0;
  for (;;) {
    int value = HAL_GPIO_ReadPin(TERM_DECLARED_GPIO_Port, TERM_DECLARED_Pin) ==
                GPIO_PIN_SET;
    if (value != candidate) {
      candidate = value;
      samples = 1;
    } else if (samples < 4)
      samples++;
    if (samples == 4 && termination != candidate) {
      termination = candidate;
      snprintf(line, sizeof(line), "termination_declared=%d (manual switch)\n",
               termination);
      xcan_log(line);
    }
    uint32_t now = uptime_ms();
    if (!image_confirmed && now >= pdMS_TO_TICKS(XCAN_CONFIRM_DELAY_MS)) {
      int confirm = xcan_boot_confirm();
      snprintf(line, sizeof(line), "MCUboot confirm=%d\n", confirm);
      xcan_log(line);
      if (confirm != 0)
        xcan_fatal();
      update_ready = true;
      image_confirmed = 1;
    }
    if ((int32_t)(now - heartbeat) >= 0) {
      snprintf(line, sizeof(line),
               "alive ms=%lu hal_ms=%lu termination_declared=%d SAFE\n",
               (unsigned long)now, (unsigned long)HAL_GetTick(), termination);
      xcan_log(line);
      HAL_GPIO_TogglePin(HEARTBEAT_LED_GPIO_Port, HEARTBEAT_LED_Pin);
      heartbeat = now + 1000;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void xcan_start(void) {
  usb_task =
      xTaskCreateStatic(usb_thread, "usb", 384, NULL, 2, usb_stack, &usb_tcb);
  configASSERT(usb_task);
  configASSERT(xTaskCreateStatic(main_thread, "main", 512, NULL, 1, main_stack,
                                 &main_tcb));
  vTaskStartScheduler();
  xcan_fatal();
}
