# Dual Collection Modes and Service Lifecycle Implementation Plan

> **For agentic workers:** Execute this plan inline in this session, one gated task at a time. Keep every completed task independently testable and update `docs/planning/progress.md` before its commit.

**Goal:** Run the existing ETW collector and subscription-aware IPC publisher from either a temporary elevated collector or a Windows service, while keeping all UI work in the normal user process.

**Architecture:** A service-neutral `CollectorHost` owns one ETW session, one owner-scoped `IpcServer`, and one `SnapshotPublisher` loop. A small `EtwSnapshotProvider` adapts `WindowsEtwSession` to the publisher without exposing ETW or Windows service types through IPC. A separate SCM adapter owns install/start/stop/restart/uninstall operations; the service entry is a worker-only callback that never creates windows. Runtime dispatch supplies the mode and returns classified errors to the future UI/service-control boundary.

**Tech Stack:** C++20, CMake, native Win32 SCM APIs, ETW, existing named-pipe IPC, `std::jthread`, `std::chrono`.

## Global Constraints

- Target Windows 11 24H2 x64 first; current 25H2 results are development-host evidence only.
- UI remains normal user; only portable collector/service owns ETW privileges.
- Windows services never create UI or call window APIs.
- Keep C++20, RAII, explicit handle ownership, `/W4 /WX /permissive- /sdl`, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, tests and Release builds.
- Public configuration remains in `AppConfig`; no timing constants are added to host or service code.
- Run `tools/quality.ps1 -Mode Format` after every source change and `Fast` for every task; run `Full` before each commit.

---

### Task 1: ETW capture adapter and service-neutral collector host

**Files:**

- Create: `include/network_flow_monitor/collector/etw_snapshot_provider.h`
- Create: `src/collector/etw_snapshot_provider.cpp`
- Create: `include/network_flow_monitor/collector/collector_host.h`
- Create: `src/collector/collector_host.cpp`
- Create: `tests/collector_host_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `EtwSnapshotProvider(WindowsEtwSession&)` implements `ipc::SnapshotProvider::Capture() noexcept`: call `FlushAndQuery()`, return `CaptureError::FlushFailed` or `SnapshotFailed` with current health on failure, and create the counter snapshot with one `steady_clock::now()` sample.
- `CollectorHost(WindowsEtwSession&, IpcServer&, SnapshotPublisher&, HostWait&)` exposes `Start(now)`, `Run(stop_token) noexcept`, `Stop() noexcept`, and `state()/last_error()`.
- `HostWait` supplies interruptible waits until the publisher’s `next_deadline`; tests inject a fake wait and clock while production uses `WaitForSingleObject`/`std::this_thread::sleep_for` without fixed-rate repaint or polling.

- [x] **Step 1: Register the host tests and run the red phase**

Add tests for provider flush failure, snapshot timestamp capture, host start ordering (ETW before IPC), no-client no-capture, due-client capture, stop idempotence, stop during wait, and publisher failure transitioning host health without throwing from the worker.

- [x] **Step 2: Implement the ETW provider**

Keep `FlushAndQuery()` and `MakeSnapshot(captured_at)` in one `Capture()` call; never fabricate counters after a failed flush and never call UI code. Classify `WindowsEtwSessionError` into the existing `CaptureError` values.

- [x] **Step 3: Implement host lifecycle and deadline wait**

Start the ETW session, then the IPC server, and roll back the ETW session if server startup fails. In `Run`, call `SnapshotPublisher::RunDue(now)` only at a real publisher deadline and wait on stop; call `Stop()` in every exit path and preserve the first classified error.

- [x] **Step 4: Verify Task 1**

Run Format, Fast and Full. Require the new `collector.host` test in Debug/Release, existing ETW integration unchanged, and no UI/window dependency in the collector target. Completed: Fast passed with 24/24 Debug tests; Full passed with 26/26 Debug and Release tests, including non-skipped ETW integration, `/analyze`, both Clang analyses and Release x64. Commit `feat: add service-neutral collector host` after the full gate.

Task 1 implementation notes: `SnapshotPublisher` now serializes subscription callbacks and deadline/publish access because IPC session workers and the collector host run concurrently. The host starts ETW before IPC, rolls back ETW on IPC startup failure, and treats a not-running ETW session as an idempotent stop result.

### Task 2: Service lifecycle policy and SCM adapter

**Files:**

- Create: `include/network_flow_monitor/service/service_control.h`
- Create: `src/service/service_control.cpp`
- Create: `include/network_flow_monitor/service/service_entry.h`
- Create: `src/service/service_entry.cpp`
- Create: `tests/service_control_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- `ServiceControl` exposes install/uninstall/start/stop/restart/query with typed `ServiceAction`, `ServiceStartType`, `ServiceState` and `ServiceResult`; all names and executable paths are fixed by the product, not arbitrary command strings.
- `ServiceEntry::Run()` receives the service callback and owns only the `CollectorHost`; it reports `SERVICE_RUNNING`, `SERVICE_STOP_PENDING`, `SERVICE_STOPPED` and classified Win32 errors through `SetServiceStatus`.

- [ ] **Step 1: Add fake SCM API tests**
- [ ] **Step 2: Implement RAII SCM handles and fixed service configuration**
- [ ] **Step 3: Implement service callback stop control without UI creation**
- [ ] **Step 4: Run Fast/Full and commit `feat: add service lifecycle adapter`**

### Task 3: Runtime-mode wiring and portable collector launch

**Files:**

- Modify: `src/app/main.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/runtime_collector_integration_tests.cpp`
- Modify: `docs/architecture.md`
- Modify: `docs/configuration.md`

**Interfaces:**

- `--collector` launches the collector host after the explicit UAC boundary; `--service` enters `StartServiceCtrlDispatcherW`; `--install`, `--uninstall` and `--service-control` call only typed service actions.
- UI mode remains normal privilege and reports collector/service state through the existing IPC client; it never starts ETW directly.

- [ ] **Step 1: Register runtime dispatch tests and prove unsupported modes remain classified**
- [ ] **Step 2: Wire portable host and service entry without window calls**
- [ ] **Step 3: Verify portable shutdown, service startup and reconnect behavior**
- [ ] **Step 4: Run Full and commit `feat: wire dual collection modes`**

### Task 4: Milestone 5 audit and handoff to UI

**Files:**

- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/planning/findings.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`

- [ ] **Step 1: Map service/portable/security requirements to evidence**
- [ ] **Step 2: Record 24H2 and privilege-environment limitations explicitly**
- [ ] **Step 3: Run Full, packaging smoke checks and commit `feat: complete dual collection modes`**
- [ ] **Step 4: Mark exactly one next milestone in progress: tray and floating UI**
