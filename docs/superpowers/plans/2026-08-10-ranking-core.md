# Ranking Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic, independently tested application accumulator and sliding-window Top N engine over cumulative counters.

**Architecture:** `ApplicationAccumulator` turns application-attributed traffic deltas into one monotonic counter per opaque `ApplicationId`. `RankingEngine` owns a bounded deque of unique-ID snapshots, tracks counter epochs across resets, calculates rates from real `steady_clock` intervals, and produces immutable ranking snapshots without timers or UI dependencies.

**Tech Stack:** C++20 standard library, CMake 3.28+, MSVC x64, CTest.

## Global Constraints

- Target Windows 11 24H2 x64, but keep this pure logic module free of Win32, ETW, IPC, COM, and UI headers.
- Treat `ApplicationId` as an opaque, case-sensitive UTF-8 string; identity normalization belongs to the process subsystem.
- Use `std::chrono::steady_clock::time_point` and `std::chrono::milliseconds`; no wall clock and no unitless time interface.
- Reject non-increasing timestamps, duplicate IDs, invalid options, empty IDs, and accumulator overflow without mutating state.
- Treat decreasing counters as a new per-app/per-direction epoch, never as a huge unsigned delta.
- Top N truncates only the list, not system totals; zero-total-rate applications are absent from Top N.
- First-party code must pass the repository Full quality gate.
- Git commits are conditional on explicit user authorization at execution time.

---

### Task 1: Implement the application counter model and monotonic accumulator

**Files:**
- Create: `include/network_flow_monitor/ranking/counter_types.h`
- Create: `include/network_flow_monitor/ranking/application_accumulator.h`
- Create: `src/ranking/application_accumulator.cpp`
- Create: `tests/application_accumulator_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: application-attributed byte deltas from the future Collector.
- Produces: stable `ApplicationId`, metadata/counter/snapshot types and `ApplicationAccumulator::AddTraffic()`/`MakeSnapshot()`.

- [x] **Step 1: Write failing accumulator tests**

Define tests against this contract:

```cpp
using ApplicationId = std::string;
enum class TrafficDirection : std::uint8_t { Upload, Download };
struct ApplicationMetadata { std::string display_name; std::string icon_key; bool operator==(const ApplicationMetadata&) const = default; };
struct ApplicationCounters { ApplicationId id; ApplicationMetadata metadata; std::uint64_t upload_bytes; std::uint64_t download_bytes; bool operator==(const ApplicationCounters&) const = default; };
struct CounterSnapshot { std::chrono::steady_clock::time_point captured_at; std::vector<ApplicationCounters> applications; };
enum class AccumulationError : std::uint8_t { None, EmptyApplicationId, CounterOverflow };
struct AccumulationResult { AccumulationError error; [[nodiscard]] bool ok() const noexcept; };
```

Test one upload, separate directions, two simulated processes using the same ID, distinct IDs, empty ID rejection, zero-byte no-op, deterministic ID ordering, metadata enrichment, metadata conflict counting, and `UINT64_MAX + 1` rollback.

- [x] **Step 2: Verify the accumulator red phase**

Register `ranking.accumulator`, build its target, and expect missing API compilation failure:

```powershell
cmake --build --preset windows-x64-debug --target nfm_application_accumulator_tests
```

- [x] **Step 3: Implement the accumulator with strong failure semantics**

Declare:

```cpp
class ApplicationAccumulator {
public:
    [[nodiscard]] AccumulationResult AddTraffic(std::string_view id,
                                                const ApplicationMetadata& metadata,
                                                TrafficDirection direction,
                                                std::uint64_t bytes);
    [[nodiscard]] CounterSnapshot MakeSnapshot(std::chrono::steady_clock::time_point captured_at) const;
    [[nodiscard]] std::uint64_t metadata_conflicts() const noexcept;
private:
    std::unordered_map<ApplicationId, ApplicationCounters> counters_;
    std::uint64_t metadata_conflicts_{};
};
```

Check overflow with `current > UINT64_MAX - bytes` before modifying a slot. Insert only after validation. Preserve the first non-empty metadata value, enrich empty fields, and increment a saturating diagnostic counter for conflicting non-empty values. Sort the copied snapshot vector by `id` before return.

- [x] **Step 4: Run focused tests and Fast gate**

Run:

```powershell
ctest --test-dir build/windows-x64-debug -R ranking.accumulator --output-on-failure
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

