# Progress

## Core foundation

- C++20 configuration, process identity, ranking, ETW collection and owner-scoped Named Pipe foundations are implemented.
- Protocol V1 messages and golden fixtures cover Hello, HelloAck, TrafficTick, RankingSnapshot, CollectorStatus and ShutdownRequest.
- Portable Collector lifetime includes parent-process watch and a named stop event.

## WebView2 product architecture — 2026-08-28

- Architecture frozen as C++20 Win32/WebView2 Product Host plus packaged React frontend and C++20 portable Collector.
- Runtime, Native/Web seam, bridge, frontend toolchain, product behavior, native surface evidence, quality and distribution contracts recorded in `.scratch/cpp-webview2-react-ui/spec.md`.
- Four feature-oriented vertical-slice tickets define project foundation, settings/application shell, the complete Dock Capsule experience, and Monitor Panel delivery.
- The product specification follows the `to-spec` issue format with user-facing problem and solution statements, 70 user stories, implementation decisions, three confirmed high-level testing seams, explicit scope and further notes.
- Ticket 01 establishes module ownership, constant placement, utility reuse, dependency direction, theme tokens and localization foundations.
- The retained context menu command set is Show/Hide Monitor, Pin/Unpin, Settings, Collector status/Retry Collector and Exit; auto-hide is an authoritative Settings option.
- Repository cleanup completed: the retained tree contains the portable C++ Collector/Core baseline and the frozen WebView2/React product contract.
- No Git commit authorized.

## Collector baseline cleanup — complete

- Added independent `nfm-collector.exe` entry/build/install/package targets.
- Reduced runtime parsing and IPC authorization to the portable current-logon-session model.
- Removed presentation/runtime implementations, their tests, historical implementation plans and generated outputs that do not belong to the accepted topology.
- Reduced `AppConfig` persistence to the single user document used by the future Product Host.
- Rewrote `tools/quality.ps1`, `tools/package.ps1` and `tools/portable-acceptance.ps1` for the retained baseline and conditional future React workspace.
- Updated Protocol V1, delivery, startup, stop-event and authorization ADRs to the current contract.

## Verification — 2026-08-28

- `clang-format --dry-run --Werror`: passed for 95 C/C++ files.
- MSVC Debug x64 `/W4 /WX /permissive- /sdl`: passed.
- Debug CTest excluding `requires-admin`: 25/25 passed.
- clang-tidy and configured Clang Static Analyzer build: completed successfully.
- MSVC `/analyze` build: completed successfully.
- MSVC Release x64 build: passed.
- Release CTest excluding `requires-admin`: 25/25 passed.
- CPack produced `NetworkFlowMonitor-0.1.0-win-x64.zip` containing the collector validation baseline.
- Generated `build/` and `dist/` trees are removed after verification so the repository returns to a source-only state.

Product Host and frontend implementation remain Ticket 01 work.
