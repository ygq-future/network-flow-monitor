# ADR 0017：混合架构——C++ 高性能采集引擎与 C# .NET 10 WPF 现代前端

- 状态：已接受
- 日期：2026-08-16
- 取代：ADR-0001 中“纯 C++ 手写 Win32/Direct2D UI 界面”的部分

## 背景

Network Flow Monitor 原先采用纯 C++20 + Win32 + Direct2D / DirectComposition 绘制 UI。随着对 Windows 11 Fluent 视觉质感、复杂渐变动态插值、多状态平滑过渡动画要求的提升，手写 Direct2D 逐帧画刷与消息循环导致前端 UI 迭代成本高、动效细腻度难以达到声明式 XAML/WPF 标准。

同时，C++ 内核采集层（ETW 驱动、PID 防复用、应用识别与滑动窗口排行）已经非常稳定、高效，且需要高权限执行内核追踪。

## 决策

将系统架构演进为**双进程混合架构**：
1. **C++ Core Engine（高性能后台/服务）**：保留纯 C++20，仅负责 ETW 网络捕获、进程关联、滑动窗口计算与 Named Pipe 服务端，以 Windows 服务或便携提权后台运行；完全剥离所有 Direct2D / 窗口 UI 绘制。
2. **C# .NET 10 WPF UI（现代 Fluent 客户端）**：以普通用户权限运行，负责胶囊动效、贴边停靠、托盘菜单、监控面板、应用图标提取、配置持久化与设置界面。
3. **IPC 协议与推流模型**：基于现有命名管道，提供带版本帧头的 JSON 协议。独立分频推送 4 Hz `TrafficTick`（轻量全局带宽速率）与 1 Hz `RankingSnapshot`（Top N 应用排行明细）。

## 架构不可违反的 7 项不变式（Architecture Invariants）

1. **Privileged pipe is an authenticated security boundary**：提权命名管道是安全防护边界。管道必须具有严格的安全描述符（仅限当前用户与 LocalSystem 访问），拒绝提权逃逸风险。
2. **IPC thread never directly mutates WPF presentation state**：IPC 接收线程严禁直接操作 WPF 视觉呈现或直接调用 UI 动效方法；所有数据仅单向解析并更新至线程安全的 `TelemetryStore`。
3. **Telemetry snapshots describe state; they are not animation events**：遥测快照仅表达瞬时/区间采样数据状态，绝不是动画触发事件。UI 动画由 ViewModel 依据目标状态自主驱动。
4. **Continuous telemetry animation must not depend on layout-size animation**：持续高频动效严禁对 `FrameworkElement.Height`/`Width` 或 `RowDefinition.Height` 产生动画以避免 Measure/Arrange 布局抖动；必须使用固定几何布局结合 `RenderTransform` / `ScaleTransform` / `Geometry.Clip` 与 `ColorAnimation` 走合成通道。
5. **Portable collector lifetime and Service lifetime are separate policies**：便携采集器与系统服务的生命周期策略严格隔离。便携采集器由普通权限 UI 探测拉起，并通过 Parent-Process 句柄等待 + Named Pipe 心跳租约实现干净自清理；Windows 服务模式遵循独立 SCM 托管。
6. **Protocol v1 has fixed framing, limits and cross-language fixtures**：IPC 协议拥有固定头部、严格长度与格式限制，并建立跨语言自动化契约测试（Protocol Fixtures）防止 C++ 与 C# 序列化/反序列化漂移。
7. **Existing C++ UI is not removed until the new end-to-end tracer bullet passes**：在新的 C++ 引擎 + C# WPF 端到端 Tracer Bullet 验证通过之前，现有 C++ UI 及测试基线保持完好，不提前破坏性删除。

## 后果

- **优势**：WPF XAML 声明式动画和现代 Fluent 样式使胶囊渐变流体动效的实现效率和视觉质感大幅提升；C++ 后台保持极致的计算性能与权限隔离。
- **代价**：运行时增加 .NET 10 开销（内存基线 40-70MB，单文件发布体积增加）；需维护一套跨语言 IPC 协议契约测试。
