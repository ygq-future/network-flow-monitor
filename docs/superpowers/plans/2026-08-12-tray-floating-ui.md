# Tray and Floating UI Implementation Plan

> **For agentic workers:** Execute this plan inline in this session, one gated task at a time. Keep each task independently testable and run the repository quality gate before committing.

**Goal:** Add the normal-user Win32 UI that connects to the existing IPC client, computes the configured real-time ranking, and presents a tray icon, edge-aware floating meter, and hover ranking popup.

**Architecture:** Keep all window, tray, Direct2D and DirectWrite work on one UI thread. Isolate DIP geometry and interaction transitions as platform-neutral value logic. `UiController` owns the IPC client, ranking engine, configuration snapshot and window objects; IPC worker callbacks post immutable snapshots to the UI message queue. A small portable-process launcher starts the existing executable with `runas --collector` and owns its process handle; service mode only connects to the service endpoint.

**Tech Stack:** C++20, native Win32, Shell notification API, Direct2D, DirectWrite, existing `IpcClient`, `RankingEngine`, `AppConfig`, CMake, GoogleTest-free executable tests following repository conventions.

## Global Constraints

- Target Windows 11 24H2 x64 first; current host evidence is Windows 11 25H2 only.
- UI remains normal privilege and never calls ETW, SCM, or service callbacks directly.
- Use C++20, RAII, explicit Win32 handle ownership, `/W4 /WX /permissive- /sdl`, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, unit/integration tests and Release builds.
- Use existing `AppConfig`, `RankingEngine`, `IpcClient` and `CollectorRuntime` boundaries; do not add a second UI refresh setting or a new configuration key.
- UI redraw is dirty-driven: snapshots and visual/interaction/layout changes invalidate affected windows; no idle high-frequency timer or animation loop.
- Direct2D/DirectWrite resources and HWNDs are created and destroyed on the UI thread; IPC callbacks never touch HWNDs.
- Do not implement taskbar embedding, Explorer injection, WebView/Qt/WinUI, kernel drivers, long-term history or privileged UI.

---

### Task 1: Pure DPI geometry and interaction state

**Files:**

