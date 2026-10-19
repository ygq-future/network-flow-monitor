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

## Ticket 03 — 完整贴边胶囊体验 — target-host acceptance pending

- 实现可配置贴边胶囊（默认 10 × 64 DIP，宽度 8–24 DIP、高度 40–160 DIP，端部圆润度受宽度一半约束，提供至少 16 DIP 向内有效命中区域）。
- 落地双向三段固定渐变与对数速率映射（Logarithmic Rate Mapping），实现速率上升 250 ms cubic ease-out、下降 450 ms quadratic ease-out 平滑重定向与 Reduced Motion 0 ms 立即切换。
- 构建确定性 C++ `DockCapsuleCoordinator` 行为模型，统一协调 `TrayOnly`、`CapsuleVisible`、`PanelVisible` 状态转换、Windows 系统拖动阈值、拖动跟随、释放点多显示器识别、左右停靠吸附与 Tie-Break 规则、任务栏安全边缘保留、Hover 展开（350 ms）/ Dismiss 收起（500 ms）/ Re-entry 取消收起与全屏应用置顶让出。
- 建立 Native 权威 `surfaceChanged` Bridge 投影；React 仅渲染 Native 选择的胶囊、面板或设置 Surface，Pin、Hover、Dismiss、拖动和窗口几何由 Host 统一协调。
- WebView 子窗口鼠标输入归一化进入 Win32 adapter，支持 pointer capture、取消、DPI/显示器重算、前台全屏检测和停靠位置持久化。
- 实现 340 DIP 基础监控面板与保留式原生上下文菜单（Show/Hide Monitor、Pin/Unpin、Settings、Collector Status/Retry、Exit），菜单弹出期间自动挂起 Dismiss 计时并在关闭后重新核验指针状态。
- 形成 ADR 0020：《贴边胶囊 Surface 拓扑、视觉合成与交互协调模型》。
- 编写 C++ 协调器与 Bridge 测试（29 项全部通过）以及前端单元/合约测试（11 个测试文件全部通过）。
- Fast Gate 11 项全部通过；Full Gate 19 项全部通过，包含 clang-tidy 0 diagnostics、MSVC `/analyze` 0 warnings、Release 构建与 29 项测试、便携包生成。
- Windows 11 24H2 x64 交互 Surface、High Contrast、RDP、device-loss fallback 与 idle CPU/working-set 验收待执行。
- 未自主执行 Git Commit，等待用户最终确认。

## Ticket 04 — 完整监控面板与产品交付 & 全量视觉/交互精修 — complete

- 完成 `RankingSnapshot` 端到端数据链路：C++ `CollectorCompanion` 接收并转换 `RankingSnapshotMessage`、`BridgeDispatcher` 序列化 `rankingSnapshot`、`NativeBridge` 与 React `BridgeContext` 实时同步 Top N 应用排行与会话重置清空。
- 修复贴边胶囊与监控面板核心交互：
  - 通过 50 ms Win32 物理屏幕坐标探测（`GetCursorPos` + `PtInRect` + `GetAsyncKeyState`）结合前端 `startWindowDrag` 命令，解决 WebView2 Chromium 子窗口吞噬鼠标进入/离开/拖拽消息的问题，确保胶囊与监控面板拖拽、左右贴边吸附、悬停展开（350 ms）与离开自动收起（500 ms）稳定触发。
  - 修复右键菜单跨 DPI 坐标偏移问题，直接使用 Win32 物理光标坐标 `GetCursorPos` 弹出原生菜单。
  - 修复关闭设置窗口后胶囊消失问题：退出设置或关闭未固定的监控面板时自动恢复至 `CapsuleVisible` 贴边胶囊态。
  - 修复设置面板失去焦点后无法 `Alt+Tab` 切回的问题：切换到设置视图时动态启用 `WS_EX_APPWINDOW` 并移除 `WS_EX_TOOLWINDOW`，处于胶囊/浮动面板态时恢复 `WS_EX_TOOLWINDOW`。
