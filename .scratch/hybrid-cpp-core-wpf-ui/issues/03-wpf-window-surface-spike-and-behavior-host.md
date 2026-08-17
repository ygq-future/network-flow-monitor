# 03 — WPF 窗口宿主预研 (Spike) 与确定性行为模型集成

**What to build:**
Execute the prerequisite WPF Window Surface Technical Spike (`WpfSurfaceSpike`) on Windows 11 and record a definitive technical conclusion for Ticket 04; implement the two-window topology (`DockCapsuleWindow` and `MonitorPanelWindow`), and integrate the pure, deterministic `MonitorBehaviorModel` presentation seam. The UI handles `TrayOnly` / `CapsuleVisible` / `PanelVisible` visibility states, `HideMonitorRequested` / `ShowMonitorRequested`, left/right work-area docking, release-point monitor selection, mixed-DPI scaling, 2px auto-hidden taskbar reveal preservation, Pin and AutoHide state rules, hover delay (350 ms), dismiss delay (500 ms), continuous capsule-to-panel pointer tracking transfer, temporary upward fit correction, fullscreen topmost-yielding, context-menu-close pointer reconciliation, and emission of persistence intents (`EffectIntent.PersistAnchor`, `EffectIntent.PersistPin`).

**Blocked by:** 01 — Protocol V1 基础帧协议与端到端 Tracer Bullet

**Status:** complete

- [x] `WpfSurfaceSpike` executes as an interactive, live Windows 11 target-host technical acceptance spike (separate from automated headless quality gates) to validate `WindowStyle=None`, `AllowsTransparency=False`, DWM system backdrop (Mica/Acrylic), and corner rounding (`DWMWCP_ROUND`) without opaque black rectangular gutters or GDI layered-window software fallback.
- [x] `WpfSurfaceSpike` validates the 10 DIP visible capsule pill geometry with $\ge 16\text{ DIP}$ effective inward hit target on a live display, confirms that docking does not obstruct auto-hidden taskbar reveal zones, and records a definitive durable decision in ADR/findings consumable by Ticket 04.
- [x] Pure C# `NetworkFlowMonitor.Presentation.MonitorBehaviorModel` is implemented without any references to `PresentationFramework`, `DependencyObject`, `HwndSource`, `Dispatcher`, timers, real clock, filesystem I/O, or Win32 APIs.
- [x] Pure C# unit tests cover all behavior transitions: `TrayOnly` $\leftrightarrow$ `CapsuleVisible` $\leftrightarrow$ `PanelVisible`, hover reveal delay (350 ms), click reveal, pointer leave dismiss delay (500 ms) with smooth mid-flight reversal upon pointer return, drag start/threshold/cancel/complete, left/right tie-breaking, Pin state preserved when AutoHide is disabled, context menu pause/resume and close pointer reconciliation, collector disconnect/reconnect/reset, monitor removal fallback, and fullscreen topmost-yield.
- [x] Two top-level WPF windows (`DockCapsuleWindow` and `MonitorPanelWindow`) are created representing mutually exclusive states of one logical monitor.
- [x] `HwndSourceHook` normalizes raw Win32 messages (`WM_NCHITTEST`, `WM_MOVING`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`) into `MonitorEvent` inputs and drives the window adapters from the transition result.
- [x] Temporary upward shift on reveal fits the screen without altering the persisted vertical anchor offset; external fullscreen foreground window lowers the monitor to non-topmost and restores topmost upon clearance.
