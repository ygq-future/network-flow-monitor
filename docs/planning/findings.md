# Findings and Decisions

## Requirements
- Target Windows 11 only.
- Keep the utility and runtime footprint very small.
- Provide a notification-area icon and a desktop floating window.
- Support edge docking and automatic edge hiding.
- Show a dynamically positioned hover panel containing the real-time Top X network-consuming applications.
- Rank applications by recent real-time throughput, displaying upload and download separately.
- Aggregate multiple processes belonging to the same application.
- Support two collection modes from one executable: portable elevated collector and optional Windows service.
- Do not embed in the taskbar and do not implement a kernel driver.
- Use C++20 and native Windows APIs.
- Centralize user-tunable behavior such as Top N, ranking window, display refresh interval, hover delay, and edge-hide timing in typed, validated configuration.
- Expose service installation, mode selection, status, start, stop, restart, startup type, and uninstall actions in the settings UI.
- Keep project documentation under `docs/`, except root `README.md` and `AGENTS.md`; keep required tool configuration such as `.gitignore`, `.clang-format`, `.clang-tidy`, and `.editorconfig` at the repository root.
- Require formatting, lint, compiler warnings-as-errors, static analysis, tests, and Release build validation after each feature and before every Git commit.

## Research Findings
- Windows 11 does not expose a stable, general-purpose third-party API for embedding a live custom panel in the taskbar. Official taskbar customization focuses on pins and notification-area behavior.
- Windows services run in isolated Session 0 and cannot directly display the interactive UI. A service-based design therefore requires a separate per-user UI process and IPC.
- ETW kernel network tracing can enable TCP/IP and UDP/IP events with `EVENT_TRACE_FLAG_NETWORK_TCPIP`.
- Local Windows SDK 10.0.26100.0 does not export the planned `TcpIpGuid`, `UdpIpGuid`, or `ProcessGuid` symbolic constants. The adapter therefore centralizes the official classic provider GUID values behind one translation unit instead of duplicating literals or relying on unavailable names.
- TCP/IP ETW event payloads include the associated PID and packet size; process ETW events can track process lifetime and mitigate PID-reuse errors.
- Enabling some TCP extended statistics requires administrator privileges. Privileged collection is therefore a real technical requirement, not merely a packaging preference.
- Native AOT remains self-contained and includes required runtime libraries; it also has trimming, dynamic loading, and Windows COM limitations. It is not the smallest or simplest fit for this native utility.
- Microsoft's `windows-rs` exposes Windows APIs to Rust, but the ETW, Direct2D, window-procedure, and service boundaries would still require substantial unsafe FFI and less direct debugging than C++.
- Microsoft's current MSVC installation guidance recommends the stable Visual Studio Installer channel and the **Desktop development with C++** workload; preview compiler components are not required.
- Current supported Windows SDK releases include the 26100 and 28000 families. The project should use the newest stable SDK installed by Visual Studio rather than pinning a preview SDK.
- Local inspection found Visual Studio 18 Community at `C:\Program Files\Microsoft Visual Studio\18\Community`, MSVC `14.51.36231`, bundled CMake and Ninja, and Windows SDK `10.0.26100.0`. `cl`, CMake, and Ninja are not available in the ordinary PowerShell `PATH`, which is normal outside a Visual Studio developer shell.
- Visual Studio also contains `clang-format.exe` and `clang-tidy.exe` under `VC\Tools\Llvm\x64\bin`; no separate LLVM installation is currently needed.
- LLVM documents `clang-tidy` as a C++ linter and static-analysis framework and recommends using a CMake-generated compilation database.
- Local bootstrap tool versions are CMake `4.3.1-msvc1`, Ninja `1.13.2`, and Git `2.47.0.windows.1`.
- The approved design spans independent collector, IPC, service, and UI subsystems. The first implementation plan is intentionally limited to Git/CMake/project/quality bootstrap so it produces a separately reviewable working baseline.
- The configuration format requires a JSON implementation, but no JSON dependency has yet been approved. Official project documentation shows that yyjson is MIT-licensed, strict RFC 8259 JSON with UTF-8 validation, supports exact `uint64` values, and integrates as one C header plus one C source file. These properties fit a small statically linked Windows utility better than a large template-heavy C++ header or an untested custom parser.
- nlohmann/json is also MIT-licensed and provides convenient C++/CMake integration, but its single-header, template-heavy API optimizes developer convenience rather than this project's binary/compile-footprint priority. The actual executable-size difference remains unverified until measured in this repository.
- Microsoft documents `REPLACEFILE_WRITE_THROUGH` as unsupported. Atomic replacement must therefore flush the temporary file first and call `ReplaceFileW` with flags `0`; `MOVEFILE_WRITE_THROUGH` is supported for the first-create `MoveFileExW` path.
- Microsoft documents that `/external:W0` does not suppress MSVC code-analysis diagnostics because those warnings have no warning level. First-party `/analyze` builds therefore also use `/analyze:external-`; CMake `SYSTEM` include paths remain the explicit boundary, while all first-party translation units continue to be analyzed.
- Microsoft documents that PID values identify a process only for that process's lifetime and are reused. Kernel process events carry the process PID in the event payload, plus a `UniqueProcessKey`; start events cannot safely use the event-header PID because they may be logged in the parent process context.
- `ProcessTrace` chronologically sorts events before delivering callbacks. Process lifetime matching can therefore use event timestamps and explicit start/end intervals, while still retaining ended identities briefly for late or same-buffer network events.
- `GetProcessTimes` returns a process creation `FILETIME` using a handle opened with `PROCESS_QUERY_LIMITED_INFORMATION`. It is a documented secondary check for PID reuse, but it cannot be the only instance key because the process may exit or deny access before it is probed.
- `QueryFullProcessImageNameW`, `GetPackageFamilyName`, and `GetApplicationUserModelId` all work with `PROCESS_QUERY_LIMITED_INFORMATION`; access is still subject to the process DACL and protected-process restrictions, so resolution failure is an expected state rather than proof that the process did not exist.
- Microsoft distinguishes package identity from application identity: a package can declare multiple applications, and the version-independent AUMID identifies an application within a package. Grouping packaged traffic only by package family would therefore merge potentially different applications.
- `GetApplicationUserModelId` explicitly reports `APPMODEL_ERROR_NO_APPLICATION` for processes without application identity, while `GetPackageFamilyName` reports `APPMODEL_ERROR_NO_PACKAGE`. These are normal negative results and must not be logged as Win32 failures.
- `GetFinalPathNameByHandleW` can resolve symbolic links and return a normalized final path, but requires a file handle and may fail on inaccessible path components. The first process-identity milestone will not open executable files merely to canonicalize aliases; it will structurally normalize the documented process image path and prefer false separation over false merging.
- Microsoft labels `ZwQueryInformationProcess` and its returned structures as internal and subject to change. The process tracker will not depend on undocumented process sequence-number queries even though they could appear to offer a compact PID-reuse key.
- Start and Rundown observations require the same creation-time race check: a probed creation time later than either observation means the PID may have been reused before probing. Rundown describes already-running processes; it is not evidence that reuse validation can be skipped.
- A controlled access-denied integration test must account for a development host where the current token has `SeDebugPrivilege` enabled. The test creates its own suspended child, applies an empty DACL, temporarily disables that privilege for the probe, and restores it before cleanup; it does not depend on probing a protected system process.
- CMake's clang-tidy co-compilation parses target compile semantics. Only the process target that contains a `noexcept` exception boundary needs analysis-only `/EHsc`; adding it globally exposed unrelated historical diagnostics and would not represent the production target boundary accurately.
- Incremental analysis objects can retain an obsolete C++ ABI after a shared value type moves between libraries. The unified quality runner now builds every Debug, tidy, `/analyze`, and Release tree with `--clean-first`, so a passing gate proves all dependents were rebuilt consistently.
- Modern Windows supports up to eight multiplexed SystemTraceProvider logger slots. New code should use a custom session name and GUID with `EVENT_TRACE_SYSTEM_LOGGER_MODE`; Microsoft marks the `KERNEL_LOGGER_NAME`/`SystemTraceControlGuid` NT Kernel Logger route as deprecated because it conflicts with other controllers.
- `EVENT_TRACE_FLAG_NETWORK_TCPIP` enables both classic TCP/IP and UDP/IP kernel events; `EVENT_TRACE_FLAG_PROCESS` adds Start, End, DCStart, and DCEnd process events. TCP/UDP event-header PID is not reliable because logging can occur on another thread, while the published version-2 payload begins with `uint32 PID` and `uint32 size` for the required IPv4/IPv6 send and receive schemas.
- Process Start can run in the parent context, so the lifecycle PID must also come from payload. Process V2 publishes a pointer-sized `UniqueProcessKey`, PID, image name, SID, and command line; the Collector needs only key, PID, and image hint and must never request or retain the SID/command-line properties.
- Microsoft's Process class page declares event version 3, while the older Process V2 schema documents the same required property names. A live session on Windows 11 25H2 build 26200 emitted Process version 4 for Start, End, and DCStart plus version 2 opcode 11 for a separate process event. The process adapter therefore accepts only versions 2/3/4 and uses TDH for the three approved names; exact 24H2 emission remains unverified until a target-host run.
- The 2026-08-11 live Collector run on Windows 11 25H2 build 26200 observed TCP/UDP version 2, Process lifecycle version 4, a 64 KiB buffer request realized as 64 buffers of 64 KiB, and zero `EventsLost`, `LogBuffersLost`, and `RealTimeBuffersLost` during the bounded loopback matrix/burst. These are measurements, not performance or sizing promises.
- This host runs with an elevated interactive shell and exposes neither a linked standard token nor a non-elevated Explorer token. The access-denied integration therefore uses Windows Software Restriction Policy `SAFER_LEVELID_NORMALUSER`, verifies `TokenHasRestrictions` and absence of Administrators membership, then requires the child ETW start to return the classified `AccessDenied` result.
- `EVENT_TRACE_LOGFILEW` with `PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD` is the recommended consumer shape. Omitting `PROCESS_TRACE_MODE_RAW_TIMESTAMP` makes `ProcessTrace` convert the controller's QPC timestamp to 100ns system time, which can then pass through the existing named FILETIME conversion.
- `TdhGetProperty` supports classic ETW but Microsoft calls it inefficient because it scans properties on every call. The selected hybrid decoder therefore reads the documented fixed PID/size prefix directly for high-volume network version-2 events and uses TDH only for low-rate process lifecycle properties.
- A real-time session's minimum automatic flush period is one second. Refresh intervals below one second require an explicit `EVENT_TRACE_CONTROL_FLUSH` initiated by the future subscription/snapshot scheduler; a second independent user-visible refresh setting would still be incorrect.
- Producer loss and consumer delivery loss are distinct: `EVENT_TRACE_PROPERTIES` exposes `EventsLost`, `LogBuffersLost`, and `RealTimeBuffersLost`, while real-time lost-event notifications can also occur. Restarting cannot reconstruct missing bytes, so loss must mark health degraded and remain visible to the UI.
- Microsoft documents that a default named-pipe security descriptor grants full control to LocalSystem, administrators, and creator owner, plus read access to Everyone and Anonymous. The IPC server must therefore pass an explicit descriptor rather than treating the pipe name as a security boundary.
- Microsoft documents that service-hosted named pipes can be reachable remotely and recommends denying Network or using a local-only transport. The selected pipe mode uses `PIPE_REJECT_REMOTE_CLIENTS` and has no Network allow ACE.
- Generic write access for a pipe includes the same bit as `FILE_CREATE_PIPE_INSTANCE`; the design separates server-instance rights from the authorized client's specific data rights instead of granting broad generic write in the DACL.
- Overlapped pipe I/O can keep one read and one write outstanding with separate `OVERLAPPED` events. `CancelIoEx` only requests cancellation, so RAII shutdown must wait for the final completion before releasing the operation state.
- `ImpersonateNamedPipeClient` failure leaves the privileged server in its own context. Client identity verification is therefore fail-closed and guarded by unconditional `RevertToSelf`; the server performs no privileged client-directed operation during impersonation.
- The current `ApplicationAccumulator` retains every Application ID for the Collector lifetime. A long-running service would therefore have unbounded application state and outbound snapshot growth. Milestone 4 includes a 15-second active horizon derived from the configured 10-second maximum ranking window plus 5-second maximum refresh interval, two fixed aggregate buckets, and a 4096-record safety cap with explicit degraded `Other` accounting.
- Microsoft documents that serious-error dialogs belong to the process error-mode mechanism and that child processes inherit the parent's process error mode. Automated integration tests now set `SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX` before spawning children so loader/Windows Error Reporting failures return as observable exit results instead of leaving a session-level dialog owned by `csrss.exe`.
- The real IPC server sends an initial `SubscriptionAccepted` that echoes the Hello request ID, followed by a second acceptance for the client's Subscribe request. The client protocol explicitly accepts both ordered acknowledgements and rejects duplicate or unrelated IDs; this was found only by exercising the worker against the real server.
- The first real multi-session stress run exposed two lifecycle bugs: finished sessions were excluded from the logical active count but still held native pipe handles, and `CreateNamedPipe` can report `ERROR_ACCESS_DENIED` when later instances hit the instance cap. The server now joins/removes finished sessions before creating another instance and treats recoverable Busy/instance-cap results as bounded retry rather than a terminal accept-loop failure.
- The Task 10 integration test on Windows 11 25H2 queried the live pipe DACL, found the authorized SID with the exact client data mask (`FILE_GENERIC_READ | FILE_GENERIC_WRITE`), found no World allow ACE, and verified first-instance conflict, unknown-version, oversized-header and truncated-prefix isolation. This host cannot manufacture a separate user token or remote transport inside the serialized test, so anonymous cross-user `AccessCheck` and remote-rejection evidence remain explicit environment limitations; the unit/security tests still cover the policy and `PIPE_REJECT_REMOTE_CLIENTS` configuration.