- 完成 Windows 11 原生黑白毛玻璃风格（Neutral Monochrome + DWM Acrylic）全量精修：
  - 浅色模式采用通透暖白胶囊底座（`rgba(255,255,255,0.94)`），深色模式采用曜石黑胶囊底座（`rgba(9,9,11,0.92)`）；移除生硬的中间分割线，上下行采用高区分度的曜石黑/冰白 ⇄ 天青蓝双向圆头流光胶囊柱（`rounded-b-full` / `rounded-t-full`）。
  - 实现 340 × 440 DIP 生产级监控面板：包含可拖拽顶栏、上下行总速率卡片、带相对流量进度底条与等宽右对齐数字的 Top N 应用程序实时流量排行榜、采集状态诊断条。
  - 重构自定义毛玻璃弹出式下拉组件 `Select`（彻底替换原生 `<select>`）、禁止按钮文字折行（`whitespace-nowrap`），紧凑化 780 × 500 DIP 设置中心布局，消除底部冗余留白。
- Fast Quality Gate 11 项全部通过（含 C++ 29 项测试与前端 11 个测试套件），最新前端静态产物已同步至 `build/windows-x64-debug/ui/` 与 `build/windows-x64-release/ui/`。

## 第二轮实机交互与视觉打磨 — complete

- **流量速率曲线平滑与自适应上限 (`rateMapping.ts` & `DockCapsule.tsx`)**：以 `512 B/s` 为感知底噪锚点并引入 1.35 次幂对数曲线与自适应峰值上限，使轻量流量平稳保持在 ~12%–20% 区间。
- **胶囊可自定义双色速度渐变 (`DockCapsule.tsx`, `SettingsCenter.tsx`)**：提供 5 组精选双色预设与自定义上下行颜色选择器，胶囊填充随速率平滑呈现渐变过渡。
- **高帧率丝滑拖拽与边缘细线自动隐藏 (`win32_window.cpp`, `dock_capsule_coordinator.cpp`, `DockCapsule.tsx`)**：提升主窗口光标追踪采样频率并保持抓握偏移；开启自动隐藏后保留 16 DIP 窗口命中区，胶囊向屏幕边缘平移收缩为 4px 边缘指示条，鼠标靠近即刻滑出唤醒。
- **胶囊与面板缩放过渡动画 (`BasicMonitorPanel.tsx`)**：根据停靠方向（`left center` / `right center`）配置弹性缩放与透明度过渡（`cubic-bezier(0.22, 1, 0.36, 1)`）。
- **紧凑型动态高度监控面板、真实应用图标与 FLIP 排序动画 (`collector_companion.cpp`, `BasicMonitorPanel.tsx`)**：
  - 统一为“应用程序 / ↓ 下载 / ↑ 上传”三列表格：首行展示总下载与上传速率，下方展示 Top N 应用，面板高度随活跃应用数量通过 `resizePanel` 动态伸缩；
  - C++ 端通过 `SHGetFileInfoW` + 32-bit ARGB DIB 提取真实 `.exe` 图标 Base64 数据并缓存，前端排行行位置变化时执行 340ms FLIP 平滑动画。
- **独立预热设置窗口与按需消息投递 (`product_host.cpp`, `webview_controller.cpp`, `win32_window.cpp`)**：
  - 设置中心采用独立 `Win32Window` + `WebView2Controller` 实例，在主窗口就绪后静默预热并通过 `ICoreWebView2Controller::put_IsVisible` 管理可见性，支持毫秒级即时打开与隐藏复用；
  - 所有 WebMessage 回调经 `post_ui_task` 异步派发，严格按可见表面门控 Bridge 状态广播（仅在设置可见时推送配置与状态，仅在面板展开时推送应用排行）。

## 第三轮视觉组件与全屏防误触增强 — complete

- **自定义 Fluent 弹出式拾色器组件 (`ColorPicker.tsx`, `SettingsCenter.tsx`)**：
  - 基于 `createPortal` 与视口自适应定位实现 `ColorPicker` 浮层组件，外部触发按钮展示紧凑方向标识与色点预览；
  - 弹窗内集成 2D 饱和度/明度（SV）拖拽画布、360° 色相（Hue）滑块条、12 色精选快捷色块与 `#RRGGBB` 十六进制精确输入框，全面适配深浅色主题。
