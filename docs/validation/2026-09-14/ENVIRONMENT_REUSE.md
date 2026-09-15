# 2026-09-14：复用主机 Zephyr 环境

默认构建已改用主机现有 west 工作区，不再强制读取项目 .deps。使用 ZEPHYR_BASE 或 --zephyr-base 指定源码，模块由该工作区的 Zephyr/west 发现。脚本不下载依赖，不修改主机工作区；构建缓存写入项目 build/.cache。

## 版本与输出

- Zephyr 4.4.99：c199f92c7e4bba820573d6be9ba0c75385601b67。
- STM32 HAL：33576ef05e529cad803f210cc95b52b607757c96。
- CMSIS 6：b2dfbe1a20bbd49c2d2c605073799671074bbb30。
- 上述源码的 tracked diff 均为空，路径及提交记录于 environment.json。
- 工具链沿用 Zephyr SDK 1.0.1 / GNU Arm 14.3.0，Python 使用主机 zephyrproject/.venv。
- 新输出：build/system/bringup 和 build/system/usb；.clangd 默认编译数据库同步指向后者。

| 固件 | Flash 使用 | RAM 使用 | 构建 | 启动静态检查 |
|---|---:|---:|---|---|
| bringup | 30,480 B | 5,568 B | 通过 | 通过 |
| USB | 67,156 B | 13,704 B | 通过 | 通过 |

## 兼容修改与验证

安装版本的 Vendor to-host 回调返回 net_buf 指针，原 4.4.0 回调则返回 int 并接收协议栈提供的缓冲区。根据本机 Zephyr include/zephyr/usb/usbd.h、subsys/usb/device_next/usbd_ch9.c 和 samples/subsys/usb/webusb/src/msosv2.h 核对契约，调整项目 msos_request：验证请求字段，按 wLength 与描述符长度的较小值分配控制 IN 缓冲区，分配失败或不支持的请求返回 NULL。保留版本条件下的旧回调分支，本轮未重建旧版本。开发版版本号不能唯一标识 API，兼容性仅针对以上确切提交。

两种固件均通过 tools/check_build.py：最早复位指令调用 SRAM hook、hook 无写入/压栈/屏蔽中断指令、Flash 向量与入口一致、144 MHz 配置、未启用 CAN 或 MCUboot。构建日志、反汇编和镜像哈希保存在同目录。C 协议测试使用 ASan/UBSan 通过。脚本缺少 Zephyr 路径、路径无效时均以退出码 2 明确报错。Rust 代码未改，本轮未重复 Rust 测试。

## 保留项与边界

原 build/bringup/previous-flash.bin 和 build/usb/zephyr/zephyr.bin 的 SHA256 与 2026-09-11 记录一致，未覆盖历史镜像。west.yml 保留旧实机基线，默认构建不使用它。

.deps/zephyr、hal_stm32、cmsis_6 不再是默认构建依赖，但本轮未删除，尚未释放磁盘空间。.deps/mingw 与 mingw-debs 为 Windows 交叉工具链相关内容，不属于 Zephyr 重复模块。

本轮没有板卡操作；4.4.99 编译通过不替代上板验证。下一步需使用新镜像重新验证启动、USB 枚举、INFO/ECHO、PB2 两种声明状态及 WinUSB 描述符行为。Windows 实机和示波器测量仍待验证；CAN 与注入仍未启用。

后续已执行新版烧录与 Linux USB HIL，结果见 [HIL 报告](hil/REPORT.md)；以上“本轮”指环境迁移阶段。
