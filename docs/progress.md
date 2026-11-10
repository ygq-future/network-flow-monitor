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

- **流量速率曲线与自适应上限 (`rateMapping.ts` & `DockCapsule.tsx`)**：以 `10 KB/s` 为感知底噪锚点，对数位置做 2 次幂映射并配合自适应峰值上限（下限 10 MB/s、上限 100 MB/s）；上限 10 MB/s 时 300 KB/s 约占半区 26%、1 MB/s 约 46%、10 MB/s 满格，背景小流量保持近乎静止。
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

## 第十四轮采集器二次启动防护、断连胶囊颜色清除、托盘/窗口稳定性与设置按钮收敛 — complete

- **防止二次启动采集程序破坏现有 ETW 会话并补齐异常诊断日志 (`collector_host.cpp`, `snapshot_publisher.cpp`, `collector_companion.cpp`, `collector_tests.cpp`, `host_tests.cpp`)**：
  - `CollectorHost::Start()` 调整为先启动 `server_->Start()`（通过 `FILE_FLAG_FIRST_PIPE_INSTANCE` 独占获取命名管道端点所有权），成功后再启动 `session_->Start()`；手动二次启动 `nfm-collector.exe` 时在管道独占检查阶段立即返回 `ERROR_ACCESS_DENIED (5)` 退出，不再抢停正在运行的 `NetworkFlowMonitor.SystemTrace` ETW 会话。
  - `SnapshotPublisher::RunDue()` 在 `provider_->Capture()` 失败时主动推送零速率 `TrafficTick`、空 `RankingSnapshot` 与 `etw_session_active = false` 的 `CollectorStatus`。
  - `CollectorCompanion` 补齐 `Backoff` 断线重连、子进程异常退出及 `etw_session_active = false` 的错误状态与 `logs/nfm-host.log` 落盘记录。
- **采集器断开或停止时同步清除贴边胶囊与监控面板流量颜色 (`collector_companion.cpp`, `BridgeContext.tsx`, `DockCapsule.tsx`, `BasicMonitorPanel.tsx`, `DockCapsule.test.tsx`)**：
  - `CollectorCompanion::stop()` 及 `StateChanged()` 进入非 `Streaming` 状态时，立即向前端推送零值 `TrafficTickDto`、空 `RankingSnapshotDto` 和非活跃 `CollectorStatusDto`。
  - 前端统一要求 `status.etwSessionActive && status.state !== 'Stopped' && status.state !== 'Failed'` 才视为活跃连接，断连或 ETW 失活时立即将速率与胶囊上下行颜色条高度清零。
- **修复关闭设置窗口后胶囊偶发消失及左键点击托盘图标无响应 (`dock_capsule_coordinator.cpp`, `product_host.cpp`, `win32_window.cpp`, `host_tests.cpp`)**：
  - `ProductHost::close_settings_window()` 隐藏设置窗口后立即重置收拢计时器并调用 `apply_coordinator_state()`、`publish_surface_state()` 与 `webview_->set_visible(true)` + `ensure_alive()`。
  - `DockCapsuleCoordinator::set_fullscreen_active_on_same_monitor()` 仅在刚进入全屏的边沿（`entered_fullscreen`）自动收拢面板，且 `show_monitor()` 主动清除 `is_click_through` 并激活前台焦点，`Win32Window` 过滤当前进程窗口与任务栏/外壳扩展组件的全屏误判。
- **合并设置中心重复的「重试连接」与「重启采集服务」按钮 (`SettingsCenter.tsx`, `SettingsCenter.test.tsx`)**：
  - 将「采集与诊断」标签页中后端行为完全相同的两个并排按钮合并为单一主操作按钮：运行正常时显示「重启采集服务」，断开或异常时自动显示「重试连接」。

## 第十五轮 WebView2 原生内存目标水位、隐藏态 TrySuspend 挂起与前端高频渲染隔离优化 — complete

- **WebView2 原生内存水位控制与隐藏态自动休眠 (`webview_controller.h`, `webview_controller.cpp`, `product_host.h`, `product_host.cpp`, `host_tests.cpp`)**：
  - 精简 `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS`，移除全局阻止 Chromium 回收后台渲染器与显存的 `--disable-renderer-backgrounding` 与 `--disable-backgrounding-occluded-windows`，并启用 `--js-flags=--lite-mode` 降低 V8 JIT 内存占用；
  - `WebView2Controller` 封装 `ICoreWebView2_19::put_MemoryUsageTargetLevel` 与 `ICoreWebView2_3::TrySuspend` / `Resume`：当 `settings_webview_` 预热完成处于隐藏态或用户关闭设置窗口时，自动切换为 `COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW` 并执行 `TrySuspend` 释放工作集与 GPU 合成缓存；用户打开设置时先 `Resume` 并恢复 `NORMAL` 水位，兼顾后台极低内存占用与毫秒级零闪烁打开体验；
  - 主窗口处于贴边胶囊态（`CapsuleVisible`）时将内存目标水位设为 `LOW`，展开监控面板（`PanelVisible`）时切回 `NORMAL`；处于 `TrayOnly` 或全屏隐藏态时自动挂起主 WebView 并停止推送 `trafficTick`。
- **前端高频 4Hz 流量节拍 Context 隔离与非活跃面板 DOM 卸载 (`BridgeContext.tsx`, `App.tsx`, `ProductSurface.test.tsx`)**：
  - 将 4Hz `TrafficTickContext` 和 1Hz `RankingSnapshotContext` 从主 `BridgeContext` 拆离，并对速率未变化的 `trafficTick` 跳过状态更新，避免每秒 4 次心跳触发无关组件树重渲染与 V8 临时对象分配；
  - `ProductSurface` 在胶囊收拢过渡动画（280ms）结束后自动从 DOM 卸载 `<BasicMonitorPanel />` 子树，并在面板重新展开时由 Host 补发最新 `RankingSnapshotDto`。
- **进程图标缓存容量收敛 (`collector_companion.cpp`)**：
  - 将 `ExtractExecutableIconDataUri` 图标缓存上限从 256 项收敛至 64 项并按条目淘汰，减少 Host 长期运行时的常驻内存体积。

## 第十六轮 v0.1.1 版本发布准备与质量门禁验收 — complete

