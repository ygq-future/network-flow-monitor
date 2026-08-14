# 02 — 展示、固定并操控单一监控面板

**What to build:** 在已运行的胶囊路径上完成一个单一监控面板。产品不再区分 NFM 与 TopA，也不再提供排行展开/收起按钮。面板第一行依次展示产品图标和 `Network Flow Monitor`、下载图标与总下载速率、上传图标与总上传速率、Pin；随后直接展示配置的 Top N 应用行，每行依次为应用图标和可省略名称、下载速率、上传速率。Pin 关闭时，胶囊悬浮展示完整面板，离开完整面板后恢复胶囊；Pin 打开时，完整面板固定显示。右键菜单、紧凑尺寸、超高排行、断线和重连均须稳定。

**Blocked by:** 01 — 拖动、停靠并恢复监控面板/胶囊.

**Status:** ready-for-human

## Comments

- 2026-08-14: 按重置后的单一监控面板规格重新实现。`MonitorPanelBehavior` 移除了 NFM/TopA 区分、TopA toggle、manual override 和双窗口 pointer-occupancy 状态，surface 收敛为 Hidden/Capsule/Panel，deadline 收敛为 None/CapsuleReveal/Dismiss；独立排行 HWND（`RankingPopup`）与 `TopALayout` 删除，排行渲染、滚动、键盘/滚轮、图标缓存和行重定位动画合并进单一 `FloatingWindow`。面板高度由 header + 实际排行行/状态行 + padding 推导（`RankingReadyRows` 携带 `visible_rows`），少于 top_n 时不出现空白底部。全量门禁 `20260814-181430-full` 通过（136 文件格式化、严格 Debug/Release、38/38 测试、clang-tidy/Clang Static Analyzer、MSVC /analyze、CPack）。目标机真实 hover/HWND 链路仍未在目标机验证，不以模型/控制器测试代替运行时验收。
- 2026-08-14: 第三次目标机复测确认旧 Ticket 02 不能验收：Pin 关闭的启动胶囊没有通过真实悬浮显示面板，点击显示后悬浮也不会自动展示排行；截图同时确认双 HWND 布局产生大面积空白、固定高度和行内容重叠。此前的模型测试、控制器测试和 Full 门只能证明被测路径通过，不能证明真实目标机悬浮链路正确；`ready-for-human` 结论撤回。
- 2026-08-14: 用户取消两级 NFM/TopA 产品模型，改为胶囊与单一监控面板两种 presentation。旧 TopA toggle、独立 TopA 可见性、手动 TopA persistence、双窗口间隙和双窗口布局全部退出需求。父 spec、领域词汇和后续视觉 ticket 已同步；实现及验收必须从本版未勾选清单重新开始。

- [x] 删除产品行为中的 NFM/TopA 区分、独立排行 HWND、TopA toggle、TopA manual override 和双窗口 pointer-occupancy 状态；BehaviorModel 只保留胶囊、完整监控面板、Pin、菜单、断线和 transition 等可观察状态。
- [x] 完整监控面板使用单一窗口/单一命中区域；右键菜单、拖动、DPI、工作区变化和指针进入/离开都只经过这一条适配链路。
- [x] 第一行严格按“产品图标 + `Network Flow Monitor`｜下载图标 + 总下载速率｜上传图标 + 总上传速率｜Pin”排列；下载列始终位于上传列之前，不渲染 Top N 标题或排行展开/收起按钮。
- [x] 后续每个排行行严格按“应用图标 + 应用名称｜下载速率｜上传速率”排列。图标与名称字形的垂直中心位于同一条水平中线；图标盒固定，异步真实图标替换 fallback 时不移动行内容。
- [x] 面板使用集中的紧凑固定设计宽度，不设置用户可配置宽度，不为完整应用名保留空白列。下载/上传列固定宽度并右对齐，应用名称只使用剩余宽度，超出时使用省略号且不得覆盖速率列。
- [x] 面板高度严格由一行 header、当前可见排行行或一行状态、padding 和 border 计算；不继承胶囊高度、不保留原 NFM 最小高度、不出现无内容的底部空白。
- [x] Pin 关闭且全局 auto-hide 开启时，默认/重启 presentation 为胶囊。真实鼠标进入胶囊并达到配置的 hover delay 后展示完整监控面板；从胶囊移入新显示面板视为连续占用，不得被排队的旧 leave 取消。
- [x] Pin 关闭时，鼠标离开完整监控面板后按既定 dismiss/auto-hide 行为直接恢复胶囊；返回过程中重新进入会从当前 transition 反向，不经过第二窗口、第二计时器或中间可见状态。
- [x] 胶囊单击且未超过系统拖动阈值时立即显示完整监控面板；拖动超过阈值取消点击和悬浮激活。普通面板点击、拖动或右键不得改变 Pin。
- [x] Pin 打开时完整监控面板固定显示并持久化，鼠标离开不收缩；Pin 关闭后恢复正常胶囊悬浮/离开行为。全局 auto-hide 关闭时面板始终显示、Pin 禁用但不清除已保存偏好。
- [x] 监控面板使用应用原生右键菜单。菜单打开期间暂停 auto-hide deadline；点击菜单外部（包括面板内部）正常关闭菜单；菜单关闭后按真实指针位置恢复行为；Show/Hide Monitor 立即生效。托盘菜单不自行改变当前 presentation。
- [x] 面板接近工作区底部时整体向上最小修正且不覆盖持久化锚点；排行内容本身超过可用高度时仅排行 viewport 内部滚动，支持滚轮/触控板、方向键、Page Up/Page Down、Home/End 和交互 scrollbar。
- [x] 排行窗口预热显示 `Collecting data…`，连接正常但无流量显示 `No network activity`；状态只占一个内容行且不产生额外空白。断线保留面板/胶囊和 Pin presentation、清除旧行并显示明确无数据状态；重连或 collector instance 变化后重新预热。
- [x] 应用图标继续通过既有身份 metadata 异步缓存解析，普通 EXE、打包应用、系统/已退出/不可访问进程均有稳定 fallback；绘制不阻塞、不提权。
- [x] 排行顺序只随 accepted snapshot 更新；固定速率列使用 tabular numerals； retained/new/exiting 行从当前视觉状态 retarget，不排队陈旧快照。
- [x] 自动化测试从高层 BehaviorModel seam 覆盖 Pin/hover/dismiss/menu/disconnect/bottom/viewport/effect 组合，并删除只为已取消 TopA 状态存在的测试与产品分支。
- [x] Win32/UI smoke 必须通过真实窗口和真实消息泵覆盖：启动为胶囊、真实 cursor enter、系统 timer 到期、完整面板实际可见、指针转移、完整离开恢复胶囊、Pin 后固定、右键菜单、Show/Hide、DPI、滚动、断线/重连和干净退出。直接调用归一化 deadline 不得作为悬浮链路的唯一证据。
- [ ] 在目标 Windows 11 主机人工验收至少十次胶囊悬浮→完整面板→离开恢复循环，以及 Pin 开/关、点击、拖动、菜单、紧凑布局、名称省略、行对齐、底部修正、滚动和断线恢复；发现的问题继续留在本 ticket，不以自动化门通过代替运行时验收。
- [x] 对改动文件格式化并依次通过全仓格式检查、严格 Debug、clang-tidy/Clang Static Analyzer、MSVC `/analyze`、全部测试、Release x64 和打包；记录结果但未经用户授权不创建 commit。
