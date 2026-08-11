# Process Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This repository is being executed inline and sequentially; do not dispatch subagents.

**Goal:** Build a deterministic Windows 11 process-lifetime and application-identity module that prevents PID reuse from misattributing network traffic and supplies stable IDs to the existing accumulator.

**Architecture:** A pure `ProcessTracker` owns generation-based process instances and event-time intervals; a pure identity policy chooses AUMID, package family, normalized executable path, system, or instance-scoped unresolved IDs. A small Win32 adapter probes one process with `PROCESS_QUERY_LIMITED_INFORMATION`, converts bounded strings to UTF-8, and rejects stale PID probes by creation time. The future Collector owns call order and feeds resolved IDs to `ApplicationAccumulator`.

**Tech Stack:** C++20, CMake, Win32 Kernel32/AppModel APIs, `std::chrono`, CTest, clang-format, clang-tidy/Clang Static Analyzer, MSVC `/analyze`.

## Global Constraints

- Target Windows 11 24H2 x64; use only documented Windows APIs.
- Do not add a third-party dependency or optional runtime DLL.
- Do not read command lines, remote addresses, payloads, process memory, or user SIDs.
- Request only `PROCESS_QUERY_LIMITED_INFORMATION`; do not enable `SeDebugPrivilege` and do not retain process handles.
- All time across public interfaces uses `TraceTimePoint`/`TraceDuration`; no unitless time integers.
- Prefer false separation over false merging when identity evidence is incomplete.
- All Windows handles use RAII; exceptions cannot escape the Win32 probe boundary.
- Every task ends with Format, Fast, then Full quality gates. Git commit steps are conditional on explicit user commit authorization; otherwise record the proposed message without staging or committing.

---

### Task 1: Shared application identity and deterministic identity policy

**Files:**
- Create: `include/network_flow_monitor/process/application_identity.h`
- Create: `include/network_flow_monitor/process/process_instance.h`
- Create: `include/network_flow_monitor/process/identity_resolver.h`
- Create: `src/process/identity_resolver.cpp`
- Modify: `include/network_flow_monitor/ranking/counter_types.h`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/identity_resolver_tests.cpp`

**Interfaces:**
- Consumes: UTF-8 AUMID, package family, Win32 image path, image-name hint, and `ProcessInstanceId` supplied by later tracker/probe tasks.
- Produces: shared `nfm::process::ApplicationId`, `ApplicationMetadata`, `ProcessInstanceId`, `TraceTimePoint`, `NormalizeWindowsImagePath`, and `ResolveApplicationIdentity`; `ranking/counter_types.h` aliases the two shared identity types without changing ranking behavior.

- [x] **Step 1: Move only the shared identity value types and register the failing identity test**

Create `application_identity.h` with the exact shared values:

```cpp
namespace nfm::process {
using ApplicationId = std::string;

struct ApplicationMetadata {
    std::string display_name;
    std::string icon_key;
    bool operator==(const ApplicationMetadata&) const = default;
};
} // namespace nfm::process
```

Create `process_instance.h` with:

```cpp
namespace nfm::process {
using TraceDuration =
    std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
using TraceTimePoint = std::chrono::sys_time<TraceDuration>;

struct ProcessInstanceId {
    std::uint32_t pid{};
    std::uint64_t generation{};
    bool operator==(const ProcessInstanceId&) const = default;
};
} // namespace nfm::process
```

Change `ranking/counter_types.h` to include `application_identity.h` and declare:

```cpp
using nfm::process::ApplicationId;
using nfm::process::ApplicationMetadata;
```

Register `identity.resolver` in `tests/CMakeLists.txt` before the resolver header exists. Add tests that name the APIs below and cover AUMID priority, package-family fallback, path fallback, system identities, unresolved identity, same-name/different-path separation, and path forms.

- [x] **Step 2: Run the red phase**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Format
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

Expected: compilation fails with C1083 for `network_flow_monitor/process/identity_resolver.h`, while existing ranking sources still compile far enough to prove the moved alias is compatible.

- [x] **Step 3: Implement the public identity policy**

Create `identity_resolver.h` with these exact types:

```cpp
enum class IdentityKind : std::uint8_t {
    PackageApplication,
    PackageFamily,
    Executable,
    System,
    Unresolved,
};

