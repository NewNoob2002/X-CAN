# X-CAN 当前状态与工作区索引

更新：2026-09-14，以源码、构建配置及已归档测试为准。

当前主线为 **HAL + FreeRTOS + CherryUSB**。CubeMX 再集成的基础 Release 已烧录并通过 Linux HIL；在此基础上新增的 MCUboot 双槽和 USB 升级尚未烧录。最新 Release App 为 34,852 B，签名镜像 35,514 B，RAM 预留 11,480 B。

本轮验证了启动、安全 GPIO、UART、HAL/RTOS 双时基、PB2 0→1→0、USB INFO/ECHO、MS OS 请求及有界 USB 恢复。当前仍是 M1 收尾、M2 协议基础完成；尚不能采集 CAN 帧或执行故障注入。Windows、物理拔插/冷启动及波形验收仍待完成。

## 计划节点

沿用 [原计划](HARDWARE_REVIEW_AND_PLAN.md) 的 M0–M5 编号，不重新编号。

| 节点 | 当前状态 | 已完成 | 未关闭项 / 下一验收条件 |
|---|---|---|---|
| M0 样板基线 | 部分完成，波形延期 | 四页原理图与 ST 资料核对；修订 X；Flash 启动；实装 12 MHz；供电静态值；终端 OL/120Ω；安全脚静态状态；PB2 两状态 | 示波器检查电源纹波、时钟、NRST、上掉电/复位毛刺；实物料号与晶体参数、原理图标注仍需归档 |
| M1 HAL + FreeRTOS + Vendor USB | 本轮 HAL 基础 Linux HIL 通过 | CubeMX 再集成；SRAM hook、USART1、双时基、PB2 0→1→0；烧录读回、106 次回环、MS OS/STALL；250 ms 停读、句柄重开、半包后 USB 复位恢复 | Windows 实机；看门狗及复位原因；冷启动、正反插、挂起/恢复、物理拔插与长时压力 |
| M2 CAN + Rust 分析工具 | 升级、confirm和revert实机通过，CAN待实现 | v2规范与编解码；MCUboot双槽；Rust 512字节分块；0.1.0→0.1.1确认持久化；0.1.2未确认回滚；Product data保持不变 | 验证擦除、写入和swap中途断电恢复；实现 HAL FDCAN 后端、内部回环及外部收发 |
| M3 定时显性脉冲 | 未开始，波形验收等待示波器 | 硬件门控逻辑及 TIM2 单脉冲方案已规划 | TIM2/ARM 事务、参数边界、取消与失联退出；数字端和 CANH/CANL 波形；并发负载下脉宽验收 |
| M4 错误与恢复观察 | 未开始，依赖 M2/M3 | 试验方法已规划 | 两个正常 CAN 节点与 X-CAN 的明确拓扑；注入前后帧序号、DUT 错误状态、波形和恢复时间证据 |
| M5 双平台联调与交付 | 未开始 | Rust Linux/Windows CLI 已产出，Linux 权限规则可用 | 双平台长期采集、拥塞/恢复、温升及性能范围、工具分发和操作文档；完整 GUI 范围未定义 |

M0 波形延期不阻塞基础软件和内部回环；不免除 M3/M4 的物理波形验收。原计划工期是设备可用时的初始估算，本次不据此推算完成日期。

## 已验证恢复基线（本轮 HAL）

- 板型：xcan_g431 / STM32G431RB，DBGMCU_IDCODE=0x20036468；OPTR=0xFBEFF8AA，未修改选项字节。
- Zephyr：主机 /home/gtc/zephyrproject/zephyr，4.4.99，提交 c199f92c7e4bba820573d6be9ba0c75385601b67；实际 HAL/CMSIS 提交见 [环境记录](validation/2026-09-14/environment.json)。
- 板上当前为 build/Release/xcan.bin，SHA256 为 ec2939ce68e6037281e564fd161e4cdefb5dd8203b22ca8f498c8a58242af8ca；完整编程前备份为 build/hil-reintegration/preflash.bin，哈希见本轮 backup-result.json。历史镜像与当前固件必须区分。
- 当前链接占用：Flash 26,876 B / 128 KiB，RAM 预留 9,720 B / 32 KiB；未做 CAN 缓冲压力或任务栈高水位实测。
- 实机：USB C0CA:0313，序列号 2036365058315010002D0055；J-Link 63728769；USART1 经 /dev/ttyUSB0。Serial Studio 与自动串口捕获需轮流占用。
- 当前功能：list / info / echo / self-test；PB2 为人工终端声明，0→1→0 已通过。safe=1 是软件模式字段，不能代替电气测量。
- 当前协议固定 64 字节记录，最大载荷 52 字节、单在途事务；不能直接承载完整 64 字节 CAN-FD 数据加元信息。[v2规范](USB_CAN_PROTOCOL_V2.md)已定义变长消息、完整FD帧、批量与拥塞规则，并有独立双端编解码和本地测试；尚未替换板上v1或接入CAN。

