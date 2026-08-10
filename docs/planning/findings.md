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

## Visual/Browser Findings
- The visual companion presented three taskbar placement choices, but the mockups did not add enough information beyond the text options.
- The user chose to remove taskbar integration entirely and retain only tray and floating-window modes.