enum class IdentityResolutionError : std::uint8_t {
    None,
    InvalidImagePath,
};

struct ProcessProbeData {
    std::optional<std::string> application_user_model_id;
    std::optional<std::string> package_family_name;
    std::optional<std::string> image_path;
    std::string image_name_hint;
};

struct ResolvedApplicationIdentity {
    ApplicationId id;
    ApplicationMetadata metadata;
    IdentityKind kind{IdentityKind::Unresolved};
    bool operator==(const ResolvedApplicationIdentity&) const = default;
};

struct IdentityResolutionResult {
    IdentityResolutionError error{IdentityResolutionError::None};
    ResolvedApplicationIdentity identity;
    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] std::optional<std::string>
NormalizeWindowsImagePath(std::string_view path);

[[nodiscard]] IdentityResolutionResult
ResolveApplicationIdentity(ProcessInstanceId instance, const ProcessProbeData& data);
```

Implement prefixes exactly as the spec: `package-app:`, `package-family:`, `executable:`, `system:idle`, `system:kernel`, and `unresolved:<pid>:<generation>`. Treat PID 4 as kernel only when `image_name_hint` equals `System` under ASCII case-insensitive comparison; otherwise resolve it normally or unresolved. An empty optional string is treated as absent.

`NormalizeWindowsImagePath` rejects embedded NUL, empty/relative paths, converts slash to backslash, maps extended DOS and UNC prefixes, and removes only non-root trailing separators. Preserve case and do not touch the filesystem.

- [x] **Step 4: Complete identity tests and preserve ranking behavior**

The focused test must include assertions equivalent to:

```cpp
context.Check(Resolve(instance, {.application_user_model_id = "Family!App",
                                 .package_family_name = "Family",
                                 .image_path = "C:\\App\\app.exe"})
                      .identity.id == "package-app:Family!App",
              "AUMID wins");

context.Check(Resolve(first, {.image_path = "C:\\One\\same.exe"}).identity.id !=
                  Resolve(second, {.image_path = "D:\\Two\\same.exe"}).identity.id,
              "same filename in different paths stays separate");

context.Check(NormalizeWindowsImagePath("\\\\?\\UNC\\server\\share\\app.exe") ==
                  "\\\\server\\share\\app.exe",
              "extended UNC path normalized");
```

Also run `ranking.accumulator` and `ranking.engine` to verify the type extraction does not change accumulation or ordering.

- [x] **Step 5: Run Task 1 quality gates**

Run Format, Fast, and Full through `tools/quality.ps1`. Expected: Debug/Release CTest 8/8, strict compiler, both Clang analyses, MSVC `/analyze`, and Release x64 pass.

- [x] **Step 6: Record the conditional checkpoint**

Proposed commit: `feat: define deterministic application identity policy`. Commit only with explicit authorization.

---

### Task 2: Event-time process tracker and PID-reuse defense

**Files:**
- Create: `include/network_flow_monitor/process/process_tracker.h`
- Create: `src/process/process_tracker.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/process_tracker_tests.cpp`

**Interfaces:**
- Consumes: `ProcessInstanceId`, `TraceTimePoint`, `ProcessProbeData`, and `ResolveApplicationIdentity` from Task 1.
- Produces: `ProcessTracker::ObserveStart`, `ObserveEnd`, `ApplyProbeData`, `ResolveAt`, `AdvanceWatermark`, `diagnostics`, and `ProcessProbeRequest` for Task 3 and the future Collector.

- [x] **Step 1: Register lifecycle tests against the missing tracker API**

Create tests for: ordinary start/resolve/end, duplicate start, explicit PID reuse, stale end key, implicit reuse without comparable keys, reuse-boundary lookup, late event, TTL expiry, watermark rollback, cache capacity eviction, stale probe result, generation exhaustion seam, and saturating diagnostics.

Use a named helper instead of unitless ticks:

```cpp
TraceTimePoint At(std::chrono::seconds offset) {
    return TraceTimePoint{std::chrono::duration_cast<TraceDuration>(offset)};
}
```

Register `process.tracker`, format, and run Fast. Expected red result: C1083 for `process_tracker.h`.

- [x] **Step 2: Define the tracker contract**

Create the header with:

```cpp
inline constexpr auto kRetiredProcessRetention = std::chrono::seconds{30};
inline constexpr std::size_t kMaximumRetiredProcesses = 65'536U;

