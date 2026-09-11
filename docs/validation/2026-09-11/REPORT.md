# X-CAN 实机与 Zephyr 驱动核对

后续实施更新：基础固件已构建、烧录并通过Linux Vendor USB回环，PB2两个状态已验证；见[M1实施记录](M1_IMPLEMENTATION.md)。下文保留实施前的LED固件快照与驱动审计，不能将其中“尚未构建”等历史状态视为当前状态。

日期：2026-09-11。范围：现有 LED 固件的只读寄存器检查、用户提供的电压测量、指定源码版本静态核对。未构建或烧录 Zephyr，未执行 CAN 发送、故障注入或主动复位。

## 1. 实机证据

原始记录见 [jlink-registers.txt](jlink-registers.txt)，结构化记录见 [evidence.json](evidence.json)。探针序列号 63728769，目标 STM32G431RB。用户修正接口并自行通过 JLinkExe 验证后，本次全部 mem32 读取成功；NRST 已接通。之前一次补读超时没有得到寄存器值，不能作为芯片状态证据。

| 项目 | 结果 | 解释与限制 |
|---|---|---|
| DBGMCU_IDCODE | `0x20036468` | REV_ID=`0x2003`，ES0431 对应修订 X；DEV_ID 按 CMSIS 的低 12 位掩码为 `0x468`。当前 SVD 将 DEV_ID 错标为 16 位，不能采用它输出的 `0x6468` |
| DHCSR，读取前/后 | 均为 `0x01010001` | S_HALT=0；本轮未发 halt/reset/write，正常退出连接。运行中逐项读取，不是原子快照 |
| 3V3_SYS / 5V_SYS | 3.31 V / 5.14 V | 用户用 UT33B+ 提供的直流读数；分别在已核对的 USB VDD 3.0–3.6 V、收发器 VCC 4.5–5.5 V 范围内。未验证纹波、负载压降、U2 引脚电压 |
| J-Link VTref | 3.407 V | 探针报告值，不能替代万用表电源验收，也不与其平均 |
| 默认控制电平，用户万用表实测 | PC4/STB≈3.3 V；PC5/ARM≈0 V；PA1≈0 V；U2 TXD≈3.3 V | 当前 LED 固件、CAN 端子脱离工作总线条件下，四项符合预期。数值为用户近似读数，不证明上电瞬态、逻辑门动态通路或注入波形合格 |
| FLASH_OPTR | `0xFBEFF8AA` | nSWBOOT0=0、nBOOT0=1，对照 RM0440 表5为主 Flash 启动；RDP=0xAA。只读，未更改选项字节 |
| RCC_CFGR | `0x0000000F` | 当前系统源为 PLL；AHB、APB1、APB2 未分频 |
| RCC_PLLCFGR | `0x11005523` | HSE、M=3、N=85、R=2，PLLR 开启。用户随后确认实装 12 MHz（原理图 8 MHz 标注未改），故推算系统时钟为 `12 / 3 × 85 / 2 = 170 MHz`；此前基于 8 MHz 的 113.33 MHz 推算不适用于实物。尚未测频率，也未读取电压档位/boost 配置 |
| RCC_AHB2ENR | `0x00000023` | GPIOA/B/F 开启，GPIOC 关闭 |
| GPIOC 读取 | 六个字均 `0xFFFFFFFF` | 时钟关闭，不能作为 PC4/STB、PC5/ARM 模式或电平证据；没有为读取而改时钟 |
| PA1、PB2、PB8/9、PA9/10 | MODER 对应字段均为 3（模拟模式） | 当前 LED 固件未在这些脚启用注入、终端数字输入、FDCAN 或 USART1 复用。模拟模式下 IDR 不能验证外部电平 |
| 串口接线 | USART_TX → `/dev/ttyUSB0`，用户确认 | 波特率未提供，当前 PA9 未复用 USART1；未猜波特率或宣称日志已通过 |

寄存器地址、位掩码以 STM32G431 CMSIS 头文件与本地 RM0440 复核；修订映射来自本地 ES0431 Rev9 表1。未取得匹配 LED 固件的 ELF，因此不作函数级执行状态判断。

## 2. 源码基线