Expected: accumulator and existing tests pass under strict warnings and clang analysis.

- [x] **Step 5: Create the optional verified checkpoint**

After a Full gate and explicit commit authorization:

```powershell
git add include/network_flow_monitor/ranking src/ranking src/CMakeLists.txt tests/CMakeLists.txt tests/application_accumulator_tests.cpp
git commit -m "feat: add monotonic application accumulator"
```

Otherwise record the proposed message without committing.

---

### Task 2: Implement snapshot validation, history retention, and warm-up

**Files:**
- Create: `include/network_flow_monitor/ranking/ranking_engine.h`
- Create: `src/ranking/ranking_engine.cpp`
- Create: `tests/ranking_engine_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: unique-ID `CounterSnapshot` values and `RankingOptions`.
- Produces: state-preserving `RankingEngine::PushSnapshot()` results with `WarmingUp`/`Ready` status.

- [x] **Step 1: Write failing validation and warm-up tests**

Use this public surface:

```cpp
struct RankingOptions { std::chrono::milliseconds window; std::uint8_t top_n; };
enum class RankingStatus : std::uint8_t { WarmingUp, Ready };
enum class RankingError : std::uint8_t { None, InvalidWindow, InvalidTopN, NonIncreasingTimestamp, EmptyApplicationId, DuplicateApplicationId, NonFiniteRate };
struct TransferRate { double upload_bytes_per_second; double download_bytes_per_second; [[nodiscard]] double total_bytes_per_second() const noexcept; };
struct RankedApplication { ApplicationId id; ApplicationMetadata metadata; TransferRate rate; };
struct RankingDiagnostics { std::uint64_t counter_discontinuities; std::uint64_t ignored_single_sample_applications; };
struct RankingSnapshot { RankingStatus status; std::chrono::steady_clock::duration sampled_span; TransferRate total_rate; std::vector<RankedApplication> applications; RankingDiagnostics diagnostics; };
struct RankingResult { RankingError error; RankingSnapshot snapshot; [[nodiscard]] bool accepted() const noexcept; };
```

Test invalid zero/negative window, Top N 0/21, first snapshot warm-up, 1999ms warm-up for a 2000ms window, ready at 2000ms, repeated/decreasing timestamp rejection, empty ID rejection, duplicate ID rejection, and proof that rejected pushes leave the next valid result identical to a clean engine.

- [x] **Step 2: Verify the ranking-engine red phase**

Register `ranking.engine`, build `nfm_ranking_engine_tests`, and expect missing API compilation failure.

- [x] **Step 3: Implement validation and bounded history**

Construct `RankingEngine` through a factory that validates options without exceptions:

```cpp
struct RankingEngineCreateResult {
    std::unique_ptr<RankingEngine> engine;
    RankingError error;
};
[[nodiscard]] RankingEngineCreateResult CreateRankingEngine(RankingOptions options);
```

Before mutating the deque, verify strict timestamp increase and detect duplicate IDs with a temporary set. Keep the newest snapshot not later than `current - window` plus all newer snapshots; when insufficient elapsed history exists, return `WarmingUp` with empty rates/list.

- [x] **Step 4: Calculate ready-state rates from the actual span**

Select the newest baseline with `captured_at <= current - window`. For an application present in both baseline and current with non-decreasing counters, compute:

```cpp
const double seconds = std::chrono::duration<double>(current_time - baseline_time).count();
const double upload_rate = static_cast<double>(current.upload_bytes - baseline.upload_bytes) / seconds;
```

For an app first observed after the global baseline, require a second observation and divide by its own first-to-current span. Count one-sample apps in diagnostics. Do not emit a Top N entry until the engine is globally ready.

- [x] **Step 5: Run warm-up/rate tests and the Fast gate**

Add cases for irregular 700ms/1300ms/2400ms timestamps and assert the divisor uses the actual 2400ms baseline span. Run:

```powershell
ctest --test-dir build/windows-x64-debug -R ranking.engine --output-on-failure
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

- [x] **Step 6: Create the optional verified checkpoint**

After Full verification and explicit authorization:

```powershell
git add include/network_flow_monitor/ranking/ranking_engine.h src/ranking/ranking_engine.cpp src/CMakeLists.txt tests/CMakeLists.txt tests/ranking_engine_tests.cpp
git commit -m "feat: calculate sliding-window application rates"
```

Otherwise record the proposed message without committing.

---

