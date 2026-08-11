# 安全 IPC 设计

- 日期：2026-08-11
- 状态：已批准
- 适用里程碑：Milestone 4 — Secure IPC
- 关联决策：ADR 0002、ADR 0005、ADR 0009、ADR 0010

## 1. 目标与范围

实现独立于窗口和服务入口的 `ipc` 模块，使普通权限 UI 能从绿色提权 Collector 或 Windows 服务安全接收：

- Collector 实例身份、状态和健康诊断。
- 活跃应用的累计上传/下载字节。
- 应用显示名和 icon key catalog。
- 已校验的数据显示刷新订阅。

本里程碑实现纯协议 codec、所有者安全策略、Windows 命名管道 transport、服务器/客户端连接状态机、集中快照 publisher、重连策略和本地集成测试。它不实现 SCM 安装、UAC 启动、最终运行模式编排、设置 UI 或真实窗口；这些由后续里程碑调用本模块。

## 2. 已确认事实与设计判断

### 2.1 已确认的 Windows 行为

- `CreateNamedPipe` 接受显式 security descriptor；默认 descriptor 会给 LocalSystem、Administrators 和 creator owner 完全控制，并给 Everyone 和 Anonymous 读权限。
- 命名管道可跨网络访问；只用于本机时必须明确拒绝远程主体或使用等价本地传输限制。
- Microsoft 建议用 logon SID 限制不同 Terminal Services session。logon SID 也存在于 run-as 登录中。
- 同步管道调用可以无限等待。Overlapped `ConnectNamedPipe`、`ReadFile` 和 `WriteFile` 可并行，并能通过独立 `OVERLAPPED`/event 判断完成。
- `CancelIoEx` 只请求取消；调用者必须继续等待 overlapped operation 完成后才能释放或复用其状态。
- `ImpersonateNamedPipeClient` 失败时线程仍保持服务身份，因此失败必须立即拒绝请求；成功路径结束时必须 `RevertToSelf`。

### 2.2 设计判断

- 当前 ETW 快照是整机数据，身份模型不读取进程 SID。服务不能声称能够按 Windows 用户过滤流量。
- 首版是个人工具，服务限定安装者账户比“所有交互用户共享整机流量”更安全。
- 最多四个 UI 客户端足以覆盖正常 UI、设置窗口和诊断实例；这是资源安全上限，不是并发性能承诺。
- byte-mode 加自有长度帧比 message-mode 更适合先读固定 header、再读有界 payload，也更容易用纯内存 codec 测试分段和连续帧。

## 3. 模块边界

`ipc` 内部拆分为以下单一职责单元：

| 单元 | 职责 | 不负责 |
|---|---|---|
| `protocol_types` | wire 常量、消息枚举、neutral DTO 和错误枚举 | Win32 handle、窗口、ETW |
| `frame_codec` | little-endian header/payload 编解码、增量输入与边界拒绝 | pipe I/O、重连 |
| `snapshot_codec` | health、catalog、snapshot 的 version-1 payload | C++ ABI 序列化 |
| `application_catalog` | Application ID 到不复用 handle 的实例内映射、metadata revision | 进程身份解析、图标加载 |
| `security_policy` | authorized SID、pipe name、显式 descriptor 和连接主体校验 | 用户选择、服务安装 |
| `named_pipe_transport` | RAII pipe/event/overlapped operation、connect/read/write/cancel | 协议语义 |
| `ipc_server` | accept、握手、订阅、四客户端生命周期、错误关闭 | ETW 解码、排行 |
| `snapshot_publisher` | 合并订阅、单次采样、按客户端到期发布和慢客户端 mailbox | 每用户排行 |
| `ipc_client` | 连接、握手、catalog/snapshot 接收和错误分类 | 托盘/窗口重绘 |
| `reconnect_policy` | 可注入时钟/抖动的有界退避状态机 | 创建线程、显示提示 |

公共接口只暴露 neutral 值、`std::span`、`std::chrono` 和明确所有权类型。Win32 `HANDLE`、`OVERLAPPED` 和 SID buffer 不跨出 Windows adapter API。

## 4. 授权主体与管道名称

### 4.1 主体

