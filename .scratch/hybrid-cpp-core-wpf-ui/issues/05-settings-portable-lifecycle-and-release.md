# 05 — 设置控制中心、便携双进程生命周期与发布打包收尾

**What to build:**
Deliver the Fluent Settings control center, full portable companion lifecycle management, system tray integration, cross-language automated quality gate, and self-contained portable release packaging. Upon passing final Target-Host Acceptance on Windows 11, legacy C++ Win32 UI source is safely retired.

**Blocked by:** 04 — 胶囊现代流体动效与监控面板交互质感

**Status:** ready-for-agent

- [ ] Modern Fluent Settings window implemented with General, Monitor, Ranking, Collector, and Diagnostics tabs.
- [ ] Settings Collector tab displays connection state, collector PID, instance ID, elevation status, ETW session health, dropped packets, and active PIDs, with "Retry Collector", "Restart Collector", and "Copy Diagnostics" actions (no Windows Service management UI).
- [ ] System, Light, and Dark themes sync in real-time with Windows 11; English and Simplified Chinese bilingual localization switches dynamically without restart; brand name remains untranslated.
- [ ] Executable-adjacent JSON `ConfigStore` performs atomic file writes with `.damaged` file preservation and diagnostic logging upon corruption.
- [ ] Single interactive user session enforced via session-scoped mutex: secondary UI launch activates existing window and exits without spawning duplicate processes.
- [ ] Portable companion lifecycle: UI launches `nfm-collector.exe` via UAC on cold start; collector monitors UI parent process handle via `WaitForSingleObject` and exits immediately on UI close or crash; 3-second reconnect grace period cleans up collector if pipe disconnects while UI lives.
- [ ] System tray icon integrates with shared context menu and application lifecycle.
- [ ] `tools/quality.ps1` updated as the unified quality gate compiling C++20 and .NET 10, running cross-language protocol tests, unit tests, and producing `dist/NetworkFlowMonitor-<version>-win-x64-portable.zip`.
- [ ] Full Target-Host Acceptance test passes on a live Windows 11 environment without installation or lingering background processes.
- [ ] Architecture Gate 7 passed: legacy C++ Win32/Direct2D UI source in `src/ui/` is cleanly removed, and the final repository builds and tests 100% green.
