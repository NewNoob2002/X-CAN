# CubeMX 再集成与 M1 HIL

2026-09-14：基于用户重新生成的 CubeMX 工程恢复 HAL + FreeRTOS + CherryUSB。Release 已烧录并通过下列基础 Linux HIL，板上最终运行本轮 SAFE 镜像，TERM_DECLARED 已恢复为 0。完整 M1 的其余验收项仍单独列出。

## 集成

- 保留本次生成的时钟、GPIO、USART1 和 TIM6 初始化。使用六个用户标签；复用生成的 huart1，移除旧板级代码的重复 UART 初始化。PC6 等用户额外 GPIO 配置保持原生成结果。
- 从本机回收站恢复应用源码，按旧构建元数据核对可匹配的源码哈希。xcan_board.c 与旧归档构建哈希不同，已读取并按当前生成代码调整，未把它标作字节一致恢复。47 个中间件文件均与既有导入清单一致；未下载新依赖。见 restored-sources.json。
- main.c 的 USER CODE 接入安全初始化与 RTOS 启动。USB_Init 0 提前返回，使 HAL PCD 不初始化控制器；CherryUSB 独占端点与 USB IRQ。构建检查确认 HAL_PCD_Init/IRQ 不在最终镜像中。
- 根 CMake 从 CubeMX 启动模板生成带首指令 SRAM hook 的构建副本；不修改原模板。应用侧 interrupts.c 和 sysmem.c 取代对应生成文件，保留 FreeRTOS SVC/PendSV/SysTick、HAL TIM6 和有界 libc 堆。VTOR 指向 0x08000000。
- .ioc 和链接脚本统一为 1 KiB libc 堆、2 KiB MSP 预留；RTOS 采用静态任务与 ARM_CM4F 端口。

## 构建与本地验证

| 配置 | Flash BIN | RAM 预留 | 检查 |
|---|---:|---:|---|
| Release | 26,876 B | 9,720 B | 通过 |
| Debug | 31,244 B | 9,728 B | 通过 |

GNU Arm 14.3.1；元数据见 release-metadata.json、debug-metadata.json。执行 python3 tools/build.py（Release/Debug）与 python3 tools/test.py：启动首指令、应用入口、向量/IRQ、堆栈边界通过；C v1/v2 与真实 CherryUSB 核心的 ASan/UBSan 回归通过，含 3392 组 USB 分片组合、106 个 v2 共享向量及 10 项 Rust 测试。v2 仍仅本地验证。

## 实机结果

目标 STM32G431RB，J-Link 63728769，USB 序列号 2036365058315010002D0055。DBGMCU_IDCODE=0x20036468，OPTR=0xFBEFF8AA 未修改；备份连接 VTref=3.414 V。

| 检查 | 结果与证据 |
|---|---|
| 备份、烧录与读回 | 整片 128 KiB 备份；一次实际编程，verifybin 成功。见 backup-result.json、flash.log、deployment-result.json |
| 启动、UART、双时基 | USB init=0；19 个心跳样本中 HAL 与 RTOS 计数同步推进。见 boot.log、runtime-result.json |
| 安全引脚 | PC4=1、PC5=0、PA1=0、PB9=1；推挽、无上下拉，IDR/ODR 均符合预期。见 runtime-registers.log |
| 时基与中断 | TIM6 PSC=143/ARR=999；SysTick LOAD=143999；TIM6/USB/PendSV/SysTick 优先级 4/6/15/15；CFSR/HFSR=0 |
| USB Bulk | Rust CLI 106 次回环与畸形长度拒绝通过。见 observed-tests.json |
| MS OS 控制请求 | 描述符长度裁剪、两种非法请求 STALL、两次后续恢复均通过。见 observed-tests.json |
| 有界 USB 恢复 | 暂停读取 250 ms、释放后重开句柄、发送 7 字节半包后一次 USB 总线复位，后续 ECHO/INFO 均正常。见 usb-recovery-result.json |
| PB2 | 用户切换，USB/UART 确认 0→1→0；PB2 输入模式、无内部上下拉。见 observed-tests.json、term-high-uart.log |

寄存器与 GPIO IDR 不能代替示波器测量；双时基使用同一片上时钟体系，结果不代表外部频率精度或抖动验收。USB v1 半包后的恢复在本轮通过显式 USB 复位验证，未声称主机异常退出后自动清除半包。

首次烧录工具调用在 UART 预检查阶段因系统 Python 缺少 pyserial 而退出，未执行目标命令；改用已有 Zephyr Python 虚拟环境后完成操作，无安装包。串口工具启动失败时现在会打印完整异常。

## 仍未覆盖

Windows 实机 WinUSB、冷上电、正反插/物理拔插、USB 挂起恢复、示波器波形仍待验证。当前固件不包含 CAN 收发、v2 联机、定时注入、看门狗或复位原因记录；本轮不将 M1–M5 标记全部完成。

## 再生成与复现

保留根 CMake、firmware/src、Core 中应用自有文件及 Middlewares。CubeMX main.c USER CODE 中的安全初始化、RTOS 启动、USB_Init 提前返回和错误处理必须保留。堆/栈值已回写 .ioc。生成后执行 tools/build.py 的 Release/Debug 和 tools/test.py，产物检查会拒绝应用入口丢失或 HAL PCD 意外接管的镜像。

当前 BIN/ELF 在 build/Release；原始备份在 build/hil-reintegration/preflash.bin。各命令与操作边界见 preflight.json、*.jlink、observed-tests.json 和 usb-recovery.py。硬件脚本使用 /home/gtc/zephyrproject/.venv/bin/python；这些脚本绑定本报告中的明确设备，重新运行前需确认台架与授权范围。
