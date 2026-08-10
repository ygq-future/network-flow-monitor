# Network Flow Monitor

一款面向 Windows 11 的轻量网络流量监控工具。它通过通知区图标和桌面悬浮窗显示系统实时上下行速率，并在悬停面板中展示实时流量最高的应用。

## 当前状态

项目处于设计与文档阶段，尚未开始编写生产代码。已确认的首版方向：

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

## 首版非目标

- Windows 10、Windows Server 和非 Windows 平台。
- 任务栏嵌入、任务栏内部窗口注入或 Explorer 补丁。
- 限速、防火墙、阻断连接、结束进程或抓取报文内容。
- 历史流量数据库、云同步、账号系统、插件系统和自动更新。
- 自定义内核驱动。

## 开发前提

参见 [开发环境](docs/development-setup.md)。仓库当前还没有 CMake 工程；书面规格通过评审后才会生成实现计划和代码结构。
