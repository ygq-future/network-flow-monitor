# ADR 0013：使用当前用户 Run 项启动 native host

- 状态：已接受
- 日期：2026-08-12
- 修订：2026-08-28

## 决策

`startup.ui_at_login` 由普通权限 native host 管理，操作目标为 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`：
- **关闭（禁用）自启时**：扫描 `Run` 键及当前用户 `Startup` 启动文件夹，只要存在与当前应用同名（如 `NetworkFlowMonitor`）或指向同名可执行文件（`NetworkFlowMonitor.exe`，无论位于哪个磁盘路径）的开机启动项，一律全部删除。
- **开启（启用）自启时**：先执行上述同名跨路径清理，然后将当前正在运行的应用程序绝对路径（带引号，如 `"D:\...\NetworkFlowMonitor.exe"`）写入 `NetworkFlowMonitor` 值；宿主每次启动且配置开启自启时也会自动同步刷新为当前运行路径。
- 失败必须反馈给设置页、记录带 Win32 错误码的诊断日志，并保持配置与注册表状态一致。

## 后果

- 自启只影响当前用户且不触发 UAC。
- 便携包目录移动后，关闭或开启自启以及重新启动应用均不会残留失效的旧路径启动项。