- Create: `include/network_flow_monitor/ui/dpi_layout.h`
- Create: `src/ui/dpi_layout.cpp`
- Create: `include/network_flow_monitor/ui/interaction_state.h`
- Create: `src/ui/interaction_state.cpp`
- Create: `tests/ui_layout_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `ui::PixelRect { std::int32_t left, top, right, bottom; }` and `ui::PixelSize { width, height; }` are value types with `Width()`, `Height()`, and equality.
- `ui::DpiScale(int dpi)`, `ui::DipToPixels(double dip, int dpi)`, and `ui::PixelsToDip(std::int32_t pixels, int dpi)` use 96 DPI as the reference and saturate to `int32_t` bounds.
- `ui::ClampRectToWorkArea(PixelRect candidate, PixelRect work_area)` keeps the entire candidate visible, reducing its origin only when its dimensions fit; oversized candidates are reduced to the work area.
- `ui::SnapRectToWorkArea(PixelRect rect, PixelRect work_area, int distance_px)` snaps only edges within the configured distance and never moves a window outside the work area.
- `ui::PopupPlacementResult ChoosePopupPlacement(PixelRect anchor, PixelSize popup_size, PixelRect work_area)` returns a complete visible rectangle and deterministic `Above/Below/Left/Right` choice.
- `ui::InteractionStateMachine` accepts `MouseTarget`, `InteractionEvent`, `std::chrono::steady_clock::time_point`, and the configured hover/dismiss/auto-hide delays; it returns state transitions and dirty flags without Win32 calls.

- [x] **Step 1: Add failing geometry/state tests**

Cover 96/144/192 DPI conversion, negative multi-monitor coordinates, clamping, edge snapping, all popup directions, oversized popups, hover delay, anchor-to-popup movement, dismiss delay, auto-hide delay, reveal-strip recovery, and dirty-flag coalescing.

- [x] **Step 2: Run the focused test and verify the expected missing-header failure**

Run `cmake --build --preset windows-x64-debug --target nfm_ui_layout_tests`; expected result is a missing UI header until the implementation exists.

- [x] **Step 3: Implement the pure value logic**

Use integer pixel rectangles at the Win32 boundary, `std::chrono` for deadlines, deterministic candidate ordering, and no HWND or global timer dependency.

- [x] **Step 4: Run focused tests and the Fast gate**

Run `build/windows-x64-debug/tests/nfm_ui_layout_tests.exe`, then `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast`; all existing tests and the new UI test must pass.

- [x] **Step 5: Commit**

Commit `feat: add ui geometry and interaction model` after `git diff --check` and the Fast gate pass.

### Task 2: Direct2D resources and tray controller

**Files:**

- Create: `include/network_flow_monitor/ui/render_resources.h`
- Create: `src/ui/render_resources.cpp`
- Create: `include/network_flow_monitor/ui/tray_controller.h`
- Create: `src/ui/tray_controller.cpp`
- Create: `tests/ui_tray_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `ui::RenderResources` owns `ID2D1Factory`, `IDWriteFactory`, shared text formats and per-window render targets through WRL smart pointers; `BeginDraw`/`EndDraw` classify `D2DERR_RECREATE_TARGET` without throwing across the window procedure.
- `ui::TrayController` accepts a callback interface, creates/deletes one `NOTIFYICONDATAW`, maps left-click/right-click/hover notification messages to callbacks, and never performs blocking work in the Shell callback.
- `ui::TrayApi` is a fakeable seam for `Shell_NotifyIconW`, menu creation and cursor position so add/delete/menu behavior is unit-testable.

- [x] **Step 1: Register tray/resource tests and verify the red phase**

Add fake API tests for add/delete idempotence, callback routing, menu dismissal, unsupported Shell failures, and render-target recreation classification.

- [x] **Step 2: Implement RAII render resources and tray adapter**

Initialize COM on the UI thread, create Direct2D/DirectWrite factories once, use a stable callback message, and remove the icon before UI shutdown.

- [x] **Step 3: Run focused tests, format and Fast**

The focused tray test and the full non-admin Fast gate must pass; no source-level lint suppression may hide a resource ownership issue.

- [x] **Step 4: Commit**

Commit `feat: add d2d resources and tray controller` after the required checks.

### Task 3: Floating meter and ranking popup windows

**Files:**

- Create: `include/network_flow_monitor/ui/floating_window.h`
- Create: `src/ui/floating_window.cpp`
- Create: `include/network_flow_monitor/ui/ranking_popup.h`
- Create: `src/ui/ranking_popup.cpp`
- Create: `tests/ui_window_logic_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `ui::FloatingWindow` creates one borderless per-monitor-DPI-aware tool window, exposes `Show`, `Hide`, `SetSnapshot`, `SetInteractionState`, `Reposition`, and `Destroy`, and owns no IPC or ranking engine.
- `ui::RankingPopup` creates one no-activate tool popup, measures rows using DirectWrite, exposes `ShowAt(anchor, work_area)`, `Hide`, `SetSnapshot`, `Reposition`, and `Destroy`, and keeps the anchor/popup hit regions available to the controller.
- Both windows accept a `UiWindowObserver` for state changes and call `InvalidateRect` only after a changed snapshot, layout, or interaction state.

- [x] **Step 1: Add fake-window tests for placement and dirty redraw**

Verify `WM_DPICHANGED` chooses the suggested rectangle, work-area changes re-clamp both windows, popup does not activate, and repeated identical snapshots do not schedule redundant invalidation.

- [x] **Step 2: Implement the window procedures and painting**

Use `WS_EX_TOOLWINDOW`, `WM_MOUSEACTIVATE`, `WM_NCHITTEST`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`, `WM_PAINT`, and `WM_DESTROY`; draw minimal background/text/rows with Direct2D and DirectWrite.

