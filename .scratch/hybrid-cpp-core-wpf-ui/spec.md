# Hybrid C++ Core Engine and .NET 10 WPF Fluent UI Modernization

Status: ready-for-agent
Architecture-Mode: Portable-Companion-Only (ADR-0017)

## Problem Statement

Network Flow Monitor's user experience currently suffers from a fundamental impedance mismatch: handwritten Win32 message loops and Direct2D drawing surfaces cannot cost-effectively deliver the fluid, modern Windows 11 Fluent micro-animations expected of desktop ambient widgets. Specifically:

1. **Capsule Animation Stutter and Discrete Stepping**: In raw Win32, driving gradient fills and height transitions across 1 Hz polling intervals via `PostMessage` and `DwmFlush` either creates 0 ms busy-loops that finish in less than 1 ms (rendering as a 1-frame instant jump) or skips frames under Win32 `WM_PAINT` coalescing. The dock capsule's rate fill visibly jumps in discrete steps rather than smoothly breathing with live bandwidth.
2. **High UI Development Friction in C++**: Building accessible, localized, theme-aware, animated UI (such as rounded pill shapes, interactive settings, and dynamic gradient morphing) in raw C++ Direct2D/DirectWrite requires thousands of lines of low-level boilerplate, custom hit-testing, and manual timer plumbing.
3. **Core Engine Strength**: Conversely, the project's C++ core engine—ETW packet capture, PID reuse defense, multi-process identity aggregation, sliding-window rate calculation, and process tracking—is already robust, highly optimized, and memory-efficient.

From the user's perspective, the application must be a **100% zero-install, portable green utility**: extracting the ZIP and running `NetworkFlowMonitor.exe` provides a modern, silky-smooth desktop experience. When elevated ETW kernel tracing is needed, a background companion collector process (`nfm-collector.exe`) is launched on demand via a standard UAC prompt. Closing or killing the UI automatically terminates the collector companion, leaving no persistent services, SCM entries, scheduled tasks, or orphan processes behind.

## Solution

Evolve Network Flow Monitor into a **Portable-Only Decoupled Dual-Process Architecture** (ADR-0017):

```
┌────────────────────────────────────────────────────────┐
│               NetworkFlowMonitor.exe                   │
│               .NET 10 / C# 14 / WPF                    │
│             (Standard User Privileges)                 │
│                                                        │
│  - Dock Capsule & Single Monitor Panel Windows         │
│  - Presentation / MonitorBehaviorModel (Deterministic) │
│  - TelemetryStore & Ingestion Engine                   │
│  - Settings Control Center & Diagnostics               │
│  - Executable-Adjacent JSON ConfigStore                │
│  - System Tray Controller & Shared Context Menu        │
└───────────────────────────┬────────────────────────────┘
                            │
                            │ Authenticated Local Named Pipe
                            │ (12-Byte Header Protocol V1)
                            │
┌───────────────────────────▼────────────────────────────┐
│                  nfm-collector.exe                     │
│                        C++20                           │
│             (Elevated Portable Companion)              │
│                                                        │
│  - User-Mode ETW Kernel Network Packet Trace           │
│  - Application Identity & PID Tracking                 │
│  - Sliding-Window Rate & Top-N Ranking Aggregation     │
│  - Dual-Cadence Dispatch (4 Hz Traffic, 1 Hz Ranking)  │
│  - Parent-Process Watch & Auto-Termination             │
└────────────────────────────────────────────────────────┘
```

1. **Target End State Architecture**:
   - **C++ Core Companion (`nfm-collector.exe`)**: A single lightweight executable dedicated exclusively to elevated kernel ETW packet capture, process identity resolution, sliding-window rate calculation, Top-N ranking algorithms, and owner-scoped Named Pipe IPC server dispatch. Completely stripped of all windowing, GDI, Direct2D, DirectWrite, DirectComposition, and Windows Service (SCM) UI/management code.
   - **Modern .NET 10 / C# 14 WPF Fluent UI (`NetworkFlowMonitor.exe`)**: Runs with standard user token (no mandatory UAC prompt for UI interaction, no elevated desktop risks). Consumes the IPC stream via `NamedPipeClient`, maintains an atomic latest-state `TelemetryStore`, and delegates all presentation state transitions to a platform-independent, deterministic `MonitorBehaviorModel`.
   - **Phased Migration Rule**: During migration, the legacy C++ UI target and its test baseline remain fully functional in the repository until Architecture Gate 7 (Automated Hybrid E2E & Target-Host Tracer Acceptance) passes.
   - **Green Zero-Install Policy**: The product operates exclusively as a portable companion pair. Windows Services, SCM registration, LocalSystem daemons, and Scheduled Task elevation backdoors are strictly excluded. Cold starts intentionally accept a standard UAC prompt to launch the elevated collector companion.