- **全屏窗口识别与双配置防误触策略 (`app_config.h`, `config_codec.cpp`, `dock_capsule_coordinator.cpp`, `win32_window.cpp`, `product_host.cpp`)**：
  - 在「贴边胶囊」分类下提供两个全屏策略配置项：`floating.ignore_hover_when_fullscreen`（全屏时锁定并鼠标穿透，默认开启）与 `floating.hide_capsule_when_fullscreen`（全屏时隐藏胶囊，默认关闭）；
  - `Win32Window::is_foreground_fullscreen_on_same_monitor` 结合 `GA_ROOT`、`SHQueryUserNotificationState(QUNS_RUNNING_D3D_FULL_SCREEN)`、窗口矩形与客户端区域屏幕覆盖检测识别同显示器全屏应用与游戏；
  - 进入全屏时自动解除 `nfm_pinned` 固定状态并收起为胶囊：开启“全屏时锁定并鼠标穿透”时动态启用 `WS_EX_TRANSPARENT | WS_EX_LAYERED` 实现操作系统级鼠标穿透并锁定悬停/点击展开；开启“全屏时隐藏胶囊”时执行 `window_->hide()`，退出全屏后自动恢复。

## 第四轮窗口拖动原生级流畅度优化 — complete

- **统一接入 Windows 原生移动循环 (`win32_window.cpp`, `product_host.cpp`, `dock_capsule_coordinator.cpp`)**：
  - 监控面板顶栏拖拽与贴边胶囊拖拽均遵循系统拖拽阈值（`SM_CXDRAG` / `SM_CYDRAG`）及指针捕获，跨越阈值后立即切换至 `Win32Window::perform_system_move()`（`WM_SYSCOMMAND` `SC_MOVE | HTCAPTION`），由 Windows 内核与 DWM 直接驱动窗口位置，消除 `WM_TIMER` 系统时钟粒度限制；
  - 原生移动循环结束后自动采集成对的释放点物理坐标，触发 `DockCapsuleCoordinator::on_pointer_up` 完成多显示器识别、左右贴边吸附、Tie-Break 规则、工作区及自动隐藏任务栏边缘避让与配置持久化；未达阈值的胶囊点击仍立即展开面板。
- **消除拖动期间跨进程冗余重排与重复 Win32 调用 (`win32_window.cpp`, `webview_controller.cpp`)**：
  - `Win32Window` 缓存窗口位置、尺寸与 `topmost` 状态，仅在几何或置顶状态实际变化时调用 `SetWindowPos`；
  - `WebView2Controller::resize` 缓存宽高，避免仅窗口位移时每帧发起跨进程 `ICoreWebView2Controller::put_Bounds` 重算；新增 `WM_MOVE` -> `ICoreWebView2Controller::NotifyParentWindowPositionChanged` 同步宿主窗口位置。
- **清理子窗口冗余子类化消息 (`webview_controller.cpp`, `win32_window.h`)**：
  - 移除对父窗口自身的重复 `SetWindowSubclass` 与无消费的 `kWebSurfacePointerDownMessage`，避免监控面板非拖拽区点击误触发拖拽候选。

## 第五轮胶囊与监控面板双向形变缩放过渡优化 — complete

- **双层常驻同场形变过渡舞台 (`App.tsx`, `DockCapsule.tsx`, `BasicMonitorPanel.tsx`)**：
  - `<DockCapsule />` 与 `<BasicMonitorPanel />` 在浮动窗口中常驻挂载并锚定于停靠侧顶角（`fixed top-0 right-0 / left-0`），在渲染期同步驱动双向 CSS Transition；
  - 展开（Capsule → Panel）：监控面板从胶囊的真实宽高比（`scaleX = capsuleWidth / 340`、`scaleY = capsuleHeight / panelHeight`、32px 大圆角）经 `320ms cubic-bezier(0.16, 1, 0.3, 1)` 阻尼曲线放大展开至完整面板；
  - 收拢（Panel → Capsule）：收到 `surfaceChanged(capsule)` 同一帧内，监控面板以 `260ms cubic-bezier(0.36, 0, 0.66, -0.05)` 从完整面板坍缩回贴边胶囊尺寸并淡出，同时贴边胶囊同步浮现；
  - 严格遵循 `prefersReducedMotion`（开启减少动态效果时 `0ms` 立即切换）。
