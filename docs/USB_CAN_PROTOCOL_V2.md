# X-CAN USB/CAN 交互协议 v2

日期：2026-09-14。状态：v2 编解码和固件升级命令已接入 USB，bring-up v1 同端点兼容保留；FDCAN 数据通路尚未接入。本规范不表示已有 CAN 收发能力。

## 1. 范围与选择

单通道 CAN/CAN-FD，支持配置、开始/停止、帧流、状态与单次发送；接收数据最多 64 字节。注入命令留待 M3 定义，不开放任意寄存器写入、无限周期发送或未经实现的能力位。

继续使用单 Vendor 接口的一对 Bulk OUT/IN，端点由描述符发现。USB FS 的 64 字节包是传输层单位，不能当成一条业务消息。业务消息为变长头+载荷，总长不超过 544 字节，载荷不超过 520 字节；该上限允许 FW_WRITE 携带 512 字节固件及8字节写入信息。一次完整 64 字节 FD 帧上报为 120 字节；5 帧批量为 472 字节。

不新增 CRC：本阶段依赖 USB 传输校验，应用严格校验边界、字段、会话与序号；这不能检测内容被软件改成另一条合法消息的情况。以后用于文件存储或不可靠链路时应另定义封装校验。

## 2. v1/v2 识别与会话

只读 EP0 请求 bmRequestType=0xC0、bRequest=3、wValue=0、wIndex=0、wLength=8 返回 58 43 41 4E 02 00 20 02，即 XCAN、major=2、minor=0、max_message=544（LE16）。STALL 表示不能建立 v2 会话，不向 v1 Bulk 端点试发 HELLO。

主机在新 USB 配置/重新配置且清除旧端点数据后发送 HELLO，session=0、非零 request_id；设备返回新非零 session。session 是配置代际标记，不是认证凭据。USB reset/deconfigure/disconnect 使旧会话失效，设备停止 CAN、撤销 ARM、清理队列和未完成请求；不自动恢复旧 START/TX/注入。代际令牌不得与仍可能回调的旧配置相同；设备重启后允许重用，但主机必须建立新会话，不能跨设备连接合并数据。

每个会话只允许一个尚未收到响应的控制请求。request_id 从1开始递增，不复用；达到UINT32_MAX后重新建立会话。异步事件可以穿插，不能误当控制响应。设备按会话检查请求ID的严格递增，再执行动作；重复/倒退ID不得重复执行，返回 BAD_STATE。主机仅接受匹配 session、request_id 和 opcode 的响应。

主机控制请求使用2秒总截止时间，USB空读不重置截止时间。超时、断连、非法响应或解析错误立即使会话无效，不自动重发 SEND。重新配置后才能用新会话恢复；超时的物理发送结果为“不确定”，不能推断没有上总线。

HELLO 只允许在新配置的无会话状态执行，成功后为 STOPPED、默认 listen-only、ARM=0；配置速率尚未设置。已有活动会话上的重复 HELLO 返回 BAD_STATE，不隐式替换会话。

## 3. 消息头：24字节

多字节整数均为无符号、小端；不使用 C/Rust 结构体内存布局或位域作为线格式。载荷长度不包含消息头，不存在尾部填充。保留字段发送为0，接收非零拒绝。

| 偏移 | 长度 | 字段 | 约束 |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII XCAN |
| 4 | 1 | version | 2，拒绝其他版本 |
| 5 | 1 | kind | 1请求、2响应、3事件 |
| 6 | 2 | opcode | 控制1..0x7FFF，事件0x8000..0xFFFF |
| 8 | 2 | payload_length | 0..520 |
| 10 | 2 | status | 请求/事件为0；响应见下表 |
| 12 | 4 | request_id | 请求/响应非零；事件为0 |
| 16 | 4 | session | 已建立会话非零；HELLO请求/失败响应为0 |
| 20 | 4 | event_sequence | 请求/响应为0；事件序号从0开始，模2^32递增 |

响应状态：0 OK、1 BAD_PAYLOAD、2 UNSUPPORTED、3 BAD_STATE、4 BUSY、5 STALE_SESSION、6 INTERNAL。失败响应载荷必须为空，回显 opcode/request_id 与原请求 session。HELLO成功响应例外：携带新session。STALE_SESSION 后主机终止会话。