- **全仓版本号同步升级至 `0.1.1` (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`)**：
  - 同步升级 CMake 工程版本、前端包清单、Windows 资源文件 `FILEVERSION`/`PRODUCTVERSION`（`0,1,1,0` / `"0.1.1"`）以及前端默认回退版本号。
- **撰写中英双语发布说明 (`.github/release-note/v0.1.1.md`)**：
  - 汇总 WebView2 原生内存目标水位与隐藏态 `TrySuspend` 自动休眠、4Hz 流量节拍 Context 隔离与非活跃面板 DOM 卸载、采集器二次启动管道独占保护、断连胶囊颜色清零、关闭设置/托盘左键唤醒修复与设置按钮精简。

## 第十七轮 v0.1.2 在线自更新零 PowerShell 依赖修复与版本发布 — complete

- **修复在线自更新下载完成后未替换文件与未自动重启问题 (`app_updater.h`, `app_updater.cpp`, `host_tests.cpp`)**：
  - 移除导致无控制台子进程立即退出的 `DETACHED_PROCESS` 标志，消除对 `powershell.exe` 及其模块的运行期依赖；
  - 解压阶段改用 Windows 11 原生内置 `tar.exe -xf`，替换与重启阶段改用 `CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP` 启动 UTF-8 `apply-update.cmd`（`tasklist` 等待主进程退出、`xcopy` 重试覆盖文件、`start` 重新拉起新版主程序并清理 `data/update`）；
  - 新增 `TestDetachedUpdateScriptExecution` 端到端测试验证无窗口后台脚本真实执行落盘。
- **同步升级版本号至 `0.1.2` 并撰写 `.github/release-note/v0.1.2.md`**：
  - 通过真机实测 `0.1.0 -> 0.1.1` 在线更新、文件替换与自动重启全链路验证，发布 `v0.1.2`。

## 第十八轮排除程序流量显示与统计过滤（Phase 1） — complete

- **权威配置与 JSON 持久化扩展 (`app_config.h`, `config_validation.cpp`, `config_codec.cpp`, `config_tests.cpp`)**：
  - 在 `RankingConfig` 中新增 `excluded_programs`（上限 `kMaximumExcludedPrograms = 128`，单项上限 `260` 字节），支持 `data/config.json` 读写校验与向后兼容默认空列表加载。
- **全量应用累计字节同步与排除程序实时过滤 (`protocol_types.h`, `frame_codec.cpp`, `snapshot_publisher.cpp`, `collector_companion.h/.cpp`, `product_host.h/.cpp`, `host_tests.cpp`)**：
  - `SnapshotPublisher` 在 `RankingSnapshotMessage` 中同步每个应用的累计 `total_download_bytes` 与 `total_upload_bytes`（含当前速率为 0 但产生过流量的活跃应用），为实时过滤与后续历史流量统计提供无损字节计数基础；
  - `CollectorCompanion` 实现 `is_application_excluded` 大小写不敏感匹配（支持进程文件名如 `mihomo-multi.exe`、不含后缀名或完整路径），实时从 `RankingSnapshot` 应用列表、全局上下行总速率以及 4Hz `TrafficTick` 中扣除被排除程序的速率，无需重启管理员采集进程即可即时生效；
  - `CollectorCompanion` 持续记录已观测到的联网应用清单（`connected_applications()`），供设置页随时检索选择。
- **Native/Web Bridge 扩展与设置中心排除程序管理 UI (`bridge_types.h/.cpp`, `bridge_dispatcher.h/.cpp`, `native_bridge.h/.cpp`, `SettingsCenter.tsx`, `SettingsCenter.test.tsx`)**：
  - 新增 `requestConnectedApplications`、`browseExcludedProgram`（调用 Win32 `IFileOpenDialog` 原生文件选择框选择 `.exe`）与 `connectedApplications` 桥接事件；
  - 在设置中心「通用」页新增「排除程序统计」折叠管理面板：支持展示已排除程序标签并一键移除、手动输入进程名添加、原生浏览 `.exe` 添加，以及对当前已连接程序列表进行模糊搜索筛选、点击选中后再点击「添加」确认排除。

## 第十九轮多级递进历史流量统计（Phase 2） — complete

- **持久化二维历史流量存储 (`traffic_history_store.h/.cpp`, `config_paths.h/.cpp`, `product_host.h/.cpp`, `host_tests.cpp`)**：
  - 实现 `TrafficHistoryStore`，以「本地自然日 (`YYYY-MM-DD`) + 归一化应用身份 (`executable:<path>` / `package-app:` / `app:`)」为二维存储粒度，基于 `RankingSnapshotMessage` 中各应用的累计 `total_download_bytes` 与 `total_upload_bytes` 实时差分累加；
  - 支持跨午夜自然日自动切分、Collector 会话重置（`on_session_reset`）无损续接、实时与导出双阶段自动剔除 `ranking.excluded_programs` 被排除程序，以及每 30 秒定时与宿主退出时向 `<app_root>/data/traffic_history.json` 原子写盘（`CreateFileW` 临时文件 + `FlushFileBuffers` + `ReplaceFileW` / `MoveFileExW`）。
- **Bridge 历史数据协议与原生右键菜单直达 (`bridge_types.h/.cpp`, `bridge_dispatcher.h/.cpp`, `native_bridge.h/.cpp`, `win32_window.h/.cpp`, `product_host.h/.cpp`)**：
  - 新增 `requestTrafficHistory` 命令與 `trafficHistoryUpdated` 事件，`SurfaceChangedDto` 新增 `route` 字段；
  - 原生右键菜单（托盘与浮动表面）新增「流量统计 / Traffic History」快捷入口，点击直接唤醒设置窗口并切换至「流量统计」标签页。
- **多级递进历史流量统计标签页 (`TrafficHistoryPanel.tsx`, `SettingsCenter.tsx`, `SettingsCenter.test.tsx`)**：
  - 在设置中心顶栏新增「流量统计 / Traffic History」标签页；
  - 支持「按时间统计 ⇄ 按应用总榜」双根维度切换，以及 **日 (`YYYY/MM/DD`) / 周 / 月 (`YYYY/MM`) / 季 (`YYYY Q1~Q4`) / 年 (`YYYY`)** 五级时间周期粒度聚合；
  - 支持多级递进下钻与面包屑导航（如：时间周期行 → 该周期内各应用流量排行 → 指定应用在各时间周期的历史明细；或：应用总榜 → 指定应用在各时间周期的历史明细）；
  - 支持按日期/应用名/进程名/路径模糊搜索筛选、按周期/应用名/下载/上传/总流量列头升序与降序排序、底部当前筛选合计汇总行，以及右侧双色水平比例条的 **对数比例（Logarithmic）** 与 **线性比例（Linear）** 切换。

## 第二十轮 v0.2.0 小版本发布与质量门禁验收 — complete

- **全仓版本号同步升级至 `0.2.0` (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`)**：
  - 将项目版本号从 `0.1.2` 升级至小版本 `0.2.0`，同步更新 CMake 工程版本、前端包清单、Windows 资源文件 `FILEVERSION`/`PRODUCTVERSION`（`0,2,0,0` / `"0.2.0"`）以及前端默认回退版本号。
- **撰写中英双语发布说明 (`.github/release-note/v0.2.0.md`)**：
  - 完整汇总多级递进历史流量统计（持久化二维账本、按时间/应用双根维度、日/周/月/季/年五级粒度、面包屑多级下钻、搜索排序、汇总底栏与对数/线性比例条切换）与排除程序流量显示及统计过滤（实时总速率/排行/历史账本全链路过滤、原生文件浏览与已连接应用模糊搜索添加）。
- **在线自更新多级代理与直连双向自动兜底 (`app_updater.h/.cpp`, `host_tests.cpp`)**：
  - 新增 `normalize_winhttp_proxy_server` 与 `build_http_access_candidates`，使检查更新与下载归档包按「系统自动代理 (`AUTOMATIC_PROXY`) $\rightarrow$ 环境变量/注册表 `ProxyServer`/本地代理端口 (`NAMED_PROXY`) $\rightarrow$ 强制直连 (`NO_PROXY`)」顺序自动重试兜底，解决未开启系统代理开关时直连 GitHub Release CDN 超时或代理失效导致更新失败的问题。

## 第二十一轮 贴边胶囊偶发消失的窗口表现自愈 — complete

- **现象与代码证据 (`product_host.cpp`, `win32_window.cpp`)**：
  - 用户配置为 `hide_capsule_when_fullscreen=false`，`TrayOnly` 在运行时不可达，因此胶囊消失并非协调器主动隐藏，而是 HWND 被外部（shell、全屏/远程桌面工具、DPI/显示切换）隐藏、最小化、移动或丢失置顶；
  - `ProductHost` 仅在状态迁移时调用 `apply_coordinator_state()`，`on_surface_clock_tick()` 只比较协调器状态，窗口被外部改动后没有任何自愈路径，只能等用户点击托盘触发 `show_monitor()` 才重新放映；
  - 1 秒看门狗原本调用 `window_->set_topmost(true)`，但 `Win32Window::set_topmost()` 只缓存“期望值”，实际丢失置顶样式后调用会直接返回；`set_bounds()` 同样只比较缓存请求，外部移动/缩放后会被判定为“无需移动”。
- **窗口状态以真实 Win32 事实为准 (`win32_window.h/.cpp`, `host_tests.cpp`)**：
  - 新增 `is_presented()`（`IsWindowVisible` + `IsIconic`）、`is_topmost()`（实时 `WS_EX_TOPMOST`）与 `get_bounds()`（`GetWindowRect`）；
  - `set_bounds()` 改为与实时窗口矩形比较（不再维护 `bounds_initialized_` / `last_*` 缓存），`set_topmost()` 同时校验实时扩展样式，外部改动后再次请求即可真正修复；
  - `WM_SETTINGCHANGE` 的 `SPI_SETWORKAREA` 重新枚举显示器并应用几何，覆盖任务栏移动/自动隐藏、分辨率与显示拓扑变化等不产生 `WM_DISPLAYCHANGE` 的工作区变化。
- **表现自愈看门狗 (`product_host.h/.cpp`)**：
  - 记录最近一次实际下发的窗口几何；1 秒看门狗检测「未呈现 / 置顶样式丢失 / 几何漂移」（拖动、收拢与面板收缩动画期间跳过），命中即重新 `apply_coordinator_state()` + `webview_->set_visible(true)` + `ensure_alive()` + `publish_surface_state()`；
  - 修复事件写入 `logs/nfm-host.log`（`Window presentation repaired: ...`，30 秒日志冷却），便于后续定位外部触发源；
  - `host_tests.cpp` 新增无窗口展示的 headless 用例，覆盖外部移动后的 `set_bounds` 复位与外部失去置顶后的 `set_topmost` 恢复。

## 第二十二轮 表面时钟改为实测真实时间差 — complete

- **问题与实测证据 (`win32_window.cpp`)**：
  - `SetTimer(8ms)` 受系统时钟节拍量化并会被合并，实测 2 秒内仅 127~128 次 `WM_TIMER`（平均 15.7ms/次），而 `on_surface_clock_tick()` 固定按 8ms 推进时间，导致所有交互计时约为配置值的 $1.96$ 倍：悬停展开 350ms → ≈685ms、自动隐藏 500ms → ≈980ms、收起 250ms → ≈490ms、表现自愈看门狗 1s → ≈1.95s、历史落盘 30s → ≈58.5s；
  - 原生移动循环、模态流程期间消息泵阻塞时 tick 完全缺失，固定步长还会让这段时间被永久丢弃。
- **实测时间差驱动的表面时钟 (`win32_window.h/.cpp`, `host_tests.cpp`)**：
  - `Win32Window` 在创建表面时钟时读取 `QueryPerformanceFrequency` 与起始 `QueryPerformanceCounter`，每次 `WM_TIMER` 调用 `next_tick_elapsed()` 计算真实间隔（`tick_elapsed_from_counter_delta()` 为纯函数，先限幅再换算以避免溢出，四舍五入到毫秒）；
  - 单帧间隔上限 `kMaximumTickElapsed = 250ms`，避免长时间阻塞后一次性完成全部交互计时器；`SetTimer` 仍只负责触发节奏与指针采样，协调器 API 与时序语义保持不变；
  - `host_tests.cpp` 新增纯函数用例，覆盖 8ms 精确换算、15.7ms 就近取整、负值与非法频率归零以及 250ms 上限钳制。

## 第二十三轮 UI 线程停顿诊断与 WebView2 轮询收敛 — complete