enum class ProcessStartKind : std::uint8_t { Started, Rundown };
enum class ProcessTrackerError : std::uint8_t {
    None,
    NonIncreasingStartTime,
    EndBeforeStart,
    WatermarkMovedBackward,
    GenerationExhausted,
    NotFound,
    StaleEnd,
    StaleProbeResult,
    InvalidProbeData,
};

struct ProcessStartObservation {
    std::uint32_t pid{};
    TraceTimePoint observed_at;
    std::optional<std::uint64_t> kernel_process_key;
    std::string image_name_hint;
    ProcessStartKind kind{ProcessStartKind::Started};
};

struct ProcessEndObservation {
    std::uint32_t pid{};
    TraceTimePoint observed_at;
    std::optional<std::uint64_t> kernel_process_key;
};

struct ProcessProbeRequest {
    ProcessInstanceId instance;
    TraceTimePoint observed_at;
    ProcessStartKind kind{ProcessStartKind::Started};
};

struct TrackedProcess {
    ProcessInstanceId instance;
    ResolvedApplicationIdentity application;
};

struct ProcessTrackerDiagnostics {
    std::uint64_t implicit_pid_reuses{};
    std::uint64_t stale_end_events{};
    std::uint64_t stale_probe_results{};
    std::uint64_t unmatched_network_events{};
    std::uint64_t expired_retired_processes{};
    std::uint64_t capacity_evictions{};
};

struct ObserveStartResult {
    ProcessTrackerError error{ProcessTrackerError::None};
    ProcessInstanceId instance;
    std::optional<ProcessProbeRequest> probe_request;
    [[nodiscard]] bool accepted() const noexcept;
};

struct ProcessLookupResult {
    ProcessTrackerError error{ProcessTrackerError::None};
    std::optional<TrackedProcess> process;
    [[nodiscard]] bool found() const noexcept;
};

class ProcessTracker {
  public:
    [[nodiscard]] ObserveStartResult ObserveStart(ProcessStartObservation observation);
    [[nodiscard]] ProcessTrackerError ObserveEnd(ProcessEndObservation observation);
    [[nodiscard]] ProcessTrackerError ApplyProbeData(ProcessInstanceId instance,
                                                     const ProcessProbeData& data);
    [[nodiscard]] ProcessLookupResult ResolveAt(std::uint32_t pid,
                                                TraceTimePoint event_time);
    [[nodiscard]] ProcessTrackerError AdvanceWatermark(TraceTimePoint watermark);
    [[nodiscard]] const ProcessTrackerDiagnostics& diagnostics() const noexcept;

