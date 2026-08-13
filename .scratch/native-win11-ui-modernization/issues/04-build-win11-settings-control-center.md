# 04 — 把 Settings 变成完整的 Win11 控制中心

**What to build:** 把 01–03 已可运行的配置与视觉能力正式整合为一个完整、可访问的 Win11 Settings 用户体验。窗口显示本地化的 Settings/设置标题和当前页标题，按 General、Monitor、Ranking、Collector、Diagnostics 组织全部公开配置和既有授权/服务操作；窄窗口使用顶部 selector。所有控件采用统一 Fluent 样式但保留原生键盘、IME、焦点、选择和辅助功能语义。用户可以从最终入口调整语言、主题、监控 opacity、胶囊尺寸、auto-hide/pin、排行、采集模式和诊断选项，并获得实时预览、校验、保存错误与回滚反馈。

**Blocked by:** 03 — 获得 Fluent、双语且流畅的监控体验.

**Status:** ready-for-agent

- [ ] Settings 使用最终五分区信息架构：General、Monitor、Ranking、Collector、Diagnostics；所有当前公开 `AppConfig` 项和已有服务/授权操作恰有一个清晰归属，不重复概念或遗漏入口。
- [ ] 系统标题区域显示本地化的 `Settings` / `设置`，内容区显示当前分区标题；`Network Flow Monitor` 品牌规则与其他表面一致。
- [ ] General 提供 language、theme 和 UI login startup；切换 language/theme 立即更新已打开 Settings、托盘、NFM 和 TopA，并在保存失败时提供诊断与一致回滚。
- [ ] Monitor 提供 opacity、auto-hide、auto-hide delay、pin、胶囊宽高和相关交互选项；胶囊尺寸有实时预览和恢复默认，输入范围与 schema 保持 8–24 DIP、40–160 DIP。
- [ ] Ranking 提供 Top N、ranking window、display refresh interval、speed unit 和上传/下载显示选项；继续遵守一个 refresh interval 同时决定订阅快照与可见数据更新的既有规则。
- [ ] Collector 提供 portable/service 后端选择、一次性免 UAC 授权、连接/采集状态和现有服务生命周期控制；不得把采集模式误表现为不同 UI 或不同配置文件。
- [ ] Diagnostics 提供健康/错误状态、日志等级及现有可用诊断动作；系统错误展示本地化类别、原始系统文本和错误码，不记录敏感载荷。
- [ ] 正常宽度使用左侧导航；窄窗口折叠为顶部 selector，不建立第二套移动 UI。布局在文本缩放和中英文下保持可用，不依靠固定英文字符串宽度。
- [ ] edits、combo boxes、buttons 等保留原生 HWND 编辑、选择、键盘、IME、焦点和 accessibility 语义，通过 subclass、custom draw、theme attributes 和统一令牌获得新样式；Switch 等少数控件可以窄范围自绘，但不得重写文本输入和选择系统。
- [ ] 所有交互控件具有清晰 hover、press、focus、selected、enabled、disabled 状态。pin、TopA toggle、switch、导航和动态状态暴露准确 accessible name/role/state 与本地化 tooltip。
- [ ] Settings 请求 Mica、使用 Win11 圆角和主题令牌，不受 monitor opacity 控制；Mica 不可用、高对比度或远程会话时使用 03 提供的可读 fallback。
- [ ] 03 的最小主题/语言/opacity 验证入口被收敛进最终 Settings，不保留重复的临时控件或第二套配置路径。
- [ ] 配置加载、校验、保存和原子发布继续由 config 模块负责；Settings/controller 执行 BehaviorModel/config effect，不把 I/O 引入模型。损坏文件保留诊断，EXE 目录不可写时不静默转移到用户目录。
- [ ] 需要采集器重连、startup registration 或服务动作的保存继续保留既有回滚与真实状态重新查询语义；视觉重构不能让 UI 根据按钮状态假定系统操作成功。
- [ ] 自动化测试覆盖分区与字段归属、窄窗口导航、双语标题、输入范围/单位、即时主题/语言应用、胶囊 preview/reset、保存成功/失败/回滚，以及原生控件的键盘焦点和 tab order smoke。
- [ ] Win32 Settings integration smoke 覆盖窗口创建、所有分区导航、代表性的 edit/combo/switch/button 操作、DPI/theme/language 变化、服务与授权入口仍可达、保存/取消和干净关闭。
- [ ] 本 slice 在目标 Windows 11 主机上完成人工验收：五分区信息架构、正常/窄窗口、中英文、light/dark、文本缩放、键盘/IME、焦点可见性、屏幕阅读器基本名称、胶囊预览、Mica/fallback 及所有既有服务入口均实际可用；问题在本 ticket 修复。
- [ ] 运行该 slice 所需的格式、严格 Debug、相关测试、静态分析和 Release smoke；完成 ticket 前运行仓库规定的完整质量门并记录结果，但未经用户授权不创建 commit。