- **现象**：个别运行了一段时间的实例出现「胶囊/面板动画与流量排序流畅，但悬停后面板不收起、设置页与托盘点不动」；实测宿主 UI 线程处理 `WM_NULL` 需 ~1s，线程周期计数为 0（等待而非计算），同期显示器在 2560×1440@125% 与 2048×1152@96% 之间切换；旧版安装实例与当前 Release 构建均出现，重启后恢复，Debug/分析构建未复现。
- **停顿日志 (`win32_window.cpp`, `product_host.cpp`)**：表面时钟保留未钳制的真实间隔（诊断上限 10s），宿主在间隔 ≥750ms 时写 `[WARNING] UI thread stalled for ... ms between surface ticks`（5 秒冷却），用于把外部阻塞源（shell / IME-TSF / WebView2 浏览器进程 / DWM）和发生时刻关联起来。
- **WebView2 轮询收敛 (`webview_controller.cpp`)**：`ensure_alive()` 不再每秒调用 `get_Bounds()` 轮询浏览器进程，而是与最近一次下发的控制器尺寸比较，仅在父窗口客户区变化时 `resize()`；UI 线程因此不再在每次看门狗中等待跨进程 COM 往返，同时保留尺寸自愈能力。
- `host_tests.cpp` 补充诊断间隔换算用例（5s 间隔按 10s 诊断上限换算为 500ms）。

## 第二十四轮 设置页打开卡顿：历史图标提取与全量历史推送 — complete

- **实测定位 (`product_host.cpp`, `traffic_history_store.cpp`)**：
  - 用户打开设置后宿主 UI 线程停顿 **2.7s**（诊断日志 `UI thread stalled for 2707/2741 ms`）；同一时刻 WebView 渲染进程仍流畅；
  - 根因一：`export_dto(resolve_icons=true)` 在 UI 线程对历史中的**每个 executable 路径**调用 `SHGetFileInfoW` + 图标位图/PNG 编码；本机历史含 **1007 条日/应用记录、406 个不同路径**，首次导出即数秒；且设置打开路径会重复导出（导航回调、`requestConfig`、`broadcast_to_all_bridges` 每个桥各一次）；
  - 根因二：`record_snapshot` 在每次聚合快照（4Hz）返回 changed 时，设置窗口可见就推送**全量历史**（序列化 ~300KB + 渲染进程反复解析/重渲染）。
- **修复**：
  - `lifecycle::cached_executable_icon_data_uri()` 提供只读缓存的图标查询；历史导出改用该接口，**绝不在 UI 线程触碰 shell/文件系统**（实时排行路径仍按需提取并回填缓存）；
  - 历史推送限频 `kHistoryPushInterval = 30s`，且只发给设置桥；`on_request_config` 与排除程序变更后的广播不再重复导出/向所有桥推送历史；
  - 图标缓存容量从 64 提升到 256，覆盖实时应用集合。
- **实机验证（全新进程、冷缓存）**：设置窗口出现 207ms，打开 2.5s 后宿主与设置窗口 `WM_NULL` 均为 **0ms**，日志无停顿记录；设置页直接回显当前 Top N=10 等配置（不再先显示默认值数秒）。

## 第二十五轮 图标缓存持久化 + 后台预热 + 同名程序归并 — complete

- **持久化图标缓存 (`collector_companion.cpp`, `platform/atomic_file.*`)**：
  - 新增 `data/icon_cache.json`（`{schemaVersion, icons:{<lower path>: <data uri>}}`），启动时一次性载入内存；导出只读内存；新增图标标记 dirty，由预热线程分批与退出时原子落盘；
  - 原子写入/读取抽取为 `host/platform/atomic_file.{h,cpp}`，历史存储与图标缓存共用同一实现。
- **后台预热 (`product_host.cpp`)**：启动完成后由独立线程遍历历史与已连接程序的 executable 路径，仅对未缓存项调用 `SHGetFileInfo` 提取（每 16 个落盘一次），结束后强制刷新一次设置页历史；UI 线程全程不参与，退出时 `stop + join`。
- **同名程序归并 (`traffic_history_store.cpp`)**：历史键从「完整路径」改为「可执行文件名」（`exe:<name>`），同一程序不同安装路径在加载/记录时即合并字节数；历史文件下次保存后自动完成迁移。
- **测试与验证**：新增同名校验用例；全新进程冷缓存下设置窗口 202ms 出现、宿主/设置窗口响应 0–1ms，预热日志与 `icon_cache.json` 生成正常。

## 第二十六轮 v0.3.0 小版本发布与质量门禁验收 — complete

- **全仓版本号同步升级至 `0.3.0` (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`)**：
  - 将项目版本号从 `0.2.0` 升级至小版本 `0.3.0`，同步更新 CMake 工程版本、前端包清单、Windows 资源文件 `FILEVERSION`/`PRODUCTVERSION`（`0,3,0,0` / `"0.3.0"`）以及前端默认回退版本号。
- **撰写中英双语发布说明 (`.github/release-note/v0.3.0.md`)**：
  - 汇总自 `v0.2.0` 以来的 5 项用户可见改进：图标缓存持久化与后台预热、历史统计按可执行文件名归并、历史导出与推送移出 UI 线程、UI 线程停顿诊断与 WebView2 尺寸轮询收敛、实测帧差表面时钟与贴边胶囊表现自愈。
- **质量门禁**：`tools/quality.ps1 -Mode Fast` 12/12 通过（版本一致性 `v0.3.0`、clang-format、Prettier、ESLint 0 warning、`tsc --noEmit`、Vitest 10 passed、Debug 构建与 8 组 C++ 测试）；`-Mode Full` 20/20 通过（clang-tidy 0 diagnostics、MSVC `/analyze` 0 warnings、Release 构建与 8 组测试、CPack 打包）。
- **便携发行包 (`tools/package.ps1`)**：`dist/NetworkFlowMonitor-0.3.0-win-x64.zip`（884352 字节，SHA256 `73D2E5FF136582F07A77DA773A44F9822E94DA05DCB3425D65233CBFD8FAA4A9`），归档内含 `NetworkFlowMonitor.exe` / `nfm-collector.exe`（文件版本 `0.3.0.0`）、`ui/`、`README.md`、`LICENSE`。
- **发布流程**：提交版本升级与发布说明后推送 `v0.3.0` tag，由 `.github/workflows/quality-and-publish.yml` 在 CI 上重跑 Full 门禁、重新打包并以该归档与发布说明创建 GitHub Release。

## 第二十七轮 v0.3.1 贴边胶囊隐形与唤醒五路根因修复及发布 — complete

- **实机诊断结论**：
  - 排查本机 `v0.3.0` 运行日志 `logs/nfm-host.log`（537 行）发现第 21 轮新增的 `Window presentation repaired` 触发次数为 `0`，证明胶囊隐形时外层宿主 `HWND` 仍处于 `Visible=true, Iconic=false, WS_EX_TOPMOST=true` 且 `GetWindowRect` 与缓存几何一致，仅靠点击托盘执行 `on_show_monitor()` 才能恢复。
- **五路根因与修复 (`product_host.cpp`, `webview_controller.cpp`, `win32_window.cpp`, `surface_types.h`)**：
  1. **WebView2 子窗口视口漂移与调用顺序修复 (`product_host.cpp`, `webview_controller.cpp`)**：`apply_coordinator_state()` 改为先调用 `window_->set_bounds()` 更新父窗口客户区，再调用 `webview_->resize()` 下发视口尺寸；`WebView2Controller::ensure_alive()` 与 `resize()` 补充基于本地 Win32 子窗口 `GetClientRect(child)` 和 `IsWindowVisible(child)` 的零跨进程 COM 校验，一旦子窗口尺寸与父窗口客户区不一致或不可见，立即重新下发 `put_Bounds` 与 `put_IsVisible(TRUE)`。
  2. **同帧收缩完成吞掉表面状态广播修复 (`product_host.cpp`)**：将 `on_surface_clock_tick` 中的协调器状态变更判断独立为 `coordinator_state_changed`，当 `panel_shrink_bounds_remaining_`（240ms）或 `collapse_bounds_remaining_`（260ms）与 `dismiss_timer` / `auto_hide_timer` 在同一 tick 到期时，始终触发 `publish_surface_state()`，保证前端正常回到胶囊态。
  3. **可见胶囊保持活跃内存与合成状态 (`product_host.cpp`, `webview_controller.cpp`)**：遵循 WebView2 SDK 契约，主窗口在 `CapsuleVisible` 与 `PanelVisible` 可见期间均保持正常活跃状态，仅在窗口隐藏（`TrayOnly` / `is_fullscreen_hidden` 或隐藏的设置预热窗口）时通过 `TrySuspend` / `Resume` 管理生命周期。
  4. **看门狗显示器拓扑同步、DWM Cloak 检查与 Z 序重申 (`product_host.cpp`, `win32_window.h/.cpp`, `surface_types.h`)**：`ensure_window_presentation()` 每秒比对最新 `Win32Window::enumerate_monitors()`，捕获分辨率/DPI/工作区切换；`Win32Window::is_presented()` 补充 `DWMWA_CLOAKED == 0` 校验；`set_topmost(true, true)` 支持在修复时强制重申 `HWND_TOPMOST` Z 序。
  5. **同屏全屏检测收敛为前台窗口几何判定 (`win32_window.cpp`)**：`is_foreground_fullscreen_on_same_monitor()` 严格以同一显示器上的前台窗口实际覆盖显示器矩形为准，消除高频 Shell 跨进程查询导致的 UI 线程周期性停顿，并保证从全屏应用切回桌面后自动隐藏胶囊正常响应悬停。
- **回归测试 (`tests/host_tests.cpp`)**：新增 `CheckCollapseFinishedDoesNotSuppressSurfacePublish` 与置顶窗口 Z 序重申用例。
- **v0.3.1 版本发布 (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`, `.github/release-note/v0.3.1.md`)**：
  - 全仓版本号升级至 `0.3.1`，撰写中英双语发布说明 `.github/release-note/v0.3.1.md`。
  - `tools/quality.ps1 -Mode Fast`（12/12）与 `-Mode Full`（20/20）全部通过；`tools/package.ps1 -Configuration Release` 生成便携归档 `dist/NetworkFlowMonitor-0.3.1-win-x64.zip`（885493 字节，SHA256 `37E5FF0D9683860D67953592E8AAB7A80839194936B414E16E72EE9B9EDEFA76`）。

