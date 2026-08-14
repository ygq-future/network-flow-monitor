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
普通权限,承载托盘、监控面板与设置界面。

## 界面

**监控面板 (Monitor panel)**
同时展示 Network Flow Monitor 身份、总上传/下载速率、Pin 以及 Top N 应用排行的单一桌面界面;它与胶囊是同一产品表面的两种可见形态。
_Avoid_: NFM 主面板、TopA 面板、悬浮窗、排行面板

**托盘图标 (Tray icon)**
常驻控制入口,右键进入设置、退出等操作;不展示速率。

**贴边 (Dock / snap)**
监控面板吸附到当前显示器工作区左边或右边的行为;上下边不参与贴边。

**贴边隐藏 (Hide-on-dock)**
监控面板贴边后自动缩成胶囊的行为。

**胶囊 (Dock capsule)**
未固定的监控面板贴边收缩后的固定尺寸入口,同时以颜色填充表达当前网络速率;可在不展示监控面板时沿贴边方向拖动。
_Avoid_: reveal 条、细圆棍

**固定显示 (Pin)**
阻止监控面板在指针离开后自动收缩为胶囊的持久用户状态。
_Avoid_: 锁定、冻结

**离线遮罩 (Offline overlay)**
采集器未连接时监控面板显示的离线状态。

## 采集

**一次性授权 (One-time authorization)**
管理员执行一次,授予当前用户 `SeSystemProfilePrivilege` 并用 `EventAccessControl` 把它加入 ETW 会话 DACL,此后采集器免 UAC 启动。见 ADR-0015。

**采集会话 (Collection session)**
固定名的自定义 SystemTraceProvider 会话(`NetworkFlowMonitor.SystemTrace`),是唯一的按进程网络计数数据源。见 ADR-0009。