2. **Dual-Cadence IPC Stream**:
   - **4 Hz `TrafficTick` (250 ms)**: Lightweight total upload/download rates, sequence number, and collector-instance monotonic timestamp.
   - **1 Hz `RankingSnapshot` (1000 ms)**: Full Top N application details, process metadata, and aggregate rates.

3. **Zero-Layout-Thrashing Presentation & Animation**:
   - Fixed capsule geometry; fill height and gradient progression animate exclusively via layout-free visual transforms (`RenderTransform` / `ScaleTransform`, `Geometry.Clip`, and `ColorAnimation`) that avoid continuous layout-size mutation.
   - Asymmetric Ballistics: 250 ms Fluent Cubic Ease-Out ($1 - (1-t)^3$) on rate attack/growth, and 450 ms natural momentum decay ($1 - (1-t)^2$) on rate falloff. Animation frames exist only while a transition is active; the application does not own a fixed 16 ms animation timer or perpetual 60 Hz redraw loop.

4. **Windowing & Interop Subsystem**:
   - Two top-level WPF windows representing mutually exclusive states of one logical monitor: `DockCapsuleWindow` and `MonitorPanelWindow`.
   - Standard WPF `Window` with `WindowStyle=None`, `AllowsTransparency=False`, and `HwndSourceHook` for deterministic left/right work-area docking, hover reveal, auto-hide collapse, multi-DPI scaling, and fullscreen topmost-yield.

## Predecessor Spec Disposition

This matrix explicitly records the fate of every confirmed product behavior and architectural requirement from the previous specification (`Native Win11 UI modernization and monitor-panel interaction`):

| Requirement / Product Behavior | Disposition | New Spec Mapping & Architecture Rationale |
| :--- | :--- | :--- |
| **Deterministic Monitor Behavior Seam** | **Retained** | Migrated to `NetworkFlowMonitor.Presentation.MonitorBehaviorModel` as a pure C# deterministic model without UI/Win32/Timer dependencies. |
| **Single Monitor Panel (No Separate TopA Product)** | **Retained** | Header and Top N rows reside in one unified `MonitorPanelWindow`; no second floating window or separate visibility toggle. |
| **Left / Right Work-Area Docking Only** | **Retained** | Panel and capsule dock exclusively to left/right monitor work-area edges; top/bottom docking prohibited. |
| **Release-Point Monitor Selection** | **Retained** | Dock monitor chosen by pointer release coordinates across multi-monitor environments. |
| **Exact-Tie Side Rule & First-Use Default** | **Retained** | Exact-tie retains previous side; first unresolved tie defaults to the right edge. |
| **Work-Area Clamping & Taskbar Respect** | **Retained** | Vertical position constrained to monitor work area across all taskbar orientations (bottom/top/left/right). |
| **Auto-Hidden Taskbar Reveal Reservation** | **Retained** | Capsule reserves the 2-pixel reveal zone when docked on an auto-hidden taskbar edge. |
| **Temporary Upward Correction on Panel Reveal** | **Retained** | When panel exceeds available vertical space, it shifts upward to fit; persisted anchor offset is unchanged. |
| **Ranking Viewport Vertical Scrolling** | **Retained** | When Top N rows exceed available screen height, only the ranking row viewport scrolls via touchpad, wheel, and keyboard. |
| **Monitor Removal Fallback** | **Retained** | If saved monitor disconnects, window safely restores to primary display; does not auto-jump if disconnected display returns. |
| **Completed vs Canceled Drag Persistence** | **Retained** | Persists position only after completed drag; canceled drag restores previous anchor; persistence failure preserves current session position and emits diagnostics. |
| **Pin Semantics & Global Auto-Hide** | **Retained** | Pin managed via shared context menu; disabling auto-hide keeps panel visible and disables Pin item while preserving stored Pin preference. |
| **Context Menu Nested Loop & Dismiss Pause** | **Retained** | Opening context menu pauses auto-hide timer; closing menu reconciles pointer location and resumes delay. |
| **Tray Context Menu Isolation** | **Retained** | Opening tray context menu does not alter monitor panel / capsule visibility. |
| **Collector Disconnect / Reconnect / Reset** | **Retained** | Disconnect preserves visibility/Pin, clears stale ranking, and displays offline status; reconnect with new instance ID resets sequence and restarts warm-up. |
| **Ranking Reordering Retargeting** | **Retained** | Ranking reordering retargets smoothly from current visual positions without queuing obsolete animations. |
| **High Contrast & Reduced Motion** | **Retained** | High Contrast overrides custom palettes; Reduced Motion collapses animation durations to 0 ms immediately. |
| **Fullscreen Topmost-Yield Policy** | **Retained** | When external visible foreground window covers monitor rect, panel/capsule drops to non-topmost; restores topmost when cleared. |
| **Executable-Adjacent Atomic Config Save** | **Retained** | Managed in C# by `ConfigStore` writing adjacent JSON with atomic swap; malformed configs preserved as `.damaged` with diagnostics. |
| **Windows Service Runtime & Management** | **Replaced / Out of Scope** | Removed from current product architecture. Portable elevated companion is the sole supported host. Future service support requires a new ADR. |
| **Pure C++ Win32/Direct2D UI Rendering** | **Replaced** | Replaced by .NET 10 WPF Fluent XAML UI (ADR-0017). |
| **1 Hz Single-Cadence IPC Push** | **Replaced** | Replaced by dual-cadence 4 Hz `TrafficTick` + 1 Hz `RankingSnapshot`. |
| **Raw Win32 DwmFlush Animation Pacing** | **Replaced** | Replaced by WPF target-based animations using `RenderTransform`, `ScaleTransform`, `ColorAnimation`, and retarget-from-current-value semantics. Animation frames exist only while a transition is active; the application does not own a fixed 16 ms animation timer or perpetual 60 Hz redraw loop. |
| **Multi-Size `.ico` Runtime Generation** | **Deferred** | Runtime uses high-quality SVG/vector XAML assets; embedded multi-resolution icon resource deferred to packaging milestone. |