- Portable Collector：启动 UI 所在登录会话的 logon SID。
- Service Collector：安装阶段记录的 owner user SID。
- Server identity：LocalSystem 或同一用户的已提升 token。

服务 owner SID 属于受保护机器级安装状态，不能由普通客户端 Hello 自报。后续 service milestone 负责生成和持久化；IPC server 只接受已验证的 `AuthorizedPrincipal` 构造参数。

### 4.2 名称

endpoint 使用确定性本地名称：

```text
\\.\pipe\NetworkFlowMonitor.<mode>.<sid>
```

`mode` 只允许 `portable` 或 `service`，SID 必须由 Win32 token/安装状态取得并转换为 canonical string。管道名不是 secret；安全性来自 DACL、remote rejection、first-instance 和连接后主体复核。

### 4.3 DACL 与打开权限

security descriptor 必须显式构造：

- LocalSystem：服务器实例所需完全权限。
- Portable 模式的 server logon SID：创建后续实例和 server duplex handle 所需的完全权限；它是具体登录会话 SID，不是 user SID 或 Builtin Administrators group。
- authorized SID：`FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE` 以及实际连接所需的最小属性权限。
- 不添加 Everyone、Anonymous、Authenticated Users、Builtin Administrators 或 Network allow ACE。

Service 模式由 LocalSystem 创建实例，不需要额外 server SID。Portable 模式常见的同账户 UAC 提升会让 server 与 UI 共享 logon SID，此时同一登录会话是既定信任边界；若使用其他管理员凭据，descriptor 同时包含 server logon SID 的 server 权限和 UI logon SID 的 data 权限。authorized service-owner user ACE 或独立 UI logon ACE 本身不获得 `FILE_CREATE_PIPE_INSTANCE`。客户端使用具体权限打开，禁止用会映射到创建实例权限的宽泛写授权。

server flags：

```text
PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE（仅首实例）
PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS
```

client 使用 `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`，不允许服务以 delegation/impersonation 权限替客户端执行文件或注册表操作。

### 4.4 连接后复核

读取完整 `ClientHello` 后，server：

1. 调用 `ImpersonateNamedPipeClient`；失败立即关闭。
2. RAII guard 确保所有路径 `RevertToSelf`。
3. 打开 thread token 仅查询 `TokenUser` 和 logon SID group。
4. Portable 模式要求预期 logon SID；service 模式要求预期 user SID。
5. 可读取 client PID/session ID 作为诊断，但不能把 PID 当作授权依据。

任何模拟或 token 查询失败都作为授权拒绝处理，不在高权限上下文继续处理 Hello。内部 `ClientIdentityResult` 保留 `ImpersonationFailed`、`TokenQueryFailed` 或 `RevertFailed` 及 Win32 error 供健康诊断；对协议客户端只暴露统一的 Unauthorized 结果，所有非 `None` 结果都禁止继续执行高权限操作。

## 5. Version-1 wire envelope

每个 header 恰好 24 bytes，字段按 little-endian 逐项写入：

| Offset | Width | Field | Version-1 rule |
|---:|---:|---|---|
| 0 | 4 | magic | `0x314D464E`，内存字节为 `NFM1` |
| 4 | 2 | header size | 必须为 24 |
| 6 | 2 | protocol version | Hello 为客户端首选版本；握手后必须为协商版本 1 |
| 8 | 2 | message type | 只接受方向允许的已知枚举 |
| 10 | 2 | flags | version 1 必须为 0 |
| 12 | 4 | payload length | `0..1,048,576` |
| 16 | 8 | request ID | client request 非零；push 可为 0 |

不 `reinterpret_cast` wire buffer 为 C++ struct。codec 使用显式 load/store helper，并在任何 offset 加法前检查剩余长度。

### 5.1 消息类型

| Value | Message | Direction | Purpose |
|---:|---|---|---|
| 1 | `ClientHello` | client → server | 版本范围和初始刷新间隔 |
| 2 | `ServerHello` | server → client | 选定版本、frame 上限和 Collector instance |
| 3 | `Subscribe` | client → server | 更新刷新间隔 |
| 4 | `SubscriptionAccepted` | server → client | 返回实际间隔 |
| 5 | `ApplicationCatalog` | server → client | 分块发布 handle/metadata revision |
| 6 | `Snapshot` | server → client | 健康和累计计数 |
| 7 | `CollectorStatus` | server → client | 无完整快照时发布状态变化 |
| 8 | `ProtocolError` | both | 有界错误分类；发送后关闭或按定义继续 |

