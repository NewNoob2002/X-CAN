# X-CAN Boot冻结、USB升级与实机验证

日期：2026-09-14。源码基线：015ca6a334cf0a634498c45d829be2ca37c92d10，工作区含尚未提交的 CubeMX 迁移与本次实现。

## 冻结Boot与密钥

- 冻结目录：`docs/validation/2026-09-14/boot-freeze/`
- Release `xcan_boot.bin`：14,512 B，SHA-256 `ff10334bfefae8d39093619af2af74fc8b9c19764bce12738cf6e75d67822b8e`。
- 本次重新构建的 Release Boot BIN/HEX/ELF/MAP 和生成公钥均与冻结目录逐字节一致。
- 生产私钥：`/home/gtc/Desktop/workspace/MyKey/XCAN/xcan-signing-ecdsa-p256.pem`，权限0600。
- 公钥：`/home/gtc/Desktop/workspace/MyKey/XCAN/xcan-signing-public.pem`，权限0644；DER SHA-256 `9b044372d62bc7bede56344763548b674b776fec6dd0b12e26021f49cf0b14dc`。

## 已实现升级路径

Rust `update` 只读取带 MCUboot magic 的签名镜像，计算整文件 SHA-256，通过 v2 Bulk 协议按最多512字节发送。App 在健康确认完成后接受升级，先擦除 Secondary trailer，再按状态查询逐页擦除其余区域；镜像写入从0x08013000开始，Flash按8字节 doubleword 编程，最后短块补0xFF。重复旧块只有在Flash内容一致时成功。

`FW_FINISH`重新读取 Secondary，检查 MCUboot magic 和整文件 SHA-256，再调用 `boot_set_pending_multi(0, 0)`。Boot负责ECDSA-P256验证、offset swap、断电恢复和未确认回滚。生产私钥没有编入App或Rust工具。

## 构建与测试结果

- Release：App 34,968 B；签名镜像35,632 B；签名区余量13,520 B；RAM预留12,216 B。
- Debug：App 41,012 B；签名镜像41,675 B；签名区余量7,477 B；RAM预留12,216 B。
- `python3 tools/test.py`：v1、v2、升级状态机、真实CherryUSB core+假控制器及全部Rust测试通过，C测试启用ASan/UBSan。
- 升级状态机覆盖整槽边界、trailer首擦、512字节块、最后短块、乱序、相同块幂等、冲突重发、错误hash、错误magic、pending时机、abort限制和Product data隔离。
- `cargo clippy --all-targets --locked --offline -- -D warnings`、Release/Debug交叉编译、`tools/check_build.py`及`git diff --check`通过。
- Rust Release CLI：`host/target/release/xcan`，SHA-256 `10f06ac7a3e91b5df80bc2a32b051131d6cf03855ed96f09c7e61eb0140f5e34`。
- 当前 Release签名镜像：`build/Release/xcan.signed.bin`，SHA-256 `846624b63a32d4471d4005b7fb363dd064a17dc82d5ff2e00a7853deed922789`。ECDSA签名含随机量，后续重新签名时文件哈希会变化。
- `xcan_fw_finish`静态栈占用168 B，`usb_work`为56 B；USB任务栈为1,536 B。Flash操作期间不保持FreeRTOS临界区。

## 实机验证结果

在STM32G431RB实机上完成0.1.0 factory烧录校验、USB协议自检、0.1.1升级与确认后持久复位、0.1.2未确认回滚以及Product data隔离验证。最终Primary为已确认的0.1.1，Secondary保留0.1.2，Product data前后逐字节相同。完整证据见`hil/REPORT.md`。

首次实机启动发现CherryUSB路径跳过CubeMX USB MSP时钟初始化。已在`Core/Src/main.c`的CubeMX用户区复用`HAL_PCD_MspInit()`修复；顶层CMake和冻结Boot未修改。

仍未执行擦除、写入或swap中途的物理断电恢复测试，该项保留为量产冻结前的独立HIL验证。