## User Stories

### Dock Capsule & Fluid Rate Dynamics
1. As a user, I want the dock capsule to expand its fill height smoothly with a continuous fluid ease-out animation when network bandwidth increases, so that sudden downloads feel responsive and natural.
2. As a user, I want the capsule fill height to glide down with gentle physical momentum when network traffic stops, so that intermittent packets do not cause jarring, flickering on/off flashes.
3. As a user, I want download traffic represented from the top edge downward toward the center line, so that download direction is consistently localized.
4. As a user, I want upload traffic represented from the bottom edge upward toward the center line, so that upload direction is consistently localized.
5. As a user, I want download colors to smoothly transition from cyan (low rate) to vibrant blue (medium rate) to purple (high rate / GiB saturation), so that bandwidth magnitude is intuitively legible by color.
6. As a user, I want upload colors to smoothly transition from yellow (low rate) to orange (medium rate) to fiery red (high rate / GiB saturation), so that upload bandwidth is visually distinct from download.
7. As a user, I want the gradient colors at the leading tip of the fill bar to morph smoothly frame-by-frame during rate transitions, so that color changes never step or snap abruptly.
8. As a user, I want capsule fill height and colors to animate smoothly between 4 Hz telemetry updates on high-refresh-rate displays without visible 250 ms stepping.
9. As a user, I want the capsule presentation to avoid running active render loops or invalidating visuals when telemetry targets are stable, so that background battery life is preserved.
10. As a user with "Reduce Motion" enabled in Windows 11 Accessibility settings, I want capsule rate changes to update immediately without animation duration, respecting my accessibility choice.
11. As a user, I want to configure capsule width (8–24 DIP) and capsule height (40–160 DIP), so that I can tailor the capsule footprint to my screen.
12. As a user, I want a narrow capsule to maintain an invisible inward hit target of at least 16 DIP, so that I can easily hover or click it without pixel-hunting.
13. As a user, I want logarithmic scaling across Byte, KiB, and MiB thresholds, so that both low-speed pings and high-speed multi-gigabit downloads produce discernible visual feedback.
14. As a user, I want rates at or above 1 GiB/s to smoothly saturate at the capsule center line without overflowing or clipping defects.

### Docking, Edge Snapping & Hover Transitions
15. As a user, I want the dock capsule and monitor panel to dock exclusively to the left or right work-area edge of the display where I release the pointer, so that windows never attach to the top or bottom.
16. As a multi-monitor user, I want dragging across display boundaries to smoothly transition docking to the target monitor's work area.
17. As a user, I want vertical movement constrained to the active monitor work area, so that the capsule is never pushed behind taskbars placed at any screen edge.
18. As a user with an auto-hidden taskbar on the docked edge, I want the capsule to reserve the 2-pixel reveal zone, so that it never blocks taskbar activation.
19. As a user with Pin off, I want hovering the capsule for the configured hover delay (default 350 ms) to reveal the complete monitor panel.
20. As a user with Pin off, I want clicking the capsule to reveal the monitor panel immediately without waiting for the hover delay.
21. As a user with Pin off, I want moving the pointer outside the monitor panel to collapse it back into the capsule after the configured auto-hide delay (default 500 ms).
22. As a user, I want moving from the capsule into the newly revealed panel to count as a single continuous interaction without accidental premature dismissal.
23. As a user, I want dragging the capsule beyond the system drag threshold to cancel hover expansion, so that moving the capsule is not mistaken for a reveal request.
24. As a user, I want the capsule position (monitor ID, side, vertical DIP offset) saved upon drag completion, so that my placement is restored across restarts.
25. As a user whose saved monitor is disconnected, I want the capsule to safely restore to the primary display without opening off-screen.
26. As a user, I do not want the panel to jump unexpectedly if a previously disconnected monitor reconnects.
27. As a user near the bottom of the screen, I want the monitor panel to shift upward just enough to stay fully within the work area upon reveal, without altering my stored anchor offset.