server 收到 server-only 类型、client 收到 client-only 类型、Hello 前收到其他类型或同一连接第二次 Hello 都是协议错误。

### 5.2 握手

`ClientHello` payload：

- `minimum_protocol_version: uint16`
- `maximum_protocol_version: uint16`
- `requested_refresh_ms: uint32`

header protocol version 等于 `maximum_protocol_version`。version 1 server 只在范围包含 1 时接受。刷新范围复用 `AppConfig` 的 100–5000ms；非法值返回 `InvalidSubscription` 并关闭。

`minimum_protocol_version` 和 `maximum_protocol_version` 都必须非零且 minimum 不大于 maximum。`ServerHello` 回显 ClientHello 的非零 request ID；`SubscriptionAccepted` 回显对应 Subscribe request ID。server push 的 catalog/snapshot/status 使用 request ID 0；无法安全取得原 request ID 的 framing error 也使用 0。

`ServerHello` payload：

- `selected_protocol_version: uint16`
- `reserved: uint16 = 0`
- `maximum_frame_payload: uint32 = 1,048,576`
- `collector_instance_guid: 16 bytes`，使用 GUID 的规范字段编码，不复制内存布局

GUID 编码顺序固定为 `Data1:uint32 little-endian`、`Data2:uint16 little-endian`、`Data3:uint16 little-endian`、`Data4[8]` 原始字节。

握手必须在连接后 3 秒内完成。版本无交集返回包含 server min/max 的 `VersionMismatch`，客户端进入不自动重试状态。

`Subscribe` payload 恰好为 `requested_refresh_ms:uint32`，只允许在握手完成后发送且 request ID 必须非零。`SubscriptionAccepted` payload 恰好为 `effective_refresh_ms:uint32`。同一连接可以更新订阅；新值原子替换旧值，非法值不部分应用。

## 6. Application catalog

每个 Collector instance 维护：

- `next_handle: uint64`，从 1 开始，0 保留。
- `ApplicationId -> handle` 映射。
- 每个 handle 的 `metadata_revision: uint32`。
- 单调 `catalog_sequence: uint64`。

handle 和 revision 溢出都使 Collector/IPC 进入明确失败状态；同一 instance 内禁止复用 handle。应用被活跃累计器驱逐时删除 ID 映射并发布 removal，同一 ID 再次出现会取得更大的新 handle。每个 client session 的 known-handle 集合随 catalog additions/removals 更新。Collector 重启后通过新 GUID 建立完全独立命名空间。

`catalog_sequence` 在 instance 内严格递增。新连接的 session 从空 known-handle 集合开始，即使 Collector instance 未变化也必须发送一次当前 catalog 的完整 baseline；之后发送严格连续的 delta sequence。缺少任意 sequence 时客户端拒绝后续 snapshot 并重连。

`ApplicationCatalog` payload：

- `catalog_sequence: uint64`
- `chunk_index: uint32`
- `chunk_count: uint32`
- `entry_count: uint32`
- `catalog_flags: uint32`，bit 0 为完整 baseline，其他位必须为零
- entries

每个 entry：

- `application_handle: uint64`
- `metadata_revision: uint32`
- `entry_flags: uint32`，bit 0 为 removed，其他位必须为零
- `display_name_length: uint32`
- `icon_key_length: uint32`
- UTF-8 bytes

普通 add/update entry 的两个字符串各最多 131,072 bytes；removed entry 的两个长度必须为零。完整 baseline 不包含 removed entry。整个 frame 仍受 1 MiB 上限。catalog 可以拆成多帧；`chunk_count` 必须非零，index 从 0 连续递增。同一 sequence 的 flags/chunk count 必须一致，所有 chunks 完成后才原子应用。非法 UTF-8、重复 handle/revision、缺块或总长度溢出都使客户端拒绝该 catalog sequence。

内部完整 Application ID 不发送。UI 为 RankingEngine 构造仅在该 Collector instance 内稳定的本地 ID：

```text
ipc:<instance-guid>:<application-handle>
```

## 7. Snapshot payload

