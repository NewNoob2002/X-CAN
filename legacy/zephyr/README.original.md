# X-CAN

STM32G431RBT6 CAN/CAN-FD分析仪，计划支持定时显性脉冲注入。实装晶振12MHz（PDF仍标8MHz）。当前实现为板级启动、串口、终端人工声明和Vendor USB验证固件；CAN与注入尚未启用。

当前计划为 **M1收尾，M2协议基础已完成、CAN待接入**。节点状态、工作区职责与下一步见 [项目状态](docs/PROJECT_STATUS.md)。

## 当前固件

- 当前构建复用主机Zephyr 4.4.99，已完成烧录与Linux USB HIL；历史基线为v4.4.0，独立板型xcan_g431，直接从0x08000000启动。不修改选项字节；实机OPTR保持0xFBEFF8AA。
- ES0431 SRAM首次写入规避在最早期汇编hook中执行。当前支持已验证的parity disabled配置；检测到parity开启则停在具名汇编位置，不自动改选项字节。
- HSE12MHz / M3 × N72，CPU144MHz、USB PLLQ /6 =48MHz；未做频率波形测量。
- PC5/ARM低、PC4/STB高、PA1注入低、PB9/TX高；LED_STATUS/PB0每秒翻转。
- USART1 PA9/TX、PA10/RX，115200 8N1；日志只需TX接串口RX及共地。
- PB2外部下拉，10ms采样，连续4次相同后更新声明；不自动检测120Ω。
- USB C0CA:0313，单Vendor接口、Bulk IN/OUT、芯片UID序列号、MS OS 2.0 WinUSB描述符，无remote wakeup。

## 构建固件

构建复用已有Zephyr west工作区及其模块，不创建或下载.deps依赖。通过ZEPHYR_BASE或--zephyr-base选择源码，使用该环境的Python运行脚本。本机使用Zephyr SDK1.0.1、GNU Arm14.3.0和Python3.12。

在项目根目录执行：

    export ZEPHYR_BASE=/home/gtc/zephyrproject/zephyr
    export ZEPHYR_SDK_INSTALL_DIR=/home/gtc/zephyr-sdk-1.0.1
    /home/gtc/zephyrproject/.venv/bin/python tools/build.py
    /home/gtc/zephyrproject/.venv/bin/python tools/build.py --usb
    /home/gtc/zephyrproject/.venv/bin/python tools/check_build.py build/system/bringup
    /home/gtc/zephyrproject/.venv/bin/python tools/check_build.py build/system/usb

输出分别为build/system/bringup/zephyr/和build/system/usb/zephyr/，含ELF/BIN/HEX/map；工具链能力缓存位于build/.cache。check_build.py检查复位向量、hook顺序和配置，生成build-metadata.json与startup-disassembly.txt。更换Zephyr源码目录或工具链前，移走对应build/system输出目录，避免沿用旧CMake缓存。

本机Zephyr为4.4.99（c199f92c7e4bba820573d6be9ba0c75385601b67），已完成编译、烧录和Linux USB HIL，PB2声明状态0→1→0通过。开发版API可能继续变化，不代表任意4.4.99提交均兼容。当前MS OS描述符回调保留4.4.0分支。原west.yml保留为2026-09-11实机基线的版本记录，当前构建脚本不读取它，也不自动执行west update。

原build/bringup和build/usb保持为历史产物目录。.deps下的zephyr、hal_stm32、cmsis_6不再用于默认构建，本轮未删除；mingw及mingw-debs属于Windows交叉构建工具，不能当作Zephyr依赖一并清理。本次环境迁移记录见docs/validation/2026-09-14/ENVIRONMENT_REUSE.md，上板结果见docs/validation/2026-09-14/hil/REPORT.md。

旧LED固件完整128KiB备份：build/bringup/previous-flash.bin，请保留。当前4.4.99镜像的已验证烧录脚本为docs/validation/2026-09-14/hil/flash.jlink；build/usb/flash.jlink仅对应历史4.4.0镜像。再次烧录前核对镜像哈希、目标和地址。脚本不修改选项字节或mass erase。

## Rust工具

    cargo build --release --manifest-path host/Cargo.toml --locked
    host/target/release/xcan list
    host/target/release/xcan info 2036365058315010002D0055
    host/target/release/xcan echo 2036365058315010002D0055 hello
    host/target/release/xcan self-test 2036365058315010002D0055

设备操作要求明确的非空序列号；超时2秒，失败不自动重发。self-test仅做有界USB回环，不产生CAN流量。

Linux永久权限（本机gtc已在plugdev组）：

    sudo install -m 0644 host/70-xcan.rules /etc/udev/rules.d/70-xcan.rules
    sudo udevadm control --reload-rules
    sudo udevadm trigger --action=add --subsystem-match=usb --attr-match=idVendor=c0ca --attr-match=idProduct=0313
    sudo udevadm settle

规则匹配指定VID/PID，拔插和重启后仍适用。规则位于73-seat-late.rules之前，支持当前本地会话的uaccess授权。

Windows程序已交叉构建至host/target/x86_64-pc-windows-gnu/release/xcan.exe，命令相同。Windows10/11的实际WinUSB绑定、枚举及回环仍待测试；构建成功不等于Windows实机通过。在Windows本机编译需Rust及配套C构建工具，vendored特性自动编译libusb。

CAN帧传输规范见 [USB/CAN协议v2](docs/USB_CAN_PROTOCOL_V2.md)：已实现独立C/Rust编解码和本地测试，当前CLI/板上固件仍使用v1。

## 检查

    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/src tests/protocol_test.c firmware/src/protocol.c -o build/protocol-test
    ASAN_OPTIONS=detect_leaks=0 build/protocol-test
    cargo test --manifest-path host/Cargo.toml --locked
    cargo clippy --manifest-path host/Cargo.toml --locked -- -D warnings

协议见docs/USB_BRINGUP_PROTOCOL.md；实施记录见docs/validation/2026-09-11/M1_IMPLEMENTATION.md。
示波器项目延期；CAN/FD、注入、看门狗、完整异常恢复和Windows实机仍按后续阶段验收。