## Technical Decisions
| Decision | Rationale |
|----------|-----------|
| Native C++20/Win32 | Direct Windows API access with minimal runtime and packaging overhead. |
| Direct2D + DirectWrite | Lightweight high-DPI rendering without a large UI framework. |
| ETW-based TCP/UDP collection | Provides packet size and PID without a custom driver. |
| Normal-privilege UI + privileged collector | Limits elevation to the component that requires it. |
| Named-pipe IPC with explicit ACLs | Supports service and portable collector modes with a securable native transport. |
| Application ID based primarily on normalized executable path/package identity | Avoids incorrectly merging unrelated same-named executables. |
| Two-second sliding ranking window | Balances responsiveness with stable ordering. |
| Collector publishes cumulative counters; each UI session calculates its own configured window and ranking | Prevents machine-wide service state from being tied to one user's UI preferences. |
| One display refresh interval controls snapshot delivery and visible value updates | Avoids two user-facing settings for the same observable behavior. |
| No independent idle repaint timer | Repaint only when data, theme, layout, DPI, or interaction state changes. |
| Use clang-format, clang-tidy, MSVC strict warnings, MSVC code analysis, tests, and Release builds as a commit gate | Formatting alone does not detect correctness or API misuse; the checks cover complementary failure classes. |
| Store persistent planning files in `docs/planning/` | User requested a clean repository root; explicit agent instructions will compensate for the planning skill's default root discovery. |