固定前缀：

- `snapshot_sequence: uint64`
- `required_catalog_sequence: uint64`
- `uptime_microseconds: uint64`
- `application_count: uint32`
- `snapshot_flags: uint32`
- version-1 `CollectorHealthWire`
- version-1 `IpcServerHealthWire`

应用记录固定 24 bytes：

- `application_handle: uint64`
- `upload_bytes: uint64`
- `download_bytes: uint64`

同一 snapshot 中 handle 唯一且非零，字节计数不降。client 只有在完整接收 `required_catalog_sequence` 后才接受 snapshot；否则断开并重新握手，不能显示缺身份的数据。

`snapshot_sequence` 和 `uptime_microseconds` 在同一 instance 内都必须严格递增。client adapter 在检查 duration 可表示后，把 uptime 作为任意零点的 `std::chrono::steady_clock::time_point`；RankingEngine 只依赖同一 instance 内的差值。新 instance 清空历史；同一 instance 重连可保留历史，但仍必须重新取得 catalog。

`CollectorHealthWire` 顺序固定为：

1. `state:uint8`、`system_process_rundown_confirmed:uint8`、`reserved:uint16=0`。
2. `first_error`、`last_error`，每个按 `domain:uint8`、`stage:uint8`、`reserved:uint16=0`、`code:uint32`。
3. 以下 `uint64`，严格按表中顺序：

| Order | Counter |
|---:|---|
| 1–5 | `events_received`, `events_decoded`, `events_ignored`, `malformed_events`, `unsupported_schema_events` |
| 6–7 | `network_events_counted`, `network_bytes_counted` |
| 8–11 | `tcp_events`, `tcp_bytes`, `udp_events`, `udp_bytes` |
| 12–15 | `ipv4_events`, `ipv4_bytes`, `ipv6_events`, `ipv6_bytes` |
| 16–19 | `upload_events`, `upload_bytes`, `download_events`, `download_bytes` |
| 20–22 | `process_start_events`, `process_rundown_events`, `process_end_events` |
| 23–26 | `process_probe_failures`, `lifecycle_errors`, `unmatched_network_events`, `unmatched_network_bytes` |
| 27–28 | `tcp_retransmit_events`, `tcp_retransmitted_bytes` |
| 29–32 | `events_lost`, `log_buffers_lost`, `realtime_buffers_lost`, `statistics_resets` |
| 33–36 | `callback_failures`, `unexpected_failures`, `counter_overflows`, `active_application_capacity_hits` |
| 37–38 | `aggregated_application_events`, `aggregated_application_bytes` |

`IpcServerHealthWire` 顺序固定为：

1. `state:uint8`（0 Stopped、1 Starting、2 Running、3 Degraded、4 Failed）、`active_clients:uint8`、`reserved:uint16=0`、`last_win32_error:uint32`。
2. 以下 `uint64`：`accepted_connections`、`unauthorized_connections`、`protocol_errors`、`frames_received`、`frames_sent`、`bytes_received`、`bytes_sent`、`snapshots_captured`、`snapshots_published`、`pending_snapshots_replaced`、`slow_client_disconnects`、`cancellation_failures`、`internal_failures`。

enum 使用 `uint8`，bool 只接受 0/1，保留位必须为零；不能复制 native struct。上述字段增加、删除或重排需要协议版本升级。

`snapshot_flags` version 1 定义：

- bit 0：部分新身份已聚合到 `collector:other`。
- 其他位必须为零。

`CollectorStatus` payload 为 `uptime_microseconds:uint64`，随后是相同的 `CollectorHealthWire` 和 `IpcServerHealthWire`，不含 application records。

`ProtocolError` payload 恰好 12 bytes：`error_code:uint16`、`retry_disposition:uint8`、`reserved:uint8=0`、`server_min_version:uint16`、`server_max_version:uint16`、`win32_error:uint32`。retry disposition 为 0 CloseTerminal、1 CloseRetryable、2 Continue；未知值拒绝。

## 8. 有界活跃应用状态

当前累计器永久保留所有出现过的 Application ID，不适合长期服务。IPC 实施前修改累计边界：

