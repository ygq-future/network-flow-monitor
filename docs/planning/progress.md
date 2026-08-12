# Progress Log

## Session: 2026-08-10

### Phase 1: Product design and specification
- **Status:** complete
- **Started:** 2026-08-10
- Actions taken:
  - Inspected the workspace; it is empty and is not a Git repository.
  - Confirmed Windows 11-only support.
  - Compared portable elevation and Windows service collection modes.
  - Selected a dual-mode single-executable architecture.
  - Removed taskbar embedding after identifying Win11 compatibility and existing TrafficMonitor conflicts.
  - Confirmed real-time ranking and multi-process application aggregation.
  - Verified the relevant ETW event capabilities using Microsoft documentation.
  - Compared C++, Rust, and C# Native AOT; selected C++20/Win32.
  - Began persistent planning records with the `planning-with-files` workflow.
  - Checked the local compiler environment: Visual Studio 18 Community, MSVC 14.51.36231, bundled CMake/Ninja, and Windows SDK 10.0.26100.0 are installed; ordinary PowerShell has simply not loaded the developer `PATH`.
  - Received approval for the consolidated architecture with a correction: expose configurable timing and Top N, combine snapshot and visible-data refresh into one setting, and manage service lifecycle through the UI.
  - Wrote and self-reviewed the canonical design specification.
  - User approved the revised written specification and authorized Git and project initialization.
- Files created/modified:
  - `task_plan.md` (created)
  - `findings.md` (created)
  - `progress.md` (created)
  - `findings.md` (updated with current MSVC/SDK and local-environment findings)
  - `docs/superpowers/specs/2026-08-10-network-flow-monitor-design.md` (created)

### Phase 2: Repository documentation and constraints
- **Status:** complete
- Actions taken:
  - Wrote the project overview, development setup, architecture, configuration model, agent constraints, and five ADRs.
  - Verified there are no unresolved placeholders and all relative Markdown links resolve.
  - Began moving persistent planning records to `docs/planning/` and adding mandatory C++ quality gates per user review.
  - Moved all three persistent planning records from the repository root to `docs/planning/`.
  - Added `.clang-format`, `.clang-tidy`, and `.editorconfig` at the repository root for automatic tool discovery.
  - Confirmed the installed clang-format can parse `.clang-format` and `clang-tidy --verify-config` reports no errors.
  - Added the code-quality policy and ADR 0006; updated architecture, development setup, formal specification, README, AGENTS, and Git ignore rules.
  - Expanded AI-tool ignores to cover Cursor, Windsurf, Trae, Continue, Claude, Codex, Gemini, Kiro, Junie, Augment, Codeium, Tabnine, Qodo, Amazon Q, Cline, Roo, and Aider local state.
  - Re-ran the written-spec self-review after the requested changes; root layout, planning references, relative links, placeholder scan, quality configuration, and ignore coverage passed.
- Files created/modified:
  - `.gitignore`
  - `README.md`
  - `AGENTS.md`
  - `docs/development-setup.md`
  - `docs/architecture.md`
  - `docs/configuration.md`
  - `docs/decisions/README.md`
  - `docs/decisions/0001-native-cpp-win32.md`
  - `docs/decisions/0002-dual-mode-collector.md`
  - `docs/decisions/0003-etw-per-application-accounting.md`
  - `docs/decisions/0004-tray-and-floating-ui.md`
  - `docs/decisions/0005-client-owned-ranking-configuration.md`
  - `docs/decisions/0006-mandatory-code-quality-gate.md`
  - `docs/code-quality.md`
  - `.clang-format`
  - `.clang-tidy`
  - `.editorconfig`

### Phase 3: Implementation planning
- **Status:** complete
- Actions taken:
  - Invoked the `writing-plans` workflow before touching code.
  - Scoped the first plan to a testable project bootstrap rather than mixing ETW, IPC, service, and UI implementations into one oversized plan.
  - Verified local bootstrap tools: CMake 4.3.1-msvc1, Ninja 1.13.2, and Git 2.47.0.windows.1.
  - Wrote `docs/superpowers/plans/2026-08-10-project-bootstrap.md` with 3 tasks and 30 testable steps.
  - Self-reviewed the plan for spec coverage, placeholders, type/signature consistency, task sequence, and Markdown fence balance.
  - Confirmed the only naive relative-link scan result is a fenced README snippet whose path is correct after insertion into README.
  - User chose the sequential subagent-driven execution option and authorized the bootstrap plan.
- Files created/modified:
  - `docs/planning/task_plan.md`
  - `docs/planning/findings.md`
  - `docs/planning/progress.md`
  - `docs/superpowers/plans/2026-08-10-project-bootstrap.md`

### Phase 4: Project bootstrap implementation
- **Status:** complete
- **Started:** 2026-08-10
- Actions taken:
  - Began Task 1 of the approved bootstrap plan using a fresh implementation subagent followed by primary-agent review.
  - Kept planning-file edits and Git commits under primary-agent control.
  - The implementation subagent reported that its session exposed no filesystem or command tools and made no changes; continued Task 1 in the primary session.
  - Initialized Git on branch `main` after explicit user authorization and verified the existing global commit identity without modifying it.
  - Created the C++20/CMake/CTest project skeleton, strict first-party compiler options, optional static-analysis integration, and unified PowerShell quality runner.
  - Committed the verified Task 1 baseline as `3957a9a build: initialize native Windows project`.
  - Added runtime-mode tests before implementation and verified the intended LNK2019/LNK1120 missing-definition failure.
  - Implemented the bounded runtime-mode parser and allocation-free Win32 command-line dispatch for UI, collector, service, install, uninstall, and service-control modes.
  - Runtime-mode quality gate: Pass
    - Command: `pwsh -NoProfile -File tools/quality.ps1 -Mode Full`
    - clang-format: Pass
    - MSVC Debug `/W4 /WX /permissive- /sdl`: Pass
    - CTest Debug: 2/2 passed
    - clang-tidy and Clang Static Analyzer: Pass
    - MSVC `/analyze`: Pass
    - MSVC Release x64: Pass
    - CTest Release: 2/2 passed
  - Bootstrap implementation checkpoint:
    - `3957a9a build: initialize native Windows project`
    - `ff37e1b feat: add runtime mode dispatch`
    - Verified command: `pwsh -NoProfile -File tools/quality.ps1 -Mode Full`
    - Debug CTest: 2/2 passed
    - Release CTest: 2/2 passed
    - clang-format, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Debug, and Release: Pass
  - Bootstrap source/build-tool files created or modified:
    - `.clang-format`
    - `CMakeLists.txt`
    - `CMakePresets.json`
    - `cmake/ProjectOptions.cmake`
    - `cmake/StaticAnalysis.cmake`
    - `include/network_flow_monitor/app/runtime_mode.h`
    - `src/CMakeLists.txt`
    - `src/app/main.cpp`
    - `src/app/runtime_mode.cpp`
    - `tests/CMakeLists.txt`
    - `tests/bootstrap_tests.cpp`
    - `tests/runtime_mode_tests.cpp`
    - `tools/quality.ps1`
  - Synchronized README and development setup with the verified presets, tests, and quality modes.
  - Corrected the implementation plan's Markdown-link validator and completed all bootstrap-plan steps.
  - Documentation-sync quality gate: Pass
    - Command: `pwsh -NoProfile -File tools/quality.ps1 -Mode Full`
    - clang-format: Pass
    - MSVC Debug `/W4 /WX /permissive- /sdl`: Pass
    - CTest Debug: 2/2 passed
    - clang-tidy and Clang Static Analyzer: Pass
    - MSVC `/analyze`: Pass
    - MSVC Release x64: Pass
    - CTest Release: 2/2 passed
  - Project bootstrap quality gate: Pass
    - Command: `pwsh -NoProfile -File tools/quality.ps1 -Mode Full`
    - clang-format: Pass
    - MSVC Debug `/W4 /WX /permissive- /sdl`: Pass
    - CTest Debug `bootstrap.toolchain`: 1/1 passed
    - clang-tidy and Clang Static Analyzer: Pass
    - MSVC `/analyze`: Pass
    - MSVC Release x64: Pass
    - CTest Release `bootstrap.toolchain`: 1/1 passed
  - Created verified Git history checkpoints:
    - `3957a9a build: initialize native Windows project`
    - `ff37e1b feat: add runtime mode dispatch`
    - `af63968 docs: record project bootstrap workflow`
- Files created/modified:
  - `.git/` (repository metadata, not tracked)
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `cmake/ProjectOptions.cmake`
  - `cmake/StaticAnalysis.cmake`
  - `src/CMakeLists.txt`
  - `src/app/main.cpp`
  - `tests/CMakeLists.txt`
  - `tests/bootstrap_tests.cpp`
  - `tools/quality.ps1`
  - `docs/planning/task_plan.md`
  - `docs/planning/progress.md`

### Phase 4.1: Execution queue correction
- **Status:** complete
- **Started:** 2026-08-10
- Actions taken:
  - Corrected the stale bootstrap status from `in_progress` to `complete`.
  - Replaced the single coarse implementation item with an ordered, dependency-aware queue covering configuration/ranking, process identity, ETW, IPC, dual collection modes, native UI, settings/service management, and integrated delivery.
  - Added explicit milestone deliverables and completion gates so a status is based on artifacts and verification rather than a guessed percentage.
  - Recorded that work cannot continue in the background after a response ends; `ready`, `in_progress`, and `complete` are distinct states.
  - Set Milestone 1 (versioned configuration plus ranking core) as the next ready subsystem because it is required by later UI/IPC work and can be exhaustively unit tested without privilege or ETW dependencies.
- Files modified:
  - `docs/planning/task_plan.md`
  - `docs/planning/progress.md`

### Phase 4.2: Milestone 1 design boundary
- **Status:** complete
- **Started:** 2026-08-10
- Actions taken:
  - User authorized Codex to lead the remaining project implementation and make ordinary technical sequencing decisions.
  - Preserved the explicit project rule that dependency additions and Git commits require their own authorization boundaries.
  - Re-read the approved configuration/ranking ADR and configuration specification.
  - Identified the only unresolved Milestone 1 boundary: the JSON implementation used for validated, versioned configuration persistence.
  - Compared a small static C JSON library, a template-heavy C++ header library, and a custom restricted parser using official project documentation.
  - Recommended vendoring a pinned yyjson release as first-party-isolated third-party source, with its MIT license and version recorded; no runtime DLL or configure-time network fetch.
  - User approved the recommended yyjson approach.
  - Verified yyjson 0.12.0 is the current upstream release and selected it as the pinned version; executable-size impact remains a measurement task, not an assumed fact.
  - Added ADR 0007 and the focused Milestone 1 configuration/ranking design specification.
  - During self-review, removed an unjustified cross-field rule that would have required the ranking window to exceed the refresh interval.
  - During self-review, moved multi-process application merging to a monotonic `ApplicationAccumulator`; the ranking engine now rejects ambiguous duplicate IDs instead of summing process snapshots that can decrease after process exit.
  - Self-review passed for placeholder scan, navigational Markdown links, scope, and whitespace.
  - Did not create a Git commit because the project rule requires explicit commit authorization; this overrides the brainstorming workflow's default commit step.
  - User reviewed and approved the written Milestone 1 specification.
- Files modified:
  - `docs/decisions/0007-vendored-yyjson.md`
  - `docs/decisions/README.md`
  - `docs/superpowers/specs/2026-08-10-config-ranking-design.md`
  - `docs/planning/findings.md`
  - `docs/planning/progress.md`

### Phase 4.3: Milestone 1 implementation planning
- **Status:** complete
- **Started:** 2026-08-10
- Actions taken:
  - Used the `writing-plans` workflow after written-spec approval.
  - Split Milestone 1 into two independently testable plans: configuration core and ranking core.
  - Defined exact source/header/test/CMake boundaries, public signatures, red/green TDD steps, quality commands, conditional commit checkpoints, and documentation completion gates.
  - Recorded exact yyjson 0.12.0 SHA-256 values for `yyjson.h`, `yyjson.c`, and `LICENSE` from the approved tag.
  - Verified from Microsoft documentation that `REPLACEFILE_WRITE_THROUGH` is unsupported; the plan flushes the temporary file and calls `ReplaceFileW` with flags `0`, while first creation uses supported `MOVEFILE_WRITE_THROUGH`.
  - Self-review found and fixed a storage-scope ambiguity: codecs and stores now require `ConfigDocumentKind::User` or `Service`, preventing personal settings from being written into the machine service document.
  - Self-review found and fixed CMake sequencing: `nfm_config` is created only when its real Task 2 sources exist.
  - Plan self-review passed: 52 total checked steps defined, code fences balanced, no forbidden placeholders, required interface names consistent, and `git diff --check` clean.
- Files created:
  - `docs/superpowers/plans/2026-08-10-config-core.md`
  - `docs/superpowers/plans/2026-08-10-ranking-core.md`
- Files modified:
  - `docs/superpowers/specs/2026-08-10-config-ranking-design.md`
  - `docs/planning/task_plan.md`
  - `docs/planning/findings.md`
  - `docs/planning/progress.md`

### Phase 4.4: Milestone 1 implementation
- **Status:** complete
- **Started:** 2026-08-10
- Actions taken:
  - User selected current-session sequential execution and authorized Codex to continue through all project tasks unless a genuinely required product decision blocks progress.
  - Reviewed both Milestone 1 plans and found no blocking ambiguity after the scope corrections recorded above.
  - Created branch `feature/config-ranking` so implementation does not proceed directly on `main`; the unavailable worktree skill was not required because this checkout is not being used concurrently by the user.
  - Kept all Git commit steps conditional because the explicit repository commit-authorization rule remains in force.
  - Recorded the pre-yyjson Full-gate Release executable baseline as 111,616 bytes.
- Current task:
  - Process identity design specification and implementation plan.