## Issues Encountered
| Issue | Resolution |
|-------|------------|
| Taskbar integration conflicts with an existing TrafficMonitor installation and relies on unstable Win11 internals | Removed taskbar integration from scope. |
| "Green" distribution was initially treated as incompatible with elevation | Clarified that a portable executable can launch an elevated collector each run; installation and privilege are separate concerns. |

## Dual-mode runtime audit (2026-08-12)

- The runtime composition root now links the single executable to the existing ETW, IPC and SCM
  boundaries. The default UI branch does not construct or start an ETW session.
- Portable IPC authorization uses the current logon SID for both the client identity and the
  temporary collector's server logon ACE. Service IPC authorization uses the built-in Users SID
  for clients and LocalSystem for the service server. These shapes are validated in the runtime
  integration test and are not a substitute for a cross-user live test.
- Direct Release smoke launches classify `--service` as `ERROR_FAILED_SERVICE_CONTROLLER_CONNECT`
  (1063) when not started by SCM and classify `--service-control` as
  `ERROR_SERVICE_DOES_NOT_EXIST` (1060) before any service is installed. CI deliberately does not
  install, start, upgrade, or delete a real machine service.
- The post-wiring Release executable measured 340,992 bytes on the current toolchain. `dumpbin`
  reports SHELL32, USER32, TDH, ADVAPI32, KERNEL32 and the synchronization API-set; this is a
  development measurement, not a product-size target or a claim of self-contained packaging.