- **C++ 恒定宽度透明画布、逻辑命中区与动态鼠标穿透 (`product_host.h`, `product_host.cpp`, `win32_window.h`, `win32_window.cpp`)**：
  - 主浮动窗口保持 `340 DIP` 面板等宽透明画布并启用 `SWP_NOCOPYBITS`，使胶囊与面板在恒定水平画布内平滑过渡；
  - `Win32Window` 引入 `logical_hit_rect_` 与非命中区动态 `WS_EX_TRANSPARENT | WS_EX_LAYERED` 穿透：胶囊态下仅 `16 × 64` DIP 胶囊逻辑矩形响应鼠标悬停/点击/拖拽，其余透明画布区域直接穿透至底层桌面应用。

## 第六轮设置中心三分类整合、胶囊边框控制与诊断日志落盘 — complete

- **设置中心 3 大核心标签页 (`SettingsCenter.tsx`, `zh-CN.ts`, `en-US.ts`, `locales.ts`)**：
  - **通用 (`general`)**：集中提供登录自动启动、速率单位、应用主题、界面语言、排行显示数量 (Top N)、排行滑动窗口与刷新周期设置。
  - **贴边胶囊 (`capsule`)**：提供胶囊尺寸与圆角、悬停展开延迟、收起延迟、自动隐藏、胶囊边框显示开关 (`floating.show_capsule_border`)、双色渐变配色与全屏防误触策略。
  - **采集与诊断 (`collector`)**：一站式展示采集器进程状态、重试/重启操作、ETW 会话健康度、诊断日志级别与系统组件状态，并提供「打开日志目录」快捷按钮。
- **`openLogDirectory` 桥接命令、真实文件日志落盘与设置窗口拖拽增强 (`bridge_types.h/.cpp`, `native_bridge.h`, `bridge_dispatcher.cpp`, `product_host.h/.cpp`, `win32_window.cpp`)**：
  - 在 Bridge 白名单协议中注册 `openLogDirectory` 命令，`ProductHost::on_open_log_directory()` 自动创建并打开 `logs` 目录。
  - 实现 `ProductHost::write_diagnostic_log()`，按 `diagnostics.log_level`（`off` / `error` / `info` / `debug`）将宿主启动、设置变更、Collector 状态/重试与错误信息实时追加写入 `logs/nfm-host.log`。
  - 在 `Win32Window::perform_system_move()` 中释放当前线程鼠标捕获并激活窗口，为 `SettingsWindowListener` 接入系统拖拽阈值移动链路。
  - 设置窗口自适应高度上限设为 `680 DIP`，确保各标签页完整展示。

## 第七轮增量构建头文件依赖修复与全量测试无弹窗保护 — complete

- **修复 MSVC 中文环境 `/showIncludes` 依赖追踪失效 (`CMakePresets.json`, `tools/quality.ps1`)**：
  - 统一注入 `VSLANG=1033`，使 `cl.exe /showIncludes` 稳定输出英文 `Note: including file:`，消除因 GBK/UTF-8 编码错配导致的 Ninja `#deps 0` 及增量构建跳过重编译引发的 ABI 栈损坏问题。
- **全局抑制测试进程 CRT / Windows 模态错误弹窗 (`tests/test_headless_runtime.cpp`, `tests/CMakeLists.txt`)**：
  - 所有 C++ 测试目标自动链接 `test_headless_runtime.cpp`，在 `main()` 前配置 `SetErrorMode`、`_set_abort_behavior`、`_CrtSetReportHook2` 与 `_CrtSetReportMode(..., _CRTDBG_MODE_FILE)` + `_CRTDBG_FILE_STDERR`，严格保障 Headless 测试契约。
