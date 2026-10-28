# Architecture

## Overview

Network Flow Monitor is a lightweight Windows 11 per-application traffic monitor built on a strict two-process native/web architecture:

```text
NetworkFlowMonitor.exe (Standard User)
  ├── C++20 Win32 Product Host
  │     ├── Tray shell & single-instance mutex
  │     ├── Deterministic DockCapsuleCoordinator (TrayOnly / CapsuleVisible / PanelVisible)
  │     ├── Pre-warmed independent Settings window
  │     ├── Authoritative AppConfig & rotating diagnostic logger
  │     ├── CollectorCompanion lifecycle & on-demand UAC elevation
  │     └── Versioned allow-listed Native/Web Bridge
  └── Packaged React + TypeScript + Tailwind CSS UI (WebView2 https://nfm.local/)
          │
          │ Owner-scoped local Named Pipe (Protocol V1, 12-byte header + UTF-8 JSON)
          ▼
nfm-collector.exe (Portable Companion)
  ├── Real-time Windows ETW session (SystemTraceProvider TCP/UDP IPv4/IPv6 + Process events)
  ├── Generation-tracked process identity & PID reuse protection
  ├── Per-application byte accumulation & sliding-window Top N ranking engine
  └── Protocol V1 IPC server (4 Hz TrafficTick, 1 Hz RankingSnapshot & CollectorStatus)
```

- **Product Host (`NetworkFlowMonitor.exe`)**: Authority for Windows windowing, DPI/multi-monitor coordinates, DWM transparency, configuration validation/persistence, Collector process lifecycle, and Native/Web bridge validation.
- **Frontend (`apps/nfm-ui`)**: Authority for visual presentation, rate curve mapping, CSS transitions, FLIP row animations, and user interaction triggers. It has no direct access to Win32, named pipes, filesystem, shell, or elevation APIs.
- **Portable Collector (`nfm-collector.exe`)**: Authority for real-time packet accounting, process attribution, and sliding-window rate calculation. Terminates automatically via parent-process watch or named stop event (`Local\NetworkFlowMonitor.Stop.<host-pid>.<nonce>`) when the Host exits.

---

## Core Domain Glossary

| Term | Chinese | Definition |
| :--- | :--- | :--- |
| **Product Host** | 产品宿主 | Standard-privilege Win32 process hosting windows, tray, configuration, and Collector lifecycle. |
| **Portable Collector** | 便携采集器 | Companion C++ process capturing ETW events and exiting when the Host terminates. |
| **Collection Session** | 采集会话 | A single run epoch of the Collector, uniquely identified by `collector_instance_id`. |
| **Monitor Panel** | 监控面板 | 340 DIP desktop surface displaying total download/upload rates and Top N active applications. |
| **Dock Capsule** | 贴边胶囊 | Compact edge-docked indicator showing logarithmic bidirectional traffic gradients; expands to Monitor Panel on hover or click. |
| **Dock** | 贴边 | Snapping the floating surface to the left or right edge of the release monitor's work area (`rcWork`). |
| **Pin** | 固定显示 | Persistent user preference preventing the Monitor Panel from collapsing back into the Dock Capsule on pointer leave. |
| **Tray Only** | 仅托盘 | Surface state where both Capsule and Panel are hidden while tray icon and background collection remain active. |
| **Traffic Tick** | 流量节拍 | 4 Hz (250 ms) total download and upload rate broadcast from Collector to Host/UI. |
| **Ranking Snapshot** | 排行快照 | 1 Hz (1000 ms) Top N per-application rate and metadata snapshot. |
| **Session Reset** | 会话重置 | State reset triggered when `collector_instance_id` changes, clearing stale sequence assumptions and visual history. |

---

## Key Architectural Decisions

### 1. Real-Time ETW Session & Hybrid Event Decoding
- **Dedicated Multiplexed System Logger**: Instead of occupying the legacy single-instance `KERNEL_LOGGER_NAME` (`NT Kernel Logger`), the Collector starts a dedicated real-time session named `NetworkFlowMonitor.SystemTrace` (`{1473ea71-a063-4396-8ea1-e3d1ede80af7}`) using `EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_REAL_TIME_MODE` with only `EVENT_TRACE_FLAG_NETWORK_TCPIP | EVENT_TRACE_FLAG_PROCESS`.
- **Hybrid Hot/Cold Path Decoding**:
  - **High-frequency network events (TCP/UDP v4/v6 Send, Receive, Retransmit)**: Decoded via zero-allocation fixed-prefix bounds checks directly against the documented MOF v2 payload layout (`PID` and `size`), avoiding costly per-packet `TdhGetProperty` scans.
  - **Low-frequency process lifecycle events (Start, End, DCStart)**: Decoded via TDH (`UniqueProcessKey`, `ProcessId`, `ImageFileName` across versions 2/3/4) to safely handle pointer-sized fields and variable-length strings without brittle manual offsets.
  - **Privacy by Design**: Remote IP addresses, ports, packet payloads, and command-line arguments are neither decoded nor logged.

### 2. Event-Timeline Process Identity & PID Reuse Protection
- Windows aggressively reuses PIDs. Attributing network events by bare PID or executable filename causes false merges across process generations.
- `ProcessTracker` assigns a monotonically increasing generation to every process start (`ProcessInstanceId{pid, generation}`), cross-checked via the kernel `UniqueProcessKey`, and retains recently exited instances in a bounded cache so late-arriving pre-exit ETW network events resolve to the correct historical instance.
- Process metadata is queried via `PROCESS_QUERY_LIMITED_INFORMATION` (`GetProcessTimes`, `GetApplicationUserModelId`, `GetPackageFamilyName`, `QueryFullProcessImageNameW`), rejecting probe results whose creation time postdates the observed start event. Application identity prioritizes `AUMID > PackageFamilyName > NormalizedFullImagePath > Instance-Scoped Unresolved ID`.