  private:
    friend struct ProcessTrackerTestAccess;
};
```

Result structs expose `error`, the relevant value/optional, and `accepted()`; do not encode failures as magic IDs.

- [x] **Step 3: Implement active/retired interval ownership**

Use one active entry per PID and retired entries owned by value. Allocate generation before mutation; if the next value is zero or exhausted, return `GenerationExhausted` without changing state. All diagnostic increments use a shared saturating helper.

Duplicate start means same PID, same present kernel key, and a timestamp not before the current start; enrich an empty image hint and return the existing instance without a second probe request. Any genuine later start implicitly retires the active instance at the new start time before creating the next generation.

Resolve half-open intervals `[start, end)`, choosing the matching instance with the greatest start time. Do not fall back to the current active PID when the event time is outside its interval.

- [x] **Step 4: Implement probe application and bounded cleanup**

At start, initialize system or unresolved identity with the Task 1 resolver. `ApplyProbeData` locates the exact `(pid, generation)` in active or retained state, resolves the new identity, and rejects an expired or unknown instance as `StaleProbeResult` without modifying a reused PID.

`AdvanceWatermark` must be monotonic. Remove retired entries whose `ended_at + 30s <= watermark`; then, if still over 65,536 entries, evict the earliest ended entries deterministically. Capacity and expiry diagnostics are separate.

Define `nfm::process::ProcessTrackerTestAccess` only in `process_tracker_tests.cpp`; the friend sets `next_generation_` and individual diagnostic fields for generation-exhaustion and saturation tests. Do not add a production setter or runtime option for either value.

- [x] **Step 5: Run Task 2 quality gates**

Run Format, Fast, and Full. Expected: Debug/Release CTest 9/9 and every mandatory gate pass.

- [x] **Step 6: Record the conditional checkpoint**

Proposed commit: `feat: track process lifetimes across pid reuse`. Commit only with explicit authorization.

---

### Task 3: Bounded Win32 process probe

**Files:**
- Create: `include/network_flow_monitor/process/windows_process_probe.h`
- Create: `src/process/windows_process_probe.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/windows_process_probe_tests.cpp`

**Interfaces:**
- Consumes: `ProcessProbeRequest`, `TraceTimePoint`, and `ProcessProbeData` from Tasks 1–2.
- Produces: `ProbeWindowsProcess`, `FileTimeToTraceTimePoint`, classified probe errors/stages, and validated data ready for `ProcessTracker::ApplyProbeData`.

- [x] **Step 1: Register current-process and short-lived-process tests**

Tests obtain the current PID, make a Start request whose timestamp is read with `GetSystemTimePreciseAsFileTime`, and verify:

- the probe opens only a limited-information handle and returns a creation time not later than the request time;
- the image path is absolute UTF-8 and resolves to `IdentityKind::Executable` or a package kind;
- no-package/no-application are normal absence, not a failing probe;
- a request deliberately dated before process creation returns `PidReusedBeforeProbe`;
- a controlled child process that exits before probing yields `ProcessUnavailable` or `AccessDenied`, never fabricated data.

Register `process.windows_probe`, format, and run Fast. Expected red result: C1083 for `windows_process_probe.h`.

- [x] **Step 2: Define the bounded probe API**

```cpp
enum class ProcessProbeStage : std::uint8_t {
    None,
    OpenProcess,
    CreationTime,
    ApplicationUserModelId,
    PackageFamilyName,
    ImagePath,
    Utf8Conversion,
};

enum class ProcessProbeError : std::uint8_t {
    None,
    InvalidRequest,
    ProcessUnavailable,
    AccessDenied,
    PidReusedBeforeProbe,
    StringTooLong,
    InvalidUnicode,
    OutOfMemory,
    WindowsFailure,
};

struct WindowsProcessProbeResult {
    ProcessProbeError error{ProcessProbeError::None};
    ProcessProbeStage stage{ProcessProbeStage::None};
    std::uint32_t win32_error{};
    std::optional<TraceTimePoint> creation_time;
    ProcessProbeData data;
    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] std::optional<TraceTimePoint>
FileTimeToTraceTimePoint(const FILETIME& file_time) noexcept;

