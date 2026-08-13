# 05 — 在真实 Windows 11 环境中收口整体 UI

**What to build:** 对 01–04 已分别可运行并完成局部视觉验收的 UI 做最终跨环境、跨功能收口。以真实用户完整流程组合验证停靠、胶囊、NFM、TopA、双语、主题、材质、动画和 Settings，在多显示器、混合 DPI、不同任务栏、高对比度、减少动画和远程/降级环境中修复集成问题。完成后提供完整自动化回归、目标机人工证据、全量质量门和可生成的 Release ZIP，而不是把此 ticket 当作前四张未做视觉验证的兜底。

**Blocked by:** 04 — 把 Settings 变成完整的 Win11 控制中心.

**Status:** ready-for-agent

- [ ] 审计 spec 的所有 user stories、Implementation Decisions、Testing Decisions 和 Out of Scope；将每条纳入自动化、人工验收或明确的不可自动验证证据，不遗漏或扩大范围。
- [ ] 运行完整用户旅程：首次启动、胶囊/NFM 拖动、跨屏换侧、重启恢复、悬停/点击展开、TopA toggle、pin/manual persistence、右键菜单、Settings 修改、语言/主题即时切换、断线/重连和退出。
- [ ] 在多显示器与负坐标布局验证鼠标松开点显示器、左右吸附、屏幕拔插回退、不自动跳回和跨屏胶囊拖动。
- [ ] 在混合 DPI、运行时 DPI/工作区变化和文本缩放下验证 NFM、TopA、胶囊、图标、命中区、滚动列表、Settings 导航与持久化 DIP 锚点，无截断、漂移或错误缩放。
- [ ] 验证任务栏位于上、下、左、右以及自动隐藏时的工作区约束和系统唤出区域；不存在上/下吸附、任务栏遮挡或胶囊阻止任务栏显示。
- [ ] 验证屏幕底部展开 TopA 时整体最小上移、关闭后返回原锚点，以及超高 TopA 的鼠标/触控板/键盘滚动。
- [ ] 验证 NFM/TopA/胶囊/Settings 的 English 与简体中文布局、system-language fallback、品牌/单位不翻译、长错误文本和 N=1/N>1 标题。
- [ ] 验证 light/dark/system 实时切换、Acrylic+opacity、inactive Acrylic、Settings Mica、圆角、图标/字体和所有 fallback；记录材质由系统禁用时的预期降级而非误报为失败。
- [ ] 验证正常动画、动画中途反向、快速快照 retarget、空闲 dirty-only redraw、Windows 减少动画、高对比度和远程桌面/虚拟或无 Acrylic 环境。
- [ ] 验证原生右键菜单、键盘导航、tab order、焦点可见性、IME 输入、代表性屏幕阅读器名称/状态，以及 styled native controls 没有破坏系统语义。
- [ ] 验证应用图标异步加载、fallback、进程退出/不可访问/打包身份、排行换位和固定速率列，不出现 UI 阻塞或权限提升。
- [ ] 验证唯一 EXE 旁配置在 portable/service collector 切换前后保持同一份 UI 状态；验证旧开发 schema 替换、损坏文件保留、不可写目录诊断和持久化失败的当前会话行为。
- [ ] 补齐或调整高层 BehaviorModel 回归、config/resource 测试及 Win32 adapter/integration smoke，使修复通过既定 seam 验证，不以脆弱截图或私有实现断言替代行为测试。
- [ ] 清理被高层 seam 完整取代的重复低层测试和废弃 UI 路径；确认没有旧四边吸附、side/above TopA placement、reveal strip、硬编码英文、运行时托盘 DIB 或第二套配置路径残留。
- [ ] 测量并记录最终 Release 包体、依赖、启动/退出 smoke 和目标机资源观察值；这些是实测证据，不在没有基线和参考机器数据时升级为性能阈值。
- [ ] 更新规划、进度、配置、架构/验收文档，使其与最终行为、已知平台降级和实测证据一致；服务 ETW 共存仍保持后置且不混入本 ticket。
- [ ] 按顺序运行 clang-format、全仓库格式检查、严格 Debug、clang-tidy/Clang Static Analyzer、MSVC `/analyze`、全部单元/集成测试、Release x64 和 CPack ZIP，记录完整 Full gate 结果。
- [ ] 交付最终可运行 Release ZIP 和目标机验收记录；未经用户明确授权不创建 Git commit，不把局部 smoke 或未验证视觉状态称为完成。