### 3. On-Demand UAC Elevation, One-Time ETW Authorization & Split-Token IPC
- **Standard-User Host**: `NetworkFlowMonitor.exe` always runs as a standard user so `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` login startup never triggers UAC prompts.
- **One-Time Persistent ETW Authorization**: `CollectorCompanion` first spawns `nfm-collector.exe` as a standard user. If it exits with `ERROR_ACCESS_DENIED (5)` or `ERROR_PRIVILEGE_NOT_HELD (1314)`, Host falls back to `ShellExecuteExW(..., L"runas", ...)`. When elevated, `nfm-collector.exe` runs `AuthorizeCollectorForCurrentUser()`—adding the user to `Performance Log Users`, granting `SeSystemProfilePrivilege`, and configuring DACLs on the ETW session and `SystemTraceProvider` GUIDs—so subsequent launches run without elevation.
- **Cross-UAC Split-Token Named Pipe Security**: A standard-user Host and an elevated Collector under the same Windows account have different `Logon SID`s due to UAC split tokens, but share the exact same account `TokenUser` SID. The Collector's owner-scoped Named Pipe (`Protocol V1`) sets `authorized_sid` to the current account's `TokenUser` SID and verifies both `user_sid` and `group_sids` upon client impersonation, rejecting other accounts and all remote connections (`FILE_PIPE_REJECT_REMOTE_CLIENTS`).

### 4. Protocol V1 Framing & Vendored `yyjson`
- **Framing**: 12-byte little-endian header (`magic = "NFM1" (0x314D464E)`, `uint16 version = 1`, `uint16 message_type`, `uint32 payload_length` capped at 256 KiB) followed by strict RFC 8259 UTF-8 JSON (`Hello`, `HelloAck`, `TrafficTick`, `RankingSnapshot`, `CollectorStatus`, `ShutdownRequest`).
- **Static JSON Dependency**: `yyjson 0.12.0` is vendored in `third_party/yyjson/` and compiled statically into the native binaries for deterministic, zero-DLL config persistence, IPC framing, and Native/Web bridge serialization.

### 5. Surface Topology, Morphing Stage & Window Coordination
- **Dual-Window Architecture**:
  - **Floating Surface Window**: Constant 340 DIP width transparent DWM window (`DefaultBackgroundColor = 0x00000000`) hosting both `<DockCapsule />` and `<BasicMonitorPanel />` in a shared morphing stage. In capsule mode, `Win32Window` restricts mouse hit-testing (`logical_hit_rect_`) to the capsule bounds (minimum 16 DIP inward hit target) and applies `WS_EX_TRANSPARENT | WS_EX_LAYERED` OS click-through to the rest of the canvas.
  - **Pre-Warmed & Low-Memory Suspended Settings Window**: Independent `Win32Window` + `WebView2Controller` instance pre-warmed at startup, suspended via `ICoreWebView2_3::TrySuspend` with `COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW` while hidden, and resumed via `Resume` + `put_IsVisible` for instant zero-flicker opening.
- **Deterministic `DockCapsuleCoordinator`**:
  - Coordinates `TrayOnly`, `CapsuleVisible`, and `PanelVisible` state transitions.
  - Crosses Windows drag thresholds (`SM_CXDRAG` / `SM_CYDRAG`) into a native `WM_SYSCOMMAND` (`SC_MOVE | HTCAPTION`) move loop, snapping on pointer release to the left or right edge of the target monitor's work area (`rcWork`) while reserving a 4 DIP margin for auto-hide taskbars.
  - Supports configurable hover reveal (default 350 ms), pointer-leave dismiss (default 250 ms), edge auto-hide (4 px peek strip), and fullscreen application detection (`ignore_hover_when_fullscreen` / `hide_capsule_when_fullscreen`).
  - **Presentation Self-Healing**: coordinator state is applied only on transitions, so `Win32Window` exposes live presentation facts (`IsWindowVisible`, `IsIconic`, `WS_EX_TOPMOST`, `GetWindowRect`) and `set_bounds` / `set_topmost` reconcile against the actual window instead of request caches. The Host's 1 s watchdog re-applies the intended surface whenever the HWND was hidden, minimized, moved or lost topmost (`WM_SETTINGCHANGE` `SPI_SETWORKAREA` also re-resolves monitor geometry), keeping the docked capsule recoverable without user interaction.

### 6. Authoritative Configuration & Cross-Path Startup Hygiene
- **Single Source of Truth (`AppConfig`)**: Defaults, units, legal ranges, and schema migrations are defined once in the C++ `config` module and persisted atomically to `<app_root>/data/config.json`. Host broadcasts `configChanged` with both current values and schema bounds (`min`, `max`, `step`, `unit`, `enums`) to the React UI.
- **Cross-Path Login Startup Governance (`StartupRegistration`)**: Toggling `startup.ui_at_login` scans both `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` and the user `Startup` folder, purging any stale entries matching `NetworkFlowMonitor` or `NetworkFlowMonitor.exe` across old disk locations before writing the quoted path of the active executable.
