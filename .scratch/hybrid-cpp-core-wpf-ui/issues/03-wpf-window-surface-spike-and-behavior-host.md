# 03 — WPF 窗口宿主预研 (Spike) 与确定性行为模型集成

**What to build:**
Execute the prerequisite WPF Window Surface Technical Spike (`WpfSurfaceSpike`) on Windows 11, implement the two-window topology (`DockCapsuleWindow` and `MonitorPanelWindow`), and integrate the pure, deterministic `MonitorBehaviorModel` presentation seam. The UI handles left/right work-area docking, release-point monitor selection, mixed-DPI scaling, 2px auto-hidden taskbar reveal preservation, Pin, hover delay, dismiss delay, temporary upward fit correction, and fullscreen topmost-yielding.

**Blocked by:** 01 — Protocol V1 基础帧协议与端到端 Tracer Bullet

**Status:** ready-for-agent

- [ ] `WpfSurfaceSpike` validates `WindowStyle=None`, `AllowsTransparency=False`, DWM system backdrop (Mica/Acrylic) and corner rounding (`DWMWCP_ROUND`) on a live Windows 11 host without opaque black rectangular gutters or GDI layered-window software fallback.
- [ ] `WpfSurfaceSpike` validates the 10 DIP visible capsule pill geometry with $\ge 16\text{ DIP}$ effective inward hit target and confirms that docking does not obstruct auto-hidden taskbar reveal zones.
- [ ] Pure C# `NetworkFlowMonitor.Presentation.MonitorBehaviorModel` is implemented without any references to `PresentationFramework`, `DependencyObject`, `HwndSource`, `Dispatcher`, timers, real clock, filesystem I/O, or Win32 APIs.
- [ ] Pure C# unit tests cover all behavior transitions: hover reveal delay, click reveal, pointer leave dismiss delay, drag start/threshold/cancel/complete, left/right tie-breaking, Pin state, auto-hide toggle, context menu pause/resume, collector disconnect/reconnect/reset, monitor removal fallback, and fullscreen topmost-yield.
- [ ] Two top-level WPF windows (`DockCapsuleWindow` and `MonitorPanelWindow`) are created representing mutually exclusive states of one logical monitor.
- [ ] `HwndSourceHook` normalizes raw Win32 messages (`WM_NCHITTEST`, `WM_MOVING`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`) into `MonitorEvent` inputs and drives the window adapters from the transition result.
- [ ] Temporary upward shift on reveal fits the screen without altering the persisted vertical anchor offset; external fullscreen foreground window lowers the monitor to non-topmost and restores topmost upon clearance.