- **质量门禁与构建流水线轻量化提速 (`CMakeLists.txt`, `cmake/ProjectOptions.cmake`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `tools/quality.ps1`, `apps/nfm-ui/package.json`, `apps/nfm-ui/tsconfig.json`)**：
  - 禁用无用的 C++20 模块依赖扫描（`CMAKE_CXX_SCAN_FOR_MODULES OFF`），消除每个翻译单元额外的 `cl.exe -scanDependencies` 预处理开销；
  - 将原本分散的 31 个独立测试可执行文件合并为单个统一测试运行器 `nfm_tests.exe`（通过源文件级 `COMPILE_DEFINITIONS "main=nfm_suite_<name>"` 零侵入分发并保留全部 31 项 CTest 用例与标签），把全工程 `.exe` 链接目标从 33 个缩减到 3 个（`nfm-collector.exe`、`NetworkFlowMonitor.exe`、`nfm_tests.exe`），链接器总耗时从 35.3 秒降至 **2.3 秒**，并消除约 400 MB 重复 `.pdb` 磁盘写入；
  - 引入全仓库共享预编译头（`nfm_pch` + `REUSE_FROM`）预编译 `<windows.h>` 与核心 STL 头文件；
  - 缓存 Visual Studio DevShell x64 环境变量至 `build/.vsenv-x64.json`（含 `-Clean` 模式复用），跳过每次 1.7 秒的 `Enter-VsDevShell` 冷启动探测；
  - `clang-format` 改为单进程批量调用（消除 121 次进程拉起），`cmake` 仅在缺失 `build.ninja` 时执行重配置，`ctest` 启用 `--parallel`，`Full` 模式默认支持增量分析与构建；
  - 前端启用 TypeScript `incremental`（`.tsbuildinfo`）、ESLint `--cache`、Prettier `--cache`、移除 `build` 脚本中重复的 `tsc --noEmit`，并在 `bun.lock` 或前端源码未变化时按时间戳自动跳过重复 `bun install` 与 `vite build`；`Fast -Clean` 全量构建门禁从 ~41.4 秒降至 **~18.5 秒**（其中 C++ 全量 Clean 编译+链接仅需 **8.24 秒**），日常增量 `Fast` 门禁降至 **~8.3 秒**。

## 第八轮设置标题栏融合与监控面板交互精修 — complete

- **设置中心单行融合标题栏与可操作项置顶 (`SettingsCenter.tsx`)**：
  - 将「通用 / 贴边胶囊 / 采集与诊断」分段式切换按钮集成于 `36px (h-9)` 顶部可拖拽标题栏，动态窗口高度顶部基准设为 `38 DIP`；
  - 「贴边胶囊」页将「胶囊速率配色」置顶于首位，随后依次排列胶囊尺寸/圆角与悬停/收起/自动隐藏/全屏防误触交互项；
  - 「采集与诊断」页将可操作的「采集器进程状态（重试/重启）」与「诊断日志级别（含打开日志目录）」集中置于顶部，下方统一排列只读遥测指标。
- **监控面板单行速率顶栏、双语短标题与双色对数进度条 (`BasicMonitorPanel.tsx`, `zh-CN.ts`, `en-US.ts`)**：
  - 中英文主标题统一为「流量监视器 / Flow Monitor」；
  - 全局实时上下行总速率（`↓` 下载 / `↑` 上传）集成于顶部标题栏中部，在 `rankingSnapshot` 就绪时读取 `ranking.totalDownloadBytesPerSec` / `totalUploadBytesPerSec`，与下方应用列表保持同一滑动时间窗与刷新周期（1Hz）；
  - 应用列表直接紧接顶栏分割线，应用行采用 `rounded-md` 圆角悬浮背景高亮，底部进度条使用 `calculateRateFillPercent` 对数速率映射与下载/上传双色比例分段展示。
- **贴边胶囊纯净无框渲染 (`DockCapsule.tsx`, `app_config.cpp`, `nativeBridge.ts`)**：
  - 贴边胶囊采用 `p-0` 全幅圆角填充渲染。

## 第九轮开机启动跨路径清理、便携包数据目录收敛与高价值滚动诊断日志 — complete