- `active_retention = maximum_ranking_window + maximum_refresh_interval = 15 seconds`。
- 每个应用记录最后一次 network event 的 `TraceTimePoint`。
- Collector watermark 超过 `last_activity + 15s` 后可驱逐；`collector:unattributed` 和 `collector:other` 固定桶不驱逐。
- `maximum_active_applications = 4096`，其中为 `collector:unattributed` 和 `collector:other` 各保留一个固定槽，普通动态身份最多 4094 个。
- 达到上限且没有可过期记录时，新的稳定身份不建表，其字节进入固定 ID `collector:other`、显示名 `Other`。已有身份继续累计。
- 增加饱和诊断：聚合事件数、聚合字节数和容量触发次数；第一次触发把 health 设为 `Degraded`。
- 全局 protocol/family/direction totals 仍包含这些字节；不得静默少计。

15 秒保证最大 10 秒排行窗口在最慢 5 秒刷新下仍能看到完整衰减。驱逐后同一应用重新出现会建立新累计纪元；旧 UI 历史已超出最大窗口，不能产生虚假回绕速率。

## 9. Server 与 publisher

### 9.1 连接结构

- 一个 acceptor 在有空闲容量时持续保持一个 pending overlapped `ConnectNamedPipe`。
- 成功连接后交给一个 `ClientSession` worker；server 使用同一显式 descriptor 创建下一实例，直到四个 session。所有实例的 `nMaxInstances` 都为 4；容量已满时不创建第五实例，客户端的 `ERROR_PIPE_BUSY` 进入可恢复退避。
- 每个 session handle 有独立 read/write `OVERLAPPED` 和 manual-reset event，可同时保持一个 read 与一个 write。
- session worker 只等待自己的 stop/read/write/publish signals，不访问窗口或 ETW callback。
- Stop 使用 `CancelIoEx` 请求取消所有 pending operation，等待每个 completion 进入终态，再关闭 pipe/event 并 join。

首个实例创建失败且错误表明已存在同名实例时返回 `EndpointAlreadyOwned`，不得连接、停止或复用未知 server。

### 9.2 集中采样

`SnapshotPublisher` 读取所有已握手订阅：

1. 没有客户端时不做额外显式 ETW flush。
2. 第一个客户端订阅后立即请求一次采样。
3. 之后按最短合法 refresh interval 唤醒。
4. 每次只调用一次 injected `SnapshotProvider::Capture()`；真实 host adapter 后续把它连接到 `FlushAndQuery` 和 `MakeSnapshot`。
5. 只通知已到各自发送期限的客户端；下一期限基于实际采样时间，不能按循环次数累积漂移。

IPC milestone 用 fake provider 验证调度；真实 Collector runtime wiring 属于 dual-mode host milestone。

### 9.3 慢客户端

每个 session 保存一个尚未开始写入的最新 immutable snapshot reference。新 snapshot 可替换这个待发 reference，不累计无界队列。

在发送 snapshot 前，session 根据自己的 known catalog state 生成所有缺失 metadata chunks；catalog 完整写完后才能开始对应 snapshot。已开始的 frame 不替换或交错。

单个 write 从发起后 5 秒仍未完成：

1. `CancelIoEx(pipe, &write_overlapped)`。
2. 等待 completion；接受正常完成、`ERROR_OPERATION_ABORTED` 或明确 pipe disconnect。
3. 断开该 session，增加 slow-client 诊断。

其他 session 和 publisher 不等待该客户端。

## 10. Client 与重连

client 状态：

```text
Disconnected -> Connecting -> Handshaking -> Streaming
                   |              |              |
                   +---------- recoverable ------+-> Backoff
                                  |
                          Unauthorized / Incompatible
```

可恢复错误包括 endpoint 不存在、pipe busy、broken pipe 和 Collector 正常重启。退避序列为 250ms、500ms、1s、2s、5s，之后保持 5s；实现注入 jitter source，测试使用确定值。成功进入 Streaming 后重置 attempt。

以下不自动重试：

- `Unauthorized`
- `VersionMismatch`
- 本地 protocol invariant failure

UI adapter 在离开 Streaming 时立即发布非实时状态。新 instance GUID 时清空 catalog、RankingEngine 历史和旧 health；同一 instance 重连也重新获取当前 catalog，不能假设 server 保存客户端确认状态。

## 11. 错误分类与日志

