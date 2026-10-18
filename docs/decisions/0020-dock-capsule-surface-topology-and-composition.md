# ADR 0020：贴边胶囊 Surface 拓扑、视觉合成与交互协调模型

- 状态：已接受
- 日期：2026-08-29

## 上下文

Windows 11 用户需要一个轻量、紧凑且能实时指示上下行网络流量的桌面贴边胶囊。胶囊必须能够在不打扰主工作区的情况下贴边停靠、支持实时尺寸与端部圆润度调整、呈现平滑双向速率渐变填充，并支持鼠标悬停（350 ms）与点击即时展开基础监控面板（340 DIP）。

为了确保窗口在移动、缩放、多显示器切换、全屏应用抢占与上下文菜单弹出等场景下保持确定性与稳定性，必须在 Native 宿主与 Web 视图之间划分明确的 Surface 拓扑与行为协调边界。

## 架构决策

1. **Window 与 Surface 拓扑**：
   - Win32 Host 窗口采用无边框分层结构，启用 DWM 硬件级圆角与深色阴影扩展（`DwmExtendFrameIntoClientArea`、`DWMWCP_ROUND`）。
   - WebView2 Controller 配置为原生透明背景（`DefaultBackgroundColor = 0x00000000`），避免尺寸调整与状态切换过程中的黑色边框与闪烁（No gutter artifacts）。
   - 贴边胶囊提供至少 16 DIP 的向内命中有效区域（Inward Hit Target），确保高 DPI 与触控板场景下的易点击性与悬停稳定性。

2. **确定性 C++ 行为协调器（DockCapsuleCoordinator）**：
   - Native 宿主内置纯值语义与完全可测试的 `DockCapsuleCoordinator` 协调模型，管理 `TrayOnly`、`CapsuleVisible` 与 `PanelVisible` 呈现状态。
   - 拖动判定使用 Windows `SM_CXDRAG` / `SM_CYDRAG` 系统阈值；低于阈值的轻微位移被判定为点击（Click），立即展开监控面板，超过阈值则触发捕获与拖动。
   - 吸附停靠算法以释放点所在显示器工作区（`rcWork`）的中线为界自动吸附至左侧或右侧；处于中线时保留现有侧（Tie-break rule，首次默认右侧），并保留自动隐藏任务栏唤出边缘（4 DIPs safety margin）。
   - 悬停延迟（默认 350 ms）展开面板，离开延迟（默认 500 ms）收起面板；鼠标重入立即取消收起；Pin 固定模式下永久阻止自动收起。
   - 当同一显示器出现活动全屏应用且开启 `yield_topmost_when_fullscreen` 时，自动让出置顶层级（`HWND_NOTOPMOST`）。

3. **双向流量渐变与非对称动效**：
   - 下载流量从胶囊上端向中心填充（三段渐变：蓝 `#3b82f6` -> 青 `#06b6d4` -> 绿 `#10b981`）。
   - 上传流量从胶囊下端向中心填充（三段渐变：紫 `#a855f7` -> 靛 `#6366f1` -> 天蓝 `#38bdf8`）。
   - 填充高度采用对数映射（Logarithmic Rate Mapping），适配 0 B/s 至 100 MB/s 的动态流量跨度。
   - 4 Hz 节拍速率提升采用 250 ms cubic ease-out，速率下降采用 450 ms quadratic ease-out，并支持自当前视觉值平滑重定向；检测到系统 `prefers-reduced-motion` 时立即以 0 ms 完成状态转换。

4. **保留式原生上下文菜单（Retained Context Menu）**：
   - 贴边胶囊与 340 DIP 基础监控面板共享统一的原生上下文菜单（Show/Hide Monitor、Pin/Unpin、Settings、Collector Status/Retry、Exit）。
   - 上下文菜单弹出期间自动暂停 Dismiss 计时器，菜单关闭后自动重新核对指针空间命中状态。

## 自动验证与目标机证据

- C++ Debug 构建与 29 项测试通过，覆盖协调器状态机、吸附规则、多显示器切换、全屏置顶让出和时钟推进。
- 前端 11 个测试文件通过，覆盖三段渐变、对数映射、非对称缓动、Reduced Motion、权威 Surface 投影和面板交互。
- Fast Gate 与 Full Gate 通过；clang-tidy 为 0 diagnostics，MSVC `/analyze` 为 0 warnings，Release 构建、29 项 C++ 测试和便携包生成通过。
- Windows 11 24H2 x64 的 backdrop、圆角、命中、拖动、多显示器、High Contrast、RDP、device-loss fallback 与 idle CPU/working-set 数据仍需交互验收并补录。
