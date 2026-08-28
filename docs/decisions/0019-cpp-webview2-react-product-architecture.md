# ADR 0019：C++ Win32 Host、WebView2 与 React 产品架构

- 状态：已接受
- 日期：2026-08-28

Network Flow Monitor 采用双进程架构：`nfm-collector.exe` 以 C++20 承担 ETW、进程身份、聚合、排行和 Protocol V1 server；普通权限的 `NetworkFlowMonitor.exe` 以 C++20 Win32 承担窗口、WebView2、托盘、停靠、DPI、native backdrop、Collector 生命周期和经过校验的 Native/Web bridge；WebView2 运行由 React、TypeScript、Vite 与 Tailwind CSS 构建的静态资产。生产包使用 WebView2 Evergreen Runtime 与相邻 `ui/` 资产，构建工具由 Bun、ESLint、Prettier 和 `prettier-plugin-tailwindcss` 组成。

Native Host 是操作系统能力、Protocol V1、配置和授权的权威；React 是视觉呈现的权威；Collector 是采集与排行事实的权威。Native/Web bridge 是版本化的封闭消息联合，不提供通用 RPC、文件系统或 shell 能力。透明/native-backed surface、desktop backdrop、圆角、命中、DPI、resize、4 Hz 更新和生产静态资产加载由首次依赖这些能力的胶囊功能一并验证并记录 ADR，监控面板在同一 topology 上完成生产验收。

实现队列采用四个功能型纵向切片：项目基础设施与实时链路；设置平台与应用外壳；完整贴边胶囊体验；完整监控面板与产品交付。主题、国际化、模块依赖、公共常量归属和代码复用约束从第一个 Ticket 建立，设置平台在贴边胶囊与监控功能之前完成。

该决定取代此前的产品 UI 与窗口表面架构。Collector、Protocol V1、权限隔离、产品行为和 portable lifecycle 保持为新实现的合同。
