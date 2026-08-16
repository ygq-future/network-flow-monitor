# 架构决策记录

| ADR | 决策 | 状态 |
|---|---|---|
| [0001](0001-native-cpp-win32.md) | 使用 C++20 和原生 Win32 | 已接受 |
| [0002](0002-dual-mode-collector.md) | 单一二进制支持绿色采集器和 Windows 服务 | 已接受 |
| [0003](0003-etw-per-application-accounting.md) | 使用 ETW 进行按应用实时计数 | 已接受 |
| [0004](0004-tray-and-floating-ui.md) | 仅使用托盘和悬浮窗，不嵌入任务栏 | 已接受 |
| [0005](0005-client-owned-ranking-configuration.md) | 排行配置属于 UI 客户端，采集器发布累计计数 | 已接受 |
| [0006](0006-mandatory-code-quality-gate.md) | 使用多工具 C++ 质量门并禁止红色提交 | 已接受 |
| [0007](0007-vendored-yyjson.md) | 静态集成固定版本的 yyjson | 已接受 |
| [0008](0008-event-timeline-process-identity.md) | 用事件时间线和公开 Win32 API 构造进程身份 | 已接受 |
| [0009](0009-custom-realtime-system-etw-session.md) | 使用自定义实时 SystemTraceProvider 会话 | 已接受 |
| [0010](0010-owner-scoped-versioned-named-pipe-ipc.md) | 使用所有者限定的版本化命名管道 IPC | 已接受 |
| [0011](0011-settings-service-actions-uac.md) | 设置界面管理服务并隔离 UAC | 已接受 |
| [0012](0012-zip-portable-delivery.md) | 使用静态运行库 ZIP 作为首版交付物 | 已接受 |
| [0013](0013-user-login-startup.md) | 使用当前用户 Run 项管理 UI 登录自启 | 已接受 |
| [0014](0014-portable-collector-stop-signal.md) | 使用 UI 创建的命名事件停止绿色采集器 | 已接受 |
| [0015](0015-portable-collector-one-time-authorization.md) | 便携采集器用一次性授权免 UAC | 已接受 |
| [0016](0016-adjustable-composition-frosted-glass.md) | 使用系统合成亚克力与可调节高斯模糊 | 已接受 |
| [0017](0017-hybrid-cpp-core-wpf-fluent-ui.md) | 混合架构：C++ 高性能采集引擎与 C# .NET 10 WPF 现代前端 | 已接受 |

改变已接受 ADR 的结论时，应新增取代它的 ADR 或明确更新状态及理由，不应静默修改历史。