- Completed task checkpoints:
  - Configuration core Task 1: complete
    - Red phase: expected C1083 for missing generated dependency header.
    - Vendored yyjson 0.12.0 source and MIT license hashes matched ADR 0007.
    - `nfm_yyjson` builds as an isolated static C target; `dumpbin /dependents` showed only `SHELL32.dll`, `USER32.dll`, and `KERNEL32.dll`.
    - Fast quality gate: Pass; Debug CTest 2/2.
    - Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, Debug/Release CTest 2/2.
    - Proposed checkpoint `build: vendor pinned yyjson dependency` was not committed because no explicit commit authorization exists.
  - Configuration core Task 2: complete
    - Red phase: expected C1083 for the missing `app_config.h` API.
    - Added the complete typed schema-1 `AppConfig`, documented defaults and named numeric bounds, and non-mutating centralized validation.
    - Fast quality gate: Pass; Debug CTest 3/3.
    - Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, Debug/Release CTest 3/3.
    - Proposed checkpoint `feat: add typed application configuration` was not committed because no explicit commit authorization exists.
  - Configuration core Task 3: complete
    - Red phase: expected C1083 for the missing `config_codec.h` API.
    - Added strict RFC 8259/UTF-8 parsing, duplicate/unknown/type/range/enum rejection, schema version states, separate user/service scopes, missing-field defaults, and deterministic two-space JSON serialization.
    - yyjson document and writer buffers use translation-unit-local RAII; public headers remain dependency-neutral.
    - Fast quality gate: Pass; Debug CTest 4/4.
    - Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, Debug/Release CTest 4/4.
    - Proposed checkpoint `feat: add strict versioned config codec` was not committed because no explicit commit authorization exists.
  - Configuration core Task 4: complete
    - Red phase: expected C1083 for the missing `config_paths.h` API.
    - Added pure portable/installed/service path composition and bounded Win32 loading with a 1 MiB limit.
    - Added same-directory temporary-file saves with flush-before-close and atomic `ReplaceFileW`/`MoveFileExW` publication; failed replacement preserves the original and removes only the operation's temporary file.
    - Tests cover paths, missing/damaged/oversized files, first save, replacement save, and a locked-target replacement failure.
    - Fast quality gate: Pass; Debug CTest 5/5.
    - Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, Debug/Release CTest 5/5.
    - Proposed checkpoint `feat: persist configuration atomically` was not committed because no explicit commit authorization exists.
  - Configuration core Task 5 measurement:
    - Command: `(Get-Item build/windows-x64-release/src/NetworkFlowMonitor.exe).Length`.
    - Pre-integration baseline: 111,616 bytes; current Release executable: 111,616 bytes; measured delta: 0 bytes.
    - This is expected, not a demonstrated size optimization: `NetworkFlowMonitor.exe` currently links `nfm_runtime` but not `nfm_config`, so the static yyjson/configuration implementation is not yet reachable from the executable link graph.
  - Configuration core Task 5: complete
    - Documented strict schema-1 parsing, safe missing-field defaults, user/service scope separation, 1 MiB load bound, explicit path semantics, and atomic save behavior.
    - Navigational Markdown link scan and `git diff --check`: Pass; no generated build output appears in Git status.
    - Final subsystem Full quality gate: Pass; Debug and Release CTest 5/5, clang-format, strict MSVC Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, and Release x64 all passed.
    - Proposed subsystem checkpoint `feat: complete versioned configuration core` remains uncommitted because no explicit commit authorization exists.
  - Ranking core Task 1: complete
    - Red phase: expected C1083 for the missing `application_accumulator.h` API.
    - Added opaque application IDs, cumulative upload/download counter snapshots, and a pure `ApplicationAccumulator` that merges repeated IDs before ranking.
    - Empty IDs and overflow are rejected without mutation; zero-byte inputs are no-ops; metadata is enriched deterministically, conflicts are counted saturatingly, and snapshots are sorted by ID.
    - Fast quality gate: Pass; Debug CTest 6/6.
    - Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, Debug/Release CTest 6/6.
    - Proposed checkpoint `feat: add monotonic application accumulator` was not committed because no explicit commit authorization exists.
  - Ranking core Task 2: complete
    - Red phase: expected C1083 for the missing `ranking_engine.h` API.
    - Added validated factory construction, strictly increasing snapshot admission, duplicate/empty-ID rejection, bounded boundary-predecessor history, explicit warm-up, and actual-span byte-per-second rates.
    - Applications first observed after the global baseline require two samples and use their own first-to-current span; rejected snapshots leave subsequent results identical to a clean engine.
    - Fast quality gate: Pass; Debug CTest 7/7.
    - Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, Debug/Release CTest 7/7.
    - Proposed checkpoint `feat: calculate sliding-window application rates` was not committed because no explicit commit authorization exists.
  - Ranking core Task 3: complete
    - Red phase: expected C2039 errors for the missing `RankingEngine::UpdateOptions` API.
    - Added per-application/per-direction counter epochs so reset or wrap suppresses only the affected direction once and later samples resume from the new epoch.
    - Finalized full-system totals, zero-rate filtering, deterministic four-key ordering, Top N truncation, application disappearance handling, and finite-rate checks with state-preserving candidate history.
    - Valid option updates preserve reusable history; retention follows the largest window observed during the engine lifetime, bounded by the validated 10-second public maximum.
    - Fast quality gate: Pass; Debug CTest 7/7.
    - Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, Debug/Release CTest 7/7.
    - Proposed checkpoint `feat: finalize deterministic top application ranking` was not committed because no explicit commit authorization exists.
  - Ranking core Task 4: complete
    - Final self-review found and fixed an untested warm-up counter-reset case: discontinuities are now observed on every accepted snapshot, so a reset before the ranking window becomes ready establishes the correct per-direction epoch.
    - Added a regression that resets upload at 500 ms during a 2-second warm-up and verifies recovery uses the 1.5-second post-reset span without double-counting the diagnostic.
    - Repository hygiene and navigational Markdown checks passed; build output and `.superpowers/` remain ignored and no unrelated files were introduced.
    - Final Full quality gate: Pass; format, strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, and Debug/Release CTest 7/7.
    - Milestone 1 is complete. Proposed checkpoint `feat: complete configuration and ranking core` remains uncommitted because no explicit commit authorization exists.

### Phase 4.5: Process identity design
- **Status:** complete
- **Started:** 2026-08-10
- Actions taken:
  - Began validating the process-lifetime, PID-reuse, package identity, executable-path, access-denied, and cache-expiry boundaries against documented Windows 11 APIs before implementation.
  - Confirmed from Microsoft documentation that PID values are lifetime-scoped and reused, kernel process start events carry the actual payload PID and a process key, and `ProcessTrace` delivers events chronologically.
  - Confirmed the least-rights query surface: `GetProcessTimes`, `QueryFullProcessImageNameW`, `GetPackageFamilyName`, and `GetApplicationUserModelId` accept `PROCESS_QUERY_LIMITED_INFORMATION`, while access failure remains expected for protected or short-lived processes.
  - Distinguished AUMID application identity from package-family identity; selected AUMID-first grouping so packages containing multiple applications are not merged.
  - Rejected long-lived handle ownership, PID-plus-filename grouping, and internal `ZwQueryInformationProcess` sequence queries in favor of event-time intervals plus generation and documented Win32 probes.
  - Added ADR 0008 and the process-identity design specification. Self-review corrected a synchronous/asynchronous wording contradiction and made PID 4 system classification conditional on an ETW `System` image hint.
  - Used the user's standing authorization for recommended technical choices, then wrote and self-reviewed the four-task TDD implementation plan without creating a commit.
  - Placeholder, type-consistency, whitespace, navigational Markdown link, ignored-output, and working-tree hygiene checks passed.

### Phase 4.6: Process identity implementation
- **Status:** complete
- **Started:** 2026-08-11
- Current task:
  - Milestone 2 closed; ETW Collector design is the next active item.
- Completed task checkpoints:
  - Process identity Task 1: complete
    - Red phase: expected C1083 for the missing `identity_resolver.h` API after the shared identity values were extracted successfully.
    - Moved `ApplicationId` and `ApplicationMetadata` to the process boundary without changing accumulator/ranking behavior.
    - Added strong 100ns `TraceTimePoint`, generation-based `ProcessInstanceId`, deterministic AUMID/package-family/path/system/unresolved identity priority, and structural Windows path normalization.
    - Same executable paths merge across processes; same filenames in different paths remain separate; invalid or relative paths return a diagnostic rather than falling back to a false identity.
    - Fast quality gate: Pass; Debug CTest 8/8.
    - Full quality gate: Pass; strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, and Debug/Release CTest 8/8.
    - Proposed checkpoint `feat: define deterministic application identity policy` remains uncommitted because no explicit commit authorization exists.
  - Process identity Task 2: complete
    - Red phase: expected C1083 for the missing `process_tracker.h` API.
    - Added generation-based process instances, active/retired half-open event-time intervals, kernel-key stale-end rejection, implicit PID-reuse retirement, and exact-generation probe application.
    - Retired identities survive 30 seconds for delayed network events, remain capped at 65,536 entries, evict deterministically, and never fall back to a newer process solely because its PID matches.
    - Tests cover duplicate metadata enrichment, reuse boundaries, late events/probes, expiry, capacity, watermark rollback, generation exhaustion, invalid probe preservation, system probe suppression, and saturating diagnostics.
    - Fast quality gate: Pass; Debug CTest 9/9.
    - Full quality gate: Pass; strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, and Debug/Release CTest 9/9.
    - Proposed checkpoint `feat: track process lifetimes across pid reuse` remains uncommitted because no explicit commit authorization exists.
  - Process identity Task 3: complete
    - Red phase: expected C1083 for the missing `windows_process_probe.h` API.
    - Added limited-rights, non-inheritable process opening; RAII handle ownership; FILETIME conversion; bounded AppModel/image queries; strict UTF-16-to-UTF-8 conversion; and classified access, exit, reuse, length, encoding, allocation, and Win32 errors.
    - Corrected a design gap found during end-to-end review: both Start and Rundown reject a creation time later than the ETW observation, because either can race PID reuse.
    - Integration tests cover the current process, exact FILETIME epoch, tracker handoff by generation, a short-lived child, and a controlled suspended child with an empty DACL while `SeDebugPrivilege` is disabled and restored.
    - Updated clang-tidy orchestration so only targets containing exception boundaries receive an analysis-only `/EHsc`; production compile flags remain unchanged and other targets do not gain unrelated new diagnostics.
    - Changed every quality build to `--clean-first` after an incremental tidy directory masked the shared-identity ABI change until only one side rebuilt.
    - Fast quality gate: Pass; clean Debug CTest 10/10.
    - Full quality gate: Pass; clean strict Debug, clang-tidy/Clang analyzer, MSVC `/analyze`, Release x64, and clean Debug/Release CTest 10/10.
    - `dumpbin /dependents` still reports `SHELL32.dll`, `USER32.dll`, and `KERNEL32.dll`; Release executable remains 111,616 bytes because `NetworkFlowMonitor.exe` does not yet link `nfm_process`.
    - Proposed checkpoint `feat: probe windows application identity safely` remains uncommitted because no explicit commit authorization exists.
  - Process identity Task 4 specification audit: complete
    - The initial audit found two missing explicit assertions despite 10/10 aggregate tests: end-then-start PID reuse and separation of different AUMIDs. Both cases were added, formatted, and passed the clean Fast gate.
    - Architecture and user-facing status now describe the implemented generation/time-interval model, bounded retired cache, exact identity priority, limited-rights probe, host/unresolved behavior, and privacy boundary without claiming perfect attribution.
  - Process identity Task 4 final verification: complete
    - Format and clean Fast gates passed after adding the two audit assertions; clean Debug CTest remained 10/10.
    - Final Full gate passed from clean objects: strict Debug, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Release x64, and Debug/Release CTest 10/10.
    - `dumpbin /dependents` reports only `SHELL32.dll`, `USER32.dll`, and `KERNEL32.dll`; the Release executable is 111,616 bytes because `nfm_process` is still a static library not reachable from the executable.
    - `git diff --check`, navigational Markdown links, and ignore checks for `build` and `.superpowers` passed. The intended Milestone 1 and 2 working tree remains uncommitted.
    - Milestone 2 is complete. Proposed checkpoint `feat: complete process identity core` remains uncommitted because no explicit commit authorization exists.

### Process identity specification audit

| Section 10 requirement | Evidence | Result |
|---|---|---|
| Start, end, duplicate start, explicit and implicit PID reuse | `CheckBasicLifecycle`, `CheckDuplicateStartAndStaleEnd`, `CheckExplicitPidReuse`, and `CheckPidReuseBoundaries` in `process_tracker_tests.cpp` | Covered |
| Stale end cannot close a different instance | `CheckDuplicateStartAndStaleEnd` uses a mismatched kernel key and verifies the active instance remains resolvable | Covered |
| Old/new event-time boundary and no PID-only fallback | `CheckPidReuseBoundaries` verifies 9 ms/10 ms ownership; `CheckExplicitPidReuse` verifies the ended-to-start gap returns no instance | Covered |
| Late event, 30-second TTL, capacity eviction, watermark rollback | `CheckBasicLifecycle`, `CheckRetiredExpiryAndStaleProbe`, and `CheckCapacityGenerationAndSaturation` | Covered |
| Stale probe cannot overwrite a reused PID | `CheckPidReuseBoundaries` applies late data to the exact old generation and verifies the new generation remains unresolved; expiry rejects later application | Covered |
| AUMID, package family, path, system, unresolved identity priority | `CheckIdentityPriority` and `CheckSystemAndUnresolvedIdentity` | Covered |
| Same path merges; same filename/different path and different AUMIDs separate | `CheckPathIdentitySeparation`, including the audit-added different-AUMID assertion | Covered |
| Extended DOS/UNC normalization and invalid path rejection | `CheckPathNormalization` covers extended forms, relative, empty, and embedded-NUL input | Covered |
| Generation and diagnostics never wrap | `CheckCapacityGenerationAndSaturation` tests generation exhaustion and a saturated counter; all six production diagnostic increments call the single `IncrementSaturating` invariant | Covered |
| Limited-rights current-process probe, creation time, absolute UTF-8 image, ordinary no-package state | `CheckCurrentProcessProbe` and `CheckFileTimeEpochConversion` in `windows_process_probe_tests.cpp` | Covered |
| Controlled short-lived and access-denied processes; Start/Rundown reuse check; tracker handoff | `CheckInvalidAndExitedProcess`, `CheckAccessDeniedProcess`, and the current-process Start/Rundown and exact-generation application assertions | Covered |
| RAII, bounded strings, exception boundary, and privacy exclusions | `UniqueProcessHandle`, SDK/32,768 limits, strict `WideCharToMultiByte`, and `ProbeWindowsProcess` catch boundary are code invariants; public process models contain no command line, SID, remote address, or payload fields | Covered |