未知控制 opcode 的合法封装可被解码，设备返回 UNSUPPORTED；未知事件可按长度跳过，但仍检查会话及事件序号。未知 opcode 的成功响应在基础编解码层视作不透明载荷；主机业务层不得发送或接受自己不理解的操作结果。

magic/version、头部约束或已知载荷结构非法时，编解码器停止且锁定失败，不扫描 magic 猜测下一条消息。设备接入时对此终止会话并回安全状态，不承诺响应坏包。BAD_PAYLOAD 用于结构合法、但操作参数无法执行的情况，如不能实现的位时序参数；UNSUPPORTED 用于未实现的命令或能力。截断消息在 EOF/disconnect 时必须失败；不能无限保留半条命令，未来设备端按首字节起2秒超时失效。

收发端累积字节直到24字节头，再读准确载荷长度；一个USB完成回调可能包含半条、整条或多条消息。接收器每次输出一条及 consumed，调用者继续处理剩余字节。消息可以跨USB短包，不依赖短包或ZLP划界。实际USB接入必须处理512字节整包边界：主机持续以512字节上限读取，设备消息总长是64倍数时发出结束短包/ZLP，避免主机大读请求因缺少结束信号而悬挂；USB零长度完成仅是传输结束标记，不是协议EOF；不能用它接受半条消息。该USB行为尚待集成验证。

## 4. 控制操作

| opcode | 名称 | 请求载荷 | 成功响应 | 状态要求 |
|---|---|---|---|---|
| 0x0001 | HELLO | 空 | 16字节能力信息 | 新USB配置，无会话 |
| 0x0002 | GET_STATUS | 空 | 40字节状态快照 | 已握手 |
| 0x0010 | CONFIGURE | 12字节配置 | 空 | STOPPED，无待完成TX |
| 0x0011 | START | 空 | LE32非零stream_id | STOPPED，配置有效；重复START返回BAD_STATE |
| 0x0012 | STOP | 空 | 空 | 已握手；已停止时幂等成功 |
| 0x0013 | CAN_SEND | 一条CAN记录，24..88字节 | 空，表示已接受 | RUNNING、normal、TX能力有效，无待完成TX |
| 0x0020 | FW_BEGIN | image_size u32 + 整文件SHA-256 | 空 | 先擦trailer页再响应，随后逐页擦除；相同参数幂等 |
| 0x0021 | FW_STATUS | 空 | 16字节升级状态 | 每次查询最多擦除一页 |
| 0x0022 | FW_WRITE | offset u32、length u16、reserved u16、1..512字节 | 空 | 擦除完成、顺序写入；相同旧块幂等 |
| 0x0023 | FW_FINISH | 空 | 空 | 完整写入后检查magic、SHA-256并设置test pending |
| 0x0024 | FW_ABORT | 空 | 空 | pending前清除本次RAM状态；FINISH成功后拒绝 |
| 0x0025 | FW_REBOOT | 空 | 空 | 仅完成校验后；响应发送完成后复位 |

HELLO响应：capabilities u32（bit0 RX、bit1 FD、bit2 单次TX、bit3 CAN状态事件、bit4固件升级；其余为0）、max_message u16=544、channels u8=1、timestamp_source u8（0软件接收回调、1硬件SOF、2不可用）、timestamp_hz u32=1000000、reserved u32=0。能力描述实际固件，不能把编解码支持当作硬件能力。

CONFIGURE：nominal_bitrate u32（非零）、data_bitrate u32（FD时非零，否则0）、mode u8（0 listen-only、1 normal）、fd u8（0/1）、reserved u16=0。具体可实现的位速率和采样点由驱动计算并验证；失败保持停止，不悄悄改变到近似速率。首版不开放 filter 表，接收全部有效ID；过滤协议在有明确需求时另行扩展。

START建立新的非零stream_id，会话内递增且不重用，先发送成功响应，再上报该stream的数据。stream_id用于区分停止/重启采集，不是USB session。帧序号与累计统计在同一session内不因START清零；新session重置。

STOP关闭采集和普通发送，撤ARM，取消尚未执行的TX；已经开始的物理帧不能撤回。响应发出前停止生成帧并处理旧接收队列：已发出的数据先结束，未发出的帧丢弃并计入软件丢弃。STOP成功响应之后不得再出现旧stream的FRAMES；TX_RESULT和最终STATE可以随后返回。停止失败不能伪报OK；超时按会话失效处理。

