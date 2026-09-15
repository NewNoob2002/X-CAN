#pragma once
#include <stdint.h>
void xcan_fatal(void);
#define configUSE_PREEMPTION 1
#define configCPU_CLOCK_HZ 144000000UL
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 4
#define configMINIMAL_STACK_SIZE 128
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_MUTEXES 0
#define configUSE_TIMERS 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configPRIO_BITS 4
#define configKERNEL_INTERRUPT_PRIORITY (15 << 4)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5 << 4)
#define configCHECK_HANDLER_INSTALLATION 0
#define configASSERT(x)                                                        \
  do {                                                                         \
    if (!(x))                                                                  \
      xcan_fatal();                                                            \
  } while (0)
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