[[nodiscard]] WindowsProcessProbeResult
ProbeWindowsProcess(const ProcessProbeRequest& request) noexcept;
```

This Win32-specific public header includes `windows.h` for `FILETIME`; no other `process` public header includes an SDK header.

- [x] **Step 3: Implement RAII, bounded query loops, and error classification**

Create a translation-unit-local move-only `UniqueHandle` that closes valid handles. Call:

```cpp
OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, request.instance.pid);
GetProcessTimes(handle, &creation, &exit, &kernel, &user);
GetApplicationUserModelId(handle, &length, buffer);
GetPackageFamilyName(handle, &length, buffer);
QueryFullProcessImageNameW(handle, 0, buffer, &length);
```

Use SDK maximum constants for AUMID/package family and 32,768 UTF-16 code units for the image path. Convert with `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ...)`. Catch `std::bad_alloc` and all other exceptions inside `ProbeWindowsProcess`; return `OutOfMemory` for the former and `WindowsFailure` for the latter so no exception can cross a future ETW callback.

Map `ERROR_ACCESS_DENIED` separately. Map `ERROR_INVALID_PARAMETER` for a vanished/invalid PID and other documented gone-process outcomes to `ProcessUnavailable`. Treat `APPMODEL_ERROR_NO_APPLICATION` and `APPMODEL_ERROR_NO_PACKAGE` as absent optional fields.

- [x] **Step 4: Reject the PID-reuse race and test the complete handoff**

For both `ProcessStartKind::Started` and `Rundown`, if converted creation time is later than `request.observed_at`, return `PidReusedBeforeProbe` with no data. Rundown normally has a creation time earlier than its enumeration observation; it does not bypass reuse validation. Add an end-to-end test that starts a tracker instance, probes the current process, applies the data by exact instance ID, and resolves the resulting application at the observation time.

Do not test protected processes or assume a package identity. Do not make timing assertions tighter than the documented 100ns representation.

- [x] **Step 5: Verify production dependencies and run Task 3 quality gates**

Run Format, Fast, and Full. Expected: Debug/Release CTest 10/10 and all analysis gates pass. Then run:

```powershell
dumpbin /dependents build/windows-x64-release/src/NetworkFlowMonitor.exe
```

Record the actual dependencies; the executable is not yet expected to link `nfm_process`, so do not claim a binary-size or dependency delta until Collector integration makes it reachable.

- [x] **Step 6: Record the conditional checkpoint**

Proposed commit: `feat: probe windows application identity safely`. Commit only with explicit authorization.

---

### Task 4: Complete Milestone 2 documentation and quality gate

**Files:**
- Modify: `docs/architecture.md`
- Modify: `README.md`
- Modify: `docs/planning/findings.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`
- Modify: `docs/superpowers/plans/2026-08-11-process-identity.md`

**Interfaces:**
- Consumes: verified identity policy, process tracker, and Win32 probe.
- Produces: a complete Milestone 2 baseline and one next action: ETW Collector design.

- [x] **Step 1: Synchronize architecture and user-facing status**

Document exact instance, application-ID priority, expected unknown/host behavior, retired cache, privacy boundary, and the fact that Win32 probing uses limited rights. Do not claim perfect attribution or package display-name localization.

- [x] **Step 2: Run focused and full verification**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Format
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
git diff --check
git status --short
git check-ignore build .superpowers
```

Validate navigational Markdown links outside implementation-plan code examples. Record exact Debug/Release CTest counts, static-analysis results, Release status, and repository hygiene in `progress.md`.

- [x] **Step 3: Audit the specification requirement by requirement**

Map every item in `2026-08-11-process-identity-design.md` section 10 to a named test or explicit code invariant. A green aggregate test count is insufficient if a required PID-reuse, path, package, cache, privacy, or integration case is missing.

- [x] **Step 4: Close Milestone 2 and select the next work item**

Mark Milestone 2 complete only after Tasks 1–3 and the audit are green. Set exactly one queue item to in progress: ETW Collector design and implementation planning. Do not start ETW code without its focused design specification.

- [x] **Step 5: Record the optional milestone checkpoint**

If the user explicitly authorizes commits:

```powershell
git add CMakeLists.txt src tests include docs README.md
git commit -m "feat: complete process identity core"
```

Otherwise leave the Full-gate-passing worktree uncommitted and record the proposed commit.
