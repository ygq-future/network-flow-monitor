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
