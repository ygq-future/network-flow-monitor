# 05 — 设置控制中心、便携双进程生命周期与发布打包收尾

**What to build:**
Deliver the Fluent Settings control center covering all public user configurations (left-nav sidebar + right content IA, narrow top-selector fallback, opaque Fluent backdrop independent of monitor opacity), full portable companion lifecycle management, Windows login auto-start, secondary-instance foreground activation channel, system tray integration, cross-language automated quality gate, and self-contained portable release packaging. Upon passing final Target-Host Acceptance on Windows 11, legacy C++ Win32 UI source is safely retired.

**Blocked by:** 04 — 胶囊现代流体动效与监控面板交互质感

**Status:** ready-for-agent

- [ ] Owns and implements cross-language golden protocol fixtures in `tests/fixtures/` for `ShutdownRequest`.
- [ ] Modern Fluent Settings window implemented with left-navigation and right-content cards (narrow top-selector fallback) covering General (Language, Theme, Start with Windows), Monitor (Opacity, Blur, Capsule Width/Height, Auto-Hide, Hover/Dismiss Delay, Topmost Yield, Rate Units), Ranking (Top N), Collector (live state & actions), and Diagnostics tabs.
- [ ] Settings window uses an opaque, readable Fluent surface, does not inherit Monitor Panel background opacity, and activates the existing window instance when re-opened.
- [ ] Windows user login auto-start (Run key / startup registration) managed cleanly from Settings without requiring Administrator elevation.
- [ ] Settings Collector tab displays connection state, collector PID, instance ID, elevation status, ETW session health, dropped packets, and active PIDs, with "Retry Collector", "Restart Collector", and "Copy Diagnostics" actions (no Windows Service management UI).
- [ ] System, Light, and Dark themes sync in real-time with Windows 11; English and Simplified Chinese bilingual localization switches dynamically without restart; brand name remains untranslated.
- [ ] Executable-adjacent JSON `ConfigStore` performs atomic file writes, restores persisted Pin / anchor offsets / user preferences across restarts, and performs `.damaged` file preservation and diagnostic logging upon corruption.
- [ ] Single interactive user session enforced via session-scoped mutex: secondary UI launch activates existing window via a dedicated activation channel (named pipe ping or window message) to bring it to foreground, and exits immediately without spawning duplicate processes.
- [ ] Portable companion lifecycle: UI launches `nfm-collector.exe` via UAC on cold start; collector monitors UI parent process handle via `WaitForSingleObject` and exits immediately on UI close or crash; 3-second reconnect grace period cleans up collector if pipe disconnects while UI lives.
- [ ] System tray icon integrates with shared context menu and application lifecycle.
- [ ] `tools/quality.ps1` updated as the unified quality gate compiling C++20 and .NET 10, running cross-language protocol tests, unit tests, and producing `dist/NetworkFlowMonitor-<version>-win-x64-portable.zip`.
- [ ] Full Target-Host Acceptance test passes on a live Windows 11 environment (human visual acceptance, real UAC, real ETW, live SCM/portable lifecycle) without installation or lingering background processes.
- [ ] **Legacy C++ UI Source Retirement & Final Quality Cleanliness**: Final deletion of `src/ui/` legacy C++ UI production source is strictly gated upon:
  1. Milestones 01–04 implementation complete
  2. Automated Fast quality gate green (headless)
  3. Automated Full release quality gate green (headless)
  4. `WpfSurfaceSpike` accepted on live Windows 11 host
  5. Target-Host Acceptance accepted by human reviewer
  - Upon all 5 conditions met, delete remaining `src/ui/` legacy source, obsolete CMake targets, obsolete legacy UI resources, and remaining legacy-only test assets.
  - Finally rerun Fast and Full quality gates to certify that the repository compiles, tests, and packages 100% green without any legacy C++ UI dependencies.