CAN_SEND成功响应只是“校验通过并已接收”，不是物理送达。设备最多保留一个未完成TX，后续SEND返回BUSY，直到TX_RESULT已完成USB发送。首版TX要求控制器禁用自动重发，只提交一次；不支持有界单次发送的实现不得宣告TX能力。最终结果由TX_RESULT报告；控制器发送成功仍不证明远端应用已收到。listen-only禁止SEND，FD参数必须匹配配置，不支持FD时返回UNSUPPORTED。STOP/断连/超时均不重放旧发送。

## 5. CAN记录与帧批量

| 记录内偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 1 | channel=0 |
| 1 | 1 | reserved=0 |
| 2 | 2 | flags：bit0 EXT、bit1 RTR、bit2 FD、bit3 BRS、bit4 ESI，其余0 |
| 4 | 4 | CAN ID，标准≤0x7FF，扩展≤0x1FFFFFFF；不在ID高位夹带flags |
| 8 | 8 | timestamp_us，设备启动后的单调微秒时间；UINT64_MAX表示不可用/不确定 |
| 16 | 4 | frame_sequence，按驱动交付顺序，从0开始模2^32递增 |
| 20 | 1 | DLC，原始编码 |
| 21 | 1 | data_length，实际紧随其后的数据长度 |
| 22 | 2 | reserved=0 |
| 24 | data_length | 原始数据，不填充、不解释业务字节序 |

Classical CAN：DLC 0..8，非RTR数据长度等于DLC；RTR保留请求DLC，但数据长度必须0。FD：禁止RTR，DLC 0..15对应长度 0/1/2/3/4/5/6/7/8/12/16/20/24/32/48/64。BRS/ESI仅适用于FD。SEND记录的timestamp、frame_sequence必须0，禁止主机设置ESI，由实际控制器状态决定。

FRAMES（0x8001）载荷：stream_id u32、count u16（非零）、reserved u16=0，随后准确count条变长CAN记录。记录不得跨业务消息；同一批次属于同一stream，记录按接收顺序排列。最多5条满长FD记录，或21条零数据记录；实际数量以520字节上限为准。批次允许混合标准/扩展、Classic/FD/RTR帧。

timestamp_source=0仅表示软件回调时刻，不可宣传为SOF或精确总线时间。未来硬件计数回绕扩展无法确定时用UINT64_MAX，不能生成貌似精确的时间。主机用session分开设备重启后的时间轴，用stream_id分开采集段；本轮编解码保留完整64位值，不实现硬件时间戳重建。

## 6. 状态与TX结果

GET_STATUS响应及STATE（0x8002）共用40字节：

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 1 | can_state：0 STOPPED、1 ERROR_ACTIVE、2 ERROR_WARNING、3 ERROR_PASSIVE、4 BUS_OFF |
| 1 | 1 | mode：0 listen-only、1 normal |
| 2 | 1 | termination_declared：0断开、1接入、255尚未稳定；人工声明 |
| 3 | 1 | reserved=0 |
| 4 | 4 | 最近stream_id；尚未START时0 |
| 8 | 4 | nominal_bitrate；未配置时0 |
| 12 | 4 | data_bitrate；Classic/未配置时0 |
| 16 | 8 | rx_seen：驱动已交付帧数 |
| 24 | 8 | software_dropped：软件队列/批次因拥塞或STOP丢弃的帧数 |
| 32 | 8 | hardware_loss_events：硬件FIFO丢失指示事件数，不能当精确丢帧数 |

CAN状态值是本协议定义，接入Zephyr时必须显式映射，不能直接强转驱动枚举。DLC映射及CAN标志已对照本机Zephyr提交c199f92c7e4bba820573d6be9ba0c75385601b67的include/zephyr/drivers/can.h；本协议对非法DLC拒绝，不使用饱和转换。

三个计数器在session内单调增加，溢出饱和于UINT64_MAX；原子快照，不因状态查询读取有副作用寄存器而清除他处事件。帧序号在rx_seen增加时分配，入软件队列之前完成；软件丢弃可以造成可见序号缺口，硬件丢失未必有对应帧序号。frame_sequence只用于局部顺序/缺口检查，不能跨超过2^31帧的未知间隔推断精确缺口，最终统计以累计计数器为准。

TX_RESULT（0x8003）16字节：original_request_id u32（非零）、result u16（0控制器发送成功、1取消、2bus-off、3驱动失败）、reserved u16=0、timestamp_us u64（完成通知时刻，未知为UINT64_MAX）。与控制响应分开关联；失联未收到结果时为不确定，不能自行补出“失败未发送”。

