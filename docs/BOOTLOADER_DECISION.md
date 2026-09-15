# X-CAN Bootloader 方案决策

日期：2026-09-14

## 结论

优先采用 **MCUboot 2.4.0 + 双分区 + swap using offset + test/revert**。

当前固件和最小启动程序的实测尺寸表明，STM32G431RBT6 的 128 KiB Flash 可以容纳该方案。暂不实现自定义双分区交换；自定义 Bootloader 只保留为“单 App + 恢复下载”的降级方案。

首版 Bootloader 不集成 CherryUSB、FreeRTOS、日志和自定义升级协议。App 负责把签名镜像写入 Secondary slot，MCUboot 负责验签、断电可恢复交换、试运行和回滚。设备同时保留 SWD 或 STM32 ROM Bootloader 作为两份镜像都损坏时的维护入口。

## 已核实约束

- MCU：STM32G431RBT6，128 KiB Flash，32 KiB RAM。
- STM32G431 属于 RM0440 category 2，Flash 是 **单 Bank**，共 64 个 2 KiB page，地址为 `0x08000000..0x0801FFFF`。
- 历史记录中的 `FLASH_OPTR=0xFBEFF8AA` 不能证明 DBANK 已开启。STM32G431 的 CMSIS 设备头文件没有 `FLASH_OPTR_DBANK`，该位对本器件不是 DBANK 配置项。
- 单 Bank 擦写期间，任何 Flash 读取都会暂停总线，操作完成后继续。升级下载和交换流程必须按该行为做 USB 流控和断电测试。
- 集成前 Release App 基线：Flash 28,304 B，RAM 8,288 B。
- 已落地 MCUboot 2.4.0、TinyCrypt、ECDSA-P256、Primary 校验、offset swap、App confirm、STM32 Flash backend 和签名打包，Release 实测如下：

| 构建 | Boot Flash | Boot RAM | App BIN | App RAM | 签名 App |
|---|---:|---:|---:|---:|---:|
| Release | 14,512 B | 3,424 B | 30,160 B | 8,320 B | 30,824 B |
| Debug | 16,788 B | 3,424 B | 35,008 B | 8,320 B | 35,671 B |

Release Boot 占 24 KiB 分区的 59.05%，剩余 10,064 B；Release 签名 App 占 48 KiB 门限的 62.71%，剩余 18,328 B。App 相对集成前增加 1,856 B，主要来自运行镜像确认和共享 Flash backend；本次 512 B header 与 ECDSA TLV 使签名产物比 App BIN 增加 664 B。ECDSA DER 签名长度可能随每次签名产生几字节变化，构建门限始终检查当次实际文件。

构建检查已验证 Boot/App 向量地址、App 重定位、合法签名、错误密钥拒绝、篡改镜像拒绝、Boot 不包含 FreeRTOS/CherryUSB/FDCAN、factory 文件拼接位置和全部尺寸门限。目标板已通过USB升级、交换、确认持久化和未确认回滚验证；擦除、写入和swap中途的物理断电恢复仍未验证，因此还不是量产结论。

## 推荐分区

推荐保留一页产品配置/升级记录空间，同时给 Bootloader 留出约 10 KiB 增长余量：

| 区域 | 起始地址 | 结束地址 | 大小 | Page |
|---|---:|---:|---:|---:|
| Bootloader | `0x08000000` | `0x08005FFF` | 24 KiB | 0..11 |
| Primary slot | `0x08006000` | `0x080127FF` | 50 KiB | 12..36 |
| Secondary slot | `0x08012800` | `0x0801F7FF` | 52 KiB | 37..62 |
| Product data | `0x0801F800` | `0x0801FFFF` | 2 KiB | 63 |

`swap using offset` 要求升级镜像从 Secondary 的第二个 page 开始，Secondary 比 Primary 大一个 page。按 26 个最大 slot page、8 B 最小写入粒度计算，状态和 trailer 小于一个 2 KiB page，按 MCUboot 要求取整后占一页。Primary 可容纳约 48 KiB 的完整签名镜像。

当前 Release 签名镜像为 30,824 B，距离 48 KiB 上限还有 18,328 B。“签名镜像不超过 48 KiB”和“Boot 不超过 24 KiB”已成为 Debug/Release 构建门限。

## 工程落地

- `components/MCUboot` 保存构建所需的 MCUboot 2.4.0 bootutil、TinyCrypt、ASN.1 和 imgtool 子集，不依赖参考工程绝对路径。
- `firmware/boot` 保存 STM32G431 Flash map、MCUboot 配置、公钥绑定和跳转入口。Boot 与 App 共用同一个 Flash backend 静态库。
- `STM32G431XX_FLASH.ld` 是唯一链接段定义。Boot 由 CMake 传入 `0x08000000/0x6000`，App 传入 `0x08006200/0xBE00`。
- App 在 USB 初始化成功且调度运行 1 秒后调用 `boot_set_confirmed_multi(0)`；确认失败进入现有 fatal 路径。
- 私钥不进入仓库。开发和生产构建都必须通过 `XCAN_SIGNING_KEY` 指向仓库外的私钥；生产构建同时把 `XCAN_SIGNING_KEY_USAGE` 改为 `production`。GitHub CI 每次运行生成一次临时开发密钥。

Release 构建：

```sh
XCAN_SIGNING_KEY=/home/gtc/Desktop/workspace/MyKey/XCAN/xcan-signing-ecdsa-p256.pem \
  python3 tools/build.py --preset Release
```