### Phase 4.7: ETW Collector design
- **Status:** complete
- **Started:** 2026-08-11
- Current task:
  - Validate the Windows 11 system-logger, classic TCP/UDP/process schema, timestamp, buffer-loss, callback, and shutdown boundaries against Microsoft documentation.
  - Write ADR 0009, a focused Collector design specification, and a task-by-task TDD implementation plan before adding Collector code.
- Confirmed so far:
  - A custom-named `EVENT_TRACE_SYSTEM_LOGGER_MODE` session is the supported non-deprecated way to multiplex SystemTraceProvider events; the legacy NT Kernel Logger name and control GUID would create avoidable conflicts.
  - TCP and UDP version-2 IPv4/IPv6 event schemas put payload PID and size first; event-header PID is not reliable for attribution. Process Start/End/DCStart payloads similarly carry the actual PID and a pointer-sized process key.
  - `OpenTraceW` should use real-time plus event-record mode and omit raw-timestamp mode, allowing `ProcessTrace` to convert the QPC session clock to FILETIME-form system time before the existing named conversion.
  - ETW can lose producer events or real-time buffers; the Collector must surface monotonic loss statistics and degraded health rather than silently presenting counters as complete.
  - ADR 0009 selects a custom-named/GUID real-time system logger, QPC-to-FILETIME consumer timestamps, hybrid direct/TDH decoding, explicit loss health, and non-stealing session ownership.
  - The focused design specification defines byte semantics, unattributed accounting, lifecycle/error/flush behavior, privacy limits, and mandatory administrator-context live integration coverage.
  - The five-task TDD implementation plan was self-reviewed for placeholders, schema/version consistency, test counts, fenced Markdown links, planning location, ignored output, and whitespace; all checks passed.

### Phase 4.8: ETW Collector implementation
- **Status:** complete on Windows 11 25H2 x64; exact 24H2 acceptance remains in Phase 5
- **Started:** 2026-08-11
- Current task:
  - None; all five implementation tasks are complete.
- Completed task checkpoints:
  - ETW Collector Task 1: complete
    - Red phase: the clean Fast build failed only with the expected C1083 for the missing `network_flow_monitor/collector/network_event_decoder.h`.
    - Added SDK-free traffic/lifecycle event values, version-2 TCP/UDP IPv4/IPv6 direct decoding, explicit ignored/rejected outcomes, classified health/error records, and saturating aggregate/protocol/family/direction diagnostics.
    - Added the owner-thread Collector core with event-time PID attribution, exact-generation process probing, same-application accumulation, fixed unattributed accounting, retransmit diagnostics, loss degradation, and fail-closed accumulator overflow handling.
    - Self-review found and corrected two specification gaps before closure: protocol/IP/direction health breakdowns and first/latest classified error records.
    - Fast quality gate: Pass; clean Debug CTest 12/12 and Clang analysis passed.
    - Full quality gate: Pass; clean strict Debug, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Release x64, and Debug/Release CTest 12/12 passed.
    - Proposed checkpoint `feat: add collector event and accounting core` remains uncommitted because no explicit commit authorization exists.
  - ETW Collector Task 2: complete
    - Red phase: the clean Fast build failed only with the expected C1083 for the missing `network_flow_monitor/collector/windows_etw_record_decoder.h`.
    - Added the Windows `EVENT_RECORD` adapter, centralized classic provider GUIDs, exact FILETIME conversion, full TCP/UDP version-2 matrix forwarding, and ignored-control handling that does not misclassify invalid timestamps.
    - Added an injected three-property process reader and production TDH implementation for only `UniqueProcessKey`, `ProcessId`, and `ImageFileName`; Start/End/DCStart, 4/8-byte keys, absent keys, field bounds, encoding, and TDH failures are covered without event-header PID fallback.
    - Added the Windows process-probe adapter with neutral error mapping plus retained diagnostic domain/stage/code, and moved the reusable 32 KiB image buffer off the worker stack after `/analyze` identified the original stack cost.
    - Fast quality gate: Pass; clean Debug CTest 13/13 and Clang analysis passed.
    - Full quality gate: Pass; clean strict Debug, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Release x64, and Debug/Release CTest 13/13 passed.
    - `dumpbin /dependents` reports `tdh.dll` and `KERNEL32.dll` for the adapter test after exercising the production reader. The still-unlinked product EXE remains 111,616 bytes with `SHELL32.dll`, `USER32.dll`, and `KERNEL32.dll` only.
    - Proposed checkpoint `feat: decode windows network and process events` remains uncommitted because no explicit commit authorization exists.
  - ETW Collector Task 3: complete
    - Red phase: the clean Fast build failed only with the expected C1083 for the missing `network_flow_monitor/collector/windows_etw_session.h`.
    - Added the fixed-name/GUID 64 KiB QPC real-time system logger, separate controller/consumer ownership, injected ETW API seam, `std::jthread` processing worker, reverse partial cleanup, idempotent shutdown, and explicit non-stealing conflict policy with opt-in recovery.
    - Added contained event/buffer callbacks, fatal callback termination, synchronized Collector-core access, flush/query diagnostics, loss degradation, and classified start/open/process/flush/query/stop failures. A review-found race between callback loss reads and query writes was removed with atomic published loss fields.
    - Lifecycle tests cover configuration, start/open/worker/process/flush/query/stop failures, conflict/recovery, pending close, repeated stop, destructor cleanup, callback continuation/termination, statistics, and controller/consumer handle separation.
    - Fast quality gate: Pass; clean Debug CTest 14/14 and Clang analysis passed.
    - Full quality gate: Pass; clean strict Debug, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Release x64, and Debug/Release CTest 14/14 passed.
    - `dumpbin /dependents` reports `tdh.dll`, `KERNEL32.dll`, and the standard synchronization API-set for the session test. The still-unlinked product EXE remains 111,616 bytes with no new production dependency.
    - Proposed checkpoint `feat: own realtime system etw session` remains uncommitted because no explicit commit authorization exists.
  - ETW Collector Task 4: complete on the available Windows 11 25H2 x64 host
    - Red phase: registering `collector.etw_integration` before its source existed produced the expected CMake missing-source failure and no unrelated failure.
    - Added a serialized `requires-admin` live target; Fast excludes it, while Full executes it in both Debug and Release. The test covers PID 4/System Rundown, deterministic TCP/UDP IPv4/IPv6 loopback in both directions, payload-PID application attribution, a short-lived child, a bounded burst, schema/loss/buffer diagnostics, session removal after stop, and a separate non-administrator `AccessDenied` child.
    - Live evidence corrected an invalid design assumption: current process lifecycle Start/End/DCStart events are version 4, not version 2. Official Process documentation also declares version 3. The production TDH adapter and tests now whitelist versions 2/3/4 while rejecting unknown versions; TCP/UDP remain exact version 2 fixed-prefix decoders.
    - The short-lived child can exit before a synchronous Win32 probe succeeds. The correct invariant is retention of its complete byte delta in one application bucket, including an instance-specific unresolved bucket when necessary, rather than forcing the parent's stable executable ID.
    - The permission test cannot assume a linked or non-elevated shell token: this host exposes neither. It creates a `SAFER_LEVELID_NORMALUSER` token, proves the token is restricted and not an Administrators member, then requires ETW start to return classified `AccessDenied`.
    - Final Task 4 Fast gate: Pass; clean Debug CTest 14/14 plus clang-tidy/Clang Static Analyzer passed.
    - Final Task 4 Full gate: Pass in 78.1 seconds; strict Debug, MSVC `/analyze`, Release x64, and Debug/Release CTest 15/15 passed with the live target executed rather than skipped.
    - Debug live measurement: 64 buffers × 64 KiB; TCP/UDP version 2; Process version mask `0x14` with Start/End/DCStart on version 4; zero ETW loss; current application upload/download 1,114,112 bytes each; duration 5,256 ms.
    - Release live measurement: 64 buffers × 64 KiB; the same schema; zero ETW loss; current application upload/download 1,114,112 bytes each; duration 6,294 ms. These are bounded-test observations, not product thresholds.
    - Product EXE remains 111,616 bytes and depends only on Shell32/User32/Kernel32 because runtime dispatch does not yet link `nfm_collector`; the Release integration executable is 352,768 bytes.
    - Exact Windows 11 24H2 behavior is not verified by this 25H2 build 26200 host. That target-host compatibility item remains open for Phase 5 and must not be inferred from the newer-host pass.

### ETW specification coverage audit

| Requirement | Evidence | Result |
|---|---|---|
| TCP/UDP IPv4/IPv6 Send/Receive, retransmit, payload bounds/alignment, unknown schema | `CheckProtocolMatrix`, `CheckIgnoredAndRejected`, and `CheckBoundsAndAlignment` in `network_event_decoder_tests.cpp`; Windows adapter matrix in `CheckNetworkAndTimestampAdaptation` | Covered |
| Event payload PID rather than header PID | Decoder tests assert payload PID; live matrix resolves bytes to the test application while the adapter never forwards header PID | Covered |
| Process Start/End/DCStart, schema whitelist, property bounds, no PID fallback | `CheckProcessMapping`, `CheckProcessFailuresAndProbeAdapter`, and production TDH boundary in `windows_etw_record_decoder_tests.cpp` | Covered |
| PID reuse, ended-instance retention, same-application merge, unattributed bytes | `CheckResolvedMergeAndUnattributed`, `CheckProbeFailureAndReuseBoundary`, process tracker tests, and live short-lived child delta | Covered |
| Loss/reset health, overflow, callback and partial-session cleanup | `CheckEndRetransmitOverflowAndHealth`, `CheckConfigurationLifecycleAndStatistics`, `CheckStartFailuresAndRecovery`, and `CheckRuntimeFailuresAndDestructor` | Covered |
| Live Rundown, protocols/families/directions, bounded burst, stop cleanup | `collector.etw_integration` on Windows 11 25H2 x64 in Debug and Release | Covered on 25H2; 24H2 pending |
| Required elevation and explicit non-admin failure | Admin parent starts the system logger; verified restricted non-Administrators child returns `AccessDenied` | Covered on current host |
| Privacy exclusions | Public neutral models contain no address, port, SID, command line, socket handle, or payload field; TDH fake asserts only the three approved names are requested | Covered by tests/invariant |

  - ETW Collector Task 5: complete
    - Mapped every section 10 requirement to named tests or explicit public-model invariants and corrected all audit-found coverage gaps.
    - Synchronized README, architecture, ADR 0009, design/implementation plans, findings, and persistent progress without presenting 25H2 evidence as a 24H2 result.
    - Final repository sequence `Format -> Fast -> Full` passed in 117.6 seconds. Fast executed 14/14 non-admin Debug tests; Full executed non-skipped Debug/Release 15/15 live tests plus strict MSVC, clang-tidy/Clang Static Analyzer, `/analyze`, and Release x64.
    - Final live logs again reported 64 × 64 KiB buffers, TCP/UDP v2, Process lifecycle v4, zero ETW loss, and 1,114,112 attributed bytes in each direction. Ambient unattributed bytes varied between runs and are diagnostic background, not a test threshold.
    - Navigational Markdown, `git diff --check`, ignored local/build state, dependencies, sizes, and the intended uncommitted working tree were inspected. No commit was created because the user has not explicitly authorized one.
    - Milestone 3 implementation is closed. Windows 11 24H2 x64 compatibility remains an explicit Phase 5 acceptance item rather than an inferred fact.

### Phase 4.9: Secure IPC design
- **Status:** complete
- **Started:** 2026-08-11
- Current task:
  - Execute Task 1 of the approved sequential TDD plan: bound active application accumulation before exposing snapshots over IPC.
- Completed design work:
  - Reviewed official named-pipe DACL, remote access, impersonation, overlapped I/O and cancellation behavior and separated confirmed Windows facts from project choices.
  - Compared byte-mode overlapped named pipes, message-mode/IOCP, synchronous workers, loopback TCP and RPC; selected byte-mode framing with one bounded client session worker per connection and one merged publisher.
  - ADR 0010 fixes owner-scoped portable/service principals, explicit DACL, remote rejection, first-instance behavior, versioned framing, numeric application handles, four-client limit and bounded active application state.
  - `docs/superpowers/specs/2026-08-11-secure-ipc-design.md` defines exact header offsets, message payloads, catalog/snapshot sequencing, security checks, cancellation, slow-client behavior, reconnect classification and test evidence required before closure.
  - Self-review corrected a DACL ambiguity: portable server-instance permission belongs to its concrete logon SID rather than Builtin Administrators, while the authorized UI logon SID receives data rights. It also fixed 4096 capacity accounting at 4094 dynamic plus two fixed buckets and bounded server/client catalog retention.
  - The user approved the written specification. The sequential plan at `docs/superpowers/plans/2026-08-11-secure-ipc.md` maps the milestone into ten independently gated tasks from accumulator bounds through real pipe integration.
  - Plan self-review found and corrected an undefined transport-factory seam and made the stopped-server observer wiring explicit. Placeholder, fence, task-count, type-reference, whitespace and scope scans then passed; no commit was created.

### Phase 4.22: Secure IPC implementation
- **Status:** in_progress
- **Started:** 2026-08-11
- Current task:
  - Milestone 5 Task 1 — service-neutral ETW capture adapter and collector host.
