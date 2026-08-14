# 01 — 拖动、停靠并恢复 NFM/胶囊

**What to build:** 首次落地 spec 已确定的监控面板 BehaviorModel seam，并贯通 NFM 主面板与胶囊的完整用户路径。用户可以拖动 NFM 或未展开的胶囊，在任意显示器松手后只吸附到最近的左侧或右侧；面板与胶囊始终受当前显示器工作区约束，不会吸附上下边或进入任务栏区域。胶囊可以通过悬停或单击展开 NFM，超过系统拖动阈值则保持胶囊并进入拖动。位置、侧边、显示器和固定状态使用 EXE 旁的唯一程序配置持久化，并能在重启、显示器缺失和保存失败时给出确定且可诊断的行为。胶囊以可配置 schema 支持宽高，并用上下两段渐变显示实时上传/下载速率；本 ticket 不建设最终 Settings 配置或预览界面，相关用户入口留给 04。

**Blocked by:** None — can start immediately.

**Status:** ready-for-human

- [x] 建立唯一的高层 BehaviorModel seam：输入为归一化事件和显式环境/时间/config 数据，输出为下一状态、布局/动画目标及 effect intents；模型不依赖 HWND、DWM、Win32 消息、系统时钟、真实计时器、配置 I/O 或其他副作用。
- [x] UI controller/Win32 adapter 能把启动、指针、点击、系统拖动阈值、拖动完成/取消、计时到期、工作区、DPI、显示器变化和快照归一化为模型事件，并执行模型返回的窗口、计时、重绘和持久化意图。
- [x] BehaviorModel 外部 interface 保持单一高层测试面；状态、布局、吸附、速率映射和动画规划可在实现内部拆分，但不要求调用方或主要测试跨越内部 seam。
- [x] NFM 和胶囊在每次成功拖动后，按鼠标松开点所在显示器的工作区选择最近的左侧或右侧；上下边从不参与吸附，任意距离松手都不会停留在屏幕中间。
- [x] 左右距离完全相等时沿用此前侧边；首次没有此前侧边时默认右侧。覆盖负坐标多屏、跨屏窗口和 DPI 变化。
- [x] NFM 与胶囊的纵向位置始终钳制在工作区，适用于任务栏位于上、下、左、右的情况；同侧自动隐藏任务栏的系统唤出区域保持可访问。
- [x] 胶囊在未达到悬停阈值前移动超过 Windows 拖动阈值时直接进入胶囊拖动，本次按住期间不展开 NFM；未越过阈值的单击立即展开，纯悬停在配置延迟后展开。
- [x] 胶囊可以二维拖动、跨显示器并切换左右侧，拖动期间始终保持胶囊形态；取消拖动恢复原锚点且不产生持久化 effect。
- [x] 默认胶囊为 10×64 DIP；schema 接受 8–24 DIP 宽和 40–160 DIP 高，实际向屏幕内侧的指针命中宽度不小于 16 DIP。此 ticket 只提供 schema、默认值、校验、序列化和运行时消费，不新增最终 Settings UI。
- [x] 胶囊上传填充从底部向中心增长，下载填充从顶部向中心增长；两者使用独立速率值、连续 Byte/KiB/MiB 对数区间，GiB 及以上饱和在顶端。
- [x] 上传使用黄/橙/红渐变，下载使用青/蓝/紫渐变；方向同时由上下位置表达，不只依赖颜色。零值、阈值边界、单向/双向流量和超范围均有确定输出。
- [x] 完成拖动只产生一次 PersistAnchor 等 effect，模型不直接写文件；effect 成功或失败可作为归一化事件反馈。保存失败时当前会话保留新位置并显示可诊断错误，不跳回旧位置。
- [x] 唯一配置固定为 EXE 旁的 `NetworkFlowMonitor.json`，采集后端模式不改变配置路径。schema 包含语言、主题、pin、胶囊尺寸和锚点所需字段，移除旧 snap distance/reveal strip 概念；此阶段不实现未发布旧 schema 的迁移链。
- [x] 已识别的旧开发 schema 可以按新默认结构直接替换；损坏、截断或非法配置仍保留原文件并报告诊断，不静默覆盖。
- [x] 锚点保存稳定显示器标识、左/右侧和相对工作区顶部的 DIP 偏移。原显示器缺失时回退主显示器、钳制并持久化有效位置；显示器重新连接不会自动把面板跳回去。
- [x] NFM pin 状态具备配置默认值和持久化能力；具体 pin/TopA 用户交互在 02 完成。启动恢复 pin 时只恢复 NFM 可见性，不恢复 TopA 展开。
- [x] 主要自动化测试统一从 BehaviorModel seam 驱动拖动、吸附、恢复、工作区、时间、速率胶囊和 effect intent 行为，不创建 HWND、不读真实时钟、不访问配置文件。
- [x] 保留必要的配置 codec/store 测试和 Win32 adapter smoke，证明归一化事件能进入模型、目标矩形/计时/持久化意图能被外层执行，并且 UI 启停与既有采集流程保持可运行。
- [ ] 本 slice 在目标 Windows 11 主机上完成 smoke/人工验收：左右拖动、胶囊点击/悬停/直接拖动、多显示器、任务栏边界、重启恢复、速率渐变和保存失败提示可观察；记录尚未覆盖的混合 DPI/特殊任务栏环境，不把它们推迟为 05 才首次查看。
- [x] 运行该 slice 所需的格式、严格 Debug、相关测试、静态分析和 Release smoke；完成 ticket 前运行仓库规定的完整质量门并记录结果，但未经用户授权不创建 commit。