- **开机启动全量治理 (`startup_registration.h/.cpp`, `product_host.cpp`, ADR 0013)**：
  - 新增 `StartupRegistration` 平台组件，管理 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 与用户 `Startup` 启动文件夹；
  - 关闭开机启动时：遍历 `Run` 注册表项与 `Startup` 文件夹，凡是键名匹配应用名（`NetworkFlowMonitor`）或命令行指向同名可执行文件（`NetworkFlowMonitor.exe`，无论位于哪个磁盘目录或使用何种键名）的启动项一律全部删除；
  - 开启开机启动与宿主启动同步时：先清理所有历史同名启动项，再将当前运行实例的带引号绝对路径写入 `NetworkFlowMonitor` 注册表值；若系统拒绝写入则记录 Win32 错误码并向前端报错，保持配置与注册表严格一致。
- **便携包应用根目录全量收敛 (`config_paths.h/.cpp`, `webview2_environment.cpp`, `product_host.cpp`, `tools/package.ps1`, ADR 0012)**：
  - 所有运行期数据与日志统一归口于便携包应用根目录：配置文件位于 `<app_root>/data/config.json`（兼容迁移根目录 `config.json`），WebView2 用户数据目录位于 `<app_root>/data/webview2`，日志位于 `<app_root>/logs/`；
  - 升级 `tools/package.ps1`：自动执行前端 `bun install --frozen-lockfile` 与 `bun run build`，构建 Release 便携包并校验归档条目完整性（`NetworkFlowMonitor.exe`、`nfm-collector.exe`、`ui/index.html`）。
- **滚动诊断日志器与核心故障日志 (`rotating_file_logger.h/.cpp`, `collector_companion.h/.cpp`, `webview_controller.h/.cpp`, `product_host.cpp`)**：
  - 实现线程安全的 `RotatingFileLogger`：单文件上限 `2 MiB`（`kDefaultMaxLogFileSizeBytes`），最多保留 `5` 个历史归档分卷（`nfm-host.1.log` ~ `nfm-host.5.log`）；
  - 聚焦记录系统与边界诊断事件（宿主启动环境摘要、配置损坏/保存失败及 Win32 错误码、开机启动注册表同步及清理计数、Collector 伴侣进程拉起 PID / `CreateProcessW` 错误码 / 异常退出码 `exit_code` / IPC 鉴权失败、ETW 丢包增量变化、WebView2 `ProcessFailed` 崩溃类型及自动恢复）。

## 第十轮监控面板恒定 340 DIP 防抖与丝滑高度过渡动画 — complete

- **速率 3 位有效数字格式化与 `>= 1000` 自动进位 (`formatRate.ts`, `formatRate.test.ts`, `LiveTracer.test.tsx`)**：
  - `< 100` 保留 1 位小数（如 `99.4 KB/s`），`100 ~ 999` 取整显示（如 `512 KB/s`），`>= 999.5` 自动进位至下一级单位（如 `1022.3 KB/s` -> `1.0 MB/s`），使任意极端流量下数字+单位恒定不超过 8 个字符。
- **监控面板宽度锁死 340 DIP 与顶栏定宽速率槽位 (`App.tsx`, `BasicMonitorPanel.tsx`, `BasicMonitorPanel.test.tsx`)**：
  - 移除 `App.tsx` 中覆盖面板宽高的 `w-full h-full`，将外层承载容器与 `BasicMonitorPanel` 严格锁定为 `w-[340px] min-w-[340px] max-w-[340px]`；
  - 顶栏下载与上传速率槽位固定为 `w-[66px] justify-end tabular-nums`，彻底消除随速率位数变化产生的面板宽度与文字位置抖动。
- **监控面板双向高度过渡动画与原生窗口延时收缩 (`BasicMonitorPanel.tsx`, `product_host.h`, `product_host.cpp`)**：
  - 前端根节点与列表区显式绑定像素高度（`height: ${desiredHeightDip}px` / `${contentHeightDip}px`）及 `height 240ms cubic-bezier(0.22, 1, 0.36, 1)` 阻尼过渡；
  - C++ `ProductHost::on_resize_panel` 在面板高度增加时立即扩展透明窗口画布，在面板高度减小时立即更新 `logical_hit_rect_` 并通过 `panel_shrink_bounds_remaining_ (240ms)` 延时收缩 Win32 窗口与 WebView2 视口，避免收缩动画被原生窗口提前裁切。

