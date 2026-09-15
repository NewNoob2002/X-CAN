# Zephyr 4.4.99 烧录与 Linux USB HIL

结论：本轮限定的启动、GPIO 与 Linux USB HIL 全部通过。

日期：2026-09-14。目标为 X-CAN / STM32G431RB，J-Link 63728769，USB 序列号 2036365058315010002D0055。使用 build/system/usb/zephyr/zephyr.bin，Zephyr 提交 c199f92c7e4bba820573d6be9ba0c75385601b67。镜像与备份 SHA256 见 preflight.json、result.json。

## 已完成

- 烧录前备份全部 128 KiB Flash 至 pre-flash.bin，确认前部与此前通过测试的 USB 镜像一致。原始最小固件备份仍保留。
- J-Link loadfile 写入新镜像后，verifybin 独立读回 67,156 字节，返回 Verify successful。随后复位并运行。日志见 flash.log。
- 串口捕获到 sysclk=144000000、idcode=20036468、optr=fbeff8aa、USB init=0，以及连续安全模式心跳。日志见 boot.log。
- Linux 枚举到正确 VID/PID/序列号；INFO 返回 144 MHz、正确芯片 ID、termination_declared=0、safe=1。
- 106 次 ECHO 测试覆盖载荷 0..52 字节、整包与 7 字节分片写入，全部通过；非法载荷长度被拒绝。日志见 usb-functional.log。
- 新版 MS OS 控制回调：完整 162 字节描述符、WINUSB、GUID 校验通过；wLength=1/10/64/255 返回预期长度和内容；非法 wIndex/wValue 均 STALL，随后有效请求恢复。脚本 check_msos.py 与 msos-result.json 保存刺激和结果。
- 短暂停机读取寄存器后恢复运行：PC4/STB=1、PC5/ARM=0、PA1=0、PB9/TXD=1，GPIO 模式一致；CCIPR=08000000；OPTR 前后均为 FBEFF8AA；CFSR/HFSR=0。日志见 registers.log。GPIO 为寄存器证据，不能替代示波器波形。

PB2 声明拨码切换测试通过：初始断开为 0，用户闭合后为 1（uptime_ms=288774），拨回断开后为 0（uptime_ms=352984），safe 始终为 1。见 termination-closed.log 和 termination-open.log。

## 过程事件与边界

首次尝试因 Serial Studio 独占串口在烧录前停止，未写 Flash。用户释放串口后成功执行烧录与日志捕获。J-Link MCP 寄存器操作仅返回连接文字，没有读数，因此未将它计为验证，改用明确脚本的 Commander 获取证据。

所有操作仅涉及启动、GPIO 读取及 USB；未发送 CAN 帧或产生注入脉冲，未修改选项字节，未执行 mass erase。测试后目标已恢复运行，工具已释放串口与探针。Windows 实机绑定、断电冷启动、示波器、CAN/FD 和定时显性注入不在本轮已通过项中。
