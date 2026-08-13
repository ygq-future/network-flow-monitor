# ADR 0014：使用 UI 创建的命名事件停止绿色采集器

- 状态：已接受
- 日期：2026-08-12

## 背景

绿色模式由普通权限 UI 通过 `ShellExecuteExW` 的 `runas` 动词启动提权采集器。早期退出路径
只调用 `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, ...)`，但 GUI 启动的子进程不保证拥有可用的
共享控制台，因此该信号不能作为跨进程生命周期协议。强制终止虽能回收进程，却可能留下 ETW
logger 或跳过 CollectorHost 的有序清理。

## 决策

UI 启动绿色采集器前创建一次性的手动复位事件，名称形如
`Local\\NetworkFlowMonitor.Stop.<ui-pid>.<qpc>.<sequence>`，并通过内部参数
`--collector --stop-event <name>` 传递。采集器只接受 `Local\\NetworkFlowMonitor.Stop.`
前缀、长度上限和无空白/引号的名称，再以 `SYNCHRONIZE` 权限打开事件。采集器运行期间由等待线程
监听事件并请求同一个 `std::stop_source`；UI 停止时先置位事件，再尝试原有控制台信号，等待有界
时间后才使用 `TerminateProcess` 兜底。UI 和采集器都使用 RAII 关闭各自句柄。

服务模式不使用该事件，仍由 SCM 控制服务入口生命周期。

## 后果

- GUI 启动的绿色采集器具有不依赖控制台的正常退出通道，降低 ETW 会话残留风险。
- 事件名称不是用户配置，不进入持久化文件或 IPC 数据；前缀校验限制了可打开对象范围。
- 事件等待采用短周期等待以便响应线程停止请求，最终停止延迟仍受现有有界超时控制。
- Windows 11 24H2 上的真实 UAC、关闭和 ETW 清理仍需在目标机验收；当前自动化覆盖了解析、模拟 UI 生命周期和当前主机 smoke。
