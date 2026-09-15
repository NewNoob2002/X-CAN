# 轻量方案 M1 上板可行性验证

2026-09-14：**FreeRTOS + CherryUSB 的 M1 基础可行性通过**。板卡当前运行修复后的对照固件；主线 Zephyr 源码保留。后续按用户确定的方向，由 CubeMX 生成 HAL 配置与驱动，以 HAL 为主，执行时序敏感路径才使用 LL。本轮不扩展成正式迁移。

## 已完成

- J-Link 63728769 / STM32G431RB，SWD 1000 kHz；全量备份 128 KiB Flash，镜像前缀匹配已验证 Zephyr。备份保存在本目录 pre-flash.bin（git 忽略），哈希见 [备份结果](backup-result.json)。
- 对照版烧录到 0x08000000，15,932 B 独立读回校验成功。RAM 静态占用仍为 8,024 B。修复版 BIN SHA256：5c11f9e61b5632c4704de7df28a84ba7ddbd41c3e24c9c3e13c8586df1833cf6。
- 干净复位后串口连续约 20 秒心跳，CPU 144 MHz、USB 初始化成功、PB2=0。见 [启动日志](clean-boot.log)。
- Linux USB C0CA:0313 / 2036365058315010002D0055：INFO、106 次 0..52 B 整包/分片 ECHO、畸形长度拒绝通过；MS OS 162 B 描述符、截断长度和错误请求 STALL 恢复通过。见 [USB 记录](usb-regression.log)。
- PB2：初版对照读到 0，用户闭合后读到 1；用户断开后，修复版启动和 USB 均读到 0。未再要求用户重复修复版 0→1→0，保留版本边界。
- 运行态 GPIO 模式及 IDR/ODR：PC4/STB=1、PC5/ARM=0、PA1=0、PB9/TXD=1；PB2=0。只是寄存器证据，未用仪器重新测量电压或波形。
- RCC 寄存器对应 HSE 12 MHz、PLL M3/N72/R2/Q6、SYSCLK 144 MHz、USB 48 MHz；FLASH_ACR=0x00040604。OPTR 仍为 0xFBEFF8AA，没有改动启动选项。
- 运行态正常连接、halt/read/resume 通过，无自动连接复位；CFSR/HFSR 均 0，六个 FPB 断点比较器均清零。见 [最终运行状态](runtime-registers.log)。

## 本轮发现和修复

初版整字写 FLASH_ACR 设置缓存/等待周期，误清 bit18 DBG_SWEN，导致运行后 SWD 不可连接，但串口/USB 仍工作。RM0440 的 FLASH_ACR 定义明确 0 禁用、1 启用调试。修复为读改写，仅替换 LATENCY 并启用缓存，保留调试和其他位；位置为 comparison/freertos_cherryusb/main.c 的 clock_init。

修复后的连接验证确认 DBG_SWEN=1，问题关闭。定位过程中的旧硬件断点曾触发 HFSR.DEBUGEVT；已清理比较器并执行干净复位，最终 HFSR=0。清理工具曾提示无法清理 DWT_FUNCTION2；最终无故障，FPB 六项清零，不把该工具提示扩展为完整 DWT 比较器验证。

保留初次 flash.log、debug*.log 失败记录；fixed-flash.log 的开始部分仍是旧固件下的连接复位，不能误读为修复版仍有失联。以 clean-reset.log、clean-boot.log、runtime-registers.log 和修复后 USB 回归为最终验收依据。工具审批曾超时一次，后续执行成功，与固件故障分开记录。

修复后本地 3392 组 USB 分片集成测试及 C v1 回归通过。原同功能对照 result.json 保留为修复前历史记录；当前产物和源码哈希见 [HIL 结果](result.json) 及 [构建元数据](build-metadata.json)。

## 边界与下一节点

本轮没有 CAN 发送、故障注入、选项字节写入或全片擦除。板卡最终运行修复版，安全输出保持；旧 Flash 完整备份可恢复。

尚未验证 Windows 实机、冷上电/拔插/挂起恢复、长期负载、栈水位、实际波形、FDCAN/v2、注入与 bootloader。当前结论是轻量 RTOS + USB 基础成立，不是完整分析仪或 OTA 验收。

下一步等待用户生成 CubeMX 项目，再接入已验证的协议和 CherryUSB。HAL 负责常规初始化/驱动，LL 用于经实测确认的敏感路径；保留 SRAM 首次访问勘误 hook、12 MHz HSE、安全输出上电顺序和 Flash 调试位。CubeMX/HAL 接入后重新测量体积，不直接沿用本对照的 15.56 KiB 预算作为完整应用占用。
