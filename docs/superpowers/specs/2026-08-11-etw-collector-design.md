# ETW Collector 与累计计数设计

- 日期：2026-08-11
- 状态：已按推荐技术方案完成设计，待实现
- 适用里程碑：Milestone 3 — ETW Collector

## 1. 目标与边界

实现一个由高权限进程拥有的 `collector` 模块，在 Windows 11 24H2 x64 上实时消费 TCP、UDP 和进程生命周期 ETW 事件，把网络字节归属到 Milestone 2 的进程实例和应用身份，并写入现有 `ApplicationAccumulator` 累计计数。

本里程碑实现事件值类型、解码、单线程 Collector core、实时 ETW system logger、健康/丢失统计和本机集成测试。它不实现 IPC、服务安装、UAC 启动、UI、订阅调度或多用户 ACL；这些由后续里程碑消费本模块的快照和健康状态。

## 2. 已确认事实与仍需实测的内容

已由 Microsoft 文档确认：

- `EVENT_TRACE_FLAG_NETWORK_TCPIP` 启用经典 TCP/IP 和 UDP/IP 事件，`EVENT_TRACE_FLAG_PROCESS` 启用进程 Start、End、DCStart 和 DCEnd。
- TCP/UDP 某些事件在独立线程记录，不能使用 event header PID；version 2 的 IPv4/IPv6 Send/Receive payload 都以 `uint32 PID`、`uint32 size` 开头。
- 进程 Start 也可能在父进程上下文记录，目标 PID 必须取 payload。Process V2 payload 提供 pointer-sized `UniqueProcessKey`、PID 和映像名。
- Windows 8 以后允许 multiplexed SystemTraceProvider session；新代码应使用 `EVENT_TRACE_SYSTEM_LOGGER_MODE` 和自定义名称/GUID，不应使用已弃用的 NT Kernel Logger 名称/控制 GUID。
- `ProcessTrace` 在未设置 raw timestamp 时把原始 session clock 转成 FILETIME 形式的系统时间。`CloseTrace` 可使实时 `ProcessTrace` 在排空已排队事件后返回。
- ETW 会分别统计未记录事件和无法实时交付的 buffer；丢失的字节无法通过重启恢复。

必须在目标机实测后才能确认：24H2 实际发出的 provider version/opcode 集、64 KiB 请求对应的实际 buffer pool、空闲/高流量 CPU 与工作集、显式亚秒 flush 成本、loopback/offload 下 `size` 与应用 payload 的差异。它们在测量前不是产品承诺。

## 3. 会话模型

生产会话使用项目专属名称 `NetworkFlowMonitor.SystemTrace` 和固定项目 GUID `{1473ea71-a063-4396-8ea1-e3d1ede80af7}`。属性如下：

- `Wnode.Flags = WNODE_FLAG_TRACED_GUID`。
- `Wnode.ClientContext = 1`，使用 QPC session clock。
- `LogFileMode = EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_REAL_TIME_MODE`。
- `EnableFlags = EVENT_TRACE_FLAG_NETWORK_TCPIP | EVENT_TRACE_FLAG_PROCESS`。
- `BufferSize = 64` KiB；`MinimumBuffers` 和 `MaximumBuffers` 首个实测版本传 0，让 ETW 按处理器和系统限制调整，并通过 query 记录实际值。
- `FlushTimer = 1` 秒，不设置日志文件、stack walk、paged-memory 或 no-per-processor-buffering 模式。

选择 per-processor buffering 是因为 Microsoft 明确不建议在超过约 1000 events/s 的场景使用共享单 buffer；`ProcessTrace` 负责按时间交付。任何时间倒退仍由 `ProcessTracker` 明确拒绝和诊断。

启动顺序必须是 `StartTraceW` 成功后立即 `OpenTraceW`，consumer 使用：

```cpp
PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD
```

不得设置 `PROCESS_TRACE_MODE_RAW_TIMESTAMP`。`EVENT_RECORD.EventHeader.TimeStamp` 通过命名的 `LargeIntegerFileTimeToTraceTimePoint` 转换；不能把 `QuadPart` 当无单位整数跨模块传递。

默认启动遇到 `ERROR_ALREADY_EXISTS` 返回 `SessionAlreadyExists`，不得自动停止别人的活动会话。单独的 `RecoverExistingSession` 只提供给未来已取得全局 Collector 单实例所有权的运行模式层，且必须把恢复动作写入健康诊断。

## 4. 公共事件和值类型

`collector/event_types.h` 不包含 Windows SDK 头，至少定义：

```cpp
enum class NetworkProtocol : std::uint8_t { Tcp, Udp };
enum class IpVersion : std::uint8_t { V4, V6 };

struct NetworkTrafficEvent {
    std::uint32_t pid{};
    process::TraceTimePoint observed_at;
    NetworkProtocol protocol{NetworkProtocol::Tcp};
    IpVersion ip_version{IpVersion::V4};
    ranking::TrafficDirection direction{ranking::TrafficDirection::Upload};
    std::uint64_t bytes{};
    bool retransmission{};
};

using CollectorEvent = std::variant<NetworkTrafficEvent,
                                    process::ProcessStartObservation,
                                    process::ProcessEndObservation>;
```