- The available host is Windows 11 25H2 build 26200. Exact Windows 11 24H2 schema, privilege,
  service-install, and mixed-DPI acceptance remain Phase 5 verification work. UI-owned portable
  process-handle shutdown and mode switching are intentionally left for the tray/settings
  milestones; the collector itself handles console/session control shutdown.

## Release packaging audit (2026-08-12)

- CMake now exposes an install target and CPack ZIP generator. The portable package contains the
  Release executable and README only; the current static-runtime build did not require optional
  MSVC DLLs.
- The pre-display checkpoint package was 314,540 bytes with a 658,944-byte executable. The
  current post-display checkpoint package is 376,536 bytes with a 775,168-byte executable;
  these are separate historical measurements, not a single invariant. `Compress-Archive` emits
  metadata-bearing ZIPs, so the byte count is useful for this run but the archive hash is not
  expected to be stable across repeated packaging runs.
- `tools/measure-release.ps1` recorded a successful `--ui-smoke-test` exit code 0 and a 93.713 ms
  wall-clock run for that short smoke path. This is a startup/teardown observation, not a UI or
  collector latency guarantee.
- `dumpbin /DEPENDENTS` reported only Windows system components (`ADVAPI32`, `DWrite`, `d2d1`,
  `GDI32`, `KERNEL32`, `SHELL32`, `tdh`, `USER32`, and a synchronization API-set). The exact
  dependency and binary-size result must be rechecked on the final 24H2 build toolchain.