### Task 3: Add counter epochs, deterministic Top N, and live option changes

**Files:**
- Modify: `include/network_flow_monitor/ranking/ranking_engine.h`
- Modify: `src/ranking/ranking_engine.cpp`
- Modify: `tests/ranking_engine_tests.cpp`

**Interfaces:**
- Consumes: the Task 2 engine and history.
- Produces: reset-safe directions, full totals, deterministic rankings, and `UpdateOptions()` without avoidable history loss.

- [x] **Step 1: Add failing reset and rollover tests**

For upload and download independently, push a high baseline, a lower current value, and then a higher value in the new epoch. Assert the decreasing interval emits zero for only the affected direction, increments `counter_discontinuities`, and the following interval resumes from the reset snapshot rather than the old high baseline. Repeat with `UINT64_MAX` followed by a small value and assert no enormous modular rate appears.

- [x] **Step 2: Implement per-direction epoch baselines**

Track reset observations by `(ApplicationId, direction)` with their timestamp and value. If `current < selected baseline`, replace that direction's epoch baseline with current and suppress that direction for this result. On subsequent pushes, prefer a newer epoch baseline over a snapshot from the old epoch. Remove epoch state only when no retained snapshot and no current application references the ID.

- [x] **Step 3: Add failing total, filtering, and deterministic-order tests**

Cover more active apps than Top N, zero-rate apps, exact ties, independent upload/download values, and a case where a truncated application contributes to system totals. Expected order is total descending, download descending, upload descending, then `ApplicationId` ascending.

- [x] **Step 4: Implement sorted output and totals**

Sum every valid current application into `RankingSnapshot::total_rate` before filtering the visible list. Exclude only exact zero-total rows, apply the four-key comparator, then resize to `top_n`. Validate all produced doubles with `std::isfinite`; return a state-preserving error if a time/rate invariant is violated rather than emitting NaN/Infinity.

- [x] **Step 5: Add and implement option-update tests**

Expose:

```cpp
[[nodiscard]] RankingError UpdateOptions(RankingOptions options);
```

Test Top N decrease/increase without clearing history, window decrease using retained data, window increase returning to warm-up until enough new history exists, and invalid updates leaving options/history unchanged. Retain history up to the largest window observed during the current engine lifetime, capped by the public maximum of 10 seconds plus one predecessor snapshot.

- [x] **Step 6: Run the complete ranking tests and Fast gate**

Run:

```powershell
ctest --test-dir build/windows-x64-debug -R "ranking.(accumulator|engine)" --output-on-failure
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

Expected: all ranking and existing tests pass.

- [x] **Step 7: Create the optional verified checkpoint**

After Full verification and explicit authorization:

```powershell
git add include/network_flow_monitor/ranking src/ranking tests/ranking_engine_tests.cpp
git commit -m "feat: finalize deterministic top application ranking"
```

Otherwise record the proposed message without committing.

---

### Task 4: Complete the Milestone 1 quality and documentation gate

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: the verified configuration and ranking plans.
- Produces: a completed Milestone 1 baseline ready for the process-identity plan.

- [x] **Step 1: Run all focused Debug tests**

Run:

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --test-dir build/windows-x64-debug --output-on-failure
```

Expected: bootstrap, runtime mode, config, accumulator, and ranking tests all pass.

- [x] **Step 2: Run the mandatory Full gate**

Run `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` and record format, strict Debug, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Debug/Release CTest counts, and Release x64 status in `docs/planning/progress.md`.

- [x] **Step 3: Synchronize architecture and milestone state**

Document `ApplicationAccumulator` as the pure application-merge boundary consumed by the future Collector and `RankingEngine` as the client-owned snapshot history. Mark Milestone 1 complete only if both configuration and ranking plans are fully checked and the Full gate is green. Set the single next action to writing the process-identity design specification.

- [x] **Step 4: Verify documentation and repository hygiene**

Run the navigational Markdown link check excluding implementation-plan code examples, then:

```powershell
git diff --check
git status --short
git check-ignore build .superpowers
```

Expected: no whitespace errors, generated/local state ignored, and no unrelated user files changed.

- [x] **Step 5: Create the optional milestone checkpoint**

If the user has explicitly authorized commits:

```powershell
git add CMakeLists.txt src tests include third_party docs README.md
git commit -m "feat: complete configuration and ranking core"
```

Otherwise leave the Full-gate-passing Milestone 1 worktree uncommitted and report the proposed commit.
