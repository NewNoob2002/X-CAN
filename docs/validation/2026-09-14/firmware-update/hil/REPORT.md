# X-CAN Boot与USB升级实机验证

日期：2026-09-14。目标板：STM32G431RB；J-Link序列号：`63728769`；USB序列号：`2036365058315010002D0055`。

## 结果

- PASS：0.1.0 factory 从 `0x08000000` 烧录到 `0x0801F7FF`，J-Link 校验129,024字节成功。
- PASS：USB枚举为 `c0ca:0313`，INFO返回144 MHz和IDCODE `0x20036468`，106组控制通道自检通过。
- PASS：Rust上位机按512字节块写入35,630字节的0.1.1签名镜像，设备完成SHA-256检查、pending标记和复位。
- PASS：Primary头由0.1.0的`0x00000100`变为0.1.1的`0x00010100`；健康确认后再次主动复位，Primary仍为0.1.1。
- PASS：Rust上位机写入35,636字节的0.1.2测试镜像；在10秒确认窗口内主动复位后，MCUboot回滚到0.1.1。最终Primary为0.1.1，Secondary保留0.1.2，Primary trailer包含magic、`swap_info=4`、`copy_done=1`和`image_ok=1`，与已完成revert一致。
- PASS：Product data `0x0801F800..0x0801FFFF` 前后逐字节相同，SHA-256均为`d0ff1b294b5288d1ae1421eadf5b2d38a8752b76d472ff30bed9028e25b1c5b8`。
- PASS：最终运行状态为已确认的0.1.1，USB INFO和106组自检再次通过。

## 实机发现与修复

首次factory启动时CPU停在`xcan_fatal()`，USB未枚举。Primary头和向量正确，根因是CherryUSB路径在`MX_USB_PCD_Init()`开头直接返回，跳过了CubeMX生成的`HAL_PCD_MspInit()`，导致USB 48 MHz PLL时钟选择未执行并触发板级时钟自检。

修复位于`Core/Src/main.c`的CubeMX `USB_Init 0`用户区：设置`hpcd_USB_FS.Instance`并调用现有`HAL_PCD_MspInit()`，继续跳过HAL PCD控制器初始化。顶层CMake结构和冻结Boot均未修改。修复后Release Boot仍为14,512 B，SHA-256仍为`ff10334bfefae8d39093619af2af74fc8b9c19764bce12738cf6e75d67822b8e`。

## 固定测试镜像

| 镜像 | 大小 | SHA-256 |
|---|---:|---|
| 0.1.0 clean factory | 129,024 B | `a81acb430190512286919f57157454e0ea0b8185541046a7dd006d7a83a84312` |
| 0.1.1 signed | 35,630 B | `c1037f4b43c150d80c8b087f4670e0ae9671cfc2c0a772821292fd76daa3de57` |
| 0.1.2 signed，10秒确认延迟 | 35,636 B | `926dcca508288b6acdbc4cf46c7a1d82f10f22e00cd588079a340018ec23d67b` |

测试前完整128 KiB Flash备份为`pre-test-flash.bin`，SHA-256为`973c890b0e6913c9689641903ea402de19e34f51d22ad2c3160e2af69138dc7d`。

## 软件复验

- `python3 tools/test.py`通过，包括C ASan/UBSan、升级状态机、真实CherryUSB core适配和Rust 12项测试。
- Release：App 34,968 B，签名镜像35,632 B，RAM 12,216 B；`tools/check_build.py`通过。
- Debug：App 41,012 B，签名镜像41,675 B，RAM 12,216 B；`tools/check_build.py`通过。
- `cargo clippy --all-targets --locked --offline -- -D warnings`和`git diff --check`通过。

## 证据与边界

烧录、升级、复位、镜像头、trailer、串口和Product data证据均保存在本目录。串口日志确认正常启动、CherryUSB初始化和`MCUboot confirm=0`。

本轮验证覆盖受控复位触发的未确认回滚，没有执行擦除、写入或swap中途的物理断电测试。该类断电恢复仍是量产冻结前的独立HIL项目。