`ProtocolError` code 固定为：

| Value | Code |
|---:|---|
| 1 | `MalformedHeader` |
| 2 | `UnsupportedHeader` |
| 3 | `VersionMismatch` |
| 4 | `UnknownMessageType` |
| 5 | `InvalidFlags` |
| 6 | `PayloadTooLarge` |
| 7 | `MalformedPayload` |
| 8 | `InvalidUtf8` |
| 9 | `InvalidSubscription` |
| 10 | `UnexpectedMessage` |
| 11 | `CatalogOutOfOrder` |
| 12 | `Unauthorized` |
| 13 | `ServerBusy` |
| 14 | `InternalFailure` |

error payload 只含固定 error code、server supported version range、retry disposition 和可选 Win32 code。不得发送路径、SID、命令行、远端地址、原始 frame 或 payload 内容到日志。SID 日志只允许不可逆短标识或完全省略。

malformed/unauthorized 客户端错误不使 Collector 停止；server 内部不变量、handle/catalog overflow 或 snapshot counter invariant failure 才进入 Collector/IPC failed health。

## 12. 测试与完成条件

### 12.1 纯测试

- Header golden bytes、每个 offset/endian、分段 header/payload、连续 frames 和零 payload。
- magic/header/version/type/flags/length/request ID 错误，`uint32`/`size_t` 加法溢出和 1 MiB 上限。
- Hello/Subscribe 范围、方向错误、重复 Hello、Hello timeout 和版本无交集。
- Catalog chunk、UTF-8、字符串上限、缺块、重复 handle/revision、instance reset 和 handle exhaustion seam。
- Snapshot health 每字段 round-trip、reserved bits、duplicate/unknown handle、catalog dependency、counter decrease 和 malformed app count。
- 15 秒活跃过期、10s+5s 边界、4096 容量、Other 聚合、固定桶、全局 totals 与降级诊断。
- 四客户端不同订阅只调用一次 provider；第一订阅立即采样、无客户端不显式采样、deadline 无漂移。
- mailbox 替换、catalog-before-snapshot、write timeout、取消 completion 和独立客户端继续。
- reconnect 分类、退避上限、成功重置、Unauthorized/Incompatible 不重试和 instance change 清状态。

### 12.2 Windows 本地集成

- security descriptor ACE 与具体 access mask 精确匹配；不存在 Everyone/Anonymous/Administrators allow。
- owner client 可连接；匿名模拟 token 连接失败；错误 SID 的 `AccessCheck` 失败。
- `PIPE_REJECT_REMOTE_CLIENTS` 和 first-instance conflict 返回可诊断错误。
- 两个真实本地客户端同时握手/订阅/接收；一个停止读取时另一个继续。
- server/client 任意顺序停止，pending connect/read/write 都完成取消且无遗留 pipe/thread。
- 截断、超长和未知版本客户端只影响其连接。

跨真实 Windows 用户/session 的最终验证留到 service milestone，因为本里程碑尚未创建服务 owner 安装状态；本地测试必须验证同一 security descriptor 的 token `AccessCheck`，不能声称已完成多用户实机测试。

### 12.3 完成门

- Format、严格 MSVC Debug、clang-tidy/Clang Static Analyzer、MSVC `/analyze`、全部 Debug/Release CTest 和 Release x64 通过。
- Windows 集成测试实际运行，不用 skip 掩盖安全或取消失败。
- `dumpbin /dependents`、产品 EXE 可达性和 exact size 记录；在 runtime 尚未连接 IPC 时明确说明产品依赖未变化。
- ADR、architecture、configuration、planning 和 progress 同步。
- 无默认 DACL、Everyone/Anonymous 数据权限、remote client、未绑定长度、native struct wire copy、无限 queue、异常越过 thread/IPC ABI 或敏感日志。
- Exact Windows 11 24H2 x64 仍属于 Phase 5 兼容性矩阵；更高版本的通过不能替代该目标机结果。

## 13. 官方资料

- https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights
- https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipes
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipew
- https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output
- https://learn.microsoft.com/en-us/windows/win32/fileio/cancelioex-func
- https://learn.microsoft.com/en-us/windows/win32/ipc/impersonating-a-named-pipe-client
- https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-impersonatenamedpipeclient