## Comments

### 2026-08-14 — 首轮人工验收失败与状态归属修复

- 人工验收观察到胶囊展开 NFM 后又由第二个 hover deadline 打开 TopA，随后 NFM、TopA、胶囊在各自 timer 下出现孤立常驻、闪切和点击后短暂切换；右键菜单还会被 topmost 应用窗口遮挡，菜单项可由右键激活。
- 代码和红测试确认 NFM 指针同时进入 `MonitorPanelBehavior` 与旧 `InteractionStateMachine`，胶囊到 NFM 的 transition 还会额外注入旧 `PointerEnter`。两个状态机与两个 timer 对同一可见层级拥有控制权，是本次混乱的主因；`pin=false` 不是原因。
- Ticket 01 的人工表面已收窄回其已批准边界：胶囊与 NFM 只由 `MonitorPanelBehavior` 控制，旧 TopA hover 路径不再接收 NFM/托盘指针事件，也不能在本 slice 显示 TopA。可见 pin 按钮、TopA toggle 和完整 TopA/pin 组合仍由 Ticket 02 实现。
- Context menu open/close 现为归一化模型事件；菜单期间 hover/auto-hide deadline 不改变表面，关闭后依据真实指针位置恢复一次 auto-hide。显示菜单期间 NFM/命中扩展/TopA HWND 临时退出 topmost，关闭后恢复。原生菜单不再启用 `TPM_RIGHTBUTTON`，因此右键不能选择命令。
- 回归测试先稳定失败于旧 TopA 指针路由、`TPM_RIGHTBUTTON` 和缺失的菜单生命周期，修复后转绿。Fast 门禁通过；第一次 Full clean build 返回失败但截断输出没有保留可确认诊断，随后的增量 build 无诊断通过，原样 Full 重跑在 115.1 秒内通过严格 Debug/Release、38/38 Debug 和 Release 测试、clang-tidy/Clang Static Analyzer、MSVC `/analyze`、Release x64 与 CPack。
- 本状态仅表示可以重新进行 Ticket 01 的窄范围人工验收，不表示右键菜单 Z 序、实际鼠标命中、多显示器/混合 DPI 或任务栏行为已经由自动化视觉确认。
