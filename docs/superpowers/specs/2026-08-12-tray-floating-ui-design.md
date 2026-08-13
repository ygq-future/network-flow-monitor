# 托盘与桌面悬浮 UI 设计

- 状态：提议实现方案
- 日期：2026-08-12
- 适用范围：Windows 11 24H2 x64 首发基线，当前验证主机为 Windows 11 25H2

## 1. 目标与非目标

本阶段实现普通权限 UI 进程中的通知区图标、桌面悬浮速率窗和悬停排行面板。托盘左键切换
悬浮窗；托盘或悬浮窗悬停达到 `interaction.hover_delay_ms` 后显示 Top N；鼠标离开锚点和
排行面板后经过 `interaction.dismiss_delay_ms` 隐藏。悬浮窗显示系统总上传/下载速率，排行行
显示应用图标、名称、下载、上传和合计速率。

本阶段不实现任务栏嵌入、Explorer 注入、设置页、服务 UAC 代理、长期历史、动画帧循环或自定义
字体/主题配置。服务和采集器仍不创建窗口；UI 通过现有版本化 IPC 客户端接收累计快照。

## 2. 交互状态模型

UI 主线程维护一个明确状态机：

```text
Hidden ──左键/菜单──► Visible
Visible ──悬停延迟──► RankingVisible
RankingVisible ──离开并达到消失延迟──► Visible
Visible ──自动隐藏延迟──► EdgeHidden
EdgeHidden ──鼠标进入/左键──► Visible
任何状态 ──IPC 断线──► DisconnectedOverlay
```

`EdgeHidden` 只保留 `floating.reveal_strip_dip` 的露出条；鼠标进入露出条恢复完整窗口。
排行面板被视为锚点的一部分，鼠标从托盘/悬浮窗移动到面板时不触发关闭。关闭、隐藏和断线
都是状态变化，不依赖持续动画。

所有延迟来自统一 `AppConfig`，时间接口使用 `std::chrono`。UI 只在数据、布局、视觉主题或
交互状态变脏时调用 `InvalidateRect`；不设置空闲高帧率定时器。数据显示刷新间隔由 IPC 订阅
和快照到达驱动，不能再引入第二个“UI 刷新率”。

## 3. 模块与边界

### `ui/ui_controller`

拥有 UI 线程消息循环、`IpcClient`、`RankingEngine`、`AppConfig` 快照和窗口对象。它把
`IpcClientObserver::SnapshotReceived` 转成排名快照，更新脏标志并请求一次重绘；它不读取
ETW、不调用 SCM、不执行提权。

### `ui/tray_controller`

封装 `NOTIFYICONDATAW`、`Shell_NotifyIconW` 和右键菜单。回调消息只投递到 UI 控制器，不在
Shell 回调中做阻塞 IPC 或文件操作。托盘图标可能进入系统溢出区，程序只显示提示，不尝试
修改 Explorer 的溢出策略。

### `ui/floating_window`

创建无边框 `WS_POPUP | WS_EX_TOOLWINDOW` 窗口，使用每显示器 DPI 感知上下文。窗口支持拖动、
贴边、自动隐藏和恢复；总速率由 `RankingEngine::total_rate()` 的最新结果绘制。窗口过程只
处理输入、DPI、工作区和绘制消息，业务数据由控制器注入。

### `ui/ranking_popup`

创建不激活的工具弹层，使用同一 Direct2D/DirectWrite 设备资源。弹层打开前按当前 DPI 测量
文本和行高，再为锚点计算上、下、左、右四个候选矩形：优先完整可见且不遮挡锚点的方向，
否则选择可用面积最大的方向并钳制到当前显示器工作区。

### `ui/dpi_layout` 与纯逻辑测试

集中 DIP/像素转换、工作区钳制、边缘贴附和弹层候选排序。它不依赖 HWND，可用固定矩形和 DPI
输入单元测试多显示器、负坐标、混合 DPI、自动隐藏露出条和任务栏工作区变化。

### `ui/render_resources`

