# ADR 0013：使用当前用户 Run 项启动 native host

- 状态：已接受
- 日期：2026-08-12
- 修订：2026-08-28

## 决策

`startup.ui_at_login` 由普通权限 native host 管理。启用时在 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 下写入固定值名 `NetworkFlowMonitor`，数据为带引号的 `NetworkFlowMonitor.exe` 绝对路径；禁用时删除该值。失败必须反馈给设置页并保持配置与注册表状态一致。

## 后果

- 自启只影响当前用户且不触发 UAC。
- ZIP 所在目录移动后，用户需要重新保存自启设置。
- 该决策由 WebView2 host ticket 05 实现和验收。