不包含地址、端口、SID、命令行、报文内容或 socket handle。`bytes` 扩展为 `uint64_t`，但 event decoder 只从已验证的 `uint32 size` 构造。

## 5. ETW record 分类与解码

Win32 adapter 先按 provider GUID 分类：`TcpIpGuid`、`UdpIpGuid`、`ProcessGuid`；其他 provider 和合成 header event 返回 `Ignored`，不当作损坏。

### 5.1 网络事件热路径

首版只接受 version 2 且 payload 至少 8 bytes：

| Provider | Opcode | 结果 |
|---|---:|---|
| TCP | 10 / 26 | IPv4 / IPv6 Upload |
| TCP | 11 / 27 | IPv4 / IPv6 Download |
| TCP | 14 / 30 | IPv4 / IPv6 Upload，`retransmission = true` |
| UDP | 10 / 26 | IPv4 / IPv6 Upload |
| UDP | 11 / 27 | IPv4 / IPv6 Download |

connect、accept、disconnect、reconnect、fail、TCP copy、UDP fail 和未知 opcode 返回 `Ignored`。version 0/1 或未来未知 version 返回 `UnsupportedSchema`，不能套用 version 2 offset。所有整数用 `memcpy` 或显式 little-endian helper 读取，禁止未对齐 `reinterpret_cast`。

Send/Receive 把 MOF `size` 累计到对应方向；TCP Retransmit 作为实际额外发送再次累计，同时增加 `tcp_retransmitted_bytes`。该计数不包含明确的链路层/IP/TCP header，也可能受 Windows stack 与 offload 事件口径影响，因此 UI 文案只能称“应用流量”，不能声称和网卡性能计数器逐字节一致。

### 5.2 进程事件低频路径

只处理 opcode 1 Start、2 End 和 3 DCStart；opcode 4 DCEnd、39 Defunct 和其他 opcode暂时 `Ignored`。DCStart 映射为 `ProcessStartKind::Rundown`，Start 映射为 `Started`。

进程事件不采用固定结构 offset。官方 Process class 页面声明的事件版本为 3，旧的 Process V2 页面仍记录同名 MOF 属性，而 2026-08-11 在 Windows 11 25H2 build 26200 上实测 Start/End/DCStart 为 version 4。adapter 因此对白名单 version 2、3、4 使用同一组 TDH 属性名；其他版本返回 `UnsupportedSchema`。这不是对未知未来版本的猜测，Windows 11 24H2 的实际版本仍须在目标机验收时记录。

adapter 通过 TDH 只获取：

- `UniqueProcessKey`：接受 4 或 8 bytes，零扩展到 `uint64_t`；缺失时允许 `nullopt`。
- `ProcessId`：必须恰好是 4 bytes。
- `ImageFileName`：Start/DCStart 可选，设 32 KiB 内部上限并从系统 ANSI 编码转换为严格 UTF-8；End 不读取。

不得请求 `UserSID`、`CommandLine`、ParentId 或 ExitStatus。TDH schema/property 缺失、长度越界或编码失败返回分类错误，不得把 event-header PID 作为 fallback。

## 6. Collector core 数据流

`CollectorCore` 是单线程值状态机，由 `ProcessTrace` worker 独占：

1. Start/DCStart：调用 `ProcessTracker::ObserveStart`；若返回 probe request，同步调用 `ProbeWindowsProcess`，再按完整 instance ID 应用结果。探测失败保留 system/unresolved 身份并增加分类诊断，不丢弃该实例之后的网络字节。
2. End：调用 `ObserveEnd`；stale/乱序错误进入健康诊断，不抛异常。
3. Network：按 event time 调用 `ResolveAt`。找到实例后按 application ID/metadata 写入 `ApplicationAccumulator`。
4. 没有匹配实例：仍把字节写入固定 `collector:unattributed`，显示名为 `Unattributed`，并分别累计 unmatched event/bytes；绝不猜到当前同 PID 进程。
5. TCP Retransmit：按 Upload 累计，并增加 retransmit 诊断。
6. accumulator overflow：本次更新保持不变，Collector 转为 `Failed`，避免继续发布看似完整但已冻结的累计值。

每个 ETW buffer 完成时用最后成功转换的 event time 推进 tracker watermark；若 buffer 无有效时间则不推进。首版同步进程探测是已接受的小体积选择，但集成测试必须记录事件丢失；若测量证明阻塞不可接受，再通过新 ADR 引入有界工作队列。

`CollectorCore::MakeSnapshot(steady_clock::time_point)` 只允许在 owner thread 或 worker 已停止并 join 后调用。本里程碑不为未来 IPC 伪造跨线程读取；下一里程碑必须通过 worker-thread 发布或明确同步边界获得快照。

## 7. 健康状态与错误

