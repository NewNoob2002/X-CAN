/* SPDX-License-Identifier: Apache-2.0 */
#include "xcan_board.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

void xcan_board_safe(void) {
  GPIO_InitTypeDef pin = {.Mode = GPIO_MODE_OUTPUT_PP,
                          .Pull = GPIO_NOPULL,
                          .Speed = GPIO_SPEED_FREQ_LOW};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  /* Set output latches before changing mode, including error/startup paths. */
  HAL_GPIO_WritePin(INJECT_ARM_GPIO_Port, INJECT_ARM_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CAN_STB_GPIO_Port, CAN_STB_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(INJECT_REQ_GPIO_Port, INJECT_REQ_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
  pin.Pin = CAN_STB_Pin | INJECT_ARM_Pin;
  HAL_GPIO_Init(GPIOC, &pin);
  pin.Pin = INJECT_REQ_Pin;
  HAL_GPIO_Init(GPIOA, &pin);
  pin.Pin = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOB, &pin);
}

void xcan_fatal(void) {
  __disable_irq();
  xcan_board_safe();
  for (;;) {
    __NOP();
  }
}

void xcan_board_init(void) {
  if (HAL_RCC_GetHCLKFreq() != 144000000 ||
      HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USB) != 48000000 ||
      HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN) != 48000000)
    xcan_fatal();
}

void xcan_log(const char *line) {
  /* Single application logging task, bounded blocking UART. */
  if (HAL_UART_Transmit(&huart1, (const uint8_t *)line, (uint16_t)strlen(line),
                        50) != HAL_OK)
    xcan_fatal();
}

void usb_dc_low_level_init(void) {
  /* CherryUSB owns PMA/endpoint registers and IRQ; do not also start HAL PCD.
   */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  /* G4 USB uses dedicated pad control, not an AF10 mapping. Keep pads in
   * reset/analog mode, as in ST CubeG4 USB MSP initialization. */
  GPIO_InitTypeDef pin = {.Pin = GPIO_PIN_11 | GPIO_PIN_12,
                          .Mode = GPIO_MODE_ANALOG,
                          .Pull = GPIO_NOPULL};
  HAL_GPIO_Init(GPIOA, &pin);
  __HAL_RCC_USB_CLK_ENABLE();
  __HAL_RCC_USB_FORCE_RESET();
  __HAL_RCC_USB_RELEASE_RESET();
  HAL_NVIC_SetPriority(USB_LP_IRQn, 6, 0);
  HAL_NVIC_ClearPendingIRQ(USB_LP_IRQn);
  HAL_NVIC_EnableIRQ(USB_LP_IRQn);
}
void usb_dc_low_level_deinit(void) { HAL_NVIC_DisableIRQ(USB_LP_IRQn); }
uint32_t board_id(void) { return DBGMCU->IDCODE; }