## 7. 调度、背压与恢复契约

一个USB IN所有者按完整业务消息串行发送，禁止两个线程把不同消息字节交错。预留一个控制响应槽和一个TX完成槽；RX使用有界队列，不动态扩张。已经开始发送的消息先完成，最多512字节，不能被控制消息中途打断。

调度规则：有控制响应时先发送一个，随后若有事件则至少发送一个事件，再处理后续控制响应；事件优先级为TX_RESULT、合并后的最新STATE、FRAMES。CAN状态/PB2变化只保留最新待发快照，拥塞时计数器继续累计。各类事件的event_sequence在最终提交IN的顺序上分配，不能在分优先级队列入队时预先编号；已经编号的消息失败即终止会话，不能跳过后继续。这样允许控制/事件穿插，同时不会因高优先级事件造成伪造的序号倒退。

RX溢出时丢弃新帧（drop-new），保留已有顺序，增加software_dropped。低负载下首帧进入批次后最多等1ms打包，高负载以长度上限打包；这是目标排队期限，不是USB延迟保证。主机停读时只背压USB、有界积压CAN并计数，不阻塞CAN ISR，不允许控制响应覆盖帧内存。

event_sequence记录实际设备上行事件顺序；主机发现缺口必须标记记录不完整，不能把缺口数换算成丢失CAN帧数。客户端对模差≥2^31的倒退/歧义拒绝会话。STATE被合并和CAN帧在USB前被丢弃不会制造event_sequence缺口，应由帧序号/累计计数器解释。原始数据批次、状态和TX结果都需保留session与sequence用于归档。

没有持续RX事件不是会话失效；主机空闲时可查询GET_STATUS。CAN总线错误状态与USB传输失败分开报告；bus-off后不自动恢复发送，由主机STOP、重新检查后START。所有能力和完整恢复行为需在M2/M5真实设备验收，当前本地测试不证明ISR吞吐、驱动取消或USB调度满足这些条件。

## 8. 交互示例与实施范围

正常采集：新USB配置 → EP0版本查询 → HELLO/能力 → CONFIGURE(listen-only) → START/stream_id → FRAMES与STATE → GET_STATUS/响应（事件可穿插） → STOP/响应 → GET_STATUS最终统计。

普通发送：配置normal并START → CAN_SEND/OK（接受） → TX_RESULT（成功/取消/bus-off/失败）。若OK后断连，结果标为不确定，不重发。当前没有注入opcode；后续在独立规范中定义ARM/执行/取消以及互斥条件。

当前实现：编解码、EP0发现、设备会话、严格递增请求ID和固件升级命令已接入 CherryUSB；现有 v1 INFO/ECHO/self-test 继续可用。Rust CLI 的 update 命令只接受带 MCUboot magic、520..49152 字节的已签名镜像，以512字节分块传输，不包含生产私钥。App 擦除 Secondary 全槽但只把镜像写到0x08013000..0x0801EFFF，Product data页0x0801F800不在可写区域。Boot仍负责ECDSA签名验证、swap、断电恢复和未确认回滚。CAN配置、采集和发送调度仍属于后续FDCAN接入工作。

共享样例见 tests/protocol_v2_vectors.txt，由独立Python struct布局生成后固定为线格式测试向量。完整64字节FD样例 fd_dlc_15 保留ID=0x1FFFFFFF、timestamp=0x0102030405060708、frame_sequence=0xA1B2C3D4和递增数据0..63。

本地运行：

    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/src tests/protocol_v2_test.c firmware/src/protocol_v2.c -o build/protocol-v2-test
    ASAN_OPTIONS=detect_leaks=0 build/protocol-v2-test tests/protocol_v2_vectors.txt
    cargo test --manifest-path host/Cargo.toml --locked --offline
    cargo clippy --manifest-path host/Cargo.toml --all-targets --locked --offline -- -D warnings

测试覆盖两端共享合法/非法向量、所有分块大小、粘包、截断/失败锁定、DLC长度映射、标志/ID边界、完整FD数据/时间戳、响应关联、异步事件穿插、事件缺口、重复/旧会话和超时停止。C/Rust同一向量双向重编码必须逐字节一致。测试不涉及USB设备或CAN物理总线。
