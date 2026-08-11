# ADR 0009：使用自定义实时 SystemTraceProvider 会话

- 状态：已接受
- 日期：2026-08-11

## 背景

Collector 需要同时获取 TCP、UDP 和进程生命周期事件，并以低延迟累计每个应用的上下行字节。Windows 的经典网络事件来自 SystemTraceProvider；使用全局 `KERNEL_LOGGER_NAME` 或 `SystemTraceControlGuid` 会占用已弃用的单一 NT Kernel Logger 身份，并可能与性能工具冲突。网络事件量远高于进程事件量，完全逐属性调用 TDH 会给热路径增加不必要的扫描开销，而完全手写所有可变长进程结构又容易把 pointer size、SID 或字符串边界解析错误。

## 决策

Collector 启动项目专属名称 `NetworkFlowMonitor.SystemTrace` 和 GUID `{1473ea71-a063-4396-8ea1-e3d1ede80af7}` 的实时 system logger，会话使用 `EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_REAL_TIME_MODE`，并通过 `EnableFlags` 只启用 `EVENT_TRACE_FLAG_NETWORK_TCPIP | EVENT_TRACE_FLAG_PROCESS`。不使用 `KERNEL_LOGGER_NAME`、`SystemTraceControlGuid`、文件日志、stack walk 或无关系统事件。

控制器使用 QPC 时钟；消费者以 `PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD` 调用 `OpenTraceW`，不启用 `PROCESS_TRACE_MODE_RAW_TIMESTAMP`。因此 `ProcessTrace` 把事件时间转换为 FILETIME 形式的 100ns 系统时间，随后必须通过现有命名转换函数进入 `TraceTimePoint`。

解码采用混合策略：

- 只接受目标 Windows 11 实测确认的经典 TCP/UDP version 2 事件。对 Send、Receive 和 TCP Retransmit，直接边界检查并读取公开 MOF 布局开头的 payload PID 与 `size`；未知版本或 opcode 不猜测。
- 进程 Start、End 和 DCStart 的事件量较低，使用 TDH 只读取 `UniqueProcessKey`、`ProcessId` 和 `ImageFileName`。官方资料覆盖 version 2/3，Windows 11 25H2 build 26200 实测生命周期事件为 version 4，因此只对白名单 version 2、3、4 使用同名 TDH 属性；未知版本仍拒绝。不读取或保留 SID、CommandLine、地址和端口。
- Send/Receive 的 `size` 计入相应方向；TCP Retransmit 额外计入上传并单独诊断。该口径表示 ETW 网络栈事件报告的字节，不宣称等于包含链路/IP/TCP 头和网卡 offload 影响的物理接口线速字节。

会话所有权默认不抢占同名会话。显式 orphan recovery 是独立操作，只有未来运行模式层确认没有活动 Collector 并取得跨会话单实例所有权后才能调用。部分启动、正常停止和异常退出路径都必须 RAII 清理 controller 与 consumer handle；回调捕获所有异常并把 Collector 转入失败状态，异常不得穿过 ETW ABI。

## 后果

- 避免占用已弃用的全局 NT Kernel Logger 身份，同时仍使用 Windows 11 支持的 multiplexed system logger。
- 网络热路径只做固定前缀检查和整数读取；TDH 的系统 DLL 依赖和较高解析成本仅用于低频进程事件。
- 未知 schema、损坏 payload、无匹配进程、探测失败、累计溢出和事件丢失都能分类并出现在健康状态中，不会被默认为零错误。
- ETW 自动 flush 的最小周期是一秒。未来快照调度器在订阅刷新间隔短于一秒时需要显式 `EVENT_TRACE_CONTROL_FLUSH`；仍只保留一个用户可见的数据显示刷新间隔。
- 首版缓冲配置从 64 KiB buffer、ETW 调整的 buffer pool 开始，并记录查询到的实际值和丢失计数。任何后续调优必须用 24H2 x64 测量更新本 ADR 或实现计划，不能凭感觉写宣传阈值。

## 未采用方案

- **NT Kernel Logger：**名称和控制 GUID 已被 Microsoft 标为旧式用法，且会和其他控制器争用同一身份。
- **每个网络事件调用 `TdhGetProperty`：**官方明确指出非 WPP 事件每次都会扫描属性，热路径成本不合适。
- **完整硬编码进程 payload：**进程结构含 pointer-sized key、SID 和可变长字符串；为只取三个字段承担全部 ABI 风险没有收益。
- **抓取远端地址/端口或报文载荷：**排行只需要 PID、方向和字节；额外数据扩大隐私与 IPC 面，不属于产品目标。