### Monitor Panel & Top N Application Ranking
28. As a user, I want the monitor panel to show product branding, aggregate download rate, and aggregate upload rate in a compact header row.
29. As a user, I want total download rate and total upload rate formatted with right-aligned tabular numerals and user-selectable units (B/s, KB/s, MB/s, Auto B/s, Auto bps), so that changing values do not cause text jitter.
30. As a user, I want the configured Top N application rows rendered directly below the header in the same card, with no secondary floating window or gap.
31. As a user, I want each ranking row to display the application icon, application name, download rate, and upload rate in matching column order.
32. As a user, I want application names to consume only available space and ellipsize smoothly, so that long process names never widen the fixed-width card (340 DIP).
33. As a user, I want application icons extracted asynchronously from executable paths and cached in memory, so that UI responsiveness is never blocked by disk I/O.
34. As a user, I want system, packaged, exited, or inaccessible processes to display a clean, stable fallback icon without breaking row geometry.
35. As a user on a short display or with a large Top N setting, I want over-tall ranking lists to scroll vertically with mouse wheel, touchpad, and keyboard navigation (Up, Down, PageUp, PageDown, Home, End).
36. As a user, I want ranking rows to smoothly reorder from their current positions when application bandwidth ranks change, without queuing obsolete snapshots.
37. As a user, I want clear, localized status rows displayed during data collection warm-up (`Collecting data…` / `正在采集数据…`) and zero-traffic idle (`No network activity` / `无网络活动`).

### Pin, Shared Menu, Topmost & Tray Control
38. As a user, I want a shared native context menu accessible by right-clicking the capsule, monitor panel, or tray icon.
39. As a user, I want the shared context menu to provide a toggleable "Pin / 固定显示" menu item.
40. As a user with Pin on, I want the complete monitor panel to remain permanently visible on the desktop even when the pointer leaves the window.
41. As a user with Pin off, I want the monitor panel to automatically collapse into the dock capsule after pointer exit.
42. As a user who disables auto-hide in settings, I want the monitor panel to stay visible and the Pin menu item to show disabled while preserving its stored preference.
43. As a user, I want auto-hide timers paused while the context menu is open, so that navigating menu items does not collapse the panel underneath.
44. As a user, I want clicking, dragging, or right-clicking on ordinary monitor panel content never to accidentally alter the Pin state.
45. As a user running full-screen games or media, I want the monitor panel and capsule to yield their topmost level to external full-screen foreground windows, restoring topmost once cleared.
46. As a user, I want the system tray icon to provide direct access to Settings, Retry Collector, Pin Toggle, and Exit.

### Settings Control Center, Theming & Localization
47. As a user, I want a dedicated, modern Fluent Settings window organized into General, Monitor, Ranking, Collector, and Diagnostics tabs.
48. As a user, I want to choose between System, Light, and Dark themes, with live reaction to Windows 11 system theme changes in System mode.
49. As a user, I want to choose between System, English, and Simplified Chinese languages, applying immediately to all open windows without requiring a restart.
50. As a user, I want `Network Flow Monitor` brand name kept untranslated across all language modes.
51. As a user, I want to configure background opacity (0–100%) and blur strength (0–100%) for the monitor panel with real-time preview.
52. As a user, I want an option to enable/disable launching Network Flow Monitor automatically upon Windows user login.
53. As a user, I want the Settings Collector tab to display live connection state, collector PID, collector instance ID, elevation status, ETW health, dropped packets, and active tracked processes.
54. As a user, I want direct "Retry Collector", "Restart Collector", and "Copy Diagnostics" actions on the Settings Collector tab.

### Portable Lifecycle, Reliability & Security
55. As a user, I want to run the application from its extracted portable directory without installing or registering a Windows Service.
56. As a user, I want double-clicking `NetworkFlowMonitor.exe` to start the complete application, prompting for UAC only when the elevated companion collector must be launched.
57. As a user, I want closing the UI to terminate the collector companion automatically via parent-process watch, leaving no lingering background processes.
58. As a user, I want the collector companion to detect an unexpected UI crash and terminate itself cleanly.
59. As a user, I want at most one UI process and one collector companion per Windows user session, preventing duplicate ETW sessions and duplicate tray/UI instances.
60. As a user whose collector crashes or disconnects, I want the UI to display a clear offline overlay (`Collector offline` / `采集器未运行`) and offer a one-click retry action in the tray.
61. As a security-conscious user, I want the WPF UI process to run with standard user privileges, while only the companion collector possesses ETW trace privileges.
62. As a user, I want the C++ Named Pipe IPC server to verify that client connections originate exclusively from my own user security identifier (SID), preventing unauthorized local process tampering.

