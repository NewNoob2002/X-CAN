# HAL + FreeRTOS 第一阶段移植

2026-09-14：完成工作区归档和 M1 HAL 移植，Release/Debug 构建、本地协议/USB 测试、启动及中断检查通过。**本轮未烧录；新 HAL 镜像没有 HIL 结论。** 板上仍是此前通过 HIL 的轻量 LL 对照版。

## 当前结构

| 路径 | 职责 |
|---|---|
| Core/、Drivers/、cmake/、根 CMake/启动/链接脚本 | 用户提供的 CubeMX 工程，接入 HAL 时钟/GPIO/UART、TIM6 HAL 时基、RTOS IRQ、SRAM hook |
| firmware/src/ | 已移植 RTOS 应用、共用 CherryUSB Vendor 适配、v1/v2 纯 C 协议；v2 仅参与本地测试 |
| components/ | 固定 FreeRTOS 11.3.0 与 CherryUSB 1.6.1 第三方 Middleware，各自构建为独立 target |
| legacy/zephyr/ | Zephyr 专用固件、board、west 和工具；含未完成 CAN 草稿 |
| tools/build.py、tools/check_build.py、tools/test.py | 默认 HAL 构建、产物检查、本地测试入口 |

归档前 31 个文件均有映射：27 个字节不变，4 个仅为共用源路径/输出目录和汇编 Thumb 类型适配。见 [移动前哈希](zephyr-before.json)、[映射核对](archive-map.json)。新增 CubeMX 输入哈希在 [cubemx-input.json](cubemx-input.json)，中间件来源在 [middleware-import.json](middleware-import.json)。历史文档路径保留原上下文，当前入口看根 README。

原 build/system/usb 的 Zephyr HIL BIN 和历史 LL 对照版的产物哈希保留在验证记录中。对照源码在正式 CubeMX/HAL 集成完成后删除，活动实现统一由 `Core/`、`components/` 和 `firmware/` 管理。Flash 备份、docs/Schematic、docs/Reference/ST、Rust 主机及 v2 规范没有删除。

## HAL 集成决策

- 用户输入为 170 MHz LED 工程，HSE 已标 12 MHz。改为 PLL M3/N72/R2/Q6、144 MHz CPU、48 MHz USB，与 M1 功能相符；电压范围 SCALE1、Flash 4WS。HAL 更新寄存器，保留 DBG_SWEN；板级初始化验证 HCLK/USB 计算值。
- 安全 GPIO 在 HAL_Init 前设置：PC4=1、PC5=0、PA1=0、PB9=1，先写锁存再设置输出；故障处理也回到安全值。保留用户现有 PB0/PB1/PB11/PB12 输出初始化，心跳仅切换 PB0。
- USART1 PA9/PA10、115200 8N1，使用 HAL_UART_Init/Transmit，单任务日志、50 ms 超时。PB2 使用 HAL_GPIO_ReadPin，10 ms 四样本去抖。
- HAL 独占 TIM6 1 kHz 时基，IRQ 优先级 4；不调用 FreeRTOS API。FreeRTOS SysTick=1 kHz，SVC/PendSV/SysTick 由 ARM_CM4F 端口实现；优先级 15，RTOS syscall 边界 5。USB IRQ=6，允许 FromISR 通知。TIM2 保留给后续注入。
- CherryUSB 拥有 USB PMA/端点/IRQ，HAL 仅配置时钟/引脚/NVIC，不同时初始化 PCD。新 HAL GPIO 头没有 GPIO_AF10_USB；参照 ST CubeG4 USB MSP，PA11/PA12 保持 reset/analog 状态，USB 使用专用 pad 控制。这与旧 LL 对照的 AF10 设置不同，需要 HIL 核对枚举。
- 沿用 CubeMX hard-float ABI，采用 FreeRTOS ARM_CM4F 端口与 CMSIS SystemInit 的 FPU 初始化；无需额外 CMSIS-RTOS 适配层。
- Reset 第一条调用现有 SRAM 勘误 hook，保留 OPTR parity 守卫，未写选项字节。新工具链要求给失败标签标记 Thumb 函数类型，执行语义不变。
- 主/USB/idle 栈为 2048/1536/512 B，MSP 预留 2048 B；FreeRTOS 无动态堆。newlib-nano C 堆单独限制为 1024 B，防止原 Cube _sbrk 无限增长到栈边界。

## 本地验收

| 构建 | Flash BIN | RAM 预留（含栈和 C 堆） |
|---|---:|---:|
| Release (-Os) | 23,040 B（22.50 KiB） | 9,720 B |
| Debug (-Og) | 26,908 B | 9,728 B |

工具链为 STM32CubeCLT GNU Arm 14.3.1、newlib-nano；HAL 1.2.7，CMSIS 使用用户提供的 Drivers。与之前 Zephyr SDK/picolibc/no-FPU 对照编译条件不同，不能把差额全归因于 HAL。

- C v1 通过；C v2 106 共享向量及分片/合包通过。
- 真实 CherryUSB 核心 + 当前适配代码的 3392 组分片组合、INFO、描述符、错误/恢复测试通过；ASan/UBSan 开启，ptrace 环境下关闭 LeakSanitizer。
- Rust 10 项测试通过，使用离线依赖。
- 两配置检查 Reset 首指令、hook 无 SRAM 写、向量指向 RTOS SVC/PendSV/SysTick 和 USB/TIM6 处理器、堆栈边界、无 HAL PCD/FDCAN 启动符号、无 legacy/comparison 构建源依赖。
- 归档 Zephyr M1 重新构建通过（67,152 B，非原 HIL 产物）；LL 对照调整共用路径后重新构建通过（15,932 B）。

当前产物、源码哈希和区段详见 release-metadata.json、debug-metadata.json、release-size.txt、startup-disassembly.txt。构建过程修正了 USB AF 宏不存在、旧汇编标签缺少 Thumb 类型和工具链链接选项问题，最终构建成功。

## CubeMX 再生成注意项

本次没有发现 .ioc，不能宣称配置已回写 CubeMX 模型。用户应将 .ioc 加入版本管理，并同步 12 MHz/144 MHz/48 MHz、TIM6 HAL timebase、UART、GPIO 和中断归属。

应用接入点优先放在 USER CODE 区，但时钟数值、HAL 模块开关、生成的空 SVC/PendSV/SysTick 移除、启动 hook、链接脚本、SystemInit VTOR 开关、sysmem 堆上界和 CMake 驱动列表需要再生成后复核。构建检查会捕获部分启动/IRQ 冲突，不能代替 .ioc 同步。

下一节点为新 HAL 镜像的独立备份/烧录、启动、SWD、USB、PB2 和 HAL/RTOS 双时基 HIL。随后将 v2/FDCAN 后端按 HAL API 接入；定时注入的敏感路径再按需要使用 LL。当前没有实施 CAN、注入或 bootloader。