- [x] **Step 3: Run focused tests and Fast**

Run the new window logic executable and the Fast quality gate; confirm the UI target has no SCM or ETW entry-point dependency.

- [x] **Step 4: Commit**

Commit `feat: add floating meter and ranking popup` after the checks pass.

### Task 4: UI controller, IPC/ranking bridge and portable process lifetime

**Files:**

- Create: `include/network_flow_monitor/ui/ui_controller.h`
- Create: `src/ui/ui_controller.cpp`
- Create: `include/network_flow_monitor/ui/collector_process.h`
- Create: `src/ui/collector_process.cpp`
- Create: `tests/ui_controller_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `ui::CollectorProcess` owns a `HANDLE` to the UAC-launched same executable, exposes `StartPortable`, `Stop`, `running`, and a fakeable `ShellExecuteApi`; stop first requests the process shutdown contract and uses a bounded terminate fallback only after the handle reports it is still running.
- `ui::UiController` implements `IpcClientObserver`, owns `IpcClient`, `WindowsPipeTransportFactory`, `RankingEngine`, `AppConfig`, tray/floating/popup objects and the UI message queue; observer callbacks copy snapshots into a bounded pending slot and `PostMessageW` a private update message.
- UI startup chooses the configured endpoint principal. Portable mode starts the collector process before IPC connection; service mode only connects and reports SCM state as disconnected/unknown until the settings milestone adds the real status view.

- [x] **Step 1: Add failing controller/process tests**

Test snapshot coalescing, state-to-overlay mapping, ranking option updates, portable launch failure cleanup, process cleanup, and no stale data after collector instance changes.

- [x] **Step 2: Implement controller and process seams**

Keep all observer callbacks `noexcept`, post immutable data to the UI thread, and use the existing `IpcClient` reconnect policy without introducing a second timer.

- [x] **Step 3: Run focused tests and Fast**

The controller/process tests and all existing non-admin tests must pass; no UAC or real service mutation is performed by automated tests.

- [x] **Step 4: Commit**

Commit `feat: bridge ui to ipc and ranking` after the checks pass.

### Task 5: Replace bootstrap UI entry and add manual integration harness

**Files:**

- Modify: `src/app/main.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/ui_integration_smoke_tests.cpp`
- Modify: `docs/architecture.md`
- Modify: `docs/configuration.md`

- [x] **Step 1: Add a non-blocking UI smoke test**

Launch the Release executable with an internal test switch that creates and destroys the UI message loop without a message box; verify clean exit, tray cleanup and no collector construction in UI mode.

- [x] **Step 2: Wire `wWinMain` to `UiController::Run`**

Keep existing runtime-mode parsing; default mode is the only branch that creates UI windows. All privileged modes remain the previously implemented collector/service branches.

- [ ] **Step 3: Run manual 25H2 smoke checks**

Verify tray left/right click, floating drag/snap/hide, hover ranking, disconnect overlay, monitor/DPI/work-area changes, and Release dependency output. Record only observed behavior.

- [x] **Step 4: Run Full and commit implementation checkpoint**

The implementation checkpoint is committed only after Debug/Release tests, strict builds, both Clang analyses, `/analyze`, and Release x64 pass. Step 3 remains open until the visible 25H2 interaction checks are observed.

### Task 6: Milestone 6 audit and handoff to settings

**Files:**

- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/planning/findings.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`

- [ ] **Step 1: Map UI requirements to test and manual evidence**
- [ ] **Step 2: Record 24H2, mixed-DPI and privilege limitations explicitly**
- [ ] **Step 3: Run Full and commit `feat: complete tray and floating ui`**
- [ ] **Step 4: Mark exactly one next milestone in progress: settings and service management UI**