## Implementation Decisions

### 1. Repository Structure & Binary Topology

```
nfm-project/
├── src/                                  # C++20 Core Static Libraries & Daemon
│   ├── collector/                        # ETW session, system trace provider, authorization
│   ├── ranking/                          # Sliding-window rate tracking, process aggregation, Top-N
│   ├── process/                          # PID tracker, application identity resolver
│   ├── ipc/                              # Named Pipe server, security policy, frame encoder
│   └── daemon/                           # Main entry: nfm-collector.exe (--portable default companion)
├── apps/
│   └── nfm-ui/                           # .NET 10 / C# 14 WPF Application (NetworkFlowMonitor.exe)
│       ├── Presentation/                 # Pure deterministic MonitorBehaviorModel & state machine
│       ├── Store/                        # Thread-safe atomic TelemetryStore
│       ├── Ipc/                          # Protocol V1 NamedPipeClient & frame codec
│       ├── Services/                     # AppIconCache, ConfigStore, ProcessWatchService
│       ├── ViewModels/                   # CapsuleViewModel, PanelViewModel, SettingsViewModel, TrayViewModel
│       ├── Views/                        # DockCapsuleWindow, MonitorPanelWindow, SettingsWindow
│       ├── Interop/                      # Win32 P/Invoke, HwndSourceHook, DwmAdapter, EdgeDockAdapter
│       └── Resources/                    # Fluent XAML styles, Vector templates, Localization JSON
├── tests/
│   ├── cpp/                              # C++ unit & integration tests (CMake / CTest)
│   ├── dotnet/                           # C# unit tests, Behavior model tests, Store tests (xUnit)
│   └── fixtures/                         # Cross-language golden JSON protocol test vectors
└── tools/
    ├── quality.ps1                       # Authoritative single-entry multi-language quality gate
    ├── package.ps1                       # Produces win-x64 self-contained portable distribution zip
    └── measure-release.ps1               # Verification of binary size, memory footprint & quality metrics
```

- **C++ Binary Topology**: A single C++ executable `nfm-collector.exe` acting strictly as the elevated companion daemon.
- **WPF UI Binary**: A single .NET 10 executable `NetworkFlowMonitor.exe`.
- **Portable Distribution Artifact**: Output into `dist/NetworkFlowMonitor-<version>-win-x64-portable.zip` containing self-contained `NetworkFlowMonitor.exe`, `nfm-collector.exe`, .NET runtime dependencies, native libraries, and localized strings.
- **Legacy Source Note**: Obsolete service-specific source in `src/service/` remains temporarily unreferenced during migration and will be pruned during final cleanup.

### 2. Architecture Gate Invariants (Non-Negotiable Constraints)

1. **Privileged Pipe is an Authenticated Security Boundary**:
   The Named Pipe created by `nfm-collector.exe` must enforce an explicit security descriptor allowing `GENERIC_READ | GENERIC_WRITE` strictly to the launching interactive user SID and `LocalSystem`. No anonymous or `Everyone` access is permitted.
2. **IPC Thread Never Directly Mutates WPF Presentation State**:
   Incoming IPC stream bytes are parsed on an asynchronous background I/O thread directly into an immutable DTO and stored atomically in `TelemetryStore`. The IPC thread never calls `Dispatcher.Invoke` on visual objects, modifies `ObservableCollection`, or triggers UI animations.
3. **Telemetry Snapshots Describe State, Not Animation Events**:
   Snapshots are passive state declarations ($Rate_{dl}, Rate_{ul}, Apps$). ViewModels observe state updates in `TelemetryStore` via UI scheduler polling/notifications and drive continuous presentation transforms independently. Unprocessed older ticks are coalesced to the latest state; obsolete historical frames are never queued.
4. **Continuous Telemetry Animation Must Not Depend on Layout-Size Animation**:
   Continuous rate animations are strictly forbidden from modifying `FrameworkElement.Width`, `FrameworkElement.Height`, `RowDefinition.Height`, `ColumnDefinition.Width`, or `Margin`. The capsule geometry layout size is invariant. All fill and gradient transitions must execute via `RenderTransform` (`ScaleTransform`), `Geometry.Clip`, and `ColorAnimation`.
5. **Portable Companion Lifetime Is the Sole Runtime Policy**:
   `nfm-collector.exe` exists exclusively for the duration of its owning UI session:
   - **Authoritative Process Watch**: The collector monitors the UI parent process handle via `WaitForSingleObject`. When the UI process handle is signaled (UI exited or crashed), the collector immediately stops ETW, closes IPC, and terminates cleanly (no 3-second delay).
   - **Pipe Disconnect Grace Period**: If the Named Pipe unexpectedly disconnects while the UI process handle is still alive, a bounded 3-second reconnect grace period begins; if reconnection fails upon expiration, the collector terminates cleanly.
