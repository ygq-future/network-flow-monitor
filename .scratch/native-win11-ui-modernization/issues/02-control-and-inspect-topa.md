# 02 — 查看、固定并操控 TopA

**What to build:** 在已运行的 NFM/胶囊路径上完成完整的两级监控体验。TopA 始终作为 NFM 下方的独立面板出现，展示配置的 Top N 应用图标、名称和各自上传/下载速率；NFM 保留产品身份和总速率。用户可通过悬停或专用按钮展开 TopA，可用 pin 控制 NFM 是否自动隐藏，并在固定状态下让手动展开的 TopA 持续显示。右键菜单、底部空间不足、超高排行、断线和重连均有稳定行为。

**Blocked by:** 01 — 拖动、停靠并恢复 NFM/胶囊.

**Status:** ready-for-agent

- [ ] 扩展既定 BehaviorModel 事件/状态/输出以覆盖 NFM、TopA、pin、手动 TopA persistence、context-menu-active、warm-up、idle 和 disconnected；不重新设计或平行创建另一套状态模型。
- [ ] NFM 顶部显示产品图标、`Network Flow Monitor` 品牌名、专用 TopA toggle 和 pin；下方只显示总上传/下载速率。拖动 NFM、右键或点击普通内容区都不会误触 TopA toggle。
- [ ] TopA 是 NFM 下方的独立面板，TopA 可见必然意味着 NFM 可见；不再尝试上方、左侧或右侧 placement。
- [ ] 屏幕底部空间不足时，NFM 与 TopA 整体只向上移动保持工作区内完整可见；TopA 收起或 NFM 返回胶囊时恢复持久化锚点，临时修正不产生 PersistAnchor。
- [ ] TopA 内容本身超过可用高度时限制 viewport 并内部滚动；支持鼠标滚轮、触控板、方向键、Page Up/Page Down、Home/End，滚动条在滚动或悬停列表时出现。
- [ ] TopA 标题根据配置显示 `Top {N} Application(s)`；每行左侧显示应用图标和名称，右侧显示上传、下载速率，不重复总速率。应用名可省略号截断，速率列固定宽度、右对齐并使用 tabular numerals。
- [ ] 应用图标从已有身份 metadata 异步解析和缓存；普通 EXE、打包应用、系统/已退出/不可访问进程均有稳定 fallback，图标提取不阻塞绘制、不请求提升权限。
- [ ] NFM 悬停达到阈值自动展开 TopA；专用 toggle 可立即手动展开/收起。拖动超过系统阈值会取消按钮/悬停激活。
- [ ] NFM 未固定时，TopA 无论由悬停还是按钮展开，离开整个监控面板后都按正常 dismiss/auto-hide 逻辑收起。
- [ ] NFM 已固定时，悬停展开的 TopA 仍自动收起；按钮手动展开的 TopA 保持显示，直至再次按按钮或取消 NFM 固定。取消固定会清除 TopA 手动 persistence。
- [ ] pin 状态持久化且不影响拖动。全局 auto-hide 关闭时 NFM 始终可见，pin 显示为不可用并解释原因，但保存的 pin 偏好不被清除。
- [ ] NFM 与 TopA 的应用原生右键菜单打开期间，暂停 TopA dismiss 和 NFM auto-hide deadlines；菜单关闭后根据实际指针位置恢复正常悬停/离开逻辑。托盘菜单不主动改变监控面板可见状态。
- [ ] 离开未固定的完整监控面板后，TopA 收起与 NFM 返回胶囊形成一段连续、有目标的 transition，不额外叠加两次用户等待；鼠标返回能从当前 transition 进度反向。
- [ ] 排行窗口预热显示 `Collecting data…`，连接正常但无流量显示 `No network activity`；采集不可用时保留 NFM 可见/pin 状态、显示明确默认/无数据速率、清除旧 TopA 行并禁止打开 TopA。
- [ ] 重连或 collector instance 变化后重新积累排行窗口，不跨实例使用陈旧历史；恢复后自动回到与当前 pin/auto-hide 相符的正常交互。
- [ ] 行排序只在新快照到达时更新；已有行移动、新增/退出行的过渡从当前视觉状态 retarget，不排队播放陈旧快照。此 ticket 可先使用既定动画目标，最终统一视觉令牌由 03 完成。
- [ ] 自动化测试继续统一从高层 BehaviorModel seam 覆盖 hover/manual/pin/menu/dismiss/disconnect 的组合、Bottom correction、TopA viewport 和 effect outputs；仅保留具有独立数学价值的内部纯函数测试。
- [ ] Win32/UI smoke 覆盖真实 NFM/TopA 窗口创建、按钮路由、指针跨窗口、嵌套菜单消息循环期间的 deadline、滚动/键盘路径、图标 fallback、断线/重连和干净退出。
- [ ] 本 slice 在目标 Windows 11 主机上完成人工验收：TopA 固定下拉、底部整体上移、pin/manual persistence、菜单期间不消失、列表滚动、图标/列布局和断线兜底均实际可见；发现的问题在本 ticket 修复，不留到 05 才首次验证。
- [ ] 运行该 slice 所需的格式、严格 Debug、相关测试、静态分析和 Release smoke；完成 ticket 前运行仓库规定的完整质量门并记录结果，但未经用户授权不创建 commit。
