board_runner_args(jlink "--device=STM32G431RB" "--speed=1000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
