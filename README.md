# Network Flow Monitor

一款面向 Windows 11 的轻量网络流量监控工具。它通过通知区图标和桌面悬浮窗显示系统实时上下行速率，并在悬停面板中展示实时流量最高的应用。

## 当前状态

项目已完成原生 C++20/CMake 初始化和单一二进制运行模式骨架。ETW、IPC、服务和最终 UI 仍按独立实现计划推进。已确认的首版方向：

- Windows 11 24H2 或更高版本，首发仅支持 x64。
- C++20、CMake、原生 Win32、Direct2D 和 DirectWrite。
- ETW 采集 TCP/UDP 事件，不开发内核驱动。
- 通知区图标和可贴边隐藏的桌面悬浮窗，不嵌入任务栏。
- 按最近时间窗口计算实时应用排行，并合并同一应用的多个进程。
- 同一可执行文件支持绿色提权采集器和可选 Windows 服务两种模式。
- 服务安装、启停、重启、卸载和模式切换均通过设置界面完成。

## 文档导航

- [设计规格](docs/superpowers/specs/2026-08-10-network-flow-monitor-design.md)
- [架构说明](docs/architecture.md)
- [配置模型](docs/configuration.md)
- [开发环境](docs/development-setup.md)
- [代码质量与提交门](docs/code-quality.md)
- [架构决策记录](docs/decisions/README.md)
- [代理约束](AGENTS.md)
- [任务计划](docs/planning/task_plan.md)
- [调研与发现](docs/planning/findings.md)
- [进度记录](docs/planning/progress.md)

## 构建与检查

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug

pwsh -NoProfile -File tools/quality.ps1 -Mode Format
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

提交前必须通过 `Full`。详细规则见 [代码质量与提交门](docs/code-quality.md)。

## 首版非目标

- Windows 10、Windows Server 和非 Windows 平台。
- 任务栏嵌入、任务栏内部窗口注入或 Explorer 补丁。
- 限速、防火墙、阻断连接、结束进程或抓取报文内容。
- 历史流量数据库、云同步、账号系统、插件系统和自动更新。
- 自定义内核驱动。

## 开发前提

参见 [开发环境](docs/development-setup.md)。当前骨架可直接使用 CMake 预设构建；后续子系统仍须先有审阅通过的实现计划。
