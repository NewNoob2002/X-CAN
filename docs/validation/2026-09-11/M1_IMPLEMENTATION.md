# M1 基础固件实施记录

日期：2026-09-11。本阶段完成基础固件与Linux USB最小闭环，未宣称M1所有验收项已关闭。

## 已完成

- Zephyr v4.4.0独立板型xcan_g431，12MHz HSE、CPU144MHz、USB48MHz PLLQ。
- SRAM首次写入规避位于复位入口第一条调用中，在设置栈及RAM写入之前执行。检查了最终ELF反汇编；仅支持当前已验证的parity disabled配置，不写OPTR。
- 原固件完整128KiB Flash已备份。USB固件经Commander编程及verifybin独立校验通过，OPTR前后均为0xFBEFF8AA。
- USART1 115200 8N1启动与连续心跳通过；STB高、ARM低、注入低、TX高由新固件配置。
- 用户操作声明拨码，串口/USB读到闭合1、断开0。电阻接入仍独立，字段为人工声明。
- Vendor USB C0CA:0313，序列号2036365058315010002D0055；Linux枚举、设备信息、106次整包/拆分回环和越界长度拒绝通过。
- Linux永久udev规则由用户安装并验证生效。
- Rust Linux工具及Windows x86_64程序已构建。Windows依赖工具仅解压到.deps/mingw，未安装系统包；exe导入表仅列Windows系统DLL。
- C解析器在ASan/UBSan下通过边界检查；Rust单元测试通过，Clippy无告警。

## 证据与版本

固件.bin SHA256：addfc52f8a5d31f8388388b55c7246314b5a5f5295eb8700f4b6ef53fb588390。
镜像大小61744字节，链接RAM使用13640字节；不包含运行时栈最高水位验收。
旧Flash备份SHA256：580d48a9232b60103e3e6c0dd6ccc905483cbff5838313ac4ce95b4669234183。

- usb-flash.log：最终编程、verifybin、复位运行及OPTR记录。
- usb-boot-fixed.log：USB init=0、144MHz、终端声明1与持续SAFE心跳。
- usb-functional.log：两个声明状态的信息查询及回环结果。
- m1-registers.log：暂停时读取RCC/GPIO/OPTR，随后恢复运行。
- build/usb/build-metadata.json及startup-disassembly.txt：镜像/配置哈希与启动汇编。
- build/bringup/previous-flash.bin：原LED固件备份，不进入git。

最终暂停快照：RCC_PLLCFGR=0x11514823、CFGR=0xF、CCIPR=0x08000000（CLK48SEL=2），GPIOC MODER=0xFFFFF5FF、IDR/ODR=0x10；GPIOA ODR=0、GPIOB ODR=0x201，PB2输入为0。这与PC4高、PC5低、PA1低、PB9高相符；不是模拟电平或波形测量。

此前一次运行中读取发生在CPU睡眠状态，多处外设读回相同的0x08005FA9，已判定该次外设快照不可信，未用于结论；随后暂停读取获得上述一致结果并恢复运行。

## 实现过程中的问题

MCP通用/专用烧录接口未返回可用编程证据；Commander只读检查确认目标正常后，改用记录完整日志的CLI烧录并独立校验。首次USB实现漏掉Zephyr要求的类init回调，返回-ENOTSUP；补齐回调后启动及回环通过。失败镜像、诊断和当前镜像的关系记录在usb-preflight.json，不将失败尝试记为通过。

源码评审核查了固定长度解析、响应清零、事务ID、缓冲所有权、旧配置回调epoch、GPIO输出初始化顺序、parity分支和启动hook。当前USB为一个在途事务，无CAN输出命令。USB传输错误后需要重新配置；不完整请求不保证自动恢复边界，限制见协议文档。

## 环境与后续

本机原Zephyr是4.4.99开发版（c199f92c7e4bba820573d6be9ba0c75385601b67），本次验证使用4.4.0（684c9e8f32e4373a21098559f748f06915f950c9）与其manifest依赖。SDK和Python直接复用主机安装；.deps中的三个源码检出是为版本隔离建立的本地共享克隆，不是环境缺失。用户指出副本占用偏大；当前尚未删除或默默改用4.4.99，后续环境整理需保留已验证版本证据并重建验证。

待办：Windows实机WinUSB绑定/回环，USB挂起/拔插/主机停读等更完整异常恢复，看门狗，CAN/FD分析与注入。示波器波形按用户要求延期，不阻塞已完成的基础工作，也不作为通过项。未发送任何CAN帧或注入脉冲。