6. **Protocol V1 Has Fixed 12-Byte Framing, Limits, and Cross-Language Fixtures**:
   All IPC packets have a fixed 12-byte binary header (`Magic: "NFM1"`, `Version: 1`, `Type: uint16`, `PayloadLength: uint32`). Maximum payload length is strictly capped at 256 KiB. Cross-language JSON test vectors in `tests/fixtures/` are validated by both C++ and C# test suites to guarantee schema alignment before any allocation or parsing.
7. **Existing C++ UI is Retained Until E2E Tracer Bullet Passes**:
   The existing C++ Win32 UI code and its test suite remain fully functional in the repository until the Automated Hybrid E2E tracer bullet has passed and the new C++ → Pipe → C# → Presentation → WPF path has been proven on the target environment. Legacy UI removal itself remains deferred until the final target-host acceptance gate in Milestone 5.
8. **Single Interactive Instance Invariant**:
   Within one Windows interactive user session, at most 1 $\times$ `NetworkFlowMonitor.exe` and 1 $\times$ `nfm-collector.exe` may run. Secondary UI launches detect the existing instance via session-scoped mutex, activate/restore the existing window, and exit immediately without spawning a second collector.

### 3. Deterministic Behavior Seam (`NetworkFlowMonitor.Presentation`)

To ensure rock-solid interaction without fragmented, duplicated state machines across XAML code-behinds, all high-level docking, hover, dismiss, drag, and pin behaviors are unified under a deterministic C# presentation seam:

```
Raw Win32 / WPF Input Events
            │
            ▼
    WPF / HWND Adapter
            │
            ▼ (Normalize into explicit struct)
      MonitorEvent
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                 MonitorBehaviorModel                         │
│                                                             │
│  - Pure C# state reducer (Platform-Independent)             │
│  - No references to PresentationFramework / Visual types    │
│  - No references to HwndSource                              │
│  - No calls to Dispatcher / DispatcherTimer / System.Timer  │
│  - No calls to DateTime.Now / Stopwatch.GetTimestamp        │
│  - No direct file/registry configuration I/O                │
│  - No calls to EnumDisplayMonitors / ShowWindow             │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼ (Deterministic Result)
                         Transition
                              ├── Next State (MonitorState)
                              ├── PresentationTargets (Visibility, Bounds, Viewport)
                              ├── AnimationTargets (Kind, Easing, Duration)
                              └── EffectIntents (PersistAnchor, PersistPin, LogDiagnostic)
                                      │
                                      ▼
                        WPF / Win32 Adapters Execute Side Effects
```

- **Scope of Seam**: Covers Capsule hover, hover delay deadline, click reveal, pointer leave, dismiss delay deadline, drag start, drag threshold crossing, drag cancel, drag completion, cross-monitor snapping, left/right tie-breaking, Pin state, global auto-hide toggle, context menu open/close reconciliation, collector disconnect/reconnect/instance reset, monitor removal fallback, DPI change, reduced motion, high contrast, and fullscreen topmost-yield.

### 4. Protocol V1 Specification & Security Contract

- **Fixed 12-Byte Wire Header (Little-Endian)**:
  ```
  Offset   Size   Type     Field          Description
  0        4      byte[4]  Magic          Fixed ASCII "NFM1" (0x4E, 0x46, 0x4D, 0x31)
  4        2      uint16   Version        Protocol version = 1
  6        2      uint16   MessageType    Message type identifier
  8        4      uint32   PayloadLength  Length of JSON payload in bytes (excludes 12-byte header)
  ```
- **Payload Limits**: Max `PayloadLength` = 262,144 bytes (256 KiB). Packets exceeding this or with invalid magic/version are rejected immediately before JSON allocation. Payload encoding is UTF-8 JSON.
- **Client Identity & Security Authorization**:
  - `client_pid` and `client_name` in `Hello` are protocol metadata and diagnostics only, **never authorization proof**.
  - Security authorization relies on OS-level Named Pipe security descriptors, client token SIDs, and `GetNamedPipeClientProcessId`. If the client's claimed `client_pid` in JSON contradicts the OS-verified process ID, the connection is rejected.

#### Message Types & Payload Schemas

1. **`0x0001` - `Hello` (UI $\to$ Collector)**:
   ```json
   {
     "client_pid": 12344,
     "protocol_version": 1,
     "client_name": "NetworkFlowMonitor.Ui"
   }
   ```
