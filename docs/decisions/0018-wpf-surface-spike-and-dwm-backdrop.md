# ADR 0018：WPF 窗口表面技术预研（Spike）结论与 DWM 背景合成规范

- 状态：已接受
- 日期：2026-08-17
- 关联：ADR-0016, ADR-0017, Ticket 03, Ticket 04

## 背景

在从纯 C++ Win32 迁移至 .NET 10 WPF 现代前端架构（ADR-0017）的过程中，需要验证在 Windows 11 下实现 Fluent 亚克力/云母材质、圆角裁剪、无边框停靠胶囊和监控面板的最佳技术路径。

过去在 WPF 中使用无边框透明窗口时，传统的 `AllowsTransparency=True` 方案会强制开启 GDI 分层窗口（`WS_EX_LAYERED`），导致以下严重缺陷：
1. 产生软件渲染回退，无法启用 DWM 硬件合成系统背景（Mica / Acrylic）。
2. 在 Windows 11 窗口圆角裁剪时出现黑色矩形边缘（Black gutters）。
3. 渲染性能低下，在高刷新率屏幕上带来帧率抖动。

## 预研验证与结论

通过 `WpfSurfaceSpike` 技术预研与真机验证，确定了以下核心技术规范：

### 1. 窗口宿主属性规范
- **`WindowStyle=None`** 与 **`AllowsTransparency=False`**：作为所有监控前端顶层窗口（`DockCapsuleWindow` 和 `MonitorPanelWindow`）的非协商基础。
- **`WindowChrome` 消除非客户区边距**：必须配置 `<WindowChrome.WindowChrome><WindowChrome CaptionHeight="0" CornerRadius="0" GlassFrameThickness="-1" NonClientFrameEdges="None" ResizeBorderThickness="0" /></WindowChrome.WindowChrome>`，彻底消除 Windows 默认标题栏顶部内边距与外层边框残留。
- **`Background="Transparent"`**：WPF XAML 根画刷设为 Transparent，底层由 DWM 亚克力/云母接管。
- **`DwmExtendFrameIntoClientArea`（Margins: `-1, -1, -1, -1`）**：将 DWM 窗口客户区扩展至整窗，使 DWM 系统合成材质无缝铺满背景。
- **`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW (3)`**：监控面板启用 Fluent Acrylic 亚克力背景；设置窗口启用 Mica 云母背景。
- **`DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND (2)`**：由 DWM 负责原生硬件抗锯齿圆角裁剪；WPF 内部根 `Border.CornerRadius` 必须与 `DWMWCP_*` 硬件裁剪半径严格保持同阶（标准圆角 8px，微圆角 4px，直角 0px），杜绝内外层弧线脱节。
- **`WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST`**：保持独立悬浮窗口不在任务栏及 Alt+Tab 中抢占焦点。

### 2. 双窗口拓扑与几何命中规范
- **双窗口互斥表示**：`DockCapsuleWindow`（胶囊态）与 `MonitorPanelWindow`（面板态）表示同一逻辑监控器的两种互斥呈现状态。
- **胶囊 10 DIP 胶囊与 $\ge 16\text{ DIP}$ 内向命中区**：窄胶囊通过透明 Grid 根容器提供向屏幕内侧延伸至至少 16 DIP 的无形命中区域，解决边缘像素级定位困难问题。
- **自动隐藏任务栏 2px 避让**：在停靠边缘存在自动隐藏任务栏时，胶囊外侧边界主动预留 2 像素系统唤醒区域，防止阻挡任务栏触发。

### 3. 确定性行为模型集成
- 所有窗口状态机、多显示器停靠计算、悬停 350ms 展开、离开 500ms 折叠、全屏游戏避让和临时向上修正由纯 C# `MonitorBehaviorModel` 驱动，UI 代码仅作为无状态适配器响应 `MonitorTransition` 结果。

## 供 Ticket 04 消费的决定

1. Ticket 04 开发流体渐变动效与生产级胶囊/面板时，直接基于 `DockCapsuleWindow` 和 `MonitorPanelWindow` XAML 结构开发，严禁引入 `AllowsTransparency=True`。
2. 动画必须走合成通道（`RenderTransform` / `ScaleTransform` / `Geometry.Clip` / `ColorAnimation`），固定布局尺寸，杜绝布局颠簸（Zero-Layout-Thrashing）。
3. 根容器 `Border.CornerRadius` 保持 8px 与 DWM `DWMWCP_ROUND` 完美重合。
