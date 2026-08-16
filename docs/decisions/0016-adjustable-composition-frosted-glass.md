# ADR 0016：使用系统 Windows.UI.Composition 实现可调毛玻璃

- 状态：已接受
- 日期：2026-08-15
- 批准：用户在 ticket 03 修复中明确批准原生 Composition 渲染器改造

## 背景

`DWMSBT_TRANSIENTWINDOW` 能提供系统 Acrylic，但公开 DWM API 不提供模糊半径。仅改变 Direct2D tint 的 alpha 可以增加透感，却不能满足“透明背景内容可辨认、模糊强度可在设置中调整”的要求。

## 决策

监控面板仍使用一个 Win32 HWND。Composition 背景与传统 `ID2D1HwndRenderTarget` 属于两条异步呈现链，背景会覆盖前景内容，因此前景 Direct2D 内容也必须进入同一棵 Composition 树。最终树形为：`HostBackdrop → tint SpriteVisual → foreground SpriteVisual`，三者共享一个 material container 的统一圆角裁剪。`HostBackdrop` 采样窗口绘制前的真实桌面背景；tint 控制半透明叠色；foreground 使用 `CompositionDrawingSurface` 承载业务 Direct2D 绘制。不能使用普通 `CompositionBackdropBrush`，否则它会把本窗口上一帧 Direct2D 内容重新采样进下一帧，形成反馈闪烁。

前景 `CompositionDrawingSurface` 的 `BeginDraw` 返回一个共享 atlas 的 `ID2D1DeviceContext` 和一个像素坐标 `updateOffset`。调用方在该 DC 上设置 DPI 后，应用 `Translation(offset.x × 96/dpi, offset.y × 96/dpi)` 平移变换，使业务代码在 DIP (0,0) 绘制的内容落到 atlas 的正确偏移位置。`CompositionSurfaceBrush` 以 `CompositionStretch::Fill` 将 surface 映射到 foreground visual。

公开配置 `floating.blur_strength_percent` 的范围为 0–100，映射到 0–30 DIP 的 Gaussian 标准差；`floating.opacity_percent` 只控制 tint，不降低文字、图标和速率轨道的不透明度。

实现使用 Windows SDK 自带 C++/WinRT 与 `IGraphicsEffectD2D1Interop`，不引入 Win2D、WinUI 3、Windows App SDK 或任何随包分发的图形 DLL。高对比度、Composition 初始化失败或设备丢失时退回当前可读的不透明 Direct2D HWND 路径。

## 后果

- 前景内容不能声称位于 Composition 背景之上的独立 HWND 重定向层；它必须作为同一棵 Composition 树中 tint 之上的 SpriteVisual。
- 模糊强度和 tint 透明度成为彼此独立、可持久化的设置。
- Direct3D 11、DXGI、Windows.UI.Composition 与 CoreMessaging 成为系统链接依赖，但不增加生产包中的第三方 DLL。
- composition surface 必须在每次绘制时完整清理，DPI/尺寸变化必须同步 resize；设备丢失必须释放并重建合成资源。
- 空闲时仍不运行固定绘制循环；只在数据、交互、设置或动画使视觉状态变脏时重画 drawing surface。