2. **`0x0002` - `HelloAck` (Collector $\to$ UI)**:
   ```json
   {
     "protocol_version": 1,
     "collector_instance_id": "cinst_9f8a3c2e1b",
     "collector_pid": 4588,
     "capabilities": ["etw_live_counters", "traffic_tick_4hz", "ranking_snapshot_1hz"]
   }
   ```
   *Collector Instance Reset Rule*: When `collector_instance_id` changes on reconnect, the UI resets sequence assumptions, clears stale `RankingSnapshot`, restarts ranking warm-up, and discards all cross-instance animation history.
3. **`0x0003` - `TrafficTick` (Collector $\to$ UI, 4 Hz / 250 ms)**:
   ```json
   {
     "seq": 4096,
     "monotonic_ms": 83928471,
     "download_bytes_per_sec": 5242880,
     "upload_bytes_per_sec": 1048576
   }
   ```
   *Note*: `monotonic_ms` is instance-local monotonic elapsed time and is never compared across different collector instances.
4. **`0x0004` - `RankingSnapshot` (Collector $\to$ UI, 1 Hz / 1000 ms)**:
   ```json
   {
     "seq": 1024,
     "monotonic_ms": 83928471,
     "status": "Ready",
     "total_rate": {
       "download_bytes_per_sec": 5242880,
       "upload_bytes_per_sec": 1048576
     },
     "applications": [
       {
         "id": "app_chrome",
         "executable_path": "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
         "display_name": "Google Chrome",
         "icon_key": "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
         "download_bytes_per_sec": 4194304,
         "upload_bytes_per_sec": 838860
       }
     ]
   }
   ```
5. **`0x0005` - `CollectorStatus` (Collector $\to$ UI, Event/Diagnostic)**:
   ```json
   {
     "monotonic_ms": 83928471,
     "etw_session_active": true,
     "dropped_packets": 0,
     "active_pids": 42
   }
   ```
6. **`0x0006` - `ShutdownRequest` (UI $\to$ Collector)**:
   ```json
   {
     "reason": "ui_exit"
   }
   ```
   *Note*: Used strictly as an optional fast-path graceful shutdown notification preceding process termination. Generic command execution or remote execution brokers are completely prohibited.

### 5. Mathematical Rate-to-Fill & Gradient Color Contract

- **Deterministic Rate-to-Fill Mapping Pure Function**:
  $$F(R) = \begin{cases} 0.0 & \text{if } R \le 0 \\ \operatorname{clamp}\left(\frac{\log_2(R)}{30.0}, 0.0, 1.0\right) & \text{if } R > 0 \end{cases}$$
  - $1\text{ B/s} \to 0.000$
  - $1\text{ KiB/s } (1024) \to 0.333$
  - $1\text{ MiB/s } (1048576) \to 0.667$
  - $\ge 1\text{ GiB/s } (1073741824) \to 1.000$ (smooth saturation)
- **Fixed Gradient Topology**:
  - Stop collection topology remains fixed (no adding/removing stops at runtime across thresholds).
  - Download Stops: Stop 0 (`#19D8E6` Cyan at edge), Stop 1 (`#2878D0` Blue at mid), Stop 2 (leading tip animated to `#8146B8` Purple).
  - Upload Stops: Stop 0 (`#F4D03F` Yellow at edge), Stop 1 (`#F39C32` Orange at mid), Stop 2 (leading tip animated to `#D93636` Red).
- **Interruption & Retargeting Contract**:
  - When new telemetry arrives mid-animation, the transition retargets smoothly from the current rendered presentation value ($v_{\text{current}} \to v_{\text{target}}$).
  - If $v_{\text{target}} == v_{\text{current\_target}}$, the in-flight animation continues without redundant restart.

### 6. Window Surface & Backdrop Technical Spike (Prerequisite)

Before finalizing visual implementation, an isolated WPF Technical Spike (`WpfSurfaceSpike`) must validate the following on a live Windows 11 host:

1. **Monitor Panel Surface**:
   - `WindowStyle=None` and `AllowsTransparency=False` with `DwmSetWindowAttribute` (`DWMWA_SYSTEMBACKDROP_TYPE` Mica/Acrylic and `DWMWA_WINDOW_CORNER_PREFERENCE` Round).
   - Validation that background transparency/opacity does not bleed into or reduce text/icon readability.
   - Behavior of blur-strength adjustment (if Windows 11 DWM backdrop does not support continuous Gaussian blur strength slider, spike decides between native DWM backdrop vs DirectComposition HostBackdrop interop vs retiring the slider in favor of standard system material).
2. **Pill-Shaped Dock Capsule**:
   - 10 DIP width visible pill with $\ge 16\text{ DIP}$ invisible inward hit target.
   - Confirmation that edge docking does not expose opaque black rectangular gutters or trigger layered-window GDI software fallback.
   - Confirmation that full pill curvature matches design expectations without clipping defects.

## Testing Decisions

### What Makes a Good Test

