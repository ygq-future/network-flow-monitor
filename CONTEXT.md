# Network Flow Monitor 领域词汇表

本文件定义 Network Flow Monitor 的领域概念,只记录本项目特有的术语(不含实现细节、通用编程概念)。命名冲突时以这里为准;修改领域模型时同步更新本文件。

## 运行形态

**便携模式 (Portable mode)**
双击 EXE 直接运行:普通权限的 UI 进程 + 采集器子进程,免安装、免服务注册。
_Avoid_: 绿色版(旧称)

**服务模式 (Service mode)**
采集器作为 Windows 服务常驻运行,UI 进程连接服务获取数据。

**采集器 (Collector)**
拥有 ETW 采集权限、对外发布累计计数的进程或服务;绝不在 UI 进程内运行。

**UI 进程 (UI process)**
普通权限,承载托盘、悬浮窗与设置界面。

## 界面

**悬浮窗 (Floating window)**
置顶常驻、负责展示上下行速率与排行入口的小窗口。速率固定用悬浮窗展示,托盘不放速率。

**排行面板 (Ranking popup)**
从悬浮窗或托盘锚点展开的 Top N 应用流量列表。

**托盘图标 (Tray icon)**
常驻控制入口,右键进入设置、退出等操作;不展示速率。

**贴边 (Dock / snap)**
悬浮窗吸附到工作区边缘的行为。

**贴边隐藏 (Hide-on-dock)**
悬浮窗贴边后自动缩成一条 reveal 条的行为。

**reveal 条 (Reveal strip)**
贴边隐藏后露出的细条,鼠标滑上即重新展开悬浮窗。

**离线遮罩 (Offline overlay)**
采集器未连接时悬浮窗显示的离线状态。

## 采集

**一次性授权 (One-time authorization)**
管理员执行一次,授予当前用户 `SeSystemProfilePrivilege` 并用 `EventAccessControl` 把它加入 ETW 会话 DACL,此后采集器免 UAC 启动。见 ADR-0015。

**采集会话 (Collection session)**
固定名的自定义 SystemTraceProvider 会话(`NetworkFlowMonitor.SystemTrace`),是唯一的按进程网络计数数据源。见 ADR-0009。
