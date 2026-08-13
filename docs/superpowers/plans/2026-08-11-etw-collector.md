# ETW Collector Implementation Plan

> Execute sequentially in the primary session. Do not implement secure IPC, service/UAC orchestration, or UI in this plan. Every task ends with the repository quality gates; Git commits remain conditional on explicit user authorization.

**Goal:** Implement and verify the Windows 11 24H2 x64 real-time ETW Collector described by ADR 0009 and `docs/superpowers/specs/2026-08-11-etw-collector-design.md`.

**Architecture:** A pure single-thread `CollectorCore` consumes neutral network/process events and owns `ProcessTracker` plus `ApplicationAccumulator`. A Windows record adapter classifies `EVENT_RECORD`, uses a fixed checked prefix for high-volume network version-2 records, and TDH only for process properties. A RAII real-time system logger owns StartTrace/OpenTrace/ProcessTrace/ControlTrace/CloseTrace and reports query/loss statistics. No address, port, SID, command line, or packet payload enters the model.

**Toolchain:** C++20, CMake/Ninja, MSVC, Win32 ETW (`evntrace.h`, `evntcons.h`), TDH, existing process/ranking libraries, repository `tools/quality.ps1`.

---

## Preconditions and invariants

- Target remains Windows 11 24H2 x64; no compatibility decoder for version 0/1 is implied.
- `nfm_collector` is a static first-party library. Link `Tdh.lib` and the documented Win11 ETW import library only on the Windows adapter target; pure tests must not require a live session.
- Controller timestamps use QPC and consumer timestamps are not raw. Every event time goes through a named FILETIME conversion.
- Network payload parsing never uses event-header PID or unaligned casts.
- Process TDH reads request only `UniqueProcessKey`, `ProcessId`, and `ImageFileName`; supported Process schema versions are the documented/live-confirmed 2/3/4 whitelist.
- The live integration test is mandatory in Full mode and must fail clearly, not skip, if the required administrator context is absent.
- The current executable does not link the Collector until a later runtime-mode task. Record this reachability when measuring dependencies/size.

## Task 1: Neutral events, network decoder, health, and Collector core

**Files:**

- Create: `include/network_flow_monitor/collector/event_types.h`
- Create: `include/network_flow_monitor/collector/collector_health.h`
- Create: `include/network_flow_monitor/collector/network_event_decoder.h`
- Create: `include/network_flow_monitor/collector/collector_core.h`
- Create: `src/collector/network_event_decoder.cpp`
- Create: `src/collector/collector_core.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/network_event_decoder_tests.cpp`
- Create: `tests/collector_core_tests.cpp`
- Modify: `tests/CMakeLists.txt`

### Step 1: Register failing decoder and core tests

- [x] Add `collector.network_decoder` and `collector.core` CTest targets before creating public headers.
- [x] Cover TCP/UDP IPv4/IPv6 Send/Receive, TCP retransmit, short/unaligned payload, unknown provider/version/opcode, zero/max size, and timestamp input.
- [x] Cover resolved/unresolved process attribution, same-application merge, explicit PID reuse boundary, no-match unattributed bytes, fake probe failure, retransmit diagnostics, accumulator overflow, loss-statistics reset, and saturating health counters.
- [x] Format the new tests and run Fast. Expected red result: C1083 for the missing `collector` headers, with no unrelated failure.

### Step 2: Define neutral types and classified results

- [x] Implement the exact event model from the spec, `CollectorState`, `CollectorErrorDomain`, `CollectorErrorStage`, `CollectorHealth`, and saturating-update helpers.
- [x] Define an SDK-free `ProcessProbe` interface whose `noexcept` operation returns classified success/data or an error; add a fake in tests and a Windows adapter only in Task 2.
- [x] Define decoder result states `Decoded`, `Ignored`, and `Rejected`, with explicit `UnsupportedVersion`, `PayloadTooShort`, `InvalidTimestamp`, and invariant errors.

### Step 3: Implement the network decoder

- [x] Accept only the documented provider/opcode/version-2 matrix. Read PID/size with `memcpy`; do not parse or preserve the remaining payload.
- [x] Map TCP retransmit to Upload plus `retransmission=true`; ignore connection/control/copy events without incrementing malformed diagnostics.
- [x] Reject version mismatch and short payload without producing an event. Use the existing trace time type, not unitless ticks.

### Step 4: Implement Collector core