1. **Deterministic Seam Isolation**: All high-level window logic and behavior state transitions are tested in pure C# tests without instantiating HWNDs, WPF `Window`, Dispatchers, or sleeping the thread.
2. **Cross-Language Fixture Consistency**: Golden JSON fixtures in `tests/fixtures/` are validated bidirectionally by both C++ CTest and C# xUnit test suites to guarantee that byte-level framing, limits, and payload serialization are identical.
3. **Pure Function Math Coverage**: Mathematical rate-to-fill, color interpolation, and work-area clipping algorithms are tested across negative, zero, boundary, and overflow values.

### Test Suites

| Test Suite | Framework | Target of Test | Prior Art in Codebase |
| :--- | :--- | :--- | :--- |
| **C++ Core Engine Tests** | C++20 / CTest | ETW decoder, process tracker, sliding-window rate tracking, 12-byte IPC framing, parent-watch lifecycle. | `ranking_engine_tests.cpp`, `ipc_frame_codec_tests.cpp` |
| **Protocol Fixture Tests** | C++ & C# xUnit | 12-byte header, magic validation, 256 KiB cap, `Hello`, `HelloAck`, `TrafficTick`, `RankingSnapshot`, `CollectorStatus`, `ShutdownRequest`. | `ipc_snapshot_codec_tests.cpp` |
| **MonitorBehaviorModel Tests** | C# / xUnit | Pure state transitions: hover, click, drag threshold, drag cancel, Pin, auto-hide, fullscreen yield, disconnect/reconnect. | `monitor_panel_behavior_tests.cpp` |
| **Animation & Math Tests** | C# / xUnit | Pure rate-to-fill log mapping, color channel interpolation, attack/decay curves, retargeting from in-flight values. | `ui_appearance_tests.cpp` |
| **Automated Hybrid E2E Test** | C++ Test Daemon + C# Client | Real Named Pipe launch, 12-byte framing, 4 Hz stream ingestion into `TelemetryStore`, instance reset, clean exit (No UAC/ETW required). | `ipc_server_integration_tests.cpp` |
| **Target-Host Acceptance Test** | Live Win11 Runner | Elevated portable collector, real ETW packet stream, WPF dock capsule visually fluid animation without 250 ms stepping, DWM backdrop, multi-DPI snapping, clean parent-watch termination. | `portable-acceptance.ps1` |

## Out of Scope

1. **Windows Services & SCM Management**: SCM installation, service startup configuration, LocalSystem service daemons, and service control UI are strictly out of scope.
2. **Permanent Elevation Brokers & Scheduled Tasks**: Scheduled task backdoors, persistent helper services, and installer-installed privilege brokers are rejected; cold starts accept standard UAC elevation.
3. **In-Process C++ DLL P/Invoke**: Merging elevated ETW kernel tracing into the WPF UI process, forcing the UI to run as Administrator, is strictly prohibited.
4. **Heavyweight Web UI Frameworks**: Electron, CEF, Tauri, and WebView2 are rejected due to memory and startup constraints.
5. **Kernel-Mode Drivers & WFP Packet Callouts**: Traffic accounting relies strictly on user-mode ETW event tracing.
6. **Third-Party Monolithic UI Kits as Hard Dependencies**: `Wpf.Ui` or `ModernWpf` are optional styling references; the core UI runs on native .NET 10 WPF XAML without vendor lock-in.
7. **Multiple Clients or Background Daemons**: Multiple UI clients sharing one collector or headless background collection without a UI session are excluded.

## Further Notes

### Phased Migration Milestones

- **Milestone 1**: Portable Protocol V1 & Hybrid Tracer Bullet (12-byte framing, `Hello`/`HelloAck` handshake, 4 Hz `TrafficTick`, secure pipe, parent-watch foundation, protocol fixtures, C# `TelemetryStore` ingestion, minimal WPF telemetry window).
- **Milestone 2**: Ranking Pipeline & Collector Session State (1 Hz `RankingSnapshot` decoding, `CollectorStatus`, Top-N presentation, instance ID reset, offline overlay, warm-up/idle states).
- **Milestone 3**: `WpfSurfaceSpike` Execution & Deterministic Window Behavior (Dual-window topology, `MonitorBehaviorModel` pure seam, HWND docking/DPI/Topmost interop).
- **Milestone 4**: Production Dock Capsule & Monitor Panel (Fluid `RenderTransform` + `ColorAnimation`, dynamic gradient morphing, virtualized ranking scroll, async icon cache, shared context menu).
- **Milestone 5**: Settings Control Center, Portable Lifecycle, Single Instance & Packaging (Settings view, theme/language sync, atomic config store, tray, single-instance enforcement, `tools/quality.ps1` cross-language gate, portable ZIP packaging, target-host acceptance, legacy C++ UI removal).
