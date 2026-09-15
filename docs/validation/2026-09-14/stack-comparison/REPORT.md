# Zephyr / FreeRTOS + CherryUSB 同功能对照

后续更新：已完成上板 M1 基础可行性验证，并修复 FLASH_ACR 整字写导致 SWD 关闭的问题，占用不变。当前状态及产物哈希见 [HIL 报告](hil/REPORT.md)。以下保留首次本地对照时的范围和证据，表中的“未执行 HIL”属于当时状态。

日期：2026-09-14。**当前 M1 INFO/ECHO 范围内，轻量组合显著节省资源，值得继续样板验证；尚不能宣布完整迁移或双镜像升级可交付。** 本次未访问、复位、烧录板卡，也未修改选项字节。

## 实际构建结果

| 指标 | Zephyr M1 基准 | FreeRTOS + CherryUSB | 差值 |
|---|---:|---:|---:|
| Flash | 67,156 B（65.58 KiB） | 15,932 B（15.56 KiB） | 减少 51,224 B，76.28% |
| 已分配 RAM，含栈 | 13,704 B（13.38 KiB） | 8,024 B（7.84 KiB） | 减少 5,680 B，41.45% |
| Flash 未分配 | 63,916 B | 115,140 B | 增加约 50.02 KiB |
| RAM 未分配 | 19,064 B | 24,744 B | 增加约 5.55 KiB |
| 实机 HIL | Linux M1 已通过 | **未执行** | 不宣称硬件等价 |

Flash 使用最终 BIN 长度，含地址间隙和 .data 初值。Zephyr SHA256 仍为 5a98344202229cd25b2704af109ff3882173351994c8c3646d476fc919dabc7f，未重建含未完成 v2 改动的当前源树来冒充基线。

对照 RAM = .data 56 + .bss 5920 + ISR 栈 2048 = 8024 B。任务栈已在 .bss 中，不能重复计数。链接器“RAM 100%”源于栈固定在 RAM 顶部，实际留有 24,744 B 空隙。Zephyr 用 map 中 _image_ram_size=0x3588；GNU size 的 data+bss 包含不在 RAM 的特殊区段，不能直接作同口径统计。两侧均无运行时栈水位证据。

## 功能对齐与差异

| 项目 | 实现/对齐情况 | 当前证据或限制 |
|---|---|---|
| SRAM 勘误 | 直接复用原汇编 hook | 反汇编确认 Reset 首条调用，hook 无写入/压栈 |
| 时钟 | HSE 12 MHz、CPU 144 MHz、PLLQ USB 48 MHz | ST 时钟切换序列，已编译；待 HIL |
| 安全 GPIO | PC4=1、PC5=0、PA1=0、PB9=1，先写锁存 | 对照版未测电气状态 |
| UART/LED/PB2 | 115200 8N1、心跳、10 ms 四样本声明 | 已实现，待 HIL |
| Vendor USB | C0CA:0313、同 UID 序列号、01/81 Bulk 64 B | 描述符本地通过；未枚举实机 |
| INFO/ECHO v1 | 直接复用 protocol.c，单在途 64 B | 53 载荷长度 × 64 分片大小 = 3392 组通过 |
| MS OS 2.0 | 同 162 B 描述符/GUID；严格 vendor 请求 | 长度裁剪、错误 wValue/wIndex/type、STALL 后恢复通过；Windows 未测 |
| USB 异常 | 复位/断开丢半包，取消配置不提交，提交错误停用至重新配置 | 本地通过；应用检查配置状态，补偿核心取消配置不通知应用 |
| 编译器/优化 | 同 GNU Arm 14.3.0、-Os、soft ABI、无 LTO | 同 SDK/picolibc；长整数格式化，无浮点格式化 |
| 栈 | main 2048、USB 1536、ISR 2048 B 相同 | FreeRTOS idle 512；Zephyr 还有 idle 320、工作队列 1024、UDC 512 等 |
| 调度 | **实现不同** | FreeRTOS 1 kHz tick；Zephyr tickless/10 kHz 逻辑 tick。Cherry 核心在 IRQ，协议在任务；未测中断延迟 |
| 日志/诊断 | 启动、PB2、心跳保留，诊断丰富度不同 | 无 Zephyr 故障转储；USB 提交失败无串口详情；断言/栈溢出安全停机；心跳 32 位毫秒约 49.7 天回绕，基准心跳 64 位；INFO 两侧均 32 位 |
| CAN/v2/注入/看门狗/bootloader | **两侧均未计入** | v2 集成继续暂停；现有 C/Rust codec 回归通过 |

这是相同 M1 外部功能目标的实现对照，不是纯内核性能或体积测量。通用设备模型、USB 分层、诊断和调度等均影响差额。

首轮默认浮点格式化使镜像约 24 KB；查 map 后对齐基准的长整数格式化配置，最终降为 15,932 B，采用最终结果。

## 测试与证据

- [构建日志](build.log)、[区段大小](size.txt)、[符号大小](symbols.txt)：主要代码项为整数格式化、USB 请求分发和 FSDEV IRQ；主要 RAM 项为任务栈和 USB 状态。
- [USB/C v1 本地测试](tests.log)：真实 CherryUSB 核心和应用、模拟控制器。MSOS 长度 1/10/64/162/255/65535；异常请求、零长包、畸形帧、复位/断开、取消配置、提交失败均覆盖。
- [Rust 回归](rust-regression.log)：10 项通过；[C v2 回归](c-v2-regression.log)：106 共享向量、分片/合包/帧 codec 通过。v2 未链接任何对照镜像。
- [Reset 反汇编](startup.txt)、[勘误 hook](early-reset.txt)：Flash 向量、MSP=0x20008000、无前置 SRAM 写检查通过。
- [结果及源码/产物哈希](result.json)；ELF/BIN/HEX/map、compile_commands.json 在 build/comparison/freertos_cherryusb/。
- ASan/UBSan 通过。首次 LeakSanitizer 因 ptrace 沙箱限制失败，随后仅关闭泄漏扫描；该环境故障不算作固件测试通过。

测试没有执行 MCU 外设、真实调度/IRQ 或 FSDEV 寄存器，因此没有测得枚举、吞吐、时序、Windows 绑定和栈水位。

## 对升级规划的含义与下一节点

两个当前 Zephyr 镜像已超过 128 KiB，不计 bootloader 也放不下。两个轻量 M1 镜像只有 31,864 B，说明基础应用不再立即堵住双槽规划；尚需计入 FDCAN、v2 队列、注入、诊断、镜像头/签名/trailer、页对齐和 bootloader。

可以把“每应用槽 48 KiB，另留共 32 KiB 给 boot/元数据等”作为后续预算目标；它不是既定分区，不保证 MCUboot 装得下或升级算法支持该布局。必须实际链接选定 boot/签名实现，再做断电恢复测试。

建议先让对照版通过与 Zephyr 相同的启动/GPIO/PB2/USB HIL 和 Windows 验收，再把同一 v2/FDCAN 功能纳入第二轮对照。完整应用有实测余量后，再决定 MCUboot、可恢复单槽自定义 boot 或双槽方案。主线保持原状，本次未实现 bootloader。

该独立对照工程在 CubeMX/HAL 主线完成集成后删除，避免继续维护第二套启动、平台和中间件入口。本目录保留原始构建日志、产物尺寸、HIL 结果及源码哈希用于历史审计。
