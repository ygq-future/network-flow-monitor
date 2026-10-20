# Network Flow Monitor | 网络流量监视器

[中文说明](#-中文说明) | [English](#-english)

---

## 📖 中文说明

**Network Flow Monitor** 是一款面向 **Windows 11 x64** 的轻量级、便携式按应用实时网络流量监控工具。无需安装内核驱动，基于 Windows 原生 **ETW (Event Tracing for Windows)** 实时追踪 TCP/UDP (IPv4/IPv6) 字节流，并通过 **C++20 Win32/WebView2 宿主 + React 19 前端** 提供丝滑的桌面贴边流光胶囊与实时应用流量排行榜。

### ✨ 核心特性

- **双层同场形变过渡（贴边胶囊 ⇄ 监控面板）**
  - **贴边流光胶囊 (`DockCapsule`)**：默认停靠于屏幕左/右侧边缘，采用双向对数速率映射（Logarithmic Rate Mapping）与可自定义双色速度渐变（内置 5 组精选配色 + 2D 拾色器），直观呈现实时下载与上传强度。
  - **340 DIP 实时监控面板 (`BasicMonitorPanel`)**：鼠标悬停（默认 350 ms）或点击胶囊即刻经阻尼曲线展开为 340 DIP 监控面板；顶部单行展示全局上下行总速率，下方展示带真实 `.exe` 图标、FLIP 顺滑重排动画与双色比例进度条的 **Top N 应用实时流量排行榜**，面板高度随活跃应用数动态伸缩。
- **原生级窗口体验与防打扰策略**
  - **Windows 内核级丝滑拖拽与自动吸附**：跨越系统拖拽阈值 (`SM_CXDRAG` / `SM_CYDRAG`) 后直接接入 Windows 原生移动循环，释放后自动识别多显示器工作区 (`rcWork`) 并吸附至左/右边缘，自动预留 4 DIP 自动隐藏任务栏唤醒区。
  - **透明画布局部命中与鼠标穿透**：340 DIP 透明画布在胶囊态下仅胶囊逻辑矩形响应鼠标，其余透明区域通过 `WS_EX_TRANSPARENT | WS_EX_LAYERED` 实现操作系统级点击穿透；支持自动隐藏为 4px 边缘细线。
  - **全屏应用/游戏防误触**：自动检测同显示器全屏应用或 Direct3D 全屏独占状态，支持「全屏时锁定并鼠标穿透」或「全屏时隐藏胶囊」。
- **普通权限秒开 & 一次性 ETW 持久授权**
  - 主程序 `NetworkFlowMonitor.exe` 始终以**普通用户权限**运行，完美支持 `HKCU\Run` 登录开机自启。
  - 采集器伴侣进程 `nfm-collector.exe` 优先尝试普通权限启动；若首次运行尚未授权，自动触发原生 UAC (`runas`) 提权并完成一次性持久授权（加入 `Performance Log Users` 组、授予 `SeSystemProfilePrivilege` 及配置 ETW Session/Provider GUID DACL），后续启动与开机自启无需再弹 UAC。
- **独立预热设置中心与纯净便携交付**
  - **3 大分类设置中心**：涵盖「通用」（开机自启、速率单位、深浅色主题、中英双语、Top N 与排行时间窗）、「贴边胶囊」（配色、尺寸/圆角、悬停/收起延迟、自动隐藏、全屏策略）与「采集与诊断」（采集器状态、一键重试/重启、ETW 丢包遥测、日志级别与日志目录快捷打开）。
  - **绿色便携目录收敛**：单层 ZIP 交付，静态链接 MSVC 运行库与 `WebView2LoaderStatic.lib`；所有运行期配置 (`data/config.json`)、WebView2 缓存 (`data/webview2/`) 与滚动诊断日志 (`logs/nfm-host.log`) 严格保存在软件根目录内。

### 🏗️ 系统架构

```text
NetworkFlowMonitor.exe (普通用户权限 | C++20 Win32 + WebView2 Host)
  ├── 系统托盘、单实例互斥、全屏检测、开机自启注册表治理
  ├── DockCapsuleCoordinator (TrayOnly / CapsuleVisible / PanelVisible 状态机)
  ├── 独立预热设置窗口 + 版本化白名单 Native/Web Bridge
  └── 打包静态 React 19 + Tailwind CSS 前端 (映射至 https://nfm.local/)
          │
          │ Owner-Scoped 本地命名管道 (Protocol V1: 12-byte Header + UTF-8 JSON)
          ▼
nfm-collector.exe (便携伴侣采集器 | C++20 ETW Collector)
  ├── Windows 实时 ETW 会话 (SystemTraceProvider TCP/UDP IPv4/IPv6 + Process 事件)
  ├── 进程代际追踪 (Generation-tracked PID 防复用) 与应用身份归一
  ├── 应用累计字节池与滑动窗口 Top N 速率引擎
  └── Protocol V1 服务端 (4 Hz TrafficTick / 1 Hz RankingSnapshot & CollectorStatus)
```

详细架构与核心设计决策请参见 [`docs/architecture.md`](docs/architecture.md)。

### 🛠️ 环境要求与构建指南

#### 开发环境要求
- **操作系统**：Windows 11 24H2 x64
- **C++ 工具链**：Visual Studio 2022（含 C++ 桌面开发 x64 工作负载）、CMake 3.28+、Ninja、LLVM (`clang-format`、`clang-tidy`)
- **系统运行库**：Microsoft Edge WebView2 Evergreen Runtime
- **前端工具链**：[Bun](https://bun.sh/)

#### 常用命令

```powershell
# 1. 运行快速质量门禁（代码格式检查 + C++ Debug 编译与测试 + 前端类型/Lint/单元测试/构建）
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Fast

# 2. 运行全量质量门禁（Fast + clang-tidy + MSVC /analyze + Release 构建与测试 + CPack 打包）
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Full

# 3. 一键格式化全仓 C++ 与前端代码
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Format

# 4. 生成 Release 便携式 ZIP 发行包（输出至 dist/NetworkFlowMonitor-<version>-win-x64.zip）
powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Configuration Release
```

### 📂 仓库目录结构

```text
network-flow-monitor/
├── apps/nfm-ui/               # React 19 + TypeScript + Tailwind CSS 前端工程
├── cmake/                     # CMake 编译选项、静态分析与 WebView2 集成模块
├── docs/
│   ├── architecture.md        # 系统架构、核心设计决策、IPC 协议、窗口拓扑与配置模型
│   ├── development.md         # 开发环境、质量门禁、便携打包与真机验收清单
│   └── progress.md            # 阶段演进与交付记录
├── include/network_flow_monitor/ # C++20 公共头文件 (app, collector, config, host, ipc, process, ranking)
├── resources/                 # Windows 图标与可执行文件资源清单 (.rc)
├── src/                       # C++20 核心实现源码
├── tests/                     # 统一无头测试运行器 (nfm_tests) 及 10 个模块化测试套件
├── third_party/yyjson/        # 静态内联的高性能 JSON 库 (yyjson 0.12.0)
└── tools/                     # 质量门禁 (quality.ps1)、打包 (package.ps1) 与验收脚本
```

---

## 🌐 English

**Network Flow Monitor** is a lightweight, portable per-application real-time network traffic monitor for **Windows 11 x64**. Built without kernel drivers, it leverages native **Windows ETW (Event Tracing for Windows)** to account for TCP/UDP (IPv4/IPv6) traffic per process, pairing a **C++20 Win32/WebView2 Product Host** and **React 19 UI** for a fluid edge-docked traffic capsule and live application ranking panel.

### ✨ Key Features

- **Seamless Dual-Surface Morphing (Dock Capsule ⇄ Monitor Panel)**
  - **Edge-Docked Traffic Capsule (`DockCapsule`)**: Docks cleanly to the left or right screen edge with bidirectional logarithmic rate mapping and customizable two-tone speed gradients (5 curated presets + interactive 2D HSV color picker).
  - **340 DIP Live Monitor Panel (`BasicMonitorPanel`)**: Hovering (350 ms) or clicking smoothly morphs the capsule into a 340 DIP panel featuring real-time total download/upload throughput in the header and a **Top N Application Traffic Leaderboard** with extracted 32-bit ARGB `.exe` icons, FLIP row reordering animations, and dynamic window height scaling.
- **Native Win32 Windowing & Fullscreen Yield**
  - **OS-Level Drag & Multi-Monitor Snap**: Crosses Windows drag thresholds (`SM_CXDRAG` / `SM_CYDRAG`) into a native system move loop, snapping on release to the left or right work-area edge (`rcWork`) while reserving a 4 DIP safety margin for auto-hide taskbars.
  - **Scoped Hit-Testing & Click-Through**: Maintains a 340 DIP transparent DWM surface where only the capsule bounds respond to pointer events in capsule mode; surrounding transparent regions apply `WS_EX_TRANSPARENT | WS_EX_LAYERED` OS click-through. Supports auto-hiding into a 4px edge peek strip.
  - **Fullscreen Game/App Protection**: Automatically detects active fullscreen windows or Direct3D fullscreen exclusive apps on the same monitor to either lock & click-through (`ignore_hover_when_fullscreen`) or hide the capsule (`hide_capsule_when_fullscreen`).
- **Standard-User Launch & One-Time Persistent ETW Authorization**
  - `NetworkFlowMonitor.exe` runs as a **standard user**, ensuring seamless `HKCU\Run` login startup without UAC prompts.
  - `nfm-collector.exe` starts as a standard process whenever authorized; if ETW permissions are missing on first run, the Host automatically falls back to native UAC (`runas`) and performs a one-time persistent authorization (`Performance Log Users` membership + `SeSystemProfilePrivilege` + ETW GUID DACLs).
- **Pre-Warmed Settings Center & Self-Contained Portable Package**
  - **3-Tab Settings Center**: Instant-open pre-warmed window covering **General** (login startup, speed units, System/Light/Dark themes, English/Chinese locales, Top N, ranking window), **Dock Capsule** (color presets, dimensions/rounding, hover/dismiss delays, auto-hide, fullscreen rules), and **Collector & Diagnostics** (collector lifecycle, retry/restart, ETW packet loss telemetry, rotating log levels, and log folder shortcut).
  - **True Portable Footprint**: Single-folder ZIP distribution with statically linked MSVC runtime and `WebView2LoaderStatic.lib`. All runtime files (`data/config.json`, `data/webview2/`, `logs/nfm-host.log`) stay strictly inside the application root directory.

### 🛠️ Build, Test & Package

#### Prerequisites
- **OS**: Windows 11 24H2 x64
- **C++ Toolchain**: Visual Studio 2022 (Desktop development with C++ x64), CMake 3.28+, Ninja, LLVM (`clang-format`, `clang-tidy`)
- **System Runtime**: Microsoft Edge WebView2 Evergreen Runtime
- **Frontend Toolchain**: [Bun](https://bun.sh/)

#### Quick Commands

```powershell
# Run Fast quality gate (format check, Debug C++ build/tests, frontend lint/typecheck/tests/build)
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Fast

# Run Full quality gate (Fast + clang-tidy + MSVC /analyze + Release build/tests + CPack ZIP)
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Full

# Format all C/C++ and frontend files
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Format

# Build clean Release portable ZIP package (output in dist/)
powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Configuration Release
```

For deeper technical details, see [`docs/architecture.md`](docs/architecture.md) and [`docs/development.md`](docs/development.md).