## 第十一轮普通权限双击启动、采集器动态原生 UAC 提权与跨 UAC 令牌 IPC 对齐 — complete

- **跨 UAC 拆分令牌（Split Token）的命名管道身份对齐 (`collector_runtime.cpp`, `collector_companion.cpp`, `windows_pipe_security.cpp`, `ipc_security_policy_tests.cpp`)**：
  - 普通权限 Host 与通过 UAC 提权运行的 `nfm-collector.exe` 拥有不同的 `Logon SID` 但共享完全相同的账户 `User SID (TokenUser)`；
  - 将便携模式的 `authorized_sid` 统一为当前账户 `TokenUser` SID，`VerifyPipeClient` 同时校验客户端 `user_sid` 与 `group_sids`，确保普通权限 Host 能与管理员权限 Collector 无缝建立 owner-scoped 命名管道连接，同时继续严格拒绝其他账户与远程连接。
- **采集器特权显式激活与一次性 ETW 持久授权 (`etw_authorization.h/.cpp`, `windows_etw_session.cpp`, `collector_main.cpp`, `src/CMakeLists.txt`)**：
  - 新增 `EnableCollectorPrivileges()`（通过 `AdjustTokenPrivileges` 显式激活当前令牌中的 `SeSystemProfilePrivilege`）并在 `StartTraceW` 前自动调用；
  - 当 `nfm-collector.exe` 以管理员权限启动时自动执行 `AuthorizeCollectorForCurrentUser()`（将当前用户加入本地 `Performance Log Users` 组、授予 `SeSystemProfilePrivilege` 并配置 ETW Session 与 `SystemTraceProvider` GUID DACL），使后续登录会话下的普通权限启动及 `HKCU\Run` 开机自启无需每次重复弹出 UAC。
- **采集器伴侣进程按需动态原生 UAC (`ShellExecuteExW` `"runas"`) 提权启动 (`collector_companion.cpp`)**：
  - `NetworkFlowMonitor.exe` 保持普通权限秒开 UI，不影响 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 开机自启；
  - `CollectorCompanion::start()` 优先以普通权限 `CreateProcessW` 启动 `nfm-collector.exe`；若进程因权限不足立即退出（`ERROR_ACCESS_DENIED (5)` 或 `ERROR_PRIVILEGE_NOT_HELD (1314)`），自动回退调用 `ShellExecuteExW(..., L"runas", ...)` 弹出 Windows 原生 UAC 授权窗拉起采集器；若用户拒绝 UAC（`ERROR_CANCELLED (1223)`），立即更新状态为 `Failed` 并写入诊断日志，用户随时可通过右键菜单或设置页点击「重试 / 重启」再次触发 UAC 授权。

## 第十二轮全仓规范统一、测试套件模块化收敛与中英双语文档完善 — complete

- **清理历史规范残留与碎片化文档 (`docs/architecture.md`, `docs/development.md`, `docs/progress.md`, `AGENTS.md`)**：
  - 移除 `.scratch/`、`docs/agents/`、`docs/decisions/`（14 个碎片 ADR 文件）、`docs/planning/`、`CONTEXT.md` 以及原 `docs/` 下拆分的 5 个微型文档（`code-quality.md`、`configuration.md`、`delivery.md`、`development-setup.md`、`target-host-acceptance.md`）；
  - 统一收敛为 `docs/` 根目录下的 3 个扁平权威文档：`docs/architecture.md`、`docs/development.md` 与 `docs/progress.md`。
- **移除死代码与模块化收敛 C++ 测试套件 (`src/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_headless_runtime.cpp`)**：
  - 删除未接入生产运行时的废弃 IPC 二进制协议遗留代码（`snapshot_codec.h/.cpp`、`application_catalog.h/.cpp` 及对应测试）与前端 Ticket 01 脚手架组件（`features/tracer/` 及 `LiveTracer.test.tsx`）；
  - 将剩余 29 个碎片化 C++ 测试源文件按架构模块无损合并为 10 个高内聚测试文件（`app_runtime_tests.cpp`、`config_tests.cpp`、`process_tests.cpp`、`ranking_tests.cpp`、`collector_tests.cpp`、`etw_integration_tests.cpp`、`ipc_protocol_tests.cpp`、`ipc_session_tests.cpp`、`ipc_integration_tests.cpp`、`host_tests.cpp`），保留全部断言与 CTest 并发锁策略。
