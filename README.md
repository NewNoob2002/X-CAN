# X-CAN

STM32G431RBT6 CAN/CAN-FD分析仪，计划支持定时显性脉冲注入。实装晶振12MHz（PDF仍标8MHz）。当前实现为板级启动、串口、终端人工声明和Vendor USB验证固件；CAN与注入尚未启用。

## 当前固件

- Zephyr v4.4.0，独立板型xcan_g431，直接从0x08000000启动。不修改选项字节；实机OPTR保持0xFBEFF8AA。
- ES0431 SRAM首次写入规避在最早期汇编hook中执行。当前支持已验证的parity disabled配置；检测到parity开启则停在具名汇编位置，不自动改选项字节。
- HSE12MHz / M3 × N72，CPU144MHz、USB PLLQ /6 =48MHz；未做频率波形测量。
- PC5/ARM低、PC4/STB高、PA1注入低、PB9/TX高；LED_STATUS/PB0每秒翻转。
- USART1 PA9/TX、PA10/RX，115200 8N1；日志只需TX接串口RX及共地。
- PB2外部下拉，10ms采样，连续4次相同后更新声明；不自动检测120Ω。
- USB C0CA:0313，单Vendor接口、Bulk IN/OUT、芯片UID序列号、MS OS 2.0 WinUSB描述符，无remote wakeup。

## 构建固件

依赖由west.yml固定。本机.deps为独立的锁定提交检出，不修改原Zephyr工作目录：

| 目录 | 提交 |
|---|---|
| .deps/zephyr | 684c9e8f32e4373a21098559f748f06915f950c9 |
| .deps/hal_stm32 | 39130f29ae37c1db34095478ca02b6419b70dcdc |
| .deps/cmsis_6 | 30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74 |

本机检出使用git clone --shared引用原安装目录的对象；迁移时须重新取得对应依赖，不能只搬走.deps。已验证工具链为Zephyr SDK1.0.1、GNU Arm14.3.0和Python3.12。

在项目根目录执行：

    export ZEPHYR_SDK_INSTALL_DIR=/home/gtc/zephyr-sdk-1.0.1
    /home/gtc/zephyrproject/.venv/bin/python tools/build.py
    /home/gtc/zephyrproject/.venv/bin/python tools/build.py --usb
    /home/gtc/zephyrproject/.venv/bin/python tools/check_build.py build/usb

输出分别为build/bringup/zephyr/和build/usb/zephyr/，含ELF/BIN/HEX/map。check_build.py检查复位向量、hook顺序和配置，生成build-metadata.json与startup-disassembly.txt。

旧LED固件完整128KiB备份：build/bringup/previous-flash.bin，请保留。已验证的烧录脚本为build/usb/flash.jlink；再次烧录前核对镜像哈希、目标和地址。脚本不修改选项字节或mass erase。

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

## 检查

    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/src tests/protocol_test.c firmware/src/protocol.c -o build/protocol-test
    ASAN_OPTIONS=detect_leaks=0 build/protocol-test
    cargo test --manifest-path host/Cargo.toml --locked
    cargo clippy --manifest-path host/Cargo.toml --locked -- -D warnings

协议见docs/USB_BRINGUP_PROTOCOL.md；实施记录见docs/validation/2026-09-11/M1_IMPLEMENTATION.md。
示波器项目延期；CAN/FD、注入、看门狗、完整异常恢复和Windows实机仍按后续阶段验收。