健康状态为 `Stopped`、`Starting`、`Running`、`Degraded`、`Failed`。`Degraded` 仍可发布带警告快照；`Failed` 停止接受新事件。至少保存：

- 首个/最近 session stage、Win32/TDH 错误码。
- received、decoded、ignored、malformed、unsupported-schema 事件数。
- TCP/UDP、IPv4/IPv6、upload/download 事件和字节数。
- TCP retransmit 事件和字节数。
- process start/rundown/end、probe failure、stale lifecycle 数。
- unmatched event/bytes。
- ETW `EventsLost`、`LogBuffersLost`、`RealTimeBuffersLost` 的最近单调值。
- callback allocation/unexpected failure 和 accumulator overflow。

所有本地累计诊断使用饱和增长。ETW query 返回值若小于先前值，记录 `statistics_reset` 后建立新基线，不能无符号回绕。任何 `EventsLost` 或 `RealTimeBuffersLost` 增长都把状态设为 `Degraded`；重启不会清除用户可见的本次运行丢失事实。

## 8. 生命周期、异常和恢复

`WindowsEtwSession` 用明确 RAII 类型分别管理 controller `TRACEHANDLE`、consumer `PROCESSTRACE_HANDLE` 和 `std::jthread`。部分启动失败按反向顺序清理；绝不能把 `CloseTrace` 用到 `StartTraceW` 返回的 controller handle。

停止顺序：标记 stopping，`ControlTraceW(...STOP)` 阻止新事件，`CloseTrace` 关闭实时 consumer 并允许排空已排队事件，join worker，最后释放状态。重复 `Stop` 必须幂等；destructor 调用同一路径且不抛异常。

EventRecordCallback、BufferCallback 和 worker entry 都是 `noexcept` ABI 边界。回调内部捕获 `std::bad_alloc` 和其他异常，写入预分配/原子故障槽并请求停止处理；异常不得穿过 ETW、线程或未来服务入口。

unexpected `ProcessTrace` failure转为 `Failed` 并保留错误。本模块不做无限自动重启；未来 portable/service owner 决定带上限退避和用户可见状态。

## 9. Flush 与未来订阅

ETW 自动 flush 最短一秒，而公开 refresh interval 允许 100–5000ms。`WindowsEtwSession::FlushAndQuery` 使用 `ControlTraceW(EVENT_TRACE_CONTROL_FLUSH)` 立即交付 buffer 并返回 session statistics：

- 本里程碑的 live integration test 显式调用它等待确定性结果。
- 后续 IPC snapshot scheduler 按所有客户端请求中的最短合法刷新间隔合并调用；多个客户端不能各自启动 ETW timer。
- 仍只有 `display.refresh_interval` 一个用户可见概念。flush cadence 是它在 Collector 内的实现结果，不新增“ETW 刷新率”。

## 10. 测试与完成条件

纯单元测试至少覆盖：

- TCP/UDP IPv4/IPv6 version-2 Send/Receive 和 TCP Retransmit 映射。
- payload 短于 8 bytes、未知 version、未知 provider/opcode、零/最大 `size` 和未对齐 input。
- event timestamp 转换的 epoch、负值/溢出拒绝。
- Start/End/DCStart 的 neutral mapping，pointer key 4/8 bytes、缺失 key、PID 长度、映像上限/编码错误；不得读取 SID/CommandLine。
- core 的已解析归属、unresolved process、无匹配 `collector:unattributed`、PID 复用边界、probe failure、retransmit 和 accumulator overflow。
- 所有诊断饱和，ETW statistics 增长/重置，loss 使健康降级。
- session 部分启动清理、重复停止、already-exists 不抢占、显式 recovery policy、callback failure 和 `ProcessTrace` error。

管理员权限 live integration test 至少覆盖：

- 自定义 system logger 成功启动、可 query、可 flush、可停止，停止后同名 session 不存在。
- 本机 DCStart 包含 PID 4 且 image hint 为 `System`；否则测试失败并要求更新身份设计，不能静默按 PID 4 分类。
- parent test process 的 TCP/UDP IPv4/IPv6 loopback Send/Receive 可被 payload PID 归属；测试不依赖 event-header PID。
- 短命 child 发送流量后退出，字节仍归到它的 retained process instance/application。
- 高速 burst 不允许静默少计：若没有 loss，累计至少覆盖已确认 payload；若发生 loss，health 必须 `Degraded` 且 loss/unmatched 诊断非零。
- 非管理员启动返回明确 `AccessDenied`（单独降权子进程或受控 token），不把权限失败解释为没有流量。

完成条件：Format、严格 MSVC Debug、clang-tidy/Clang Static Analyzer、MSVC `/analyze`、全部 Debug/Release CTest 和 Release x64 通过；live tests 在 Windows 11 24H2 x64 管理员上下文实际执行而非跳过；记录 session 实际 schema、buffer、loss、EXE 依赖和体积变化；架构、ADR、规划和进度同步；没有地址/端口/载荷/命令行/SID 泄漏。25H2 的通过结果只能证明当前实现和测试在该主机成立，不能替代 24H2 最终兼容性验收。