- **完善中英双语 `README.md` 并通过 Full Quality Gate**：
  - 重写中英双语 `README.md`，完整涵盖核心特性、双进程架构图、构建与质量门命令、便携打包与仓库目录结构；
  - `tools/quality.ps1 -Mode Full` 19 项检查（含 100 个 C++ 文件格式检查、Debug/Release 构建与测试、前端 10 个测试套件、clang-tidy 0 诊断、MSVC `/analyze` 0 警告及 CPack ZIP 打包）全部通过。

## 第十三轮版本一致性门禁、GitHub Actions 自动发布流水线与通用设置在线自更新 — complete

- **全仓版本一致性门禁与 Apache-2.0 协议落地 (`CMakeLists.txt`, `cmake/dependencies.h.in`, `resources/network_flow_monitor.rc`, `tools/quality.ps1`, `tools/package.ps1`, `LICENSE`, `.github/release-note/v0.1.0.md`)**：
  - 根目录新增标准 **Apache License 2.0** 协议文件并纳入 CPack 便携归档必检条目；
  - 在 `resources/network_flow_monitor.rc` 中注入 `VS_VERSION_INFO`，通过 `cmake/dependencies.h.in` 注入 `NFM_APP_VERSION` 与 `NFM_GITHUB_REPO`（`ygq-future/network-flow-monitor`）；
  - 在 `tools/quality.ps1` 中新增 **Version consistency** 门禁，强制校验 `CMakeLists.txt`、`apps/nfm-ui/package.json`、`resources/network_flow_monitor.rc` 以及 `.github/release-note/v<version>.md` 四处版本号严格一致。
- **GitHub 仓库创建与 Quality & Publish 工作流 (`.github/workflows/quality-and-publish.yml`)**：
  - 通过 GitHub CLI 创建公开仓库 `https://github.com/ygq-future/network-flow-monitor` 并绑定 `origin` 远端；
  - 编写 `.github/workflows/quality-and-publish.yml`：每次代码推送与 PR 均执行 `quality` 任务（运行 `tools/quality.ps1 -Mode Full -Clean` 与 `tools/package.ps1 -Configuration Release` 产出 Windows x64 ZIP 包与 SHA256）；`publish` 任务通过 `needs: quality` 严格门控，仅在 `quality` 通过且推送 `v*` 标签（或手动勾选 `publish_release`）时读取 `.github/release-note/v<version>.md` 发布 GitHub Release。
- **通用设置在线检查更新、动态下载替换与自动重启 (`app_updater.h/.cpp`, `bridge_types.h/.cpp`, `bridge_dispatcher.h/.cpp`, `native_bridge.h/.cpp`, `product_host.h/.cpp`, `SettingsCenter.tsx`)**：
  - C++ Host 新增 `AppUpdater` 窄平台组件，基于 WinHTTP 请求 GitHub Releases API、解析 SemVer 与 `NetworkFlowMonitor-*-win-x64.zip` 资产、后台流式下载至 `data/update/` 并解压校验至 `data/update/staged/`；
  - 生成带 UTF-8 BOM 的脱离式 PowerShell 更新脚本，触发 Host 与 Collector 优雅退出后原地覆盖当前便携目录的可执行文件与 `ui/` 静态资源（保留 `data/config.json` 与 `logs/`），随后自动重新拉起新版 `NetworkFlowMonitor.exe`；
  - 扩展 Native/Web Bridge（`checkForUpdate`、`downloadAndApplyUpdate`、`updateStatus`），在设置中心「通用」标签页底部提供版本徽标、检查更新、下载进度与一键更新重启控件；
  - `tools/quality.ps1 -Mode Full` 20 项检查全部通过。
