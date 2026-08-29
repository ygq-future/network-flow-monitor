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

## Ticket 01 — 项目基础设施与实时链路 — complete

- 建立普通权限 `NetworkFlowMonitor.exe` C++20 Win32 产品宿主，按 `host/app`、`host/bridge`、`host/lifecycle`、`host/platform`、`host/webview`、`host/windows` 完成分层设计。
- 引入 Microsoft.Web.WebView2 SDK，实现 Evergreen Runtime 检测、可操作恢复指引、本地 `ui/` 虚拟主机映射（`https://nfm.local/`）与显式导航策略白名单。
- 建立 `apps/nfm-ui/` React + TypeScript + Vite + Tailwind CSS + Bun 前端工程，配置 ESLint Flat Config 与严格 Prettier 规范。
- 建立版本化封闭联合 Native/Web Bridge，具备方向校验、256 KiB 上限、结构化错误与 C++/TypeScript 双向契约测试。
- 建立 System/Light/Dark 语义 theme token 与 System/English/Simplified Chinese 双语国际化运行时。
- 实现 React Tracer 实时速率流呈现与状态更新隔离，并提供 Settings 路由骨架。
- 升级 `tools/quality.ps1`：Fast 模式 10 项全过，Full 模式 18 项（含 C++ 28 个测试、前端 5 个测试文件 11 个测试、clang-tidy 0 诊断、MSVC analyze 0 警告、CPack ZIP 发布包生成）全通过。
- 产出便携安装包包含 `NetworkFlowMonitor.exe`、`nfm-collector.exe`、`ui/` 静态资产与文档。

## Ticket 02 — 设置平台与应用外壳 — complete

- 完善权威 `AppConfig`、强类型校验器与 JSON Codec，新增 `capsule_end_rounding_dip`（范围 0–12 DIP，默认 5 DIP，有效值不超过宽度一半）与全套配置项读写。
- 扩展 Native/Web Bridge 封闭联合契约：实现白名单校验的 `updateSetting`、`requestConfig`、`restartCollector`，以及包含配置值与控制元数据（min/max/step/units/enums）的权威 `configChanged` 广播。
- Win32 Host 壳层落地：实现基于命名互斥量 `Local\NetworkFlowMonitor_SingleInstance_Mutex` 的单实例与已有窗口复用机制、系统托盘图标（带左键恢复与右键快捷菜单）、明确退出路径与便携采集器伴侣完整生命周期（启动、重试、重启、退出清理）。
- 实现 Fluent 风格前端共享组件库：`Button`、`IconButton`、`Select`、`Slider`、`Toggle`、`Checkbox`、`SettingsSection`、`SettingsRow`。
- 构建 7 大分类设置中心（General、Appearance、Capsule、Monitor、Ranking、Collector、Diagnostics），完全消费后端 Schema Projection，支持主题、双语语言即时切换、减少动态效果识别与采集器运行状态/ETW健康度监控。
- 质量门禁验证：Fast Gate（11 项全过）与 Full Gate（19 项全过，含 C++ 28 个测试全过、前端 7 个测试文件 23 个测试全过、clang-tidy 0 诊断、MSVC /analyze 0 警告、Release 编译测试与 CPack ZIP 打包）。
- 未自主执行 Git Commit，等待用户最终确认。