在 UI 线程创建 Direct2D 工厂、设备上下文、DirectWrite 工厂和共享文本格式；处理
`D2DERR_RECREATE_TARGET` 时丢弃窗口目标位图并在下一次脏重绘中重建，不在绘制回调抛异常。

## 4. 数据流与绘制

```text
IPC snapshot
    │
    ▼
UI controller ──► RankingEngine ──► UiSnapshot + total rate
    │                                      │
    ├── dirty=data ──► floating window ────┤
    └── dirty=data/interaction ──► ranking popup
```

IPC 客户端断线、实例变化、版本不兼容或未授权时清除旧目录和快照；在新完整目录及首个基线
快照到达前，UI 显示“未连接/正在同步”，不把旧排行标成实时。快照中的应用元数据通过现有
目录句柄关联，UI 不从网络事件重新解析进程。

绘制采用固定的最小视觉层次：悬浮窗先绘制半透明背景和上下行速率文本，排行弹层绘制标题、
Top N 行、方向速率和健康提示。测量和绘制都使用当前窗口 DPI 的 DIP 值；窗口大小变化只在
布局脏时发生。图标加载失败时使用无图标占位，不阻止速率文本更新。

## 5. Win32 生命周期与安全

- 在创建任何 UI 窗口前设置 `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`；失败时记录诊断并
  使用系统默认 DPI，不伪装为已完成混合 DPI 支持。
- UI 线程是唯一访问窗口和 Direct2D 资源的线程；IPC 回调通过消息或受保护快照转交 UI 线程。
- 托盘资源在消息循环退出前删除；悬浮窗和弹层先销毁，再释放渲染资源和 IPC 客户端。
- UI 正常权限运行；启动绿色采集器或服务管理动作只调用未来设置/进程管理边界的明确 UAC
  入口，不在 UI 进程内直接开启 ETW。
- 关闭窗口默认隐藏到托盘；退出菜单执行完整的 IPC 取消订阅、窗口销毁和资源释放。

## 6. 错误处理

- `Shell_NotifyIconW` 或窗口创建失败：保留无窗口的 IPC/诊断状态并显示可诊断错误，不启动
  第二个 UI 线程重试。
- Direct2D 目标丢失：按 `D2DERR_RECREATE_TARGET` 路径释放并延迟到下一次脏事件重建。
- IPC 断线：显示非实时状态，沿用现有有限退避重连；重连成功后重新订阅当前刷新间隔。
- 配置损坏：使用 `config` 模块安全默认值并保留诊断；UI 不在绘制路径静默覆盖原文件。
- 工作区或 DPI 变化：重新测量并钳制窗口；如果候选空间不足，使用最大可用区域而不是把
  面板放到屏幕外。

## 7. 测试与验收证据

纯逻辑测试覆盖：DIP 转换、四方向候选排序、工作区钳制、贴边距离、露出条、悬停/消失状态
转换和脏标志合并。Win32 测试覆盖：托盘回调菜单、弹层不激活行为、`WM_DPICHANGED`、
`WM_DISPLAYCHANGE`、`WM_SETTINGCHANGE`、任务栏自动隐藏工作区和窗口销毁顺序。

手工/集成验收覆盖至少两台显示器、主副屏不同 DPI、负坐标、任务栏位置变化、托盘溢出和
断线重连。当前主机是 Windows 11 25H2；Windows 11 24H2 的最终兼容性仍属于 Phase 5，不能
由新主机测试推断。

## 8. 可配置项映射

本阶段不新增配置键，直接使用：

| 行为 | 配置 |
|---|---|
| Top N 和排行平滑 | `ranking.top_n`, `ranking.window_ms` |
| 快照/可见数据更新 | `display.refresh_interval_ms` |
| 悬停/关闭 | `interaction.hover_delay_ms`, `interaction.dismiss_delay_ms` |
| 贴边/自动隐藏 | `floating.snap_distance_dip`, `floating.auto_hide_enabled`, `floating.auto_hide_delay_ms`, `floating.reveal_strip_dip` |
| 透明度和方向显示 | `floating.opacity_percent`, `display.show_upload`, `display.show_download`, `display.speed_unit` |