- [x] Start/Rundown drives `ProcessTracker`, invokes the injected probe synchronously only when requested, and applies results by exact generation.
- [x] Network events resolve by event time and update `ApplicationAccumulator`; NotFound updates fixed `collector:unattributed` metadata and unmatched counters.
- [x] End, stale lifecycle, invalid data, probe failure, counter overflow, and loss-stat changes update health exactly as the spec states.
- [x] Expose owner-thread `MakeSnapshot` and immutable health access. Do not add internal mutexes or an IPC-facing timer.

### Step 5: Verify Task 1

- [x] Run Format, Fast, then Full. Debug/Release CTest 12/12 and all compiler/analyzer/Release gates passed.
- [x] Record exact results in `docs/planning/progress.md`.
- [x] Proposed checkpoint: `feat: add collector event and accounting core`; left uncommitted because no explicit commit authorization exists.

---

## Task 2: Windows EVENT_RECORD and TDH adapter

**Files:**

- Create: `include/network_flow_monitor/collector/windows_etw_record_decoder.h`
- Create: `include/network_flow_monitor/collector/windows_process_probe_adapter.h`
- Create: `src/collector/windows_etw_record_decoder.cpp`
- Create: `src/collector/windows_process_probe_adapter.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/windows_etw_record_decoder_tests.cpp`
- Modify: `tests/CMakeLists.txt`

### Step 1: Register adapter tests before implementation

- [x] Build synthetic `EVENT_RECORD` values for the network provider/version/opcode matrix, FILETIME epoch/overflow, unknown/synthetic providers, and truncated payloads.
- [x] Inject a fake process-property reader for Start/End/DCStart mapping, 4/8-byte process key, absent key, invalid PID length, image bound/encoding, and TDH failure.
- [x] Assert the fake records never request `UserSID` or `CommandLine`.
- [x] Run formatted Fast and obtain the expected missing-header C1083 red result.

### Step 2: Implement timestamp/provider adaptation

- [x] Compare against the official classic TCP/IP, UDP/IP, Process, and synthetic EventTrace provider GUID values through one centralized definition; the installed SDK does not export the first three planned symbolic names.
- [x] Convert default `ProcessTrace` timestamp using a named `LARGE_INTEGER`/FILETIME helper and reject pre-Unix/overflow representations consistently with `FileTimeToTraceTimePoint`.
- [x] Forward network records to Task 1's pure decoder with a bounded `std::span<const std::byte>`.

### Step 3: Implement bounded process TDH reads

- [x] Use `TdhGetPropertySize`/`TdhGetProperty` only for the three approved top-level names. Reuse bounded buffers; never query all process fields.
- [x] Accept process key width 4/8, PID width 4, optional key, and optional image hint. Convert legacy image bytes to UTF-8 through a bounded wide conversion.
- [x] Map Start/End/DCStart exactly, ignore DCEnd/Defunct, and never fall back to the event-header PID.
- [x] Add the production `ProcessProbe` adapter around `ProbeWindowsProcess`, retaining its classified error/stage/code without exposing `FILETIME` in neutral Collector headers.

### Step 4: Verify Task 2

- [x] Run Format, Fast, and Full. Debug/Release CTest 13/13 and all gates passed.
- [x] Inspect `dumpbin /dependents` for test/adapter artifacts and record the new system dependency separately from the still-unlinked product EXE.
- [x] Proposed checkpoint: `feat: decode windows network and process events`; left uncommitted because no explicit commit authorization exists.

---

## Task 3: RAII real-time system logger and health queries

**Files:**

- Create: `include/network_flow_monitor/collector/windows_etw_session.h`
- Create: `src/collector/windows_etw_session.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/windows_etw_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`

### Step 1: Register lifecycle tests against an injected ETW API table

- [x] Cover StartTrace failure, OpenTrace failure after controller start, worker creation failure seam, ProcessTrace failure, query/flush failure, CloseTrace pending-success classification, stop failure, repeated Stop, destructor cleanup, and callback exception flags.
- [x] Cover `ERROR_ALREADY_EXISTS` returning conflict without ControlTrace STOP, plus explicit recovery performing STOP only when the caller selects that policy.
- [x] Cover property-buffer offsets, fixed production name/GUID, system/real-time flags, QPC clock, 64 KiB buffer request, process/network flags, EventRecordCallback context, and no raw timestamp.
- [x] Format and run Fast for the expected missing-header red result.

### Step 2: Implement controller/consumer RAII

- [x] Keep controller and processing handles in separate private ownership slots of the non-copyable/non-movable RAII session; their close functions are not interchangeable.
- [x] Allocate one bounded properties buffer containing the session name and no log file name. Start the consumer immediately after the controller.
- [x] Launch a `std::jthread` that calls `ProcessTrace`; callbacks retrieve the session object through `EVENT_RECORD.UserContext`.
- [x] Implement idempotent Stop in the documented order and ensure partial construction cleans up in reverse order.

