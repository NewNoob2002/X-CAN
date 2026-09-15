# X-CAN

STM32G431RBT6 单通道 CAN/CAN-FD 分析仪，目标支持定时显性脉冲注入、错误与恢复观察，以及 Linux/Windows Rust 上位机。

当前主线为 **STM32 HAL + FreeRTOS + CherryUSB**。已在重新生成的 CubeMX 工程上恢复集成，基础 Linux HIL 已覆盖启动、安全 GPIO、双时基、PB2 和 Vendor USB。MCUboot 双槽、App USB 升级状态机及 Rust 512 字节分块命令已通过实机升级、确认和回滚验证；中途断电恢复、CAN 和注入仍待验收。

## 构建与本地测试

依赖已安装的 GNU Arm/CubeCLT、CMake、Ninja；协议测试还需本机 C 编译器、Python 3 和 Rust。HAL/CMSIS 和必要中间件源码均在仓库内，默认构建不依赖 Zephyr 或其他项目目录，也不下载库。

    python3 tools/build.py
    python3 tools/build.py --preset Debug
    python3 tools/test.py

Release 输出 build/Release/xcan.{elf,bin,hex,map}、xcan_boot 和 xcan.signed.bin。当前 App Flash 34,852 B、签名镜像 35,514 B、RAM 预留 11,480 B；48 KiB 签名镜像区尚余 13,638 B。Debug 输出 build/Debug/。build.py 自动检查启动 hook、应用入口、向量、签名与 IRQ 归属；不执行烧录。

## 工作区

| 目录 | 用途 |
|---|---|
| Core/、Drivers/、cmake/ | CubeMX/HAL/FDCAN 生成层、板级 HAL 适配、TIM6 HAL 时基 |
| components/ | 第三方 Middleware：FreeRTOS 11.3.0、CherryUSB 1.6.1 固定源码及独立库 target |
| firmware/ | `xcan_app` 应用静态库、USB Vendor 适配和共用 v1/v2 C 协议 |
| host/ | Rust CLI、协议 v2 库、Linux udev 规则 |
| tests/、tools/ | 本地测试、构建检查、串口/J-Link 工具 |
| legacy/zephyr/ | 已归档 Zephyr 工程、设备树及未完成 CAN 草稿 |
| docs/Schematic/、docs/Reference/ST/ | 原理图和 ST 手册；实装 HSE 为 12 MHz |
| docs/validation/ | 构建与 HIL 证据，原始 Flash 备份仍保留在本地 |

## 板级约定

CPU 144 MHz、USB PLLQ 48 MHz；USART1 PA9/PA10 115200 8N1；PB0 心跳；PB2 为人工终端声明（10 ms 四样本），不能代替 120Ω 电阻的实际检测。

安全态 PC4/STB=1、PC5/ARM=0、PA1/注入请求=0、PB9/TXD=1。SRAM 首次访问勘误 hook 必须在 Reset 第一条执行。Flash 启动选项不修改。

HAL 使用 TIM6 毫秒时基，FreeRTOS 使用 SysTick。CherryUSB 独占 USB 端点和 IRQ，不同时运行 HAL PCD/ST USB Device 栈。常规驱动使用 HAL，时序敏感路径后续按实测需要使用 LL。

CubeMX 模型为 g431rbt6.ioc，包含 FDCAN1、144 MHz CPU 和 48 MHz USB/FDCAN 时钟。顶层 CMake 保持 CubeMX 模板，只加入 `components`、`firmware` 子目录并链接 `xcan_app`。生成的 main、MSP、IRQ、sysmem 和 startup 各保留唯一实现；FreeRTOS 核心异常和 CherryUSB USB IRQ 通过 CubeMX USER CODE 区转发。startup 中的 SRAM 勘误 hook 必须在 `SystemInit` 和 SRAM 初始化前执行，重新生成后由构建检查验证。

硬件测试脚本需要 pyserial、pyusb。本机可复用 /home/gtc/zephyrproject/.venv/bin/python；这是 HIL 工具环境，固件构建不依赖 Zephyr。

## 上位机

    cargo build --release --manifest-path host/Cargo.toml
    host/target/release/xcan list
    host/target/release/xcan info 2036365058315010002D0055
    host/target/release/xcan self-test 2036365058315010002D0055
    host/target/release/xcan update 2036365058315010002D0055 build/Release/xcan.signed.bin

以上设备命令仅用于明确连接的 X-CAN（C0CA:0313）。v1 INFO/ECHO 保留；v2 升级已接入 USB，要求使用已签名镜像，生产私钥不进入上位机。CAN 数据通路尚未接入。

## 记录

- [当前计划状态](docs/PROJECT_STATUS.md)
- [CubeMX 再集成及本轮 HIL](docs/validation/2026-09-14/cubemx-reintegration/REPORT.md)
- [HAL 移植及再生成说明](docs/validation/2026-09-14/hal-migration/REPORT.md)
- [轻量对照 HIL](docs/validation/2026-09-14/stack-comparison/hil/REPORT.md)
- [协议 v2](docs/USB_CAN_PROTOCOL_V2.md)
- [Boot冻结与升级本地测试](docs/validation/2026-09-14/firmware-update/REPORT.md)
- [硬件评审](docs/HARDWARE_REVIEW_AND_PLAN.md)
- [Zephyr 归档入口](legacy/zephyr/README.md)