## 第二十八轮 贴边胶囊 Z 序下沉（保留 `WS_EX_TOPMOST` 标志位与 `Win+D` 瞬时闪现）根因定位与修复 — complete

- **实机复现与根因证据 (`win32_window.cpp`, `product_host.cpp`)**：
  - **现象特征完全吻合 Z 序遮挡**：用户反馈胶囊偶发消失后，第一次按 `Win+D`（显示桌面）仍不可见，第二次按 `Win+D`（还原窗口）会瞬间闪现胶囊随即再次消失，唯有手动点击托盘打开监控面板才能恢复，且正常显示时 `Win+D` 完全不影响胶囊；
  - **根因一（`GW_OWNER` IME 链拖拽导致保留 `WS_EX_TOPMOST` 下沉，看门狗漏检）**：Windows 为宿主 UI 线程自动创建的隐藏顶层窗口 `Default IME`（`GW_OWNER = NetworkFlowMonitor_HostWindow`）及 `MSCTFIME UI` 位于 Band 1 Z 序最底部（`Progman` 之下）。当输入法/焦点切换在未携带 `SWP_NOOWNERZORDER` 的情况下重排被拥有窗口时，Windows 会将宿主窗口连带拖入非置顶窗口（如 Chrome / Windows Terminal）之下、`Progman` 之上，但宿主窗口的 `GetWindowLongPtrW(GWL_EXSTYLE) & WS_EX_TOPMOST` 位仍保持为 `true`（`0x000800A8`）。因此 `Win32Window::is_topmost()` 始终返回 `true`，1 秒看门狗完全不触发，日志亦无 `Window presentation repaired` 记录；
  - **`Win+D` 两次按键的逐帧物理机制（实机 15ms 像素采样复现）**：当宿主窗口处于「普通窗口之下、`Progman` 之上」时，平时被最大化/靠右窗口遮挡；第一次按 `Win+D` 显示桌面时，Explorer 将桌面窗口 `Progman` 提至非置顶 Z 序最顶层（宿主窗口之上），桌面壁纸继续遮挡胶囊；第二次按 `Win+D` 还原窗口时，Explorer 先将 `Progman` 压回 Z 序最底层（宿主窗口之下），再对普通窗口执行约 150ms 的还原动画——在 `Progman` 已下沉而普通窗口尚未铺满屏幕的 `120ms~135ms` 间隙，胶囊瞬间显露 1~2 帧，随后立即被还原的普通窗口再次覆盖；
  - **根因二（缺少 `SWP_NOOWNERZORDER` 导致后台进程置顶自愈失效）**：实机验证表明，当宿主窗口被压到 `Progman` 及其拥有的 `Default IME` 窗口之下且宿主处于后台时，不带 `SWP_NOOWNERZORDER` 的 `SetWindowPos(hwnd_, HWND_TOPMOST, ..., SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)` 虽返回 `TRUE`，但因 owner-group Z 序约束无法将宿主窗口提回 `HWND_TOPMOST`，唯有附加 `SWP_NOOWNERZORDER`（或点击托盘触发 `SetForegroundWindow`）才能成功恢复。
- **修复 (`src/host/platform/win32_window.cpp`, `tests/host_tests.cpp`)**：
  - `Win32Window::is_topmost()` 除校验 `WS_EX_TOPMOST` 扩展样式位外，对可见窗口沿 `GW_HWNDPREV` 向上遍历前序可见、未最小化且未 Cloak 的顶层窗口：一旦发现前序存在任何非置顶窗口（说明宿主窗口已下沉至普通窗口或 `Progman` 之下），或存在与宿主窗口矩形相交的非系统托盘置顶窗口，立即返回 `false` 触发 1 秒看门狗自愈；
  - `Win32Window::set_topmost()`、`set_bounds()`、`set_click_through()`、`set_taskbar_visibility()`、`configure_surface_chrome()` 及表面时钟鼠标穿透切换中的全部 `SetWindowPos` 调用统一附加 `SWP_NOOWNERZORDER`，彻底切断 `Default IME` / `MSCTFIME UI` 拥有者链对宿主窗口 Z 序的干扰，并确保后台置顶修复 100% 生效；
  - `tests/host_tests.cpp` 扩展 `CheckWindowPresentationRecovery`，新增模拟拥有者子窗口（`NFM_OwnedHelper`）不带 `SWP_NOOWNERZORDER` 下沉至 `HWND_BOTTOM` 拖累宿主窗口后，通过 `is_topmost()` 检测并由带 `SWP_NOOWNERZORDER` 的 `set_topmost(true)` 恢复置顶的回归测试。

## 第二十九轮 v0.3.2 贴边胶囊 Z 序下沉修复发布与质量门禁验收 — complete