- Completed work:
  - Task 1 introduced a 15-second active horizon, monotonic watermarks, 4094 dynamic application slots, fixed Unattributed/Other buckets, deterministic overflow aggregation and rollback-safe counter overflow handling.
  - Collector traffic now passes ETW observation time into accumulation, advances the same accepted watermark through process and ranking state, preserves global protocol/direction totals for aggregated traffic, and exposes saturating capacity diagnostics that degrade a running collector on the first hit.
  - The intended red build failed only on the missing timestamp/watermark/disposition API, capacity constants and health fields. Focused tests then passed, including exact-expiry equality, fixed-bucket retention, existing-ID updates at capacity, Other overflow rollback, Collector watermark pruning and diagnostic saturation.
  - Final `Format -> Fast -> Full` passed. Fast completed in 36.0 seconds with Debug 14/14 non-admin tests and both Clang analyses; Full completed in 75.7 seconds with Debug/Release 15/15, MSVC `/analyze`, strict builds and Release x64.
  - The 25H2 live ETW tests again reported 64 × 64 KiB buffers, zero loss and 1,114,112 application bytes in each direction. Debug duration was 5,277 ms with zero unmatched bytes; Release duration was 5,244 ms with 1,233 ambient unmatched bytes. These are current-host diagnostics, not 24H2 acceptance evidence.
  - Release product size remains 111,616 bytes because the executable does not yet link the new Collector/IPC path. No Git commit was created without explicit authorization.
  - Task 2 added a platform-neutral 24-byte little-endian frame codec with a 1 MiB payload ceiling, exact streaming `NeedMoreData` accounting, owned decoded payloads, request-ID invariants, direction validation and all five handshake/subscription/error control payloads.
  - Golden and adversarial tests cover all eight message types, zero/maximum payloads, partial and concatenated frames, unaligned input, every fixed header field, control bounds and reserved fields. A final security audit also added `0x0101` regressions so 16-bit wire values are validated before conversion to compact 8-bit native enums.
  - The intended red build failed only for the absent `frame_codec.h`. During implementation, a test helper returned a pointer into a destroyed local variant; focused diagnosis changed it to return an owned `std::optional<Message>`. The first Fast analysis then identified enum-size, invalid-default-enum, unchecked-span, redundant-width and iterator-narrowing issues, all corrected without broad suppressions.
  - Task 2 final `Format -> Fast` completed with exit 0 in 39.9 seconds. Full completed with exit 0 in 79.1 seconds: Debug/Release CTest 16/16, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, strict builds and Release x64 all passed.
  - The 25H2 live ETW regression again used 64 × 64 KiB buffers with zero loss and 1,114,112 attributed bytes per direction. Debug/Release durations were 7,278/4,260 ms with zero unmatched bytes. The release product remains 111,616 bytes because `nfm_ipc` is not yet linked into the application entry path; no Git commit was created.
  - Task 3 added explicit, platform-neutral payload codecs for full/delta catalog chunks, Collector health, IPC server health, cumulative snapshots and status-only pushes. Compile-time wire sizes are 324-byte Collector health, 112-byte IPC health, 468-byte Snapshot fixed prefix, 444-byte CollectorStatus, and 24-byte catalog/application prefixes.
  - One bounded UTF-8 validator rejects invalid, overlong, surrogate and out-of-range sequences. Catalog encoding enforces 131,072-byte per-string bounds, exact 1 MiB frames, no split entries, zero-length removals, baseline restrictions and duplicate handles. Snapshot encoding canonicalizes handles, caps records at 4096 and the stateful decode overload rejects sequence/uptime/catalog rollback and counter decreases.
  - The intended red Fast run failed only with C1083 for `network_flow_monitor/ipc/snapshot_codec.h`. Focused implementation tests then passed. The first analysis pass found optional lifetime checks, UTF-8 cognitive complexity, an unused declaration and a no-op move; all were corrected without suppressions.
  - Final `Format -> Fast` completed with exit 0 in 43.2 seconds and Debug CTest 17/17. Full completed with exit 0 in 85.5 seconds: Debug/Release 17/17, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, strict builds and Release x64 all passed.
  - The full-gate ETW regression on 25H2 again reported 64 × 64 KiB buffers, zero loss, zero unmatched bytes and 1,114,112 attributed bytes per direction; Debug/Release durations were 7,303/6,310 ms. Product size remains 111,616 bytes because payload codecs are not linked into the application entry path.
  - A user-visible `0xc0000142` dialog was traced to a stale system hard-error window owned by `csrss.exe`, not a live test process; terminating `csrss` would have been unsafe. The exact window was closed, integration tests now set inherited error-dialog suppression before child launches, and focused plus full-gate reruns completed with zero matching error dialogs.
  - Task 4 added an instance-local `ApplicationCatalog` that assigns deterministic monotonic handles, never reuses removed handles, publishes sorted full/delta catalogs and cumulative counter records, and increments metadata revisions only when previously empty metadata is enriched. Conflicting non-empty metadata is ignored to avoid identity flapping.
  - The catalog validates strict Application-ID order and uniqueness, empty IDs, the 4096-record cap, counter monotonicity and handle/revision/sequence exhaustion. All publication work occurs in candidate state; validation or exhaustion failures leave records, handles and catalog sequence unchanged. A named `ApplicationCatalogSeed` makes the two exhaustion test seams non-positional.
  - The intended red build failed only with C1083 for `network_flow_monitor/ipc/application_catalog.h`. Focused tests cover stable assignment, removal/reappearance, metadata conflict/enrichment, 4096/4097 bounds, counter decrease and rollback across validation and exhaustion failures.
  - Task 4 final `Format -> Fast` completed with exit 0 in 45.2 seconds and Debug CTest 18/18. Full completed with exit 0 in 91.0 seconds: Debug/Release 18/18, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, strict builds and Release x64 all passed. No matching ETW test process or error window remained; the not-yet-wired product remains 111,616 bytes. No Git commit was created.
  - The user clarified that a passed stage is expected to be committed and granted standing authorization for one commit after each future stage passes the full quality gate. The previously verified Milestones 1–3 and IPC Tasks 1–4 were captured without Task 5 work as `cad27a9 feat: add verified monitoring and IPC foundations`.
  - Task 5 added neutral owner/logon-SID policy and canonical `NetworkFlowMonitor.<mode>.<sid>` endpoint naming, plus a Win32 adapter that owns a self-relative security descriptor. The explicit DACL grants exact server access to LocalSystem and, in portable mode, the concrete server logon SID; the authorized UI SID receives only `FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE`. No broad principal allow ACE is added.
  - The common portable case where elevated server and UI share one logon SID is tested and retains the unioned server access. Maximum-length valid SID names remain below the fixed 256-character limit; the public API accepts no arbitrary raw pipe name, so a fabricated valid over-limit SID is not a real input case.
  - `WindowsPipeSecurityApi` queries only the impersonated thread user and logon group SIDs through bounded token buffers. `VerifyPipeClient` rejects invalid principals before impersonation, matches service user or portable logon group by binary SID, and performs exactly one revert on every successful impersonation path. All non-`None` results are authorization failures; detailed internal error/Win32 values are not protocol disclosures.
  - Task 5 red Fast failed only with C1083 for `network_flow_monitor/ipc/security_policy.h`. Final `Format -> Fast` passed in 45.8 seconds with Debug 19/19; Full passed in 86.1 seconds with Debug/Release 19/19, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze` and Release x64. The security test reaches ADVAPI32/KERNEL32, while the unlinked product remains SHELL32/USER32/KERNEL32 and 111,616 bytes; zero matching ETW error windows remained.
  - Task 6 added a non-copyable Windows named-pipe transport with distinct manual-reset connect/read/write events, explicit Idle/Pending/Completed/CancelRequested operation states, immediate and pending completion paths, `ERROR_PIPE_CONNECTED` race handling, and final-completion-only reuse.
  - `NamedPipeConnection` owns the buffers passed to pending Win32 I/O. Exact reads use an internal staging buffer before copying to the caller destination; writes copy their source before beginning. Partial transfers continue iteratively, zero-progress success is rejected, and cancellation always drains `GetOverlappedResult` before any buffer, `OVERLAPPED`, event, or pipe handle is released.
  - The concrete factory always passes Task 5's explicit security attributes, configures duplex/overlapped byte-mode local-only server instances with a four-instance cap, applies first-instance ownership only to the initial server handle, and opens clients with exact data/synchronize rights plus identification-only SQOS. `WaitNamedPipeW` is used only after `ERROR_PIPE_BUSY` and is bounded to 250 ms.
  - The plan's initial interface listed server creation but omitted the client factory and completion-event access needed by its own Step 4 and Task 7. The implemented boundary adds those required operations without changing the accepted transport, security, or concurrency model.
  - Task 6 red Fast failed only on the missing `network_flow_monitor/ipc/named_pipe_transport.h`. The first implementation build then exposed one missing standard header and a result-type mismatch; focused diagnosis fixed their sources. The first exception-aware analysis also exposed two pre-existing unsafe exception declarations in the same IPC target: `ApplicationCatalog` no longer promises a potentially throwing default construction is `noexcept`, and token-query allocation failures are now caught and classified at the Win32 security API boundary.
  - Final `Format -> Fast` passed in 48.8 seconds with Debug 20/20 and both Clang analyses. Full passed in 98.5 seconds with Debug/Release 20/20, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, and Release x64. The transport test is 260,096 bytes and reaches ADVAPI32/KERNEL32 through security construction; the still-unlinked product remains 111,616 bytes.
  - Task 7 added the bounded `IpcServer` session manager. It owns one cancellable worker per accepted pipe, enforces the four-client cap, authenticates a complete Hello against the owner-scoped principal, echoes request IDs for Hello and subscription updates, publishes catalog-before-snapshot frames, coalesces pending snapshots, pushes collector status, isolates protocol failures, and exposes bounded health counters.
  - The real local-pipe integration test uses the current user SID with the concrete Windows descriptor and overlapped transport. It verifies ServerHello, initial and updated subscriptions, full catalog plus snapshot delivery, health accounting, repeated Stop, and no cross-session teardown. Debug CTest ran 21/21 with the integration test non-skipped; the server/client data path passed after fixing synchronous completion-event reuse and looping partial overlapped transfers.
  - The client ACE was validated with `FILE_GENERIC_READ | FILE_GENERIC_WRITE` rather than `FILE_ALL_ACCESS`; service-mode authorization remains user-SID based and portable-mode authorization remains logon-group based. This is a current Windows 11 25H2 observation; 24H2 acceptance remains deferred.
  - The first unified Fast invocation after Task 7 was externally time-limited while the host emitted the very large MSVC dependency-scan output; the unchanged gate was rerun with output redirected and ended with `Quality gate 'Fast' passed.`. The subsequent Full gate also ended with `Quality gate 'Full' passed.`, including strict Debug/Release builds, MSVC `/analyze`, both Clang analyses, and 22/22 CTest tests in each configuration. This is 25H2 evidence; exact 24H2 acceptance remains deferred.
  - Task 8 added `SnapshotPublisher`, which keeps one deadline per subscribed client, captures once for all simultaneously due clients, publishes only to the due client ids, uses a retry deadline after provider failure, and atomically commits catalog/sequence state only after server publication succeeds. The server preserves empty-recipient broadcast compatibility for existing callers; targeted recipients are an internal delivery filter and do not change the wire protocol.
  - Task 8 tests cover no-client/no-capture, immediate first subscription, shortest-deadline scheduling, per-client due filtering, update/removal, provider failure status, bounded retry and invalid-subscription reporting. Fast completed with `Quality gate 'Fast' passed.`; Full completed with `Quality gate 'Full' passed.`, Debug/Release CTest 23/23, MSVC `/analyze`, both Clang analyses, strict builds and Release x64. Current-host evidence is Windows 11 25H2; exact 24H2 acceptance remains deferred.
  - Task 9 added a pure reconnect policy with bounded exponential delays and injected jitter, plus a reconnecting client facade that owns the worker boundary and exposes explicit `Unauthorized`, `Incompatible`, `Backoff` and `Failed` states.
  - The client protocol validates Hello/Subscribe request IDs, applies complete full/delta catalog sequences atomically, rejects duplicate/unknown handles and metadata regressions, rejects snapshot/catalog/uptime/counter rollback, and maps each counter to the stable `ipc:<instance-guid>:<handle>` ranking identity. Instance and reconnect resets clear the catalog and prior snapshot before a new baseline.
  - Task 9's red phase failed only with the expected missing client/reconnect headers. Focused client tests then passed after isolating terminal-state cases and correcting removed-entry metadata in the test fixture. Final Fast completed with `Quality gate 'Fast' passed.`; Full completed with `Quality gate 'Full' passed.`, Debug/Release CTest 24/24, MSVC `/analyze`, both Clang analyses, strict builds and Release x64. Current-host evidence is Windows 11 25H2; exact 24H2 acceptance remains deferred.
  - Task 10 added a serialized real-pipe integration target. It runs two clients with different requested intervals, exercises catalog/snapshot delivery, a slow reader, a live DACL/mask check, first-instance conflict, unknown-version/oversized/truncated frames, client reconnect after server restart, and idempotent Stop cleanup. Cross-user anonymous/remote evidence is explicitly limited by the available single-user host and remains a Phase 5 environment item.
  - The real integration run exposed and fixed the initial subscription-ack compatibility gap, stale finished-session pipe handles, and the accept-loop termination on recoverable instance-cap Busy. These fixes preserve the four-client bound while allowing later clients after handles are reaped.
  - Task 10 final Fast completed with `Quality gate 'Fast' passed.`; Full completed with `Quality gate 'Full' passed.`, Debug/Release CTest 25/25 including non-skipped `ipc.integration`, MSVC `/analyze`, both Clang analyses, strict builds and Release x64. Milestone 4 is closed on the available Windows 11 25H2 host; exact 24H2 acceptance remains deferred.

### Phase 4.43: Dual collection modes — runtime wiring and audit
- **Status:** complete for Tasks 1–3; Task 4 audit is next
- **Started:** 2026-08-12
- Current task:
  - Dual-mode audit is complete; design the tray and floating UI implementation next.
- Completed work:
  - Added `EtwSnapshotProvider`, which maps `WindowsEtwSession::FlushAndQuery()` into the existing IPC capture model, preserves current collector health on failure, and timestamps successful snapshots exactly once.
  - Added `CollectorHost`, a service-neutral lifecycle owner that wires the subscription observer before startup, starts ETW before IPC, rolls ETW back when IPC startup fails, schedules `SnapshotPublisher::RunDue()` only at publisher deadlines, and makes stop idempotent.
  - Added a thread-safe boundary around `SnapshotPublisher` subscription/deadline/publish state because IPC session callbacks run on server workers while the host loop runs separately.
  - Added host tests for provider flush failure, timestamp capture, ETW-before-IPC startup, no-client no-capture, subscribed-client capture, startup denial and safe/idempotent stop.
  - The new collector target remains UI-free and links only the existing collector/IPC boundaries; no service or window API was introduced in this task.
  - Task 1 Fast gate passed in 46.6 seconds: clean Debug CTest 24/24, strict MSVC build, clang-tidy and Clang Static Analyzer.
  - Task 1 Full gate passed in 93.2 seconds: clean Debug/Release CTest 26/26 including the existing non-skipped ETW integration, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, and Release x64.
  - Current-host evidence is Windows 11 25H2; exact Windows 11 24H2 acceptance remains a Phase 5 target-host task. The product executable is still not runtime-wired to the collector path, so no footprint claim is made here.
  - Added the fixed-name `ServiceControl` SCM adapter with RAII manager/service handles, idempotent install/uninstall/start/stop/restart/query, bounded transition polling, classified Win32 errors, and no arbitrary command-line arguments.
  - Added typed manual, automatic, and delayed-automatic startup configuration; delayed automatic uses `ChangeServiceConfig2W` rather than silently treating it as ordinary automatic startup.
  - Added `ServiceEntry` and a replaceable runtime API seam. The service callback reports `SERVICE_START_PENDING`, `SERVICE_RUNNING`, `SERVICE_STOP_PENDING`, and `SERVICE_STOPPED`, handles stop/shutdown through `std::stop_source`, and delegates only to `ServiceHost`; no window/UI calls exist in the service target.
  - Service tests cover fixed path quoting, startup types, idempotence, missing-service classification, SCM/access failures, dispatcher failure, host startup failure, stop control, and the complete status sequence.
  - Task 2 Fast passed in 48.6 seconds: clean Debug CTest 25/25, strict MSVC build, clang-tidy and Clang Static Analyzer.
  - Task 2 Full passed in 98.7 seconds: clean Debug/Release CTest 27/27 including existing non-skipped ETW and IPC integrations, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, and Release x64.
  - Added `CollectorRuntime` as the application composition root. Portable mode builds an owner-scoped principal from the current logon SID, wires the ETW/IPC host, and registers console/session control shutdown; service mode builds a Users-scoped principal and enters `ServiceEntry` without window calls.
  - Runtime integration tests cover portable/service principal validation, invalid endpoint classification, and successive non-zero collector instance identities. Existing IPC client tests provide reconnect evidence; no real SCM installation or machine mutation is performed by CI.
  - Task 3 Fast passed: clean Debug CTest 26/26, strict MSVC build, clang-tidy and Clang Static Analyzer.
  - Task 3 Full passed: clean Debug/Release CTest 28/28 including live IPC and ETW integrations, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, and Release x64.
  - Exact Windows 11 24H2 behavior and UI-owned portable process-handle shutdown remain open audit items; current evidence is Windows 11 25H2 and the runtime binary is not yet the finished tray UI.
  - Dual-mode audit recorded the Release binary measurement (340,992 bytes), reachable dependencies, direct service/query smoke exit codes (1063/1060), and the deliberate no-real-SCM-mutation policy in `findings.md`.
  - Final audit Full gate passed after the documentation and packaging evidence sync: Debug/Release CTest 28/28, live IPC/ETW integrations, strict builds, both Clang analyses, MSVC `/analyze`, and Release x64.
  - Milestone 5 is complete. Milestone 6 is now the only `in_progress` milestone; its first task is UI design and native Win32/Direct2D boundary definition.

### Phase 4.51: Tray and floating UI — Task 1 implementation
- **Status:** complete; Task 1 commit follows this record
- **Started:** 2026-08-12
- Current task:
  - Task 1 now provides platform-neutral DPI geometry, work-area clamping/snap, deterministic popup placement, and the hover/dismiss/auto-hide interaction state machine.
  - Task 2 is next: Direct2D/DirectWrite resource ownership and the fakeable notification-area controller.
- Completed work:
  - Added `nfm_ui` as a first-party static library with no Win32 window dependency in the pure logic layer.
  - Added coverage for 96/144 DPI conversion, negative multi-monitor coordinates, oversized/clamped rectangles, popup placement, hover/dismiss timing, edge reveal, auto-hide, and data dirty flags.
  - The deliberate red phase produced the expected missing-header C1083 before implementation.
  - The corrected implementation passed the Fast gate: strict Debug build, clang-tidy/Clang Static Analyzer, and CTest 27/27.
- Open evidence:
  - Full quality gate passed in 98.2 seconds: Debug/Release CTest 29/29, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, and Release x64.
  - The Task 1 commit is the next repository operation; Task 2 remains the only active implementation item afterward.

### Phase 4.52: Tray and floating UI — Task 2 implementation
- **Status:** complete; Task 2 commit follows this record
- **Started:** 2026-08-12
- Current task:
  - Task 2 adds Direct2D/DirectWrite factory and text-format ownership, render-target draw-result classification, and a fakeable notification-area controller.
  - Task 3 is next after this commit: floating meter and ranking popup window logic.
- Completed work:
  - The deliberate red phase failed at CMake generation with the expected missing `ui/render_resources.cpp` source.
  - Added `nfm_ui` render-resource ownership through WRL `ComPtr`, per-window HWND render-target creation, and explicit `D2DERR_RECREATE_TARGET` classification.
  - Added `TrayApi`, `WindowsTrayApi`, and idempotent `TrayController` lifecycle with callback routing, bounded tooltip copying, menu command routing, and Shell failure propagation.
  - Focused tray/resource tests passed; the Fast gate passed with Debug CTest 29/29, strict MSVC, clang-tidy, and Clang Static Analyzer.
- Open evidence:
  - Full quality gate passed in 101.4 seconds: Debug/Release CTest 30/30, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, and Release x64.
  - The Task 2 commit is the next repository operation; Task 3 remains the only active implementation item afterward.

### Phase 4.53: Tray and floating UI — Task 3 implementation
- **Status:** complete; Task 3 commit follows this record
- **Started:** 2026-08-12
- Current task:
  - Task 3 adds borderless, no-activate floating and ranking popup windows with per-monitor DPI suggestion handling, work-area clamping, and dirty-driven invalidation.
  - Task 4 is next: UI controller, IPC/ranking bridge, and portable collector process lifetime.
- Completed work:
  - Added fake-window logic coverage for identical-snapshot no-redraw behavior, changed-generation redraws, interaction redraws, mixed-DPI suggested-rectangle clamping, and `WS_EX_NOACTIVATE`/tool-window styles.
  - Implemented Win32 class registration and lifecycle, `WM_DPICHANGED`, display/settings changes, no-activate hit testing, `WM_PAINT`, Direct2D target recreation, minimal live-rate and Top-applications rendering, and explicit `Destroy` cleanup.
  - The focused window test passed; the corrected Fast gate passed with Debug CTest 30/30, strict MSVC, clang-tidy, and Clang Static Analyzer.
- Open evidence:
  - Full quality gate passed in 104.2 seconds: Debug/Release CTest 31/31, strict builds, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, and Release x64.
  - The Task 3 commit is the next repository operation; Task 4 remains the only active implementation item afterward.

## Test Results
| Test | Input | Expected | Actual | Status |
|------|-------|----------|--------|--------|
| Repository inspection | `rg --files`, Git status/log | Discover existing project context | Directory empty; not a Git repository | Pass |
| ETW documentation check | Microsoft TCP/IP ETW documentation | PID and byte-size fields exist | Confirmed | Pass |
| Placeholder scan | All repository Markdown files | No unresolved TODO/TBD/placeholder content | No unresolved placeholders | Pass |
| Relative-link validation | All repository Markdown links | Every relative target exists | All targets resolved | Pass |
| Secure IPC plan self-review | Approved specification plus ten-task implementation plan | No placeholders, unbalanced fences, undefined transport seam, scope gap, or whitespace error | 10 tasks and 51 explicit steps; 32 fences balanced; undefined factory and wiring corrected; `git diff --check` passed | Pass |
| UI Task 1 red phase | Fast gate after registering `ui.layout` before adding geometry/state headers | Compilation fails only for the missing UI header | C1083 for `network_flow_monitor/ui/dpi_layout.h` | Pass |
| UI Task 1 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` after state-machine fix | Strict Debug build, Clang analysis, and all tests pass | Process exited 0; Debug CTest 27/27 and `Quality gate 'Fast' passed.` | Pass |
| UI Task 1 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Process exited 0 in 98.2 seconds; Debug/Release CTest 29/29, both Clang analyses, `/analyze`, and Release x64 passed | Pass |
| UI Task 2 red phase | Fast gate after registering `ui.tray` before adding render/tray sources | CMake fails only for the missing UI source | Missing `ui/render_resources.cpp` during CMake generation | Pass |
| UI Task 2 focused test | `build/windows-x64-debug/tests/nfm_ui_tray_tests.exe` | Tray/resource seam tests pass | Exit code 0 | Pass |
| UI Task 2 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` after lint fixes | Strict Debug build, Clang analysis, and all tests pass | Process exited 0; Debug CTest 29/29 and `Quality gate 'Fast' passed.` | Pass |
| UI Task 2 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Process exited 0 in 101.4 seconds; Debug/Release CTest 30/30, both Clang analyses, `/analyze`, and Release x64 passed | Pass |
| UI Task 3 focused test | `build/windows-x64-debug/tests/nfm_ui_window_logic_tests.exe` | Window logic and style checks pass | Exit code 0 | Pass |
| UI Task 3 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` after Win32 pointer-boundary annotations | Strict Debug build, Clang analysis, and all tests pass | Process exited 0; Debug CTest 30/30 and `Quality gate 'Fast' passed.` | Pass |
| UI Task 3 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` after converting fixed-style checks to `static_assert` | All mandatory gates and Debug/Release tests pass | Process exited 0 in 104.2 seconds; Debug/Release CTest 31/31, both Clang analyses, `/analyze`, and Release x64 passed | Pass |
| Secure IPC Task 1 red phase | Two focused Debug targets after adding bounded-accumulator tests | Fail only for the absent timestamp/watermark/disposition API, capacity constants and health fields | Expected MSVC missing-member and wrong-arity diagnostics; no unrelated failure | Pass |
| Secure IPC Task 1 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format check, strict Debug, non-admin tests and Clang analyses pass | Exit 0 in 36.0 s; Debug CTest 14/14; clang-tidy and Clang Static Analyzer passed | Pass |
| Secure IPC Task 1 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates plus Debug/Release live integration pass | Exit 0 in 75.7 s; Debug and Release CTest 15/15; `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 1 live ETW regression | Debug/Release `collector.etw-integration` logs | Existing ETW attribution and zero-loss behavior remains green | 64 × 64 KiB, zero loss, 1,114,112 attributed bytes each direction; 5,277/5,244 ms | Pass on 25H2 |
| Secure IPC Task 2 red phase | Register `ipc.frame_codec` test before adding public headers | Compilation fails only for the absent codec API | C1083 for `network_flow_monitor/ipc/frame_codec.h`; no unrelated failure | Pass |
| Secure IPC Task 2 final Format/Fast sequence | `tools/quality.ps1 -Mode Format`, then `Fast` | Formatting, strict Debug, 16 non-admin tests and Clang analyses pass | Exit 0 in 39.9 s; Debug CTest 16/16; clang-tidy and Clang Static Analyzer passed | Pass |
| Secure IPC Task 2 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates plus Debug/Release live integration pass | Exit 0 in 79.1 s; Debug and Release CTest 16/16; `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 2 live ETW regression | Debug/Release `collector.etw-integration` logs | Existing Collector behavior remains green after adding the codec library | 64 × 64 KiB, zero loss, 1,114,112 attributed bytes each direction; 7,278/4,260 ms; zero unmatched bytes | Pass on 25H2 |
| Secure IPC Task 3 red phase | Register `ipc.snapshot_codec` before adding its public API | Compilation fails only for the missing payload codec | C1083 for `network_flow_monitor/ipc/snapshot_codec.h`; no unrelated failure | Pass |
| Secure IPC Task 3 final Format/Fast sequence | `tools/quality.ps1 -Mode Format`, then `Fast` | Exact payload tests, strict Debug and Clang analyses pass | Exit 0 in 43.2 s; Debug CTest 17/17; both Clang analyses passed | Pass |
| Secure IPC Task 3 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release integration pass | Exit 0 in 85.5 s; Debug and Release CTest 17/17; `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 3 wire sizes | Compile-time constants plus golden-offset tests | Sizes match approved version-1 field order | Collector 324; IPC 112; Snapshot fixed 468; Status 444; catalog/record prefixes 24 bytes | Pass |
| ETW test error-dialog regression | Focused child/full integration plus Task 3 Full gate | Tests exit normally and leave no matching system error window | All runs exited 0; final matching dialog count 0 | Pass on 25H2 |
| Secure IPC Task 4 red phase | Register `ipc.application_catalog` before adding its public API | Compilation fails only for the missing catalog API | C1083 for `network_flow_monitor/ipc/application_catalog.h`; no unrelated failure | Pass |
| Secure IPC Task 4 final Format/Fast sequence | `tools/quality.ps1 -Mode Format`, then `Fast` | Catalog behavior, strict Debug and Clang analyses pass | Exit 0 in 45.2 s; Debug CTest 18/18; both Clang analyses passed | Pass |
| Secure IPC Task 4 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release integration pass | Exit 0 in 91.0 s; Debug and Release CTest 18/18; `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 4 rollback/exhaustion coverage | Focused catalog tests plus final clean gates | Invalid order/IDs/counts/counters and handle/revision exhaustion do not mutate state | Focused test exit 0; subsequent valid publications retain expected handle/sequence state | Pass |
| Secure IPC Task 4 dialog/product check | Post-Full process/window scan and Release file length | No stale ETW process/dialog; current product reachability remains unchanged | 0 matching processes, 0 matching windows; `NetworkFlowMonitor.exe` 111,616 bytes | Pass on 25H2 |
| Secure IPC Task 5 red phase | Register `ipc.security_policy` before adding its public headers | Compilation fails only for the missing security-policy API | C1083 for `network_flow_monitor/ipc/security_policy.h`; no unrelated failure | Pass |
| Secure IPC Task 5 ACL/identity behavior | Focused Debug tests plus clean gates | Exact service/portable ACEs, no broad allow, canonical names, validation, identity matching and single revert | Focused test exit 0; shared/distinct logon SIDs and every injected failure path passed | Pass on 25H2 |
| Secure IPC Task 5 final Format/Fast sequence | `tools/quality.ps1 -Mode Format`, then `Fast` | Strict Debug, 19 tests and both Clang analyses pass | Exit 0 in 45.8 s; Debug CTest 19/19 | Pass |
| Secure IPC Task 5 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release integration pass | Exit 0 in 86.1 s; Debug/Release CTest 19/19; `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 5 dependency/size boundary | `dumpbin /dependents`, Release length and post-gate window scan | Advapi32 is isolated to reachable security code; product remains unchanged; no stale dialog | Test: ADVAPI32/KERNEL32; product: SHELL32/USER32/KERNEL32, 111,616 bytes; 0 matching windows | Pass on 25H2 |
| Secure IPC Task 6 red phase | Register `ipc.named_pipe_transport` before adding its public API | Compilation fails only for the missing transport header | C1083 for `network_flow_monitor/ipc/named_pipe_transport.h`; no unrelated failure | Pass |
| Secure IPC Task 6 operation and factory behavior | Focused injected-API test | Immediate/pending/partial/error/cancel races, exact buffer ownership, flags, ACL pointer, SQOS, retry and RAII cleanup are deterministic | Focused Debug executable exit 0; all scripted lifecycle assertions passed | Pass on 25H2 |
| Secure IPC Task 6 final Format/Fast sequence | `tools/quality.ps1 -Mode Format`, then `Fast` | Strict Debug, 20 tests and both Clang analyses pass | Exit 0 in 48.8 s; Debug CTest 20/20 | Pass |
| Secure IPC Task 6 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release integration pass | Exit 0 in 98.5 s; Debug/Release CTest 20/20; `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 6 dependency/size boundary | `dumpbin /dependents` and Release lengths | Transport/security reachability and still-unlinked product boundary are recorded exactly | Test: ADVAPI32/KERNEL32, 260,096 bytes; product remains 111,616 bytes | Pass on 25H2 |
| clang-format configuration | `clang-format --style=file --dump-config` | Configuration parses | Exit 0 | Pass |
| clang-tidy configuration | `clang-tidy --verify-config` | Configuration parses | “No config errors detected”, exit 0 | Pass |
| Secure IPC Task 7 protocol/session tests | `nfm_ipc_server_tests.exe` | Handshake ordering, authorization, version/direction errors, request IDs, catalog baseline/delta/gap, mailbox coalescing and status push pass | Exit 0; all assertions passed | Pass |
| Secure IPC Task 7 real local pipe | `nfm_ipc_server_integration_tests.exe` | Owner-scoped pipe opens, Hello/replies, catalog/snapshot, subscription update, health and repeated Stop pass | Exit 0; `ipc server integration tests passed` | Pass on 25H2 |
| Secure IPC Task 7 Debug CTest | `ctest -C Debug --output-on-failure -LE requires-admin` | All registered tests pass with integration non-skipped | 21/21 passed in 1.61 s | Pass on 25H2 |
| Secure IPC Task 7 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, clang-tidy, Clang Static Analyzer and non-admin tests pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 21/21 | Pass on 25H2 |
| Secure IPC Task 7 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates plus Debug/Release integration pass | Final line `Quality gate 'Full' passed.`; Debug/Release CTest 22/22, `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 8 scheduler tests | `nfm_ipc_snapshot_publisher_tests.exe` | Deadline coalescing, due filtering, provider failure retry, removal and invalid subscription behavior pass | Exit 0; `ipc snapshot publisher tests passed` | Pass on 25H2 |
| Secure IPC Task 8 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, clang-tidy, Clang Static Analyzer and non-admin tests pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 22/22 | Pass on 25H2 |
| Secure IPC Task 8 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates plus Debug/Release integration pass | Final line `Quality gate 'Full' passed.`; Debug/Release CTest 23/23, `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 9 red phase | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` after registering `ipc.client` | Compilation fails only for the absent reconnect/client headers | C1083 for `network_flow_monitor/ipc/reconnect_policy.h` and `ipc_client.h`; no unrelated failure | Pass |
| Secure IPC Task 9 focused client tests | `nfm_ipc_client_tests.exe` | Reconnect delays/jitter, handshake IDs, catalog full/delta/removal, rollback rejection, instance reset and facade state transitions pass | Exit 0; `ipc client tests passed` | Pass on 25H2 |
| Secure IPC Task 9 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, client tests and Clang analyses pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 24/24 | Pass on 25H2 |
| Secure IPC Task 9 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates plus Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug/Release CTest 24/24, `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Secure IPC Task 10 real integration | `nfm_ipc_integration_tests.exe` | Real clients, slow-reader isolation, live DACL/mask, first-instance conflict, hostile frames, reconnect and Stop cleanup pass | Exit 0; `ipc integration tests passed`; no skipped path | Pass on 25H2 |
| Secure IPC Task 10 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, non-admin tests and Clang analyses pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 24/24 (integration is serialized `requires-admin`) | Pass on 25H2 |
| Secure IPC Task 10 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | Milestone gate including live integration, `/analyze`, and Release x64 pass | Final line `Quality gate 'Full' passed.`; Debug/Release CTest 25/25, `ipc.integration` non-skipped, `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Dual modes Task 1 host tests | `nfm_collector_host_tests.exe` and `collector.host` | Provider failure/timestamp, ETW-before-IPC, no-client/due-client scheduling, startup denial and idempotent stop pass | Focused executable and Debug/Release CTest passed; host remains UI-free | Pass on 25H2 |
| Dual modes Task 1 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, non-admin tests and Clang analyses pass | Exit 0 in 46.6 s; clean Debug CTest 24/24 and both Clang analyses passed | Pass |
| Dual modes Task 1 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates, existing live ETW integration, `/analyze`, and Release x64 pass | Exit 0 in 93.2 s; clean Debug/Release CTest 26/26, `collector.etw_integration` non-skipped, `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Dual modes Task 2 service tests | `nfm_service_control_tests.exe` and `service.control` | Fixed SCM lifecycle, startup modes, status callback and error boundaries pass | Focused executable passed; Debug/Release CTest passed with no real service mutation | Pass |
| Dual modes Task 2 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, non-admin tests and Clang analyses pass | Exit 0 in 48.6 s; clean Debug CTest 25/25 and both Clang analyses passed | Pass |
| Dual modes Task 2 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates, existing integrations, `/analyze`, and Release x64 pass | Exit 0 in 98.7 s; clean Debug/Release CTest 27/27, existing live integrations non-skipped, `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Dual modes Task 3 runtime integration | `nfm_runtime_collector_integration_tests.exe` | Portable/service principals are valid, invalid mode is classified, and collector instances are non-zero/distinct | Focused test passed; portable uses current logon SID, service uses Users SID, and successive IDs differ | Pass on 25H2 |
| Dual modes Task 3 runtime smoke | Debug `NetworkFlowMonitor.exe --service` and `--service-control` | Direct service entry is rejected by SCM with a classified error; query reports an absent service without mutation | Exit 1063 (`ERROR_FAILED_SERVICE_CONTROLLER_CONNECT`) and 1060 (`ERROR_SERVICE_DOES_NOT_EXIST`); no service was installed or changed | Pass on 25H2 |
| Dual modes Task 3 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, runtime test and Clang analyses pass | Exit 0; clean Debug CTest 26/26 and both Clang analyses passed | Pass |
| Dual modes Task 3 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates, live integrations, `/analyze`, and Release x64 pass | Exit 0; clean Debug/Release CTest 28/28, live IPC/ETW integrations, `/analyze`, both Clang analyses and Release x64 passed | Pass on 25H2 |
| Dual modes Task 4 audit Full gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` after runtime docs and packaging evidence sync | Final source and documentation state remains green | Exit 0; clean Debug/Release CTest 28/28, live IPC/ETW integrations, strict builds, both Clang analyses, `/analyze`, and Release x64 passed | Pass on 25H2 |
| Planning-file relocation | Root path checks and repository reference scan | No root planning files; references target `docs/planning/` | Confirmed | Pass |
| IDE/AI-IDE ignore coverage | Required-pattern scan of `.gitignore` | All selected patterns present | Confirmed | Pass |
| Task 1 full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | Format, Debug, lint/analyzer, `/analyze`, tests, and Release pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 1/1 passed | Pass |
| Runtime-mode parser red phase | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` with an empty implementation unit | Linker reports the missing parser definition | LNK2019/LNK1120 for `ParseRuntimeMode` and no unrelated failure | Pass |
| Task 2 full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | Format, Debug, lint/analyzer, `/analyze`, 2 tests, and Release pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 2/2 passed | Pass |
| Documentation link validation | All navigational Markdown outside generated/local state and implementation-plan code examples | Every relative target exists | No missing targets | Pass |
| Documentation-sync full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | No source drift; all required gates remain green | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 2/2 passed | Pass |
| Config Task 2 model red phase | Build `nfm_config_model_tests` before adding the model API | Compilation fails for the missing public header | C1083 for `network_flow_monitor/config/app_config.h` | Pass |
| Config Task 2 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, Debug, tests, and clang-tidy/Clang analyzer pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 3/3 passed | Pass |
| Config Task 2 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 3/3 passed | Pass |
| Config Task 3 codec red phase | Fast gate after registering `config.codec` but before adding the public API | Compilation fails for the missing codec header | C1083 for `network_flow_monitor/config/config_codec.h` | Pass |
| Config Task 3 focused codec behavior | `build/windows-x64-debug/tests/nfm_config_codec_tests.exe` | Strict parse, scope, status, bounds, enums, and round trips pass | Exit 0 after correcting the test's object-key match | Pass |
| Config Task 3 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, Debug, 4 tests, clang-tidy, and Clang analyzer pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 4/4 passed | Pass |
| Config Task 3 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 4/4 passed | Pass |
| Config Task 4 store red phase | Fast gate after registering `config.store` but before adding the public path/store API | Compilation fails for the missing path header | C1083 for `network_flow_monitor/config/config_paths.h` | Pass |
| Config Task 4 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, Debug, 5 tests, clang-tidy, and Clang analyzer pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 5/5 passed | Pass |
| Config Task 4 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 5/5 passed | Pass |
| Configuration Release size | `(Get-Item build/windows-x64-release/src/NetworkFlowMonitor.exe).Length` | Record exact size and explain link-graph context without inventing a threshold | 111,616 bytes, unchanged from baseline because the executable does not yet link `nfm_config` | Pass |
| Configuration documentation validation | Navigational Markdown link scan plus `git diff --check` | All relative links resolve and no whitespace errors exist | Links resolved; diff check exit 0; working tree contains only intended milestone files | Pass |
| Configuration final Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` after documentation sync | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 5/5 passed | Pass |
| Ranking Task 1 accumulator red phase | Fast gate after registering `ranking.accumulator` but before adding its public API | Compilation fails for the missing accumulator header | C1083 for `network_flow_monitor/ranking/application_accumulator.h` | Pass |
| Ranking Task 1 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, Debug, 6 tests, clang-tidy, and Clang analyzer pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 6/6 passed | Pass |
| Ranking Task 1 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 6/6 passed | Pass |
| Ranking Task 2 engine red phase | Fast gate after registering `ranking.engine` but before adding its public API | Compilation fails for the missing engine header | C1083 for `network_flow_monitor/ranking/ranking_engine.h` | Pass |
| Ranking Task 2 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, Debug, 7 tests, clang-tidy, and Clang analyzer pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 7/7 passed | Pass |
| Ranking Task 2 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 7/7 passed | Pass |
| Ranking Task 3 API red phase | Fast gate after adding live-option tests before implementing `UpdateOptions` | Compilation fails for the missing method | C2039 for `RankingEngine::UpdateOptions` | Pass |
| Ranking Task 3 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Reset/rollover, Top N, totals, filtering, option updates, and all prior tests pass with clang analysis | Final line `Quality gate 'Fast' passed.`; Debug CTest 7/7 passed | Pass |
| Ranking Task 3 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 7/7 passed | Pass |
| Ranking warm-up reset regression | Upload counter 1000 at 0 ms, reset to 100 at 500 ms, then 1100 at 2000 ms | Detect reset during warm-up and calculate from the 500 ms epoch | Diagnostic emitted once; recovered upload rate is 1000 / 1.5 seconds | Pass |
| Milestone 1 final Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` after the warm-up reset fix | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 7/7 passed | Pass |
| Process identity official API review | Microsoft Learn process ETW, AppModel, process security, and query API documentation | Separate documented facts from design choices and reject internal APIs | PID lifetime/reuse, payload PID, process key, limited-query APIs, AUMID/package distinction, and expected access failures recorded | Pass |
| Process identity spec/plan self-review | Placeholder, contradiction, scope, type, link, and whitespace scans | No unresolved design or implementation-plan ambiguity | Two spec issues corrected; scans and navigational links passed | Pass |
| Process identity Task 1 red phase | Fast gate after registering `identity.resolver` before its API exists | Compilation fails for the missing resolver header | C1083 for `network_flow_monitor/process/identity_resolver.h` | Pass |
| Process identity Task 1 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Shared types, identity priority, path normalization, existing ranking, and Clang analysis pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 8/8 passed | Pass |
| Process identity Task 1 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 8/8 passed | Pass |
| Process identity Task 2 red phase | Fast gate after registering `process.tracker` before its API exists | Compilation fails for the missing tracker header | C1083 for `network_flow_monitor/process/process_tracker.h` | Pass |
| Process identity Task 2 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Lifecycle, PID reuse, cache, stale-result, bounds, and Clang analysis pass | Final line `Quality gate 'Fast' passed.`; Debug CTest 9/9 passed | Pass |
| Process identity Task 2 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 9/9 passed | Pass |
| Process identity Task 3 red phase | Fast gate after registering `process.windows_probe` before its API exists | Compilation fails for the missing Win32 probe header | C1083 for `network_flow_monitor/process/windows_process_probe.h` | Pass |
| Clean-build ABI validation | Fast gate after enabling target-specific exception AST | Shared identity extraction rebuilds all dependents consistently | Initial stale LNK2019 reproduced; `--clean-first` removed stale objects and passed | Pass |
| Process identity Task 3 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Win32 current/exit/access/reuse/handoff cases and Clang analysis pass from clean objects | Final line `Quality gate 'Fast' passed.`; Debug CTest 10/10 passed | Pass |
| Process identity Task 3 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory clean gates and Debug/Release tests pass | Final line `Quality gate 'Full' passed.`; Debug and Release CTest 10/10 passed | Pass |
| Process identity Release link graph | `dumpbin /dependents` and executable length | Record exact current dependencies and explain reachability | Shell32/User32/Kernel32; 111,616 bytes; process library not yet linked into executable | Pass |
| Milestone 2 audit-added Fast gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Explicit reuse and different-AUMID assertions plus all prior checks pass from clean objects | Final gate process exited 0; clean Debug CTest 10/10 | Pass |
| Milestone 2 final Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` after specification audit and documentation sync | Every mandatory source gate and Debug/Release test passes | Process exited 0; clean Debug/Release CTest 10/10, both Clang analyses, `/analyze`, and Release x64 passed | Pass |
| Milestone 2 final repository hygiene | Markdown navigation scan, `git diff --check`, `git check-ignore build .superpowers`, status, dependencies, size | No broken links/whitespace; generated/local state ignored; link graph and size recorded accurately | Links and diff pass; both paths ignored; Shell32/User32/Kernel32; 111,616 bytes | Pass |
| ETW Collector Task 1 red phase | Fast gate after registering decoder/core tests before adding Collector headers | Compilation fails only for the missing Collector API | C1083 for `network_flow_monitor/collector/network_event_decoder.h`; no unrelated failure | Pass |
| ETW Collector Task 1 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` after the final health/error audit | Format, clean Debug, 12 tests, clang-tidy, and Clang Static Analyzer pass | Process exited 0; clean Debug CTest 12/12 and Clang analysis passed | Pass |
| ETW Collector Task 1 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` after the final health/error audit | Every mandatory source gate and Debug/Release test passes | Process exited 0; clean Debug/Release CTest 12/12, both Clang analyses, `/analyze`, and Release x64 passed | Pass |
| ETW Collector Task 2 red phase | Fast gate after registering the Windows record test before adding its headers | Compilation fails only for the missing Windows Collector API | C1083 for `network_flow_monitor/collector/windows_etw_record_decoder.h`; no unrelated failure | Pass |
| ETW Collector Task 2 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` after the final adapter audit | Format, clean Debug, 13 tests, clang-tidy, and Clang Static Analyzer pass | Process exited 0; clean Debug CTest 13/13 and Clang analysis passed | Pass |
| ETW Collector Task 2 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` after production TDH dependency coverage | Every mandatory source gate and Debug/Release test passes | Process exited 0; clean Debug/Release CTest 13/13, both Clang analyses, `/analyze`, and Release x64 passed | Pass |
| ETW Collector Task 2 link graph | `dumpbin /dependents` on adapter test and product EXE; product length | TDH appears only where production reader is reachable; product remains unchanged until linked | Adapter: tdh/Kernel32; product: Shell32/User32/Kernel32; product 111,616 bytes | Pass |
| ETW Collector Task 3 red phase | Fast gate after registering the lifecycle test before adding its session header | Compilation fails only for the missing Windows session API | C1083 for `network_flow_monitor/collector/windows_etw_session.h`; no unrelated failure | Pass |
| ETW Collector Task 3 focused race regression | Session lifecycle test after copying the consumer handle into the worker and synchronizing callback observation | Controller/consumer ownership and healthy/fatal callback results are deterministic | Focused test exit 0; subsequent clean Fast run repeated the result | Pass |
| ETW Collector Task 3 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` after the concurrency audit | Format, clean Debug, 14 tests, clang-tidy, and Clang Static Analyzer pass | Process exited 0; clean Debug CTest 14/14 and Clang analysis passed | Pass |
| ETW Collector Task 3 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | Every mandatory source gate and Debug/Release test passes | Process exited 0; clean Debug/Release CTest 14/14, both Clang analyses, `/analyze`, and Release x64 passed | Pass |
| ETW Collector Task 3 link graph | `dumpbin /dependents` on session test and product EXE; product length | ETW/TDH reachability is isolated from the not-yet-wired product | Session test: tdh/Kernel32/synchronization API-set; product unchanged at 111,616 bytes | Pass |
| ETW Collector Task 4 red phase | Configure after registering the integration target before creating its source | Configuration fails only for the missing live-test source | CMake missing-source error for `tests/etw_collector_integration_tests.cpp` | Pass |
| ETW Collector Task 4 live Debug | Serialized `collector.etw_integration` under administrator context | Rundown, matrix, child, burst, schema, loss, stop cleanup, and non-admin denial all pass | Exit 0; 64 × 64 KiB buffers, zero loss, network v2, process lifecycle v4, 5,256 ms | Pass on 25H2 |
| ETW Collector Task 4 Fast quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast` | Format, strict Debug, 14 non-admin tests, clang-tidy, and Clang Static Analyzer pass | Process exited 0; clean Debug CTest 14/14 and Clang analysis passed | Pass |
| ETW Collector Task 4 Full quality gate | `pwsh -NoProfile -File tools/quality.ps1 -Mode Full` | All mandatory gates plus non-skipped Debug/Release live tests pass | Process exited 0 in 78.1 s; Debug/Release CTest 15/15, `/analyze`, both Clang analyses, and Release x64 passed | Pass on 25H2 |
| ETW Collector Task 4 Release measurement | Release live CTest log | Record schema/buffer/loss/duration without inventing a threshold | 64 × 64 KiB, zero loss, network v2, process lifecycle v4, 1,114,112 bytes each direction, 6,294 ms | Pass on 25H2 |
| ETW Collector Task 4 link/size measurement | `dumpbin /dependents` and file lengths | Product remains isolated until runtime wiring; exact sizes recorded | Product: Shell32/User32/Kernel32, 111,616 bytes; Release integration test 352,768 bytes | Pass |

## Error Log
| Timestamp | Error | Attempt | Resolution |
|-----------|-------|---------|------------|
| 2026-08-10 | Visual companion WSL startup could not find Node | 1 | Used the Windows Node runtime directly. |
| 2026-08-10 | Visual mockup was not materially more useful than text choices | 1 | Accepted feedback and stopped using it for conceptual choices. |
| 2026-08-10 | Git status/log failed because the workspace is not a repository | 1 | Recorded the condition; no commit attempted. |
| 2026-08-10 | ripgrep rejected a lookbehind expression in the old-reference scan | 1 | Switched to a simple reference listing; the failed expression will not be retried. |
| 2026-08-10 | Planning-state patch did not match the exact Phase 4 text | 1 | Inspected the current file and prepared a targeted patch using its exact content. |
| 2026-08-10 | Combined planning/ADR read exceeded the 10-second command timeout | 1 | Re-ran the required reads separately with a 20-second limit and no PowerShell profile. |
| 2026-08-10 | Task 1 implementation subagent had no filesystem or command tools | 1 | Confirmed no files were changed and switched Task 1 execution to the primary session. |
| 2026-08-10 | PowerShell parser rejected `$LASTEXITCODE:` in `tools/quality.ps1` | 1 | Reproduced at line 22, identified the colon as an ambiguous variable-name delimiter, and changed it to `${LASTEXITCODE}:`. |
| 2026-08-10 | First SAL-annotation patch expected the pre-format `wWinMain` brace layout | 1 | Inspected the formatted source and re-applied the signature-only patch against its exact text. |
| 2026-08-10 | MSVC `/analyze` emitted C28251 for the unannotated `wWinMain` definition | 1 | Compared against the installed SDK declaration and added matching SAL annotations without suppressing the diagnostic. |
| 2026-08-10 | Initial staged whitespace check found 12 files with an extra blank line at EOF | 1 | Inspected their exact tails and removed only the redundant final blank lines before re-running the gate. |
| 2026-08-10 | First runtime lint-fix patch expected the pre-format function brace layout | 1 | Inspected the formatted files and re-applied the intended changes against exact source text. |
| 2026-08-10 | Runtime parser tests passed but clang-tidy rejected/warned on four first-party patterns | 1 | Preserved the lint policy and corrected the include guard, enum representation, test conversion, and Win32 parameter declarations. |
| 2026-08-10 | Named `wWinMain` parameters exposed fixed-ABI swappable-parameter and non-const-parameter diagnostics | 1 | Matched SDK parameter names and applied two check-specific, reasoned suppressions only to the ABI-fixed parameters. |
| 2026-08-10 | Source-only SDK include reorder patch did not match the formatted file | 1 | Inspected the actual include order and traced it to the repository's clang-format sorting policy. |
| 2026-08-10 | clang-format sorted `shellapi.h` before `windows.h`, breaking the SDK dependency | 1 | Added explicit include categories that keep project headers first and `windows.h` before other system headers. |
| 2026-08-10 | Tidy co-compilation rejected a multilevel implicit pointer conversion and treated `try` as exceptions-disabled | 1 | Made the `LocalFree` ABI conversion explicit, removed a duplicate CMake-supplied driver-mode argument, and enabled exceptions for the Clang analysis AST. |
| 2026-08-10 | Analysis-only `-fexceptions` did not change CMake's clang-tidy co-compile behavior | 1 | Removed the vector/exception requirement entirely; the bounded command surface now parses with a stack view and cannot throw across the Win32 entry. |
| 2026-08-10 | Markdown link validation repeated the fenced README-example false positive | 3 | Kept the established validation scope that skips code-example-bearing implementation plans while checking navigational docs and specifications. |
| 2026-08-10 | The persistent plan described all remaining implementation as one coarse item and still marked completed bootstrap work as active | 1 | Split Phase 4 into eight ordered milestones, added artifact-based completion gates, and synchronized the bootstrap status. |
| 2026-08-10 | Combined execution-queue patch did not match the exact dated error-log row | 1 | Read the real file anchors, traced the mismatch to the omitted date column, and applied smaller file-specific patches. |
| 2026-08-10 | First Milestone 1 Markdown link scan lost the source path in a nested PowerShell pipeline | 1 | Traced `$_` to the inner regex match object; changed the validation run to retain an explicit source directory and stop on any error. |
| 2026-08-10 | Config Task 2 Fast gate exposed aggregate-style, mutable-test-global, swappable-parameter, and invalid-enum diagnostics | 1 | Preserved the analyzers: used the documented all-public aggregate option, a test context, one expected-issue value, and moved invalid enum input coverage to the JSON parser boundary. |
| 2026-08-10 | Config Task 2 lint-fix patches used stale anchors and a multi-file patch partially applied before failure | 2 | Re-read current files, kept the valid `.clang-tidy` change, and changed to single-file patches against stable function boundaries. |
| 2026-08-10 | Config Task 2 Full gate was launched with only a startup-length tool timeout | 1 | The process was terminated before any quality result; reran the unchanged entry point with a sufficient timeout and passed. |
| 2026-08-10 | Config Task 3 focused CMake build ran outside the Visual Studio developer environment | 1 | Used the unified quality entry point, which locates CMake and initializes the Visual Studio developer shell. |
| 2026-08-10 | Config Task 3 red-phase Fast run reached format checking before compilation | 1 | Formatted the new test first, then reran and obtained the intended missing-header C1083 red result. |
| 2026-08-10 | Config Task 3 first compile inferred JSON key tables as `const char*` arrays | 1 | Declared every structural key table explicitly as `std::array<std::string_view, N>`. |
| 2026-08-10 | The first serialization scope assertion matched the collector enum value `service` | 1 | Narrowed the assertion to the JSON object-key token `"service":`; the implementation had not leaked the service group. |
| 2026-08-10 | Config Task 3 tidy/analyzer found macro null paths, swappable helpers, recursive/manual-memory patterns, and test modernization issues | 1 | Kept the checks and converted to yyjson iterator traversal, differentiated completion arguments, RAII writer buffers, explicit enum switches, and narrowly reasoned `NOLINT` annotations. |
| 2026-08-10 | Config Task 3 Fast gate was launched once with only a startup-length timeout | 1 | The process was terminated before a quality result; reran the unchanged entry point with a sufficient timeout and passed. |
| 2026-08-10 | Config Task 3 `/analyze` emitted C6297 inside yyjson despite `/external:W0` | 1 | Confirmed `/external:I` in the compile command, then followed Microsoft guidance by adding `/analyze:external-`; first-party `/analyze` remains enabled. |
| 2026-08-10 | Config Task 4 first tidy build rejected explicitness and ownership-style issues in the Win32 store | 1 | Kept the checks enabled and made Win32 `BOOL` comparisons, integer width, handle type, const ownership, and deleted special members explicit. |
| 2026-08-11 | ETW Task 3 first session lifecycle run raced `Stop()` against the worker's copied handle | 1 | Passed a stable processing-handle value into the worker instead of exposing the mutable owner slot. |
| 2026-08-11 | ETW Task 3 healthy-buffer assertion raced the test's immediate `Stop()` | 1 | Added a C++20 semaphore handshake so healthy continuation is observed before requesting shutdown; fatal termination remains checked after join. |
| 2026-08-11 | Task 4 live network events decoded but every lifecycle event was rejected | 1 | Added pair-specific schema diagnostics and observed Process v4 Start/End/DCStart; expanded only the TDH process whitelist to documented/live versions 2/3/4 and added regression tests. |
| 2026-08-11 | The first short-lived child assertion required the parent's stable application ID | 1 | Traced the exit-before-probe race and changed the invariant to a complete retained application-bucket delta, preserving valid instance-specific unresolved identity. |
| 2026-08-11 | Restricted-token permission children either blocked in ETW RPC or failed before `main`; linked and shell standard tokens were unavailable/elevated | 4 | Used `SAFER_LEVELID_NORMALUSER`, verified token restrictions and non-membership in Administrators with an impersonation token, then required the real child start to classify `AccessDenied`; added timeout termination to prevent orphan test processes. |
| 2026-08-11 | First Task 4 tidy run rejected swappable snapshots and a manual search loop | 1 | Introduced a named snapshot transition and `std::ranges::any_of`; no lint suppression was added. |
| 2026-08-11 | IPC Task 2 test helper returned a pointer into a destroyed local decoded variant | 1 | Reproduced the focused failure, traced the lifetime boundary, and returned an owned `std::optional<Message>` value instead. |
| 2026-08-11 | IPC Task 2 first Fast analysis rejected enum sizing/defaults, unchecked span access, a redundant x64 branch and iterator narrowing | 1 | Kept the checks enabled; compacted native enums while preserving 16-bit wire encoding, used a valid default, bounded span views, a compile-time x64 assertion and range operations. |
| 2026-08-11 | IPC Task 2 pre-Full audit found 16-bit values could narrow to valid 8-bit enums before validation | 1 | Validate the original `uint16_t` message/error value first, then convert only known values; added `0x0101` regression cases for both fields. |
| 2026-08-11 | A stale `nfm_etw_collector_integration_tests.exe` 0xc0000142 dialog could not be removed by ending the already-exited test | 1 | Identified its real owner as critical `csrss.exe`, closed only the exact window, set inherited test error-mode suppression, and verified zero matching dialogs after focused/full reruns. |
| 2026-08-11 | IPC Task 3 first Clang analysis found unchecked optionals, UTF-8 complexity, an unused declaration and a no-op move | 1 | Added explicit presence branches, split UTF-8 scalar validation into bounded helpers, removed dead declarations and copied the trivially-copyable status DTO. |
| 2026-08-11 | Two Task 4 Fast invocations were externally terminated by insufficient 1-second and 60-second execution timeouts | 2 | Treated both as no-result runs, confirmed no residual compiler process, and reran the unchanged gate with a sufficient timeout. |
| 2026-08-11 | The interrupted tidy build left `build.ninja` unavailable for Ninja recompaction | 1 | Verified the generated directory was inside the workspace with no live build process, isolated it under a timestamped ignored name, regenerated a clean tidy build, then moved the 553,771,018-byte stale directory to the Windows Recycle Bin. |
| 2026-08-11 | Task 4 first complete clang-tidy run found swappable constructor parameters and excessive `Publish` cognitive complexity | 1 | Replaced positional test seams with `ApplicationCatalogSeed` and split candidate removal, merge, sequence and assembly steps; the focused and full analysis builds then passed without suppression. |
| 2026-08-11 | The first catch-up staged snapshot found an extra blank line at EOF in ADR 0008 | 1 | Removed only the redundant final blank line, restaged the ADR and required `git diff --cached --check` to pass before commit. |
| 2026-08-11 | Task 5 first Clang analysis found Win32 const-SID adapters, unsafe dynamic indexes, inconsistent enum values and test-style diagnostics | 1 | Concentrated legacy `PSID` const adaptation into two check-specific documented helpers, used vector/span bounds, explicit enum values and qualified types; the next focused analysis was zero-diagnostic. |
| 2026-08-11 | Task 6 first implementation build could not deduce `std::array` and returned an operation result from connection factories | 1 | Traced the diagnostics to the missing `<array>` include and an unadapted result layer; added the include and an explicit connection-error classifier, then the focused strict build passed. |
| 2026-08-11 | Enabling exception-aware IPC analysis exposed potentially throwing work behind three `noexcept` boundaries | 1 | Kept exception analysis enabled, removed `noexcept` from the value-level catalog constructor, caught token-query allocation failures at the Win32 adapter boundary, and retained explicit catch/classification in transport allocation paths. |
| 2026-08-12 | UI Task 1 first Fast run found the ranking dismiss timer was not armed after leaving the anchor during popup transition | 1 | Treated anchor leave as a temporary no-target state; the popup enter event can still cancel the timer, and the corrected Fast/Full gates passed. |
| 2026-08-12 | UI Task 2 red gate stopped at CMake generation because the newly registered render source did not exist yet | 1 | Recorded the intentional red phase, then added the render/tray source implementations before rerunning the gate. |
| 2026-08-12 | UI Task 2 strict tidy found Win32 union access, callback parameter swapping, unsafe tooltip indexing, and an exception-escape test entry point | 4 | Used a typed notification value, `StringCchCopyW`, one check-specific union rationale, explicit interface special members, and disabled exception analysis for the non-throwing UI targets; Fast and Full then passed. |
| 2026-08-12 | UI Task 3 first Full gate found MSVC C6326 on constant runtime style comparisons | 1 | Converted fixed window-style checks to compile-time `static_assert`; Fast and Full then passed. |
| 2026-08-12 | UI Task 4 first Fast build failed on missing Shell API declarations, an invalid tray owner type, and moving from a const factory result | 1 | Added the Shell API header, corrected the owner boundary, and moved from a mutable factory result; the next clean build reached link and test stages. |
| 2026-08-12 | UI Task 4 first Full analysis found MSVC C6262 for a 65 KiB stack buffer in executable-path lookup | 1 | Replaced the fixed stack array with a heap-backed `std::wstring`; the final `/analyze` and Full gate passed. |
| 2026-08-12 | UI Task 5 first tidy run rejected Win32 message-window pointer casts and a non-null-terminated string-view data pointer | 1 | Added check-specific ABI `NOLINTNEXTLINE` comments and changed the class name to a null-terminated `std::array`; the next Fast/Full gates passed. |
| 2026-08-12 | UI Task 5 interaction review found tray clicks changed only the state machine while floating/ranking windows never received mouse transitions | 1 | Added UI-thread state application, mouse enter/leave tracking, popup placement on ranking visibility, and one deadline timer; Fast/Full then passed. |

## Phase 4.54: Tray/floating UI Task 4 implementation
- **Status:** complete on Windows 11 25H2 x64; exact 24H2 acceptance remains in Phase 5
- **Started:** 2026-08-12
- **Current task:**
  - Milestone 6 Task 5: replace the bootstrap UI entry and add the manual integration harness.
- **Actions taken:**
  - Added `CollectorProcess` with a fakeable ShellExecute seam, UAC `runas` launch, bounded graceful-stop and termination fallback, explicit handle ownership, and heap-backed executable-path discovery.
  - Added `UiController` as the IPC observer and UI-thread bridge. Snapshots are coalesced into one pending slot, ranking history is reset on collector-instance changes, and configured ranking options are applied through the existing engine.
  - Connected portable mode to the elevated collector process while service mode remains an IPC-only connection path; no service mutation or UAC action is performed by automated tests.
  - Added dynamic tray owner rebinding so the eventual UI message window can receive Shell notifications even when the controller is constructed before that window exists.
  - Added regression coverage for launch failure cleanup, graceful and forced process stop, snapshot coalescing, state-to-overlay mapping, ranking option updates, and collector-instance reset.
  - Intentional red phase: the first Task 4 Fast run stopped at CMake generation because the registered controller/process sources were not yet present.
  - Final Fast gate: Pass; clean Debug CTest 30/30, strict warnings, clang-tidy, and Clang Static Analyzer passed.
  - Final Full gate: Pass in 107.7 seconds; clean Debug/Release CTest 32/32, MSVC `/analyze`, both Clang analyses, and Release x64 passed. The 32-test count includes the two administrator-labelled live integration tests, both executed successfully on this host.

## Phase 4.55: Tray/floating UI Task 5 implementation checkpoint
- **Status:** implementation committed; visible interaction validation pending on Windows 11 25H2
- **Started:** 2026-08-12
- **Current task:**
  - Run the visible 25H2 tray/floating interaction checklist, then begin the Milestone 6 audit.
- **Actions taken:**
  - Replaced the bootstrap information message box with `UiController::Run`, a hidden message-only window, tray callback dispatch, snapshot-message dispatch, and a clean Win32 message loop.
  - Added the internal `--ui-smoke-test` switch. It selects a service-endpoint UI configuration, starts the UI without UAC or collector construction, posts a quit message, and exercises shutdown/cleanup deterministically.
  - Added `ui.integration_smoke`, which creates the controller in service mode and verifies the same loop in-process.
  - Preserved the existing `--collector`, `--service`, `--install`, `--uninstall`, and `--service-control` branches; UI mode is the only branch that creates windows.
  - Connected tray/window interaction events to the controller: state transitions now show/hide the floating meter and ranking popup, window mouse enter/leave events feed the interaction state machine, and its deadlines use one UI-thread `WM_TIMER` rather than a second data-refresh loop.
  - Real Debug executable smoke run: `NetworkFlowMonitor.exe --ui-smoke-test` exited 0; no message box appeared.
  - Final Fast gate: Pass; clean Debug CTest 31/31, strict warnings, clang-tidy, and Clang Static Analyzer passed.
  - Final Full gate: Pass in 110.0 seconds; clean Debug/Release CTest 33/33, MSVC `/analyze`, both Clang analyses, and Release x64 passed.

## Phase 4.56: Settings and service-management UI implementation checkpoint
- **Status:** implementation complete; manual UI and real SCM acceptance pending on a Windows 11 target
- **Started:** 2026-08-12
- **Actions taken:**
  - Added `SettingsController` and a native Win32 settings window covering every current public `AppConfig` option: Top N, ranking window, display refresh interval, hover/dismiss timing, edge behavior, opacity, speed unit, collector mode, service start type, log level, display toggles, and UI-at-login.
  - Added versioned config load/save wiring at UI startup and from the settings window. Invalid values are rejected before persistence; damaged/missing files use the existing safe-default path and retain diagnostics.
  - Added service actions to the settings UI: install, uninstall, start, stop, restart, query, and start-type configuration. Mutating actions use an explicit UAC `runas` boundary; query remains a normal read-only SCM operation.
  - Added internal runtime verbs for the elevated service actions and controller tests for load/save delegation, validation rejection, service-action forwarding, and start-type forwarding.
  - Added runtime rollback when changing collector mode or display refresh requires reconnecting the collector; a failed reconnect or persistence operation restores the previous ranking and collection state.
  - Fixed an MSVC `/analyze` null-handle diagnostic in the settings window by making the post-creation handle check explicit.
  - Fast quality gate: Pass (format, strict Debug build, non-admin CTest, clang-tidy/Clang analysis build).
  - Full quality gate: Pass; clean Debug/Release CTest 34/34, MSVC `/analyze`, both Clang analyses, and Release x64 build passed on Windows 11 25H2 x64.
- **Remaining acceptance:** visually exercise tray/floating/settings windows, verify service install/start/stop/restart/uninstall against actual SCM state with UAC, then run the Windows 11 24H2 x64 compatibility and packaging matrix.

## Phase 5.1: Release packaging and measurement checkpoint
- **Status:** ZIP packaging and current-host measurements complete; Windows 11 24H2 and manual acceptance pending
- **Started:** 2026-08-12
- **Actions taken:**
  - Added a CMake install target and CPack ZIP configuration for the static-runtime x64 Release executable plus README.
  - Added `tools/package.ps1`, which rebuilds Release, creates a guarded `dist/` install tree, rejects optional DLLs, and emits the ZIP artifact.
  - Added `tools/measure-release.ps1`, which records executable size, SHA-256, `dumpbin` dependencies, OS build, and the UI smoke exit/time into an ignored JSON report.
  - Added an automated native settings-window creation/close smoke test; the clean Debug CTest run now contains 35 tests.
  - Updated the delivery documentation and ADR 0012 to distinguish a portable ZIP from a future full installer. Service registration currently uses the absolute path of the stable extracted executable and does not silently copy or move user files.
  - Historical Full quality gate: Pass; Debug/Release CTest 35/35, MSVC `/analyze`, both Clang analyses, Release build, and CPack ZIP generation passed on Windows 11 25H2 x64 before the real-rate display checkpoint.
  - Historical Release measurement before the display checkpoint: executable 658,944 bytes, ZIP 314,540 bytes, UI smoke exit code 0.
  - Current Release measurement after the display checkpoint: executable 775,168 bytes, ZIP 376,536 bytes, UI smoke exit code 0, wall-clock 99.517 ms, and dependencies limited to Windows system components; detailed values are in `docs/delivery.md` and the generated ignored report. ZIP hashes may vary because `Compress-Archive` records archive metadata.
  - Added `tools/acceptance.ps1` as a read-only target-host probe. On this Windows 11 25H2 build 26200 host it reported package executable SHA-256 `63798FC5115A8A489D8562BF147A407D2B3A3C3F67A45E94F9508CF1640EB09A`, UI smoke exit 0, service not installed, and service-query exit 1060; it performed no SCM mutation.
  - The final package probe also sampled the short UI smoke path at approximately 12.9 MiB peak working set and 46.9 ms CPU time. These values are startup/teardown observations only; target-host idle/high-traffic CPU, memory, latency and ETW-loss measurements remain open.
  - The first post-tool-change Full run had one transient Release `ipc.integration` failure at the oversized-session cleanup assertion. The unchanged Release test passed in three consecutive direct reruns, and the subsequent complete Full run passed; no test relaxation or suppression was added.
- **Remaining acceptance:** execute the same package on Windows 11 24H2 x64, perform real SCM/UAC lifecycle and upgrade/uninstall checks, and collect multi-monitor/mixed-DPI visual plus CPU/workset/latency measurements.

## Phase 5.2: Settings integration closure
- **Status:** implementation complete; target-host and visual acceptance pending
- **Started:** 2026-08-12
- **Actions taken:**
  - Added a typed startup-registration boundary. Enabling `startup.ui_at_login` writes a quoted executable path to the current user's `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run`; disabling it removes the value without requiring elevation. Registry failures are returned through the normal settings error path and persistence rollback.
  - Added fake-backed tests for startup enable/disable, quoted executable-path generation, error propagation, and a settings-window smoke assertion that real SCM status is queried when the window opens.
  - Added a read-only service status line that distinguishes running/stopped/pending, not-installed, and query-failed states. Service actions refresh the line from a new SCM query rather than trusting button state.
  - When switching from service mode to portable mode, the UI starts and connects the portable collector before requesting service stop. Stop failures restore the service endpoint and previous configuration; persistence/startup-registration failures also restore the previous collection state.
  - The prior clean Debug/Release Full gate passed with 36/36 CTest tests. The display checkpoint then added real-rate/Top-N/units rendering and reveal-strip coverage; the current clean Full gate passed with 37/37 Debug and Release CTest tests, strict builds, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Release x64, and CPack ZIP on Windows 11 25H2 x64.
- **Remaining acceptance:** execute the package on Windows 11 24H2 x64, manually validate UAC/SCM install/start/stop/restart/uninstall and upgrade paths, exercise all visible UI on mixed-DPI/multiple-monitor setups, and collect target-host CPU/workset/latency/ETW-loss measurements.

## 5-Question Reboot Check
| Question | Answer |
|----------|--------|
| Where am I? | Phase 5.2; tray/floating/settings implementations and the real-rate display checkpoint pass the complete Full gate with 37/37 tests on Windows 11 25H2 x64; target-host acceptance remains pending. |
| Where am I going? | Run the packaged artifact and lifecycle/visual/performance matrix on Windows 11 24H2 x64, then close upgrade/uninstall evidence. |
| What's the goal? | Build a tiny Win11 tray/floating network monitor with real-time per-app ranking. |
| What have I learned? | See `findings.md`. |
| What have I done? | Completed Milestones 1-3, Secure IPC Tasks 1-10, Dual Modes Tasks 1-4, UI implementation Tasks 1-5, Settings integration, and the ZIP/measurement checkpoint; the service lifecycle adapter, service-neutral host, runtime principal wiring, tray/floating windows, real-rate/Top N display, settings persistence/UI, SCM status/action bridge, login startup registration, IPC/ranking bridge, portable process lifetime, message loop, packaging target, and smoke harness pass the complete Full gate on Windows 11 25H2 x64. |