### Step 3: Implement callbacks and statistics

- [x] Event callback classifies/decodes and passes events to `CollectorCore`; catch all exceptions within the callback and set a preallocated fatal slot.
- [x] Buffer callback records its last event time, loss indication, and returns false after stop/fatal request.
- [x] `FlushAndQuery` uses `ControlTraceW` with FLUSH and updates ETW properties; expose actual `NumberOfBuffers`, buffer size, and loss values for diagnostics.
- [x] Do not add an automatic restart loop or silently steal an existing session.

### Step 4: Verify Task 3

- [x] Run Format, Fast, and Full. Debug/Release CTest 14/14 and all gates passed.
- [x] Proposed checkpoint: `feat: own realtime system etw session`; left uncommitted because no explicit commit authorization exists.

---

## Task 4: Mandatory Windows 11 live Collector integration

**Files:**

- Create: `tests/etw_collector_integration_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `tools/quality.ps1`

### Step 1: Add an administrator-context integration target

- [x] Register `collector.etw_integration` with a clear label and serialized execution so concurrent tests cannot contend for the fixed system session.
- [x] Update Full mode to execute it and fail with a direct elevation message on `ERROR_ACCESS_DENIED`; Fast may run only pure/fake-session tests.
- [x] Add a child mode to the test executable for short-lived traffic generation without introducing a production command-line verb.

### Step 2: Verify process Rundown and controlled traffic

- [x] Start the Collector, flush/query it, and require DCStart PID 4 with `System` image hint on the target host.
- [x] Create local TCP and UDP servers/clients for IPv4 and IPv6. Send deterministic payloads, flush, stop/join, then inspect the snapshot by application ID.
- [x] Verify payload PID attribution, both directions, both protocols/families, and no dependency on event-header PID.
- [x] Launch a short-lived child sender and verify its retained process instance receives the bytes after exit.

### Step 3: Verify burst and loss visibility

- [x] Send a bounded high-rate burst without asserting invented throughput. If ETW reports no loss, require the expected confirmed payload minimum; if it reports loss, require `Degraded` plus nonzero loss/unmatched evidence.
- [x] Query and record actual event versions/opcodes, buffer size/count, loss values, and test duration. Do not accept silent undercount.
- [x] Stop the Collector and verify the named ETW session no longer exists.

### Step 4: Verify Task 4

- [x] Run Format, Fast, and Full from the required administrator context. Fast excludes the serialized `requires-admin` label and passes 14/14; Full runs Debug/Release CTest 15/15, including live integration, and all mandatory gates.
- [x] Record measured behavior without converting it into a product threshold.
- [x] Proposed checkpoint: `test: verify live windows etw collection`; left uncommitted because no explicit commit authorization exists.

---

## Task 5: Milestone 3 documentation, audit, and final gate

**Files:**

- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/configuration.md` if flush semantics need clarification
- Modify: `docs/planning/findings.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`
- Modify: `docs/superpowers/plans/2026-08-11-etw-collector.md`

### Step 1: Audit every specification requirement

- [x] Map specification section 10 to named unit/integration tests or explicit invariants. Specifically verify schema rejection, payload PID, all protocol/family/direction cases, process Rundown, PID reuse, unattributed bytes, privacy exclusions, loss health, partial cleanup, and required elevation.
- [x] Correct aggregate-green coverage gaps before closing the implementation milestone: Process v3/v4 schema handling, PID 4 Rundown confirmation, short-lived application-bucket retention, per-version schema diagnostics, live duration, and controlled non-admin denial are now covered.

### Step 2: Synchronize architecture and measured facts

- [x] Document the custom system logger, hybrid decoder, byte semantics, flush behavior, health/loss reporting, and lack of perfect interface-byte equivalence.
- [x] Record actual buffer/version/dependency/size observations and preserve the fact that the product EXE does not yet link `nfm_collector`.

### Step 3: Run final repository verification

- [x] Run Format, Fast, and Full through `tools/quality.ps1`.
- [x] Run `git diff --check`, `git status --short`, ignore checks for build/local state, and navigational Markdown validation outside fenced implementation examples.
- [x] Require Debug/Release CTest 15/15 and a non-skipped live integration test on the available Windows 11 25H2 host. Exact 24H2 acceptance remains explicitly assigned to the Phase 5 target-host matrix.

### Step 4: Close Milestone 3

- [x] Mark exactly one next item in progress: secure versioned IPC design and implementation planning.
- [x] Proposed milestone checkpoint: `feat: complete realtime etw collector`; left uncommitted because no explicit commit authorization exists.
