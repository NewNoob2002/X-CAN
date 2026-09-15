# Zephyr 历史工程

2026-09-14 归档。当前开发入口是仓库根目录的 CubeMX/HAL + FreeRTOS + CherryUSB。

- firmware/：Zephyr M1 与未完成的 USB/FDCAN v2 草稿。CAN 分支缺少 usb_can.c，不可当作可运行功能。
- boards/：Zephyr 设备树/Kconfig；SRAM 勘误汇编共享根目录 Core/Src/early_reset.S。
- tools/：旧 Zephyr 构建、产物检查及 M_CAN 源码补丁工具。
- west.yml：历史依赖清单；默认不拉取依赖。
- README.original.md：移动前原始说明，路径按当时根目录理解。
- C 协议编解码继续共享根目录 firmware/src/；Rust、测试、硬件资料和历史 HIL 证据没有移走。

在仓库根目录复现归档 M1：

    /home/gtc/zephyrproject/.venv/bin/python legacy/zephyr/tools/build.py --usb --zephyr-base /home/gtc/zephyrproject/zephyr

输出到 build/legacy-zephyr/usb，不覆盖已验证的 build/system/usb 基线。归档后构建通过，但重建产物不是原 HIL 镜像，恢复仍应使用已记录哈希的原产物。

归档映射、修改前哈希和四处必要的路径/汇编类型适配见 [迁移记录](../../docs/validation/2026-09-14/hal-migration/REPORT.md)。
