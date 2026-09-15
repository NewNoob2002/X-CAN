#include "boot_memory_map.h"
#include <bootutil/bootutil.h>
#include <bootutil/fault_injection_hardening.h>
#include <bootutil/image.h>
#include <stm32g431xx.h>
#include <stdint.h>

static void halt(void) {
  __disable_irq();
  for (;;)
    __WFI();
}

int main(void) {
  struct boot_rsp response;
  FIH_DECLARE(result, FIH_FAILURE);
  FIH_CALL(boot_go, result, &response);
  if (FIH_NOT_EQ(result, FIH_SUCCESS) || response.br_hdr == NULL)
    halt();

  uint32_t vector = response.br_image_off + response.br_hdr->ih_hdr_size;
  if (vector != XCAN_APP_VECTOR_BASE)
    halt();

  const uint32_t *vectors = (const uint32_t *)(uintptr_t)vector;
  uint32_t stack = vectors[0];
  uint32_t reset = vectors[1];
  if (stack < 0x20000000U || stack > 0x20008000U || (stack & 7U) != 0U ||
      (reset & 1U) == 0U || (reset & ~1U) < XCAN_APP_VECTOR_BASE ||
      (reset & ~1U) >= XCAN_PRIMARY_TRAILER_BASE)
    halt();

  __disable_irq();
  SysTick->CTRL = 0U;
  SysTick->VAL = 0U;
  for (unsigned i = 0; i < 8; ++i) {
    NVIC->ICER[i] = UINT32_MAX;
    NVIC->ICPR[i] = UINT32_MAX;
  }
  SCB->VTOR = vector;
  __DSB();
  __ISB();
  __asm volatile("msr msp, %0\n"
                 "bx %1\n"
                 :
                 : "r"(stack), "r"(reset)
                 : "memory");
  __builtin_unreachable();
}

uint32_t HAL_GetTick(void) {
  static uint32_t tick;
  return tick++;
}

void assert_failed(uint8_t *file, uint32_t line) {
  (void)file;
  (void)line;
  halt();
}