建议实施基线仍为 Zephyr v4.4.0，提交 `684c9e8f32e4373a21098559f748f06915f950c9`；其 west.yml 锁定 hal_stm32 `39130f29ae37c1db34095478ca02b6419b70dcdc`。

机器现有 Zephyr 为 `c199f92c7e4bba820573d6be9ba0c75385601b67`（describe: `v4.4.0-12514-gc199f92c7e4`），HAL 为 `33576ef05e529cad803f210cc95b52b607757c96`，均为核对时的干净工作树。它们不等于上述锁定基线。本轮从本地 git 对象提取并比较源码，未切换或修改该安装目录。以下路径相对 Zephyr 根目录，行号以 v4.4.0 为准。

## 3. 驱动结论与最小处理

| 项目 | 已核对的实现 | 首版处理 |
|---|---|---|
| SRAM 首次写入勘误 | `soc/st/stm32/stm32g4x/soc.c:30` 的 early-init、`arch/arm/core/cortex_m/reset.S` 和 `prep_c.c` 路径未找到 ES0431 §2.2.7 指定的首次写前处理；G4 未选择 early-reset hook。现有 HEAD 同类路径也未发现规避 | M1 启动验收项：实现最早期、首次 SRAM 写入前的规避，按实际 parity 选方法，并检查最终反汇编。不能在 main 中补读后宣称解决；LED 能跑不证明罕见复位条件已覆盖 |
| USB HSI48 校准 | G4 `drivers/clock_control/clock_stm32g4.c` 默认时钟路径没有 CRS 初始化；公共时钟代码使能 HSI48 并等 ready。UDC 的 48 MHz 检查验证配置频率，不测精度。现有 HEAD 同样未见 G4 CRS 处理 | 首版优先采用已有 HSE 经 PLL 产生 USB 48 MHz；若选择 HSI48，必须补 CRS/SOF 配置和验证。更新原先“优先 HSI48”建议 |
| FDCAN 外部时间戳 | `drivers/can/can_stm32_fdcan.c:471` 初始化支持 `external-timestamp-counter`，启动 counter 后选择 TSCC.TSS=2 | 复用驱动 + TIM3 counter，配置恒定时基；软件仍需处理 16 位回绕，不重写 CAN 驱动 |
| 监听 | `drivers/can/can_mcan.c:403` 的 set_mode 使用 MON | 使用 CAN_MODE_LISTENONLY；后续台架验证无 ACK，不能用 ASM 替代 |
| 错误统计 | `can_mcan.c:626` 的 PSR 包装读取在 CAN_STATS 下累计 LEC/DLEC；ISR 使用该路径。`:876` 的 get_state 读取 ECR 但只交付 TEC/REC，未暴露 CEL | 开 CAN_STATS，复用 API；应用不再并行读 PSR/ECR。CEL 不能由旁路轮询拼成完整计数；标准统计也不保证每个错误的精确次数和时间 |
| bus-off 恢复 | `can_mcan.c:481` 状态处理区分自动/手动恢复，自动路径清 INIT | 明确选恢复策略并记录；本机状态不等于 DUT 状态，DUT bus-off 须独立证据 |
| EFBI 勘误 | `can_mcan.c` 的 init 清 FDOE/BRSE/TEST/MON/ASM，但未显式清 EFBI；普通复位默认 0 | 初始化时明确保证并核查 EFBI=0；不能把“无显式规避”说成正常冷启动必然失效 |
| 发送次序 | STM32 init 选择 TXBC.TFQM，使用 queue，未形成勘误所指 dedicated buffer + FIFO 混用 | 首版若要求主机顺序，只保留一个 pending TX；按 ID 的正常优先级调度不是勘误 |
| 接收次序，版本差异 | v4.4.0 `can_mcan.c:1123` 等过滤器路径按 filter_id 奇偶分流 FIFO0/1；`:819` ISR 先处理 FIFO0 再 FIFO1，跨 FIFO 回调可能重排。现有 HEAD 已将过滤器统一导向 FIFO0，并明确说明保序目的 | MVP 各一个标准/扩展全接收过滤器，确保各自索引0落 FIFO0；需要多个过滤器时回移保序修复。保留溢出统计，不把两个 FIFO 容量当一个有序队列 |
| DMA 全局清标志 | `drivers/dma/dma_stm32.c:77` 包装逐项清 TC/HT，再调用 v2 的 TE 清除（`dma_stm32_v2.c:286`）；IRQ 正常路径亦分别清 HT/TC。虽有 clear_gi 定义，本次检查路径未调用 | 未发现该路径违反 ES0431 §2.3.1；首个 TIM2 脉冲仍无需 DMA。不要仅凭存在 GI 函数就报驱动缺陷 |
| USB remote wakeup | `drivers/usb/udc/udc_stm32.c:1008` 激活 resume、等待2ms、撤销；所选 G4 HAL/LL 路径未见勘误要求的约3ms SUSP 屏蔽处理 | 首版不启用设备 remote wakeup；仍须验证主机挂起/恢复 |
| USB PMA CRC16 | UDC 使用 HAL 接收长度，G4 HAL/LL 按 payload 长度搬运 PMA，未把附带 CRC16 当应用数据校验 | 不因该勘误额外加重试机制；真实 Bulk 数据完整性仍由双平台已知数据测试验证 |
| TIM2 OPM | 首版规划的 PWM2、OPM、SMS=0、MSM=0 不使用相关级联/连续 toggle 触发条件 | 保持简单软件启动方案；用示波器验收，而非增加 DMA |

