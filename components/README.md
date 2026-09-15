# Third-party middleware

This directory contains repository-owned snapshots of third-party middleware.
Each middleware is built as its own CMake target; board-specific integration
stays in the CubeMX project and application code.

- `FreeRTOS-Kernel/`: FreeRTOS kernel and the upstream ARM CM4F architecture port.
- `CherryUSB/`: CherryUSB device core and the upstream STM32 FSDEV controller port.

The X-CAN board glue for clocks, GPIO safety, critical sections and IRQ routing
is implemented in `Core/Src/xcan_board.c` and CubeMX USER CODE sections.
