# 架构决策记录

| ADR | 决策 | 状态 |
|---|---|---|
| [0001](0001-native-cpp-win32.md) | 使用 C++20 和原生 Win32 | 已接受 |
| [0002](0002-dual-mode-collector.md) | 单一二进制支持绿色采集器和 Windows 服务 | 已接受 |
| [0003](0003-etw-per-application-accounting.md) | 使用 ETW 进行按应用实时计数 | 已接受 |
| [0004](0004-tray-and-floating-ui.md) | 仅使用托盘和悬浮窗，不嵌入任务栏 | 已接受 |
| [0005](0005-client-owned-ranking-configuration.md) | 排行配置属于 UI 客户端，采集器发布累计计数 | 已接受 |
| [0006](0006-mandatory-code-quality-gate.md) | 使用多工具 C++ 质量门并禁止红色提交 | 已接受 |

改变已接受 ADR 的结论时，应新增取代它的 ADR 或明确更新状态及理由，不应静默修改历史。