以上是相关执行路径的静态审查，不是完整 Zephyr/HAL 安全审计，也不代表已验证最终 Kconfig、设备树、链接结果或负载性能。低功耗 flash gating、DMAMUX 高级模式在首版启用前按实际配置重新核对。

## 4. 剩余样板测量与推进顺序

1. **静态控制电平已完成**：用户确认 CAN 端子脱离工作总线，并测得 PC4/STB≈3.3 V、PC5/ARM≈0 V、PA1≈0 V、U2 TXD≈3.3 V，符合默认待机、撤防及无注入请求的静态预期。证据来自万用表，不是时钟关闭的 GPIOC 寄存器或模拟模式 IDR。上电/复位/掉电瞬态仍待示波器测量。
2. **终端静态阻值检查已通过**：用户复测表笔悬空显示 OL（原文0L）、短接显示0；固定 CANH/CANL 测点、不移动表笔，仅切换拨码，断开显示 OL、接入为120 Ω。结果符合图示 CAN1_H → SW3 → R13（120 Ω）→ CAN1_L 支路的接入/断开预期。此前报告的断开0.0 Ω已被本次对照复测更正，疑似低阻异常关闭，不再据此暂停后续验收或要求返修。OL只记录为所用量程超限，不赋予精确电阻值。另一个声明拨码的 PB2 逻辑仍待后续数字输入固件验证；物理发送/注入仍按原定阶段和明确实验条件开展。
3. **DS100 波形，按用户要求延期**：等待示波器可用后再测，不阻塞基础固件工作，也不记为验收通过。保留电源纹波、上掉电/NRST释放、ARM/注入/TXD瞬态及频率/脉冲时序测量。恢复测试时地夹仅接板上 GND，探头倍率与仪器设置一致；未确认通道和接地条件前，不将地夹夹到 CANH/CANL。NRST 已接通不等于复位波形合格，本次未主动复位验证 C16。
4. **当前推进 M1**：先锁定版本、落实 SRAM 启动处理，按实装12MHz建立独立 board，配置安全默认控制输出、USART1日志和PB2声明输入；随后完成USB时钟、Vendor USB与Rust最小双平台回环。当前LED固件的PA9是模拟模式，需新固件启用USART1后才能验证串口日志。PB2声明状态仍与电阻接入状态独立验证。
5. **M2–M4**：完成正常 CAN/FD、监听和丢失统计，再做有界脉冲及两正常节点的错误/恢复实验。用户已确认 CAN 端子脱离工作总线，本轮没有任何物理发送或注入。

结论：修订号与 Flash 启动配置已由实机关闭，直流电源、四项默认控制电平及终端接入/断开阻值获得用户测量支持。此前疑似低阻异常经固定测点复测已关闭，无需因此改板或更换收发器。用户要求示波器相关项延期，当前转向Zephyr基础固件、PB2声明逻辑及USB验证；M0保留延期项，不标记为全部验收完成。注入脉宽、毛刺和取消时序的实测要求保留在M3验收中。