- **全仓版本号同步升级至 `0.3.2` (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`)**：
  - 同步升级 CMake 工程版本、前端包清单、Windows 资源 `FILEVERSION`/`PRODUCTVERSION`（`0,3,2,0` / `"0.3.2"`）以及前端默认回退版本号。
- **撰写中英双语发布说明 (`.github/release-note/v0.3.2.md`)**：
  - 汇总第 28 轮「`is_topmost()` 实时 Z 序校验」与「全部 `SetWindowPos` 附加 `SWP_NOOWNERZORDER`」修复，并记录连续多日实机观察无胶囊消失的稳定性结论。
- **质量门禁**：`tools/quality.ps1 -Mode Fast` 12/12 通过（版本一致性 `v0.3.2`、clang-format 106 文件、Prettier、ESLint 0 warning、`tsc --noEmit`、Vitest 10 passed、Debug 构建与 8 组 C++ 测试）；`-Mode Full` 20/20 通过（clang-tidy 0 diagnostics、MSVC `/analyze` 0 warnings、Release 构建与 8 组测试、CPack 打包）。
- **便携发行包 (`tools/package.ps1 -Configuration Release`)**：`dist/NetworkFlowMonitor-0.3.2-win-x64.zip`（886028 字节，SHA256 `33C4E2774D58CD6109B8336AAA3E6AAD078B717A02EFE6B2F408E04AB38D9A61`），归档内 `NetworkFlowMonitor.exe` 与 `nfm-collector.exe` 文件版本均为 `0.3.2.0`。
- **发布流程**：提交版本升级与发布说明后推送 `v0.3.2` tag，由 `.github/workflows/quality-and-publish.yml` 在 CI 上重跑 Full 门禁、重新打包并以该归档与发布说明创建 GitHub Release。

## 第三十轮 监控面板右键菜单被面板覆盖修复 — complete

- **现象与根因 (`win32_window.cpp`, `product_host.cpp`)**：右键菜单由 `TrackPopupMenu` 模态弹出，期间 8 ms 表面时钟仍在分发，1 秒看门狗 `ensure_window_presentation()` 继续运行；v0.3.2 的 `is_topmost()` 会把任何与宿主矩形相交的置顶窗口判为「置顶丢失」，而系统菜单窗口类 `#32768` 不在豁免名单内，看门狗随即 `set_topmost(true, true)` 把面板重新顶到菜单之上。340 DIP 宽的监控面板必然与菜单相交，胶囊仅 10 DIP 宽因此基本不受影响。
- **修复**：`is_topmost()` 豁免 `#32768` 弹出菜单窗口；`ensure_window_presentation()` 在 `is_context_menu_open` 期间直接跳过修复，菜单关闭后恢复 1 秒巡检。
- **回归测试 (`host_tests.cpp`)**：`CheckWindowPresentationRecovery` 新增真实 `TrackPopupMenu` 弹出菜单覆盖可见宿主窗口的用例，菜单显示期间采样 `is_topmost()`；移除豁免后该用例失败，恢复后通过。
- **质量门禁**：`tools/quality.ps1 -Mode Fast` 12/12 通过（C++ 8 组测试、前端 10 个测试套件）。

## 第三十一轮 v0.3.3 右键菜单覆盖修复与胶囊速率曲线平稳化发布 — complete

- **全仓版本号同步升级至 `0.3.3` (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`)**：
  - 同步升级 CMake 工程版本、前端包清单、Windows 资源 `FILEVERSION`/`PRODUCTVERSION`（`0,3,3,0` / `"0.3.3"`）以及前端默认回退版本号。
- **撰写中英双语发布说明 (`.github/release-note/v0.3.3.md`)**：
  - 汇总第 30 轮「右键菜单不再被监控面板覆盖」修复与「胶囊速率曲线以 `10 KB/s` 为底噪、2 次幂对数映射」的体验改进。
- **质量门禁**：`tools/quality.ps1 -Mode Full` 20/20 通过（版本一致性 `v0.3.3`、clang-format 106 文件、Prettier、ESLint 0 warning、`tsc --noEmit`、Vitest 10 passed、Debug 构建与 8 组 C++ 测试、clang-tidy 0 diagnostics、MSVC `/analyze` 0 warnings、Release 构建与 8 组测试、CPack 打包）。
- **便携发行包 (`tools/package.ps1 -Configuration Release`)**：`dist/NetworkFlowMonitor-0.3.3-win-x64.zip`（886060 字节，SHA256 `D803A43F6BE782E76BCF91C8C4E99F609797F059EDA9599CD5D13DBB21B8B91D`）。
- **发布流程**：提交版本升级与发布说明后推送 `v0.3.3` tag，由 `.github/workflows/quality-and-publish.yml` 在 CI 上重跑 Full 门禁、重新打包并以该归档与发布说明创建 GitHub Release。

## 第三十二轮 LSP (`clangd`) 诊断对齐与 `project-quality` 全量质量门禁加固 — complete

- **根因诊断**：
  1. **编辑器 `clangd` 报错根因**：CMake 将 `compile_commands.json` 生成在 `build/windows-x64-*` 子目录，且 `windows-x64-debug` / `windows-x64-release` 启用了 MSVC 专属预编译头参数（`/Yu`、`/Fp`、`/FI...cmake_pch.hxx`）及 `/Zc:preprocessor`、`/external:anglebrackets`；仓库根目录此前缺少 `.clangd`，导致编辑器 `clangd` 回退为无 `-Iinclude` 的裸文件解析（报 `collector_runtime.h file not found` 及 `std::wstring_view` / `nfm` 级联未声明错误），同时指出 `src/app/collector_main.cpp` 中 `<span >` 头文件未使用。
  2. **`clang-tidy` 门禁漏报根因**：`.clang-tidy` 此前仅将 5 个前缀设为 `WarningsAsErrors`，其余 `readability-*`、`modernize-*`、`misc-*`、`cppcoreguidelines-*` 仅输出 warning 而不返回非 0 退出码；同时 `cmake/StaticAnalysis.cmake` 仅在传入 `ENABLE_EXCEPTIONS` 时附加 `--extra-arg=/EHsc`，导致 `nfm_config`、`nfm_ranking`、`nfm_tests` 缺失 `/EHsc`。
- **修复与质量契约加固**：
  - 新增根级 `.clangd`：指向无 PCH 的 `build/windows-x64-tidy` 编译数据库，剔除 MSVC 专属预编译头与驱动参数并启用 `UnusedIncludes: Strict`；`tools/quality.ps1 -Mode Fast` 自动确保 `build/windows-x64-tidy/compile_commands.json` 生成。
  - `cmake/StaticAnalysis.cmake` 统一为所有目标向 `clang-tidy` 注入 `--extra-arg=/EHsc` 与 `--extra-arg=-Wno-unused-command-line-argument`；`.clang-tidy` 升级为 `WarningsAsErrors: '*'`。
  - 清理 `src/` 与 `tests/` 全部 C++ `clang-tidy` / `clangd` 诊断（移除 `collector_main.cpp` 未使用的 `<span>`，修复 `RankingEngine` 构造、`ipc_server`、`traffic_history_store`、`collector_companion`、`startup_registration`、`win32_window`、`webview_controller` 及测试套件中的全部异常边界与代码规范诊断），实现全仓 55 个 C++ 编译单元 0 警告、0 错误。
  - `tools/quality.ps1` 新增 `-Mode Commit`（支持 `-CommitMsgFile`、`-CommitMessage`、`-CommitRange` Conventional Commits 校验），并在 `C++ Debug build`、`clang-tidy`、`MSVC analyze` 阶段同时校验退出码与输出 0 警告/0 诊断。
  - 新增 `.githooks/pre-commit`、`commit-msg`、`pre-push` 并配置本地 `core.hooksPath = .githooks`；`.github/workflows/quality-and-publish.yml` 接入提交范围校验；`apps/nfm-ui/.prettierrc` 显式声明 Tailwind v4 `tailwindStylesheet: "./src/app/index.css"`；同步更新 `docs/development.md` 与 `AGENTS.md`。

## 第三十三轮 贴边胶囊非对称 EMA 平滑与柔顺回落阻尼 — complete

- **现象与根因 (`DockCapsule.tsx`, `rateMapping.ts`, `BridgeContext.tsx`)**：
  - 贴边胶囊直接消费 250ms 采样的 `trafficTick`，当后台网络或内核 ETW 缓冲区在单个 250ms Tick 内突发刷出一批数据包、下一 Tick 立即归零时，瞬时速率被 $\times 4$ 放大后紧接着归零，且 `BridgeContext` 此前在连续相同数值的 `0` Tick 上跳过了状态更新，导致胶囊进度条出现「瞬间猛涨又瞬间砸回 0」的抽搐感。
- **修复 (`rateMapping.ts`, `DockCapsule.tsx`, `BridgeContext.tsx`)**：
  - 在 `rateMapping.ts` 新增 `smoothCapsuleRate` 非对称指数移动平均（Attack $\alpha = 0.42$，Release $\alpha = 0.28$，空闲归零阈值 `512 B/s`），并将上升过渡调整为 `300ms`、回落过渡延长至 `650ms cubic-bezier(0.22, 1, 0.36, 1)`。
  - `BridgeContext.tsx` 納入 `tick.seq` 比较，确保每个 250ms Tick 都能驱动胶囊平滑衰减直至归零。
  - `DockCapsule.tsx` 按 `tick.seq` 对上下行速率应用非对称 EMA 平滑，在 Collector 断开或停止时立即清零。
- **测试验证 (`rateMapping.test.ts`, `DockCapsule.test.tsx`)**：补充单 Tick 脉冲抑制与多 Tick 连续柔顺衰减单测，`tools/quality.ps1 -Mode Fast` 13/13 通过。

## 第三十四轮 v0.3.4 胶囊非对称 EMA 平滑与全仓零诊断质量门禁发布 — complete

- **全仓版本号同步升级至 `0.3.4` (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`)**：
  - 同步升级 CMake 工程版本、前端包清单、Windows 资源 `FILEVERSION`/`PRODUCTVERSION`（`0,3,4,0` / `"0.3.4"`）以及前端默认回退版本号。
- **撰写中英双语发布说明 (`.github/release-note/v0.3.4.md`)**：
  - 汇总第 32 轮「LSP (`clangd 23`) 与 `clang-tidy` 全仓零诊断对齐、Git Hooks 与 Commit 规范门禁」与第 33 轮「贴边胶囊非对称 EMA 流量平滑与柔顺回落阻尼」。
- **质量门禁**：`tools/quality.ps1 -Mode Full` 20/20 通过（版本一致性 `v0.3.4`、clang-format 106 文件、Prettier、ESLint 0 warning、`tsc --noEmit`、Vitest 10 passed、Debug 构建与 8 组 C++ 测试、clang-tidy 0 diagnostics、MSVC `/analyze` 0 warnings、Release 构建与 8 组测试、CPack 打包）。
- **便携发行包 (`tools/package.ps1 -Configuration Release`)**：`dist/NetworkFlowMonitor-0.3.4-win-x64.zip`（886416 字节，SHA256 `8A0122C05C43B89359038D3952C66BBDDA47F7A0D822400CDCFCB1FA5107766E`）。
- **发布流程**：提交版本升级与发布说明后推送 `v0.3.4` tag，由 `.github/workflows/quality-and-publish.yml` 在 CI 上重跑 Full 门禁、重新打包并以该归档与发布说明创建 GitHub Release。

## 第三十五轮 原生任务栏实时网速窗口 (`TaskbarSpeedWindow`) 实现 — complete

- **需求与设计边界**：
  - 提供极简、透明融合任务栏背景、跟随主题黑白文字配色的任务栏实时网速展示，仅在设置中心「通用」页暴露单个开关 `display.show_taskbar_speed`（`Show Taskbar Speed / 显示任务栏网速`）。
  - **原生任务栏父子挂载 (`SetParent` + 相对坐标，与任务栏浑然一体)**：采用 `WS_POPUP` + `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`（不设置独立 `WS_EX_TOPMOST`），在 Windows 10 与 Windows 11 下统一通过 `SetParent` 直接挂载到 `Shell_TrayWnd`，并在 `SetParent` 前将窗口收缩至 `1 × 1`、挂载后通过 `MoveWindow` 展开至目标尺寸以强制分配子窗口分层重定向位图；仅避让 Windows 原生任务栏组件（`TrayNotifyWnd`、`Start`、`Widgets`），保持 `MSTaskSwWClass` 原生几何不变。窗口坐标完全使用相对于 `Shell_TrayWnd` 左上角的局部坐标，因此无论是任务栏自动隐藏滑出/滑入、F11 全屏被遮挡，还是在全屏下按 `Win` 键随开始菜单浮出，均由系统 DWM 与任务栏合成层原生联动，无任何“原地突兀显隐”感。
  - **ClearType 锐利字体与三栏固定锚点防跳动 (`paint_window`)**：采样任务栏真实背景色 (`GetPixel` + `SetLayeredWindowAttributes(..., LWA_COLORKEY)`) 作为双缓冲画布底色，使 9pt `微软雅黑` (`DEFAULT_QUALITY`) 获得完整的 ClearType 亚像素抗锯齿锐度；采用「左对齐箭头前缀 (`↑:`/`↓:`) + 右对齐数值列 + 左对齐单位列 (`KB/s`/`MB/s`)」固定三栏坐标消除数字位数变化引起的单位跳动；与监控面板共享相同的 1 Hz `RankingSnapshotDto` 滑动窗口总速率数据源。
  - **交互联动 (`ProductHost`)**：左键点击网速文字直接切换 340 DIP 监控面板的展开与收起（并在 `on_surface_outside_click` 中豁免任务栏网速窗区域防止外部点击冲突），右键点击弹出原生上下文菜单。
- **测试与质量门禁 (`tests/config_tests.cpp`, `tests/host_tests.cpp`, `SettingsCenter.test.tsx`)**：
  - 新增 `display.show_taskbar_speed` 配置编解码单测、`taskbar_speed_window_suite` 速率格式化与 Win10/Win11 左右相对坐标布局单测，以及前端设置开关交互单测。

## 第三十六轮 贴边胶囊速率曲线三级幂次校准与 1 秒滚动窗口脉冲抑制 — complete

- **现象与根因 (`rateMapping.ts`, `snapshot_publisher.cpp`)**：
  - 贴边胶囊对数速率映射此前采用 2 次幂曲线（`RATE_CURVE_EXPONENT = 2`），在默认 `10 MB/s` 上限下，`1 MB/s` 静态占据半区的 `46.1%`；同时 `SnapshotPublisher` 的 4 Hz `TrafficTick` 直接用相邻 250ms 样本差分计算瞬时速率，当 1 秒内的内核 ETW 数据包集中在单个 250ms Tick 刷出时，瞬时速率被 $\times 4$ 放大至 `3~4 MB/s`，叠加较快的上升系数（`CAPSULE_ATTACK_ALPHA = 0.42`）与慢释放动画，导致任务栏显示约 `1 MB/s` 时胶囊半区视觉填充已达约 `80%`。
- **修复 (`rateMapping.ts`, `snapshot_publisher.h`, `snapshot_publisher.cpp`)**：
  - 将 `rateMapping.ts` 的 `RATE_CURVE_EXPONENT` 提升为 `3`（对数三次方映射），使默认 `10 MB/s` 上限下 `300 KB/s` 约占半区 `14.3%`、**`1 MB/s` 精准落在半区的约三分之一（`31.8%`）**、`3 MB/s` 约占 `57.2%`、`5 MB/s` 约占 `73.0%`、`10 MB/s` 占满 `100%`；同时将 `CAPSULE_ATTACK_ALPHA` 下调至 `0.28` 与释放阻尼对称。
  - 将 `SnapshotPublisher::RunDue` 的 `TrafficTick` 速率计算改为基于保留 1 秒历史样本队列（`rate_samples_`）的滚动窗口均值，保持 4 Hz (250ms) 高频推送的同时消除单 Tick 微突发导致的 $\times 4$ 速率虚高，使胶囊与任务栏/监控面板的速率口径一致。
- **测试验证 (`rateMapping.test.ts`, `ipc_session_tests.cpp`)**：更新 `1 MB/s` 对应约三分之一填充比例的断言，并新增 250ms 突发流量在 1 秒滚动窗口下平滑均值化的 IPC 集成单测。

## 第三十七轮 Windows 10 任务栏网速窗口不显示及第三方任务栏组件位移修复 — complete

- **现象与根因 (`src/host/platform/taskbar_speed_window.cpp`, `include/network_flow_monitor/host/platform/taskbar_speed_window.h`)**：
  1. **100% DPI (96 DPI) 下 `SetParent` 后分层子窗口重定向位图未重分配导致完全不可见**：`TaskbarSpeedWindow::create()` 此前以默认目标尺寸创建 `WS_EX_LAYERED` 顶层 `WS_POPUP` 窗口。当 `SetParent` 将其挂载为任务栏子窗口时，Win32 使原顶层分层重定向表面失效；在 125%+ DPI 下后续 `MoveWindow` 因尺寸改变触发表面重分配而可见，但在 96 DPI（如 Windows 10 `1920×1080` 100% 缩放）下目标尺寸与创建尺寸一致，`MoveWindow` 不产生尺寸变更，导致子窗口分层重定向位图始终未分配而在屏幕上完全不可见。
  2. **修改 `MSTaskSwWClass` 宽度引发与 TrafficMonitor 周期性互推位移**：原 Windows 10 分支通过 `MoveWindow(min_hwnd, ...)` 缩减任务栏应用区 `MSTaskSwWClass` 宽度；TrafficMonitor 的 100 ms 定时器检测到 `MSTaskSwWClass` 宽度不等于其缓存值后，将缩减后的宽度覆写为新的 `m_rcMinOri` 并再次减去自身宽度（`141 px`），导致每次开启或切换开关时 TrafficMonitor 持续向左偏移且关闭后无法复原。
  3. **冒号后数值列预留宽度偏大**：`paint_window` 此前对数值列固定预留 `32 DIP`（125% DPI 下为 `40 px`，而最大 4 字符数值 `"88.8"` / `"99.9"` 实际渲染宽度仅 `31 px`），导致冒号 `:` 与右对齐数值之间多出 `9 px` 冗余留白。
- **修复 (`taskbar_speed_window.h`, `taskbar_speed_window.cpp`)**：
  - 将初始隐藏窗口尺寸设为 `1 × 1`，并在每次执行 `SetParent(hwnd_, taskbar_hwnd)` 前先将窗口重置为 `1 × 1`，确保 `SetParent` 后的 `MoveWindow` 在 96 DPI 及任意缩放比例下均触发尺寸变更并分配有效的子窗口分层合成表面；
  - Windows 10 与 Windows 11 统一挂载至 `Shell_TrayWnd` 并使用 `compute_placement()` 仅避让系统原生组件（Windows 10 与左对齐 Windows 11 停靠在 `TrayNotifyWnd` 左侧，居中且未开启小组件的 Windows 11 XAML 任务栏停靠在左侧边缘），在 `sync_with_taskbar()` 中维护 `GW_HWNDPREV == nullptr` 顶层子窗口 Z 序，保持 `MSTaskSwWClass` 原生几何不变；
  - 在 `paint_window` 中按当前 DPI 字体实测最大 4 字符数值 `"88.8"` 的精确像素宽度（`GetTextExtentPoint32W(mem_dc, L"88.8", 4, &max_val_sz)`）加 `1 DIP` 间距计算 `val_right`，并将总窗口宽度 `kDefaultOverlayWidthDip` 从 `80 DIP` 收紧至 `74 DIP`。
- **回归测试 (`tests/host_tests.cpp`)**：
  - 新增 `TestClassicalTaskbarNonIntrusiveEmbeddingAndZOrder`：构造包含任务栏中间容器、`MSTaskSwWClass`、`TrayNotifyWnd` 的经典任务栏窗口树，验证首次开启与反复开关下 `MSTaskSwWClass` 几何零改动、`TrayNotifyWnd` 左侧精确定位、兄弟容器抢占 Z 序后的自动置顶恢复，以及 `74 × 32`（96 DPI 几何）下 `SetParent` 分层子窗口在屏幕 DC 上的真实像素合成渲染。

## 第三十八轮 配置固化默认行为与设置中心三分类精简 — complete

- **背景与目标**：
  - 滑动统计窗口与快照刷新周期属于普通用户极少关心的底层调优参数，界面过度暴露增加认知负担；
  - 智能网速单位（自动按量级匹配 `KB/s`、`MB/s`、`GB/s`）已涵盖全部使用场景，无需配置比特/字节等次级单位；
  - “采集与诊断”页在宿主右键托盘和监控面板已具备原生“重启采集器/重试采集器”菜单项的情况下属于冗余功能，日志级别默认固化为 `LogLevel::Info`。
- **改造实现**：
  1. **固化默认行为并移除冗余暴露 (`bridge_dispatcher.cpp`, `product_host.cpp`, `BasicMonitorPanel.tsx`)**：
     - 滑动统计窗口固化为 `2000 ms`，快照刷新周期固化为 `500 ms`，速率单位固化为智能速度 `Auto`，日志级别固化为 `LogLevel::Info`；
     - 从 `BridgeDispatcher::serialize_config_changed` 的 Schema 投影中移除 `rankingWindow`、`refreshInterval`、`speedUnits`、`logLevels`；
     - `ProductHost::on_update_setting` 移除上述 4 项的动态修改分支（直接落入不可修改保护）；
     - `BasicMonitorPanel.tsx` 速率格式化直接调用智能速度 `formatCompactRate(bytes)`，移除无用的 `speedUnit` 解构。
  2. **设置中心精简为三大分类 (`SettingsCenter.tsx`, `App.tsx`, `bridgeTypes.ts`, `nativeBridge.ts`)**：
     - 移除“采集与诊断”整个 Tab 及对应卡片控件，设置中心统一收敛为 **3 大分类**（`常规设置 (General)`、`贴边胶囊 (Capsule)`、`流量历史 (Traffic History)`）；
     - 清理已无引用的 `Activity`、`RotateCw` 图标组件与 `collectorStatus` 冗余订阅；
     - 在路由兼容层 `normalizeSettingsTab` 中，将历史可能传入的 `'collector'` 与 `'diagnostics'` 路由平滑归一化至 `'general'`。
- **质量门禁与测试验证**：
  - `SettingsCenter.test.tsx` 更新为 3 Tab 结构断言与常规页精简控件检查，验证历史路由兼容性；
  - `tools/quality.ps1 -Mode Full` 全流程 20 项阶段全部通过（PASS 20 / WARN 0 / FAIL 0）。

## 第三十九轮 构建与发行输出收拢为单一 `build` 目录 — complete

- **背景与目标**：
  - 消除根目录下 `build/`（中间编译物与 CMake 预设）与 `dist/`（CPack 发行包与归档）双输出目录并存的问题，将项目所有构建和发布输出统一收拢至单一顶层目录 `build/`；
  - 保持 CMake 生态标准惯例，将发布归档收拢至 `build/dist/`，清理根目录旧产物并同步所有脚本、CI 工作流和说明文档。
- **改造实现**：
  1. **打包脚本与 CI 工作流路径对齐 (`tools/package.ps1`, `.github/workflows/quality-and-publish.yml`)**：
     - 将 `tools/package.ps1` 中的 `$distRoot` 从根目录 `dist` 更新为 `build\dist`；
     - 同步更新 `.github/workflows/quality-and-publish.yml` 中打包产物路径、校验路径、归档上传路径和 GitHub Release 发布包路径至 `build/dist`。
  2. **说明文档与目录结构对齐 (`README.md`, `docs/development.md`)**：
     - 更新中英文 README 与开发手册中的便携式 Release 发行包生成指南，将输出路径从 `dist/` 调整为 `build/dist/`。
  3. **产物迁移与目录收拢**：
     - 本地已有历史发行包及 CPack 目录安全迁移至 `build/dist/`，移除根目录 `dist/`，根目录恢复仅保留唯一的构建输出根目录 `build/`。

## 第四十轮 原生系统任务栏硬件与网速监控 (`SystemTelemetrySampler` & `TaskbarSpeedWindow`) — complete

- **纯系统原生零驱动采集 (`include/network_flow_monitor/host/platform/system_telemetry_sampler.h`, `src/host/platform/system_telemetry_sampler.cpp`)**：
  - 完全基于 Windows 11 标准用户态只读接口实现 6 项硬件遥测采样（无内核驱动、无硬件端口/SMBus 竞争锁、不唤醒休眠独显）：
    1. **CPU 使用率 (`CPU: %`)**：PDH `\Processor Information(_Total)\% Processor Time`（带 `\Processor(_Total)\% Processor Time` 与 `GetSystemTimes` 兜底，与任务管理器/CoreTemp 时间利用率口径对齐）；
    2. **CPU 温度 (`CPU: ℃`)**：优先读取用户态 `CoreTempMappingObject` 只读共享内存（兼容运行 CoreTemp 的台式机），回退使用 PDH `\Thermal Zone Information(*)\High Precision Temperature`（系统 ACPI 热区十分之一开尔文转摄氏度 `(raw - 2731.5) / 10.0`，并自动过滤 AMI/Intel 台式机主板 DSDT 静态假常数 `3010`/`3030` dK）；
    3. **CPU 封装功耗 (`CPU: W`)**：PDH `\Energy Meter(*)\Power`（Windows 原生 EMI/RAPL `RAPL_Package*_PKG` 毫瓦转瓦特 `W`）；
    4. **内存使用率 (`MEM: %`)**：Win32 `GlobalMemoryStatusEx` 物理内存占用比例与已用/总容量 GB；
    5. **GPU 使用率 (`GPU: %`)**：PDH `\GPU Engine(*)\Utilization Percentage`（WDDM `dxgkrnl` 按显卡 LUID 与引擎类型聚合峰值占用）；
    6. **GPU 显存 (`VRM: %`)**：同时支持独立显卡专用显存（PDH `\GPU Adapter Memory(*)\Dedicated Usage` + `DedicatedVideoMemory` / 注册表 `HardwareInformation.qwMemorySize`）与核显共享显存自动回退（PDH `\GPU Adapter Memory(*)\Shared Usage` + `SharedSystemMemory`），任务栏展示紧凑百分比 `VRM: %`，鼠标悬浮 Tooltip 展示百分比与 `已用 / 总计 GB`。
- **配置与设置中心「任务栏监控」独立面板 (`app_config.h`, `config_codec.cpp`, `bridge_dispatcher.cpp`, `SettingsCenter.tsx`, `en-US.ts`, `zh-CN.ts`)**：
  - 在 `DisplayConfig` 中新增 `show_taskbar_cpu_usage`、`show_taskbar_cpu_temp`、`show_taskbar_cpu_power`、`show_taskbar_memory_usage`、`show_taskbar_gpu_usage`、`show_taskbar_gpu_memory`，连同 `show_taskbar_speed` 默认全部关闭（`false`）；
  - 设置中心新增 **「任务栏监控 (`Taskbar Monitor`)」** 分类面板，将原位于「通用」页的「显示任务栏网速」迁移至该面板，集中管理全部 7 项任务栏监控开关。
- **任务栏两行多列紧凑布局、动态替补、网速固定成对渲染与悬浮详情卡片 (`taskbar_speed_window.h`, `taskbar_speed_window.cpp`)**：
  - 硬件指标按固定优先级（`CPU: 使用率%`、`CPU: 温度℃`、`CPU: 功耗W`、`MEM: 使用率%`、`GPU: 使用率%`、`VRM: 显存使用率%`）依次按上下两行配对成列，当关闭某项硬件指标时后续硬件指标自动向前替补；
  - 上传网速（`↑:`）与下载网速（`↓:`）始终保持上下成对出现在最右侧列，不参与硬件空位替补；硬件列按日常 2 位数（`"88"` 宽度，`58 DIP` 列宽）预留固定数值槽位（当出现 3 位数如 `100` 时自动缩小字号以适配该 2 位数槽位宽度），网速列预留 4 字符固定数值槽位（`"88.8"` 宽度，`74 DIP` 列宽）并对 `MB/s` / `Mbps` 单位做字号适配使其与 `KB/s` 宽度对齐，鼠标悬浮在任务栏监控区域时弹出纯黑/纯白自适应主题的详情 Tooltip 卡片（展示全部已启用监控项，其中内存与显存同时展示百分比与 `已用 / 总计 GB`）。
- **测试与质量门禁 (`tests/config_tests.cpp`, `tests/host_tests.cpp`, `SettingsCenter.test.tsx`)**：
  - 新增配置默认关闭与持久化编解码测试、硬件两行替补与网速成对列构建测试、`SystemTelemetrySampler` 实机采样测试，以及设置中心「任务栏监控」Tab 交互测试。

## 第四十一轮 v0.3.5 原生系统任务栏硬件与网速监控发布 — complete

- **全仓版本号同步升级至 `0.3.5` (`CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, `apps/nfm-ui/src/bridge/nativeBridge.ts`, `apps/nfm-ui/src/features/settings/SettingsCenter.tsx`, `docs/development.md`)**：
  - 同步升级 CMake 工程版本、前端包清单、Windows 资源 `FILEVERSION`/`PRODUCTVERSION`（`0,3,5,0` / `"0.3.5"`）以及前端默认回退版本号。
- **撰写中英双语发布说明 (`.github/release-note/v0.3.5.md`)**：
  - 汇总第 35–40 轮原生系统任务栏网速与硬件遥测监控（`TaskbarSpeedWindow` + `SystemTelemetrySampler`）、核显共享显存与 CoreTemp 共享内存兼容、两行固定槽位对齐、悬浮详情卡片防闪烁、设置中心「任务栏监控」独立面板以及开机极早期启动自动恢复加固。
- **质量门禁与发布流程**：通过 `tools/quality.ps1 -Mode Full` 20/20 门禁与 `tools/package.ps1 -Configuration Release` 便携包构建，提交并推送 `v0.3.5` tag 触发 GitHub Release 自动发布。

## 第四十二轮 任务栏网速单位改为等宽字格对齐 (`TaskbarSpeedWindow::paint_window`) — complete

- **问题定位**：网速列此前对 `MB/s` / `Mbps` 单独套用 90% 字号来"凑宽度"，导致同一列上下两行字号不一致（`M` 笔画明显比 `K` 细），且换单位时字号跳变；同时比例字体下 `M`（14.66px）比 `K`（9.52px）宽约 5px，单位块宽度与后缀落点仍随单位变化。
- **改造实现 (`src/host/platform/taskbar_speed_window.cpp`)**：
  - 删除按单位切换字号的绘制分支，全部单位与数值、`CPU:` / `GPU:` 标签同为 9pt `微软雅黑`；`MEM:`（80%）与 `VRM:`（90%）标签的自适应保持不动；
  - 网速单位改为**等宽字格（tabular）**：量级字母 `K` / `M` / `G` 固定占用一个按最宽字母（`M`）测量出的字格，公共后缀 `B/s` / `bps` 紧贴字格右边界绘制，因此 `KB/s`、`MB/s`、`GB/s` 四个字形列、单位块总宽度与起笔 X 完全一致；
  - 量级字母在字格内居中：`M` 填满字格，`K` / `G` 两侧各留约 2.5px 均匀字距，避免左对齐时 `K` 后出现形似空格的 5px 缝隙；
  - 单位块整体锚定在 `val_right + rate_unit_gap_px`，数值在 `"88.8"` 固定槽位内右对齐，数值位数变化与单位切换都不会让单位左右浮动。
- **实机验证（125% DPI，`PrintWindow` 抓取任务栏浮层）**：同一会话内捕获 `1.1 GB/s` / `0.1 KB/s` 与 `346 MB/s` / `0.4 KB/s` 等帧，所有帧的单位后缀墨迹恒为 `x=295..315`、单位块恒为 `x=280..315`；`MEM:` / `VRM:` 与硬件数值槽位未受影响。

## 第四十三轮 任务栏内存标签改为 `RAM:` 并统一硬件标签字格 (`TaskbarSpeedWindow::paint_window`) — complete

- **背景**：`MEM:` 在微软雅黑下比 `CPU:` 宽约 7px（41.2px vs 34.0px），此前靠 80% 字号压缩贴合列宽；改为 `RAM:` 后若按"列内最宽标签"取槽位，标签槽会变宽 4.6px 而把 `CPU:` 功耗列的 `W` 挤出列边界，且槽宽会随"哪两项配对"变化（隐藏指标后自动替补）。
- **改造实现 (`src/host/platform/taskbar_speed_window.cpp`)**：
  - 任务栏内存标签改为 `RAM:`（Tooltip 同步为「内存占用 (RAM)」/「Memory (RAM)」）；
  - `RAM:` 与 `VRM:` 同用 90% 字号（`compact_label_font`），实测字宽 33px / 34px，与 `CPU:`（31px）、`GPU:`（32px）同量级，125% DPI 下四者墨迹高度同为 11px；
  - 硬件标签槽位改为**全局固定**：`hw_label_w` 取 `CPU:` / `GPU:` / `RAM:` / `VRM:` 四者实测宽度的最大值，所有硬件列共用；不再按列取最大，因此隐藏指标、重新配对都不会改变标签槽，数值列在所有列中从同一 X 起排；
  - 网速列仍使用 `↑:` / `↓:` 自身测量的标签宽度，不受硬件标签槽影响。
- **实机验证（125% DPI，`PrintWindow` 抓图）**：
  - 默认 6 项全开：第 2 列 `CPU:` 冒号止于 `x=111`、`RAM:` 止于 `x=113`，两行标签墨迹高度均为 11px；`W` 墨迹止于 `x=149`，列右边界 `x=151`，2px 余量不再溢出；
  - 关闭 `CPU 功耗` 让 `RAM:` 与 `GPU:` 配对：三列 `%` 墨迹分别止于 `x=70 / 147 / 224`，列内偏移一致（各列左边界 `2 / 79 / 155`），证明标签槽不随配对变化。

## 第四十四轮 WebView2 内存占用来回收敛（浏览器参数、内存目标级别、设置渲染进程回收） — complete

- **问题定位（`mem NetworkFlowMonitor` 实测 539 MB WS / 282 MB Commit）**：
  - `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` 在宿主以高完整性级别运行时会被 WebView2 忽略，因此 `--js-flags=--lite-mode`、`--disable-features=CalculateNativeWinOcclusion,IntensiveWakeUpThrottling` 从未真正进入浏览器进程；实测浏览器命令行只有 WebView2 自身参数。
  - `WebView2Controller::set_memory_target_low` 忽略入参，只做 `TrySuspend`，架构文档承诺的 `COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW` 实际从未生效。
  - 设置窗口关闭后仅 `put_IsVisible(FALSE)` + `TrySuspend`，渲染进程仍存活；实测关闭后该渲染进程 WS 降至 7 MB，但私有提交仍约 50 MB，进程槽位常驻。
- **改造实现 (`webview_controller.cpp`)**：
  - 浏览器参数改由 `ICoreWebView2EnvironmentOptions::AdditionalBrowserArguments` 传入（`Make<CoreWebView2EnvironmentOptions>()`），高权限与标准权限宿主都会生效；参数收敛为 `--disable-features=CalculateNativeWinOcclusion,SpareRendererForSitePerProcess --js-flags=--lite-mode`，移除与节能相悖的 `--disable-background-timer-throttling` 与 `IntensiveWakeUpThrottling` 禁用项。
  - 通过 `ICoreWebView2_19::put_MemoryUsageTargetLevel` 实现真实的 `NORMAL`/`LOW` 切换，并在 `reset_controller()` 重建后按 `desired_low_memory_` 重新应用。
- **设置渲染进程按需回收 (`product_host.cpp`, `product_host.h`)**：抽出 `create_settings_webview()`；`close_settings_window()` 关闭时直接 `reset()` 控制器与 bridge，释放设置渲染进程，下次打开设置时重建控制器并重新导航，首次启动仍保留预热的“零等待”打开路径。
- **实机 A/B（本机 125% DPI，启动后 60 s 稳定态，同机同数据目录）**：
  - 无参数：总 WS 429.6 MB / 私有 213.7 MB，主渲染进程 84.7 MB / 40.5 MB；
  - 最终参数：总 WS 421.2 MB / 私有 208.5 MB，主渲染进程 74.4 MB / 35.6 MB（`--lite-mode` 生效后渲染进程下降约 10 MB WS / 5 MB 私有）；
  - 仅用于评估的 `--disable-gpu`：391.0 MB / 153.7 MB（GPU 进程 38.8 MB / 13.2 MB），`--enable-low-end-device-mode`：401.8 MB，两者均未采纳为默认。
  - 设置窗口关闭后渲染进程数由 2 → 1，重新打开后 2，验证控制器销毁与重建路径可用。

## 第四十五轮 任务栏监控悬浮 Tooltip 增加 500 ms 停留判定 (`TaskbarSpeedWindow`) — complete

- **问题定位 (`src/host/platform/taskbar_speed_window.cpp`)**：`WM_MOUSEMOVE` 一旦收到就同步调用 `show_or_update_tooltip()`，指针只是扫过任务栏监控浮层（74×32 DIP）也会立刻弹出详情卡片，并在离开瞬间消失，形成闪烁。
- **改造实现 (`taskbar_speed_window.h`, `taskbar_speed_window.cpp`)**：
  - 新增 `kTooltipHoverDelay{500 ms}` 与 `kTooltipHoverTimerId`，`WM_MOUSEMOVE` 改为 `start_tooltip_hover_timer()`：仅当浮层已可见且当前无待触发计时器时 `SetTimer`，因此窗口内持续移动不会重置停留时间，进入浮层后满 500 ms 才展示；
  - `WM_TIMER` 处理 `on_tooltip_hover_timer_fired()`：先清除计时器，再校验 `enabled_` / `visible_` 与 `GetCursorPos` + `contains_screen_point`（防止丢失 `WM_MOUSELEAVE` 时误弹），通过后才调用 `show_or_update_tooltip()`；
  - `hide_tooltip()` 统一取消待触发计时器，覆盖指针离开、右键菜单、禁用浮层与 `close()`；`WM_LBUTTONDOWN` / `WM_LBUTTONDBLCLK` 同样取消，点击期间不会迟到弹出；
  - 新增只读状态访问器 `is_tooltip_visible()` / `is_tooltip_hover_timer_active()` 供测试断言。
- **测试 (`tests/host_tests.cpp`)**：新增 `TestTooltipRequiresPointerDwell`——合成任务栏容器上创建浮层并启用后，`WM_MOUSEMOVE` 不立即显示 Tooltip 且计时器已武装，重复 `WM_MOUSEMOVE` 不重置计时器，`set_enabled(false)` 取消待触发计时器，并断言停留阈值不低于 500 ms。
- **质量门禁**：`tools/quality.ps1 -Mode Fast` 13/13 通过（C++ Debug 0 警告，`host.suite` 8 个测试全绿）。

## 第四十六轮 右键菜单新增任务栏监控整体显示/隐藏开关 — complete

- **需求边界**：右键菜单需要一个**整体**开关，控制任务栏监控浮层本身的渲染，而不是设置中心「任务栏监控」页里的逐项数据开关。
- **配置与规则 (`app_config.h`, `app_config.cpp`, `config_codec.cpp`)**：
  - `DisplayConfig` 新增主开关 `show_taskbar_monitor`（默认 `true`），与 `show_taskbar_speed` 等逐项开关相互独立；
  - 新增唯一权威判定 `IsTaskbarMonitorActive(display) = show_taskbar_monitor && HasAnyTaskbarMonitorItem(display)`，渲染与遥测采样共用同一规则；
  - `display.show_taskbar_monitor` 加入编解码键表与序列化；解析侧按既有「后加键可选」约定包裹，旧配置缺失该键时保持 `LoadedWithDefaults` 并沿用可见默认值，不会被判定为不完整文档。
- **右键菜单 (`win32_window.h`, `win32_window.cpp`, `product_host.cpp`)**：
  - `ContextMenuOptions` 新增 `is_taskbar_monitor_visible`，菜单在「固定显示」之后插入 `隐藏任务栏监控` / `显示任务栏监控`（英文 `Hide/Show Taskbar Monitor`），命令号 `kCmdTrayToggleTaskbarMonitor`，回调 `on_context_menu_toggle_taskbar_monitor()`；
  - Host 侧翻转 `current_config_.display.show_taskbar_monitor` → `SaveConfigFile(User)` → `apply_taskbar_monitor_config()` → 广播 `configChanged`，与 `nfm_pinned` 的持久化模式一致，重启后保持；
  - 抽出 `apply_taskbar_monitor_config()` 统一「先 `update_config` 再按需采样遥测」的顺序，替换启动、设置应用、1 秒看门狗三处重复逻辑；1 秒采样与 `TaskbarSpeedWindow::update_config` 的启用判定改走 `IsTaskbarMonitorActive`，浮层隐藏时停止 PDH 采样。
- **契约同步 (`bridge_dispatcher.cpp`, `bridgeTypes.ts`, `nativeBridge.ts`)**：`configChanged.general.showTaskbarMonitor` 随配置下发，前端类型与 mock 默认值/键映射同步（未新增设置中心控件，开关仅由右键菜单操作）。
- **测试 (`tests/config_tests.cpp`, `tests/host_tests.cpp`)**：
  - 配置侧：默认可见、`IsTaskbarMonitorActive` 组合语义、完整文档解码、序列化往返、旧文档缺失该键仍按可见默认值加载；
  - 浮层侧：`TestMasterSwitchGatesOverlay` 验证主开关关闭时即使有监控项也不渲染、重新打开后恢复、无监控项时保持隐藏；
  - 菜单侧：`CheckContextMenuExposesTaskbarMonitorSwitch` 通过 `MN_GETHMENU` 抓取真实 `#32768` 弹出菜单内容，断言中英文标签随状态翻转且 `on_context_menu_opened` 正常回调。
- **质量门禁**：`tools/quality.ps1 -Mode Fast` 13/13 通过（C++ Debug 0 警告）。
