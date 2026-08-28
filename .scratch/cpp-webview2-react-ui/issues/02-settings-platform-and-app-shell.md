# 02 — 设置平台与应用外壳

**Blocked by:** 01 — 项目基础设施与实时链路

**Status:** ready-for-agent

在功能 UI 开发前完成权威配置链路和可运行应用外壳，使胶囊与监控面板从实现第一天即可使用真实设置、主题、语言和生命周期能力。

## 设置平台

- [ ] Native `AppConfig` 是唯一权威，所有公开设置具有版本、默认值、单位、合法范围、迁移、原子保存与 damaged-file diagnostics。
- [ ] 设置分类为 General、Appearance、Capsule、Monitor、Ranking、Collector、Diagnostics。
- [ ] General/Appearance 完成 login startup、rate units、System/Light/Dark、System/English/Simplified Chinese 与 Reduced Motion 关联呈现。
- [ ] Capsule 完成 width、height、end rounding（默认 5 DIP、存储范围 0–12 DIP、有效值不超过当前宽度一半）、hover delay、dock distance/anchor、auto-hide enablement 和 auto-hide delay。
- [ ] Monitor 完成 Pin、dismiss delay、background tint opacity、blur strength 与 full-screen topmost yield。
- [ ] Ranking 完成 Top N、ranking window 与 display refresh interval，且该刷新间隔同时约束可见数据与快照推送。
- [ ] Collector/Diagnostics 呈现连接状态、PID、instance ID、elevation、ETW health、dropped packets、active processes、last error、retry/restart 和 diagnostic level。
- [ ] `updateSetting` 仅接收 allow-listed setting/value；产品宿主校验后回送 authoritative snapshot，失败时返回可展示的结构化错误。
- [ ] Frontend 不复制 native 默认值或范围；设置控件消费经过验证的 schema projection。

## 应用外壳

- [ ] 完成 single instance、tray、Settings 激活、已有窗口复用和明确退出路径。
- [ ] 完成便携采集器 authorization、start、retry、restart、shutdown、parent ownership 和断线恢复。
- [ ] 完成 Settings 路由、导航、共享 `Button`、`IconButton`、`Select`、`Slider`、`Toggle`、`Checkbox`、`SettingsSection` 与 `SettingsRow`。
- [ ] 主题、语言和设置修改即时更新所有已打开外壳页面，无需重启。
- [ ] WebView2 Runtime 缺失、静态资产损坏、Collector 离线和配置损坏均有可操作的 native 或 shell diagnostics。
- [ ] 所有系统修改显示目标状态、错误和明确授权流程。

**Completion proof:** 应用可从托盘打开并复用 Settings；全部设置分类可读写并持久化；主题和语言实时生效；便携采集器生命周期可从应用管理；配置迁移、损坏恢复、bridge 校验和外壳状态均有自动测试，Fast/Full gate 通过。