生产密钥构建配置示例：

```sh
cmake --preset Release \
  -DXCAN_SIGNING_KEY=/secure/path/product-ecdsa-p256.pem \
  -DXCAN_SIGNING_KEY_USAGE=production \
  -DXCAN_IMAGE_VERSION=1.0.0+1
cmake --build --preset Release --parallel 8
python3 tools/check_build.py build/Release
```

产物位于 `build/Release`：

| 产物 | 用途 |
|---|---|
| `xcan_boot.elf/.bin/.hex` | Boot 调试、烧录和符号文件 |
| `xcan.elf/.bin/.hex` | 已重定位 App；裸 BIN 不带 MCUboot header |
| `xcan.signed.bin` | OTA 镜像，写入 `0x08013000`；末尾不足 8 B 的写入由下载端补 `0xFF` |
| `xcan_factory.bin` | Boot 加 Primary 签名镜像，整片擦除后从 `0x08000000` 烧录；紧凑文件本身不清除旧 Secondary/trailer |
| `firmware-metadata.json` | 版本、地址、尺寸、SHA-256 和密钥用途 |
| `build-metadata.json` | ELF/map/源码校验、RAM/Flash 和完整构建证据 |

Release map 中 Boot 的主要代码来自 `context_boot_go`、offset `swap_run`、TinyCrypt `uECC_verify` 和 SHA-256。最大静态单函数栈记录为 `uECC_verify` 的 536 B，当前 Boot ISR/主栈保留 1 KiB；目标板断电测试时应同时检查嵌套中断关闭状态和最坏签名验证路径。

两个备用布局：

| 用途 | Boot | Primary | Secondary | Product data | 完整签名镜像上限 |
|---|---:|---:|---:|---:|---:|
| 更大 App | 22 KiB | 52 KiB | 54 KiB | 0 | 约 50 KiB |
| Boot 内加入恢复协议 | 32 KiB | 46 KiB | 48 KiB | 2 KiB | 约 44 KiB |

只有在 Boot 的实际功能超过 24 KiB 时才采用 32 KiB 布局。首版不需要为尚未确定的 USB 恢复功能预留这部分复杂度。

## 为什么选择 MCUboot

- 所需的核心能力正是 MCUboot 已实现并持续测试的部分：镜像格式、签名验证、升级状态、断电续传交换、试运行确认和失败回滚。
- MCUboot 官方文档优先推荐 swap using offset，而不是新项目使用 swap using move。Offset 每个 sector 使用两个状态标记，Secondary 多一个 page，不需要独立 Scratch 区。
- 自定义双槽 Bootloader 的主要工作并不在跳转和 Flash 写入，而在断电发生于每一个擦除、复制和状态写入边界时仍能恢复。重写这套状态机没有收益。
- Direct XIP with revert 虽然免交换，但要求分别构建可从两个 slot 地址执行的镜像，升级端还要识别当前活动 slot。当前工程不需要承担双链接镜像的维护成本。
- Scratch 模式可以工作，但会占用独立 Scratch page，状态更多；本器件 page 大小一致，Offset 更合适。

## 实施边界

保持现有顶层 `CMakeLists.txt` 结构不变：

- MCUboot 和 TinyCrypt 作为第三方组件，由 `components/CMakeLists.txt` 管理。
- Boot target、App 重定位和签名产物由 `firmware/CMakeLists.txt` 管理。
- 继续复用 CubeMX 的 CMSIS、HAL、startup、system 和 IRQ 框架，不复制一套 STM32 驱动或启动文件。
- Boot 和 App 使用同一份链接段定义，通过不同 Flash region 参数生成各自产物，避免维护两份大段重复链接脚本。
- App 增加最小的 pending/confirm 接口；新固件完成 USB、CAN、任务和基本配置自检后才确认镜像。

## 实施验收

1. 先落地 24/50/52/2 KiB 分区和独立 `xcan_boot` 构建，确认 Boot 不超过 24 KiB、签名 App 不超过 48 KiB。
2. 主机测试覆盖合法签名、错误签名、损坏 hash、版本升级和重复升级请求。
3. 目标板验证正常启动、Test upgrade、App confirm、未确认复位后的 revert。
4. 在交换的每个 page 擦除/复制阶段注入复位，确认可继续交换或回滚，Boot 区和 Product data 不受影响。
5. 验证 App 写 Secondary 时 USB 能通过分块和流控稳定传输；STM32G431 单 Bank 最长 page erase 约 24.47 ms，不能假设 USB ISR 始终实时运行。
6. 验证失败后仍可通过 SWD 或 ROM Bootloader 恢复，再决定是否需要把 USB 恢复协议加入 Boot。

## 依据

- `docs/Reference/ST/rm0440-stm32g4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf`：产品分类、category 2 Flash 组织、擦写行为。
- `docs/Reference/ST/DS_stm32g431rb.pdf`：STM32G431RBT6 容量和 Flash 擦除时间。
- 参考工程 `components/mcuboot-2.4.0/docs/design.md`：Offset、Move、Scratch、Direct XIP 和 revert 行为。
- 参考工程 MCUboot 2.4.0 与 TinyCrypt 源码：尺寸探针和签名工具。
- MCUboot 官方发布页和官方设计文档，检查日期 2026-09-14；当前稳定版为 2.4.0。
