# 03 — 获得 Fluent、双语且流畅的监控体验

**What to build:** 将已完整可用的单一监控面板、胶囊和现有应用表面升级为可复用的 Windows 11 运行时视觉能力。用户可以使用系统/浅色/深色主题和系统/English/简体中文语言；监控面板在系统支持时呈现 Acrylic 加背景 opacity，Settings 请求 Mica，所有应用自有表面采用统一圆角、字体、图标和视觉令牌。监控面板、排行变化、主题和控件反馈使用有界且可中断的动画，并在高对比度、减少动画、远程桌面或合成不可用时可靠降级。本 ticket 只使用现有最小配置/诊断入口和配置文件完成运行时能力验证，不重做最终 Settings 信息架构；正式入口由 04 整合。

**Blocked by:** 02 — 展示、固定并操控单一监控面板.

**Status:** ready-for-agent

- [ ] 在既定配置 schema 和 BehaviorModel seam 上实现 system/light/dark 主题解析和运行时切换；system 模式响应 Windows 主题变更，显式 light/dark 不随系统改变。
- [ ] 实现 system/English/简体中文语言解析；默认 English，system 模式在不支持的语言下回退 English。语言切换更新所有已打开应用表面，无需重启。
- [ ] 所有应用自有可见文案进入完整资源目录，包括托盘、监控面板、现有 Settings、授权说明、状态、诊断和应用错误类别；`Network Flow Monitor` 品牌及标准速率单位保持不翻译。
- [ ] Windows 系统错误保留系统原文和数值错误码，同时添加应用语言对应的错误类别，不丢失诊断精度。
- [ ] 监控面板状态和控件文案提供自然的 English/简体中文资源；资源完整性和占位符一致性可自动检查。排行不再渲染独立 Top N 标题。
- [ ] 建立统一视觉令牌，覆盖 light/dark/high-contrast 的颜色、tint、排版层级、间距、圆角、边框、阴影、hover/press/focus/disabled 状态和动画时长；运行时表面不再依赖单一 12pt Segoe UI Regular 样式。
- [ ] 优先使用 Segoe UI Variable 并提供可靠字体 fallback；速率使用 tabular numerals，文本缩放时内容增高或重排，不通过缩小字体或裁掉速度来适配。
- [ ] 用正式多尺寸产品图标资源替代运行时绘制的 32×32 DIB，并在托盘、监控面板、Settings 和顶层窗口中使用合适尺寸；Release 包不依赖开发机可选 DLL 或外部松散资源。
- [ ] 监控面板在 active/inactive 状态请求 Acrylic，并把 opacity 只作用于背景材质；文字和图标保持完全不透明。Settings 请求 Mica且不跟随监控面板 opacity。
- [ ] Acrylic/Mica 被系统、硬件、远程桌面、节电或高对比度禁用时，自动选择同 tint、可读的实色/降级背景；材质失败不导致透明黑窗、不可读文本或启动失败。
- [ ] 所有应用拥有的监控面板、胶囊、Settings 和自绘弹层使用 Windows 11 风格圆角；原生右键菜单继续交给 Windows，不重写菜单行为。
- [ ] BehaviorModel 输出明确动画目标，Win32 adapter 只调度和实现这些目标。使用初始令牌：控件反馈 100 ms、监控面板/胶囊和工作区运动 180 ms、排行行 200 ms、主题 150 ms、胶囊速率插值不超过 200 ms 或一个刷新周期。
- [ ] reveal 使用 ease-out、dismiss 使用 ease-in；动画被新指令或指针返回打断时从当前视觉值反向/retarget，不重头播放、不排队历史动画。
- [ ] 排行行移动、新增淡入、退出淡出和胶囊速率变化均只在 transition 活动期间驱动帧；稳定空闲状态没有固定高帧率重绘或多余 deadline。
- [ ] Windows 减少动画、高对比度或降级会话下 transition 立即完成或使用必要的最小反馈；功能、命中区和布局不依赖动画存在。
- [ ] 03 只在现有最小设置/诊断入口暴露足以切换和验证主题、语言、opacity 及降级状态的控制，不建设五分区导航、不全面重绘旧 Settings、不做将在 04 被搬迁的一次性完整布局。
- [ ] 自动化测试从既定高层 seam 覆盖主题/语言/能力变化后的 observable presentation、动画目标、reduced-motion 与 high-contrast 输出；资源测试覆盖双语 key、fallback、占位符和复数。
- [ ] Win32/DWM smoke 证明主题/语言消息被归一化、DWM 能力请求和 fallback 路径可执行、图标资源可加载、窗口可重绘并干净退出；smoke 不伪装成真实模糊或视觉质量证明。
- [ ] 本 slice 在目标 Windows 11 主机上分别验证 light/dark、运行时 system 切换、English/中文即时切换、Acrylic+opacity、inactive Acrylic、Mica、圆角、字体/图标、动画中断、减少动画、高对比度和至少一种降级路径；视觉问题在本 ticket 修复，不推迟到 05。
- [ ] 运行该 slice 所需的格式、严格 Debug、相关测试、静态分析和 Release smoke；完成 ticket 前运行仓库规定的完整质量门并记录结果，但未经用户授权不创建 commit。