## 工作区职责与保留策略

| 路径 | 用途 / 当前处理 |
|---|---|
| legacy/zephyr/boards/xcan/xcan_g431/ | 板型、设备树、启动 hook；硬件映射的实现入口 |
| Core/、firmware/src/ | HAL 板级初始化、RTOS 应用、USB 适配、共用协议；Zephyr 文件在 legacy/zephyr/ |
| host/ | Rust CLI、固件升级、Cargo 锁文件与 Linux udev 规则；尚无 CAN 分析或桌面 GUI |
| tests/、tools/ | C 协议测试；构建、静态产物检查、串口与 J-Link 脚本入口 |
| docs/Schematic/、docs/Reference/ST/ | 原理图与本地资料；PDF 晶振仍标 8 MHz，实物和固件为 12 MHz |
| docs/validation/2026-09-11/ | 4.4.0 历史评审及 M1 证据，保留当时结论 |
| docs/validation/2026-09-14/ | 环境迁移、4.4.99 构建及 hil/ 实机证据；二进制备份留本地，哈希与文本记录保留 |
| build/system/ | 当前构建输出，约 40 MiB；.clangd 现指向 build/Release |
| build/bringup/、build/usb/ | 历史构建约 19/28 MiB，包含恢复所需原始备份；保留，不用于默认新构建 |
| host/target/ | Linux/Windows 主机产物，约 72 MiB；Windows 构建成功，实机待验 |
| .deps/zephyr、hal_stm32、cmsis_6 | 历史检出约 495 MiB、1.1 GiB、21 MiB；默认构建已解除依赖。本次只登记，不删除，尚未回收空间 |
| .deps/mingw、mingw-debs | Windows 交叉工具链/下载包约 386/62 MiB；与 Zephyr 模块分开看待，保留 |
| legacy/zephyr/west.yml | 4.4.0 历史复现清单；当前 tools/build.py 使用仓库 HAL 工程；归档 tools/build.py 复用外部 Zephyr |
| .clangd、.vscode/、.codegraph/ | 编辑器与索引配置；保持现有配置，不重新索引 |

占用为本次 du -sh 近似值。当前未提交内容包括环境迁移/USB 回调适配、构建与编辑器路径、9 月 14 日证据和本次文档整理；本次不自动提交。

## 接下来执行顺序

1. **Boot/升级 HIL**：烧录冻结 Boot 与当前 factory 镜像，使用 Rust update 写入不同版本，验证 test boot、1秒健康确认、未确认回滚、升级中断电和 Product data 保持。
2. **M2 内部回环**：采用 HAL FDCAN 后端，复核时钟、初始化与勘误相关路径；实现隔离外部总线的回环入口。按已测试v2线格式接入USB/FDCAN，落实有界缓冲、队列调度和设备会话状态机。Windows 待测项可保留，但不将 M1 标为全部完成。
3. **M2 物理通信**：确定对端适配器/节点、终端与线束，在明确台架上按普通 CAN、静默监听、FD、USB 帧流及丢失统计逐项验收。
4. **M3/M4 注入**：待正常通信和示波器条件具备后，先测数字脉冲，再验证总线错误与恢复。
5. **M5 交付**：以实测范围收敛吞吐、时间精度、跨平台行为和发布说明。

当前无需重新下载 ST 手册或重做最小 LED 测试。待外部条件主要为 Windows 实机、示波器、两个正常 CAN 节点及其 FD 能力。

## 证据入口

- [当前 HAL 烧录与 HIL 报告](validation/2026-09-14/cubemx-reintegration/REPORT.md) / [机器可读结果](validation/2026-09-14/cubemx-reintegration/result.json)
- [历史 Zephyr HIL](validation/2026-09-14/hil/REPORT.md)
- [环境迁移记录](validation/2026-09-14/ENVIRONMENT_REUSE.md)
- [历史 M1 实施记录](validation/2026-09-11/M1_IMPLEMENTATION.md)
- [硬件评审与完整验收方案](HARDWARE_REVIEW_AND_PLAN.md)
- [现行 USB bringup 协议](USB_BRINGUP_PROTOCOL.md)
- [CAN协议v2](USB_CAN_PROTOCOL_V2.md) / [本地测试记录](validation/2026-09-14/protocol-v2/REPORT.md)
