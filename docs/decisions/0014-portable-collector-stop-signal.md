# ADR 0014：使用 host 创建的命名事件停止 portable collector

- 状态：已接受
- 日期：2026-08-12
- 修订：2026-08-28

## 决策

native host 启动 Collector 前创建一次性手动复位事件，名称使用 `Local\NetworkFlowMonitor.Stop.<host-pid>.<nonce>`，并通过 `--stop-event <name>` 传递。Collector 仅接受该前缀、合法字符和有界长度，再以 `SYNCHRONIZE` 权限打开事件。host 退出时置位事件；Collector 将其转换为 `std::stop_source` 请求并依次停止 IPC 与 ETW。

## 后果

- 生命周期协议不依赖共享控制台。
- 事件名只用于单次运行，不写入配置或 IPC 消息。
- Windows 11 24H2 上仍需验收 UAC 启动、正常退出和 ETW logger 清理。