- The service control adapter registers the absolute path of the stable extracted executable; it
  does not silently copy or move a portable file. A future Program Files installer or upgrade
  migration requires a separate design and test path.
- The settings integration checkpoint now writes/removes the per-user login startup value through
  HKCU Run, displays a fresh SCM status after opening or mutating the service, and stops the old
  service only after a successful switch back to the portable collector. These behaviors are
  covered by fake-backed/UI smoke tests; real UAC and SCM state changes remain manual acceptance.
- The first Full run after adding the read-only acceptance script observed one transient Release
  `ipc.integration` failure while waiting for an oversized hostile session to be removed. The
  unchanged executable passed three consecutive direct reruns, and the next complete Full run
  passed; the incident remains recorded rather than hidden behind a retry in the quality runner.
- The read-only acceptance probe on Windows 11 25H2 build 26200 reported UI smoke exit 0, no
  installed service, and `--service-query` exit 1060. It does not install, start, stop, or remove
  a service; actual UAC/SCM lifecycle evidence remains target-host manual work.

## Resources
- https://learn.microsoft.com/en-us/windows/win32/etw/tcpip
- https://learn.microsoft.com/en-us/windows/win32/etw/tcpip-v0-typegroup1
- https://learn.microsoft.com/en-us/windows/win32/api/evntrace/ns-evntrace-event_trace_properties
- https://learn.microsoft.com/en-us/windows/win32/services/service-changes-for-windows-vista
- https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights
- https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getpertcpconnectionestats
- https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-quickstart
- https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/
- https://github.com/microsoft/windows-rs
- https://github.com/zhongyang219/TrafficMonitor
- https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc
- https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads
- https://clang.llvm.org/docs/ClangFormatStyleOptions.html
- https://clang.llvm.org/extra/clang-tidy/index.html
- https://learn.microsoft.com/en-us/cpp/build/reference/compiler-options-listed-by-category
- https://learn.microsoft.com/en-us/cpp/build/reference/permissive-standards-conformance
- https://github.com/ibireme/yyjson
- https://github.com/nlohmann/json
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
- https://learn.microsoft.com/en-us/windows/win32/etw/process
- https://learn.microsoft.com/en-us/windows/win32/etw/process-v2-typegroup1
- https://learn.microsoft.com/en-us/windows/win32/etw/consuming-events
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew
- https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-getpackagefamilyname
- https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-getapplicationusermodelid
- https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/package-identity-overview
- https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights
- https://learn.microsoft.com/en-us/windows/win32/procthread/zwqueryinformationprocess
- https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-a-systemtraceprovider-session
- https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-starttracew
- https://learn.microsoft.com/en-us/windows/win32/api/evntrace/ns-evntrace-event_trace_logfilew
- https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-closetrace
- https://learn.microsoft.com/en-us/windows/win32/etw/udpip
- https://learn.microsoft.com/en-us/windows/win32/etw/tcpip-sendipv4
- https://learn.microsoft.com/en-us/windows/win32/etw/tcpip-sendipv6
- https://learn.microsoft.com/en-us/windows/win32/etw/udpip-typegroup1
- https://learn.microsoft.com/en-us/windows/win32/etw/udpip-typegroup2
- https://learn.microsoft.com/en-us/windows/win32/api/tdh/nf-tdh-tdhgetproperty
- https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-flushtracew
- https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode

## Visual/Browser Findings
- The visual companion presented three taskbar placement choices, but the mockups did not add enough information beyond the text options.
- The user chose to remove taskbar integration entirely and retain only tray and floating-window modes.

## Service Runtime Finding
- A real SCM start on the current Windows 11 25H2 administrator host initially returned 1053 and generated SCM events 7009/7000. Inspection showed the registered `ImagePath` omitted the fixed `--service` runtime switch, so the executable entered its default UI branch and never registered with `StartServiceCtrlDispatcherW`.
- `ServiceControl` now constructs a quoted absolute path followed by `--service`, with a unit assertion for the exact command line. After the fix, install/start/stop/restart/uninstall all returned success in a real SCM run; final query returned 1060 after uninstall. This is current-host evidence only; UAC and Windows 11 24H2 acceptance remain open.
- The lifecycle probe initially encountered 1460 because a previous forced UI/collector termination left the fixed `NetworkFlowMonitor.SystemTrace` logger running; SCM then reported 7023 / “file already exists.” Stopping only that explicitly named project logger restored service start and restart. The probe now records expected post-uninstall 1060 separately from failures and always cleans up a service it created.

## 2026-08-12 Portable collector shutdown audit

- The portable UI launch used `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process_id)` as its only normal cross-process stop request. This is not a reliable lifecycle protocol for a GUI-subsystem child launched through UAC because the child may not share a usable console. A forced termination during the current-host smoke left the fixed ETW logger behind until it was explicitly stopped, confirming that cleanup must be treated as an observable acceptance condition.
- The UI now creates a per-launch manual-reset event named with the restricted `Local\\NetworkFlowMonitor.Stop.` prefix and passes it through the internal `--stop-event` argument. The collector opens only that validated name and requests its normal `std::stop_source` when signaled. Console/session handling remains for logoff and shutdown, and the existing bounded `TerminateProcess` path remains a last-resort fallback.
- This is a lifecycle hardening change, not a claim that UAC or ETW behavior is identical on every Windows 11 build. The current automated evidence is parser coverage, fake-backed UI process tests, Fast/Full quality gates, and current-host smoke; direct packaged portable start/signal/ETW cleanup and Windows 11 24H2 remain open.
# Native Win11 material facts (2026-08-13)

- **Verified platform guidance:** Microsoft distinguishes Mica as an opaque, performant material for long-lived base surfaces and Acrylic as a semi-transparent blurred material commonly used for transient/light-dismiss surfaces. Native Win32 applications can request Windows 11 system backdrops without adopting WinUI 3. Source: [System backdrops (Mica/Acrylic)](https://learn.microsoft.com/en-us/windows/apps/develop/ui/system-backdrops).
- **Verified degradation behavior:** Acrylic is not guaranteed to remain available. Microsoft documents that it can be disabled by the operating system, including in Remote Desktop/VM, unsupported-GPU, and power-saving scenarios; Windows Terminal consequently falls back rather than guaranteeing blur. Source: [Windows Terminal troubleshooting](https://learn.microsoft.com/en-us/windows/terminal/troubleshooting).
- **Design choice, not a platform fact:** The planned UI requests Acrylic plus configurable background opacity for NFM/TopA, uses Mica for Settings, and retains a readable same-tint fallback when composition effects are unavailable. This requires target-host visual verification; the presence or quality of blur cannot be established by unit tests alone.

## Native popup-menu ownership fact (2026-08-14)

- **Verified platform guidance:** Microsoft documents that an application using `TrackPopupMenu` must make the menu owner the foreground window so clicking outside the menu dismisses it correctly, and must post a benign message such as `WM_NULL` after the call. NFM and TopA therefore use the clicked visible surface as the shared menu owner rather than the hidden tray window. Source: [TrackPopupMenu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenu).
