# Network Flow Monitor WebView2 Product Specification

Status: ready-for-agent
Architecture-Mode: C++-Host-WebView2-React

## Problem Statement

Windows 11 用户需要一个轻量、可移植且能够按应用展示实时网络流量的桌面工具。产品需要在不打断当前工作的情况下持续显示总下载与上传速率，并允许用户通过贴边胶囊快速展开监控面板、查看 Top N 应用、调整外观和控制采集状态。

现有仓库已经具备 C++20 便携采集器、ETW 采集、进程身份、排行和 Protocol V1 基础，但缺少最终产品宿主与可交付界面。新的界面必须从项目建立之初具备主题、国际化、配置、可测试性和清晰模块边界，避免后续功能开发形成重复逻辑、散落的产品常量或需要整体返工的基础设施。

用户还需要稳定一致的窗口行为：贴边胶囊必须能够调节尺寸和端部圆润度，正确展示双向速率渐变，支持拖动和左右贴边；悬停或点击后出现监控面板；设置、托盘、右键菜单、固定显示和自动隐藏必须共享同一权威状态。产品最终应以可解压运行的便携包交付，并在 Windows 11 24H2 x64 上完成真实交互验收。

## Solution

产品采用两个普通进程组成的桌面架构。普通权限的产品宿主负责 Win32 窗口、WebView2、托盘、设置、授权流程、便携采集器生命周期和 Native/Web bridge；便携采集器负责 ETW、进程身份、应用聚合、滑动窗口排行和 Protocol V1；打包后的 React 前端负责贴边胶囊、监控面板和设置页面的视觉呈现。

产品宿主将经过验证的流量节拍、排行快照、采集状态和权威配置投影给 React。React 只能发送封闭、版本化并经过白名单校验的用户命令。所有 Windows 能力、持久化、进程控制和授权决策均保留在产品宿主中。

实施分为四个用户可见的纵向切片：项目基础设施与实时链路；设置平台与应用外壳；完整贴边胶囊体验；完整监控面板与产品交付。每个功能 Ticket 同时包含其视觉、交互、窗口行为、测试和目标机验收。

## User Stories

1. 作为 Windows 11 用户，我希望看到实时总下载速率，从而无需打开复杂工具就能了解当前网络活动。
2. 作为 Windows 11 用户，我希望看到实时总上传速率，从而能够及时发现持续上传或异常占用。
3. 作为桌面用户，我希望监控入口以紧凑的贴边胶囊呈现，从而减少对工作区的遮挡。
4. 作为桌面用户，我希望下载与上传分别从胶囊两端向中心填充，从而能够快速区分两个流量方向。
5. 作为桌面用户，我希望胶囊渐变随速率平滑变化，从而避免数据刷新产生生硬跳动。
6. 作为低动态偏好用户，我希望 Reduced Motion 能立即完成视觉变化，从而减少不适。
7. 作为用户，我希望调整贴边胶囊宽度，从而匹配自己的可见性和占用空间偏好。
8. 作为用户，我希望调整贴边胶囊高度，从而适配不同显示器和桌面布局。
9. 作为用户，我希望调整贴边胶囊端部圆润度，从而获得符合个人偏好的轮廓。
10. 作为触控板或高 DPI 用户，我希望胶囊始终有足够的有效命中区域，从而容易悬停、点击和拖动。
11. 作为多显示器用户，我希望拖动胶囊时以释放点所在显示器为准，从而获得符合直觉的放置结果。
12. 作为多显示器用户，我希望显示器断开后胶囊回到可用工作区，从而不会丢失界面。
13. 作为混合 DPI 用户，我希望窗口跨显示器移动后保持相同 DIP 尺寸和可点击性，从而获得一致体验。
14. 作为桌面用户，我希望胶囊能够吸附到工作区左侧或右侧，从而保持整齐稳定的位置。
15. 作为桌面用户，我希望应用遵循 Windows 拖动阈值，从而使点击和拖动不会相互误触。
16. 作为用户，我希望悬停胶囊后按配置延迟展开监控面板，从而快速查看详情。
17. 作为用户，我希望点击胶囊时立即展开监控面板，从而跳过悬停等待。
18. 作为用户，我希望指针重新进入面板时取消隐藏，从而能够连续操作内容。
19. 作为用户，我希望通过固定显示让监控面板保持展开，从而持续观察排行。
20. 作为用户，我希望在设置中控制自动隐藏及其延迟，从而统一管理面板的收起行为。
21. 作为用户，我希望通过右键菜单显示或隐藏监控界面，从而快速控制桌面呈现状态。
22. 作为用户，我希望通过右键菜单固定或取消固定监控面板，从而快速改变持续显示偏好。
23. 作为用户，我希望从胶囊和监控面板的右键菜单进入设置，从而无需寻找额外入口。
24. 作为用户，我希望从右键菜单查看便携采集器状态并在可用时重试，从而快速恢复采集。
25. 作为用户，我希望从右键菜单退出应用，从而同时结束产品宿主和其拥有的采集会话。
26. 作为用户，我希望右键菜单打开期间面板保持稳定，从而能够完成菜单操作。
27. 作为用户，我希望监控面板显示产品身份和总下载、上传速率，从而获得明确的当前状态概览。
28. 作为用户，我希望监控面板展示 Top N 应用，从而知道哪些应用正在占用网络。
29. 作为用户，我希望排行同时显示应用名称、图标、下载速率和上传速率，从而快速识别流量来源。
30. 作为用户，我希望长应用名称保持省略且不改变面板宽度，从而保持布局稳定。
31. 作为用户，我希望排行过长时仅滚动排行区域，从而始终保留总速率和产品标题。
32. 作为用户，我希望排行变化以平滑重排、进入和退出动画呈现，从而容易理解名次变化。
33. 作为用户，我希望断线、预热、空闲和错误状态具有清晰说明，从而不会把空白界面误解为故障。
34. 作为用户，我希望便携采集器重启后清除旧排行和旧动画，从而只看到当前采集会话的数据。
35. 作为用户，我希望可以拖动监控面板并保存位置，从而按照自己的桌面布局放置它。
36. 作为全屏应用用户，我希望监控窗口在外部全屏期间正确让出置顶层级，从而不会遮挡游戏或演示。
37. 作为使用自动隐藏任务栏的用户，我希望贴边窗口保留任务栏唤出区域，从而仍能访问任务栏。
38. 作为浅色模式用户，我希望所有页面和监控表面使用一致的浅色语义主题，从而保持清晰可读。
39. 作为深色模式用户，我希望所有页面和监控表面使用一致的深色语义主题，从而降低视觉刺激。
40. 作为跟随系统主题的用户，我希望系统主题变化即时反映到已打开界面，从而无需重启应用。
41. 作为高对比度用户，我希望系统高对比度优先于装饰性主题，从而保持可访问性。
42. 作为中文用户，我希望整个应用使用简体中文，从而能够理解所有功能和状态。
43. 作为英文用户，我希望整个应用使用英文，从而能够理解所有功能和状态。
44. 作为跟随系统语言的用户，我希望支持的系统语言即时应用，从而减少首次配置。
45. 作为系统语言不受支持的用户，我希望界面回退到英文，从而仍可正常操作。
46. 作为用户，我希望语言切换同时更新设置、托盘和右键菜单，从而避免混合语言界面。
47. 作为用户，我希望在设置中统一调整通用、外观、胶囊、监控、排行、采集和诊断选项，从而容易找到相关配置。
48. 作为用户，我希望修改设置后立即看到权威结果，从而能够边调试边确认效果。
49. 作为用户，我希望损坏的配置得到安全恢复并保留诊断信息，从而恢复使用且不丢失问题线索。
50. 作为用户，我希望设置迁移保留仍然有效的偏好，从而能够平滑升级产品。
51. 作为用户，我希望控制开机登录启动，从而决定应用是否自动运行。
52. 作为用户，我希望从设置查看采集器 PID、实例、授权、ETW 健康、丢包和错误，从而能够诊断采集状态。
53. 作为用户，我希望从设置重试或重启便携采集器，从而无需使用命令行恢复采集。
54. 作为首次运行用户，我希望授权流程明确说明目标状态和错误，从而知道系统将发生什么变化。
55. 作为用户，我希望 WebView2 Runtime 缺失时看到可操作的恢复说明，从而避免面对空白窗口。
56. 作为便携软件用户，我希望解压产品包即可运行，从而不需要安装开发工具。
57. 作为离线环境用户，我希望生产运行不依赖网络或开发服务器，从而能够稳定启动产品。
58. 作为隐私敏感用户，我希望采集器只处理计数、应用身份和健康状态，从而不会采集网络载荷内容。
59. 作为安全敏感用户，我希望采集通信仅允许当前所有者范围内的本地连接，从而降低数据和控制暴露。
60. 作为维护者，我希望产品常量、配置范围、协议标识和视觉 token 各自有唯一所有者，从而避免行为漂移。
61. 作为维护者，我希望共享工具只包含具有多个真实使用方的纯逻辑，从而保持模块职责清晰。
62. 作为维护者，我希望重复的校验、转换、格式化和状态规则只有一个权威实现，从而减少修复遗漏。
63. 作为维护者，我希望 Windows、WebView2 和存储细节位于 adapter 后方，从而能够在稳定 seam 上测试行为。
64. 作为维护者，我希望主题和国际化基础在功能开发前可用，从而让每个 Ticket 直接使用最终基础设施。
65. 作为维护者，我希望设置平台在胶囊和监控面板前完成，从而可以使用真实配置验收后续功能。
66. 作为维护者，我希望统一质量入口覆盖 C++ 和 React，从而让本地与 CI 使用相同门禁。
67. 作为维护者，我希望自动质量门保持 headless，从而能够无人值守运行并获得确定结果。
68. 作为验收人员，我希望在 Windows 11 24H2 x64 上验证真实 backdrop、圆角、拖动、DPI 和动画，从而确认桌面体验符合产品合同。
69. 作为验收人员，我希望记录空闲 WebView2 进程、CPU 和工作集观察值，从而为后续性能判断提供基线。
70. 作为发布人员，我希望便携包只包含声明的运行时文件和已构建前端资产，从而避免携带开发产物或陈旧构建结果。

## Implementation Decisions

- 产品运行时由产品宿主和便携采集器组成。产品宿主运行在交互用户令牌下；便携采集器拥有 ETW 所需权限，并受产品宿主父进程所有权和停止信号约束。
- 产品宿主是 Windows 行为、配置、持久化、授权、便携采集器生命周期和 Native/Web bridge 校验的权威。React 是视觉呈现的权威。便携采集器是流量、进程身份、应用聚合、排行和采集健康事实的权威。
- Native 模块按应用组装、bridge、生命周期、平台 adapter、WebView2 和窗口协调划分。Collector、process、ranking、IPC 和 config 保持独立领域模块。
- React 按应用组装、typed bridge、贴边胶囊、监控面板、设置和 shared 模块划分。依赖方向为应用组装到 feature，再到 shared；feature 之间通过稳定接口协作。
- 模块拥有自身策略、类型、常量、测试和 feature helper。共享模块只接收已经具有多个真实调用方的能力。
- 协议常量属于 IPC 或 bridge；配置默认值与范围属于 config；视觉值属于语义 theme token；几何和时序值属于拥有该行为的 feature。真正跨 feature 的不可变值才进入共享常量接口。
- 时间使用强类型时长；DIP、字节、速率、容量和协议限制使用强类型或带单位名称。表达产品行为的字面值在其所有者 seam 定义一次。
- 共享工具是纯函数、平台无关且具有多个消费者。Win32 行为位于平台 adapter；feature 私有 helper 保留在所属 feature。重复规则由最窄共同所有者提供唯一实现。
- C++ 使用 C++20、CMake、MSVC 和 LLVM 质量工具。React 使用 TypeScript strict mode、Vite、Tailwind CSS 和 Bun。ESLint 负责正确性，Prettier 负责格式化和 Tailwind class ordering。
- 生产环境加载打包静态资产，并通过应用拥有的 HTTPS 风格虚拟 origin 导航。开发 origin 只能通过显式开发配置启用。
- WebView2 导航、新窗口、下载和外部 scheme 采用显式 allow-list policy。静态资产缺失或损坏时，产品宿主呈现可诊断恢复界面。
- Protocol V1 使用固定版本、固定帧头、UTF-8 JSON 和 256 KiB payload 上限。未知版本、类型、非法编码和越界长度在分发前拒绝。
- Protocol V1 保留 Hello、HelloAck、TrafficTick、RankingSnapshot、CollectorStatus 和 ShutdownRequest。流量节拍以 4 Hz 发布，排行快照以 1 Hz 发布。
- 命名管道采用 owner-scoped 安全描述符、拒绝远程客户端并使用 Windows 安全身份验证客户端。payload 元数据不参与授权判断。
- Native/Web bridge 是版本化封闭联合。每个消息具有方向、schema 和 payload 限制；未知命令和字段歧义返回结构化错误。
- Native 到 React 的状态包括流量节拍、排行快照、采集状态、采集会话变化、主题、设置、呈现状态、能力和命令结果。
- React 到产品宿主的命令包括显示或隐藏监控界面、设置固定显示、打开设置、重试或重启便携采集器、更新单个白名单设置和退出应用。
- AppConfig 是公开设置的唯一权威模型。每个设置具有版本、默认值、单位、合法范围、迁移和原子持久化规则。React 消费权威 schema projection，并在产品宿主确认后采用最终值。
- 设置分类为 General、Appearance、Capsule、Monitor、Ranking、Collector 和 Diagnostics。设置平台和应用外壳在贴边胶囊与监控面板之前完成。
- 主题支持 System、Light 和 Dark；语言支持 System、English 和 Simplified Chinese。系统语言回退为 English，高对比度覆盖装饰性主题选择，运行时变更更新所有开放表面。
- 所有用户可见文本使用 locale key，所有主题相关颜色使用语义 token。共享控件从建立时即支持主题、语言、焦点、禁用和高对比度呈现。
- 产品呈现状态为 Tray Only、Dock Capsule Visible 和 Monitor Panel Visible。固定显示偏好、设置中的自动隐藏配置、显示或隐藏命令和外部全屏状态由确定性 C++ 行为模型协调。
- 贴边胶囊默认宽 10 DIP、高 64 DIP；宽度范围为 8–24 DIP，高度范围为 40–160 DIP。端部圆润度默认半径为 5 DIP，存储范围为 0–12 DIP，有效值不超过当前宽度的一半。
- 贴边胶囊向工作区内部提供至少 16 DIP 的有效命中区域。DPI 改变后几何仍以 DIP 表达。
- 下载从贴边胶囊上端向中心填充，上传从下端向中心填充。两者使用固定三段渐变，并通过对数映射将字节速率转换为填充比例。
- 速率增加采用 250 ms cubic ease-out，速率下降采用 450 ms quadratic ease-out。新目标从当前视觉值重定向；Reduced Motion 立即完成转换。
- 拖动遵循 Windows drag threshold、pointer capture、取消和完成语义。释放点决定显示器；贴边目标为工作区左侧或右侧；相等距离保留现有侧，首次相等选择右侧。
- 贴边位置受到工作区、自动隐藏任务栏唤出区域、显示器移除和混合 DPI 规则约束。临时可见性修正不覆盖用户保存的 anchor intent。
- 悬停默认 350 ms 后展开监控面板，点击立即展开。指针离开默认 500 ms 后隐藏，重新进入取消隐藏。具体延迟由 AppConfig 提供。
- 贴边胶囊与监控面板使用同一 retained native context menu command seam。菜单包含 Show/Hide Monitor、Pin/Unpin、Settings、Collector status/Retry Collector 和 Exit，并根据当前状态与语言更新标签。
- context menu 打开期间暂停 dismiss timing，关闭后重新核对 pointer state。菜单、托盘、设置和 React 入口调用同一 application action。
- 监控面板设计宽度为 340 DIP。header 展示产品身份和总下载、上传速率；排行行展示应用图标、名称、下载速率和上传速率。
- header 与排行共享速率列宽、右对齐和 tabular numerals。长名称省略；内容过高时 header 固定，仅排行 viewport 滚动。
- 应用图标异步解析和缓存，fallback 保持相同几何。Warm-up、Idle、Disconnected 和 Error 使用明确状态视图替代排行区域。
- 新 collector instance ID 建立新的采集会话。产品宿主重置序列假设，React 清除陈旧排行和跨会话动画目标，并重新进入预热状态。
- 4 Hz 总速率更新与 1 Hz 排行更新在前端状态中隔离。稳定状态没有永久 animation loop，排行快照替换旧目标而不形成动画队列。
- 产品宿主拥有 native-backed WebView2 surface、desktop backdrop、圆角裁剪、hit testing、capture、DPI 和 resize。React 负责透明或半透明内容以及视觉动画。
- High Contrast、RDP、composition unavailable 和 device loss 使用可读 fallback。resize 和呈现状态转换期间保持正确底色和轮廓。
- 产品宿主负责 single instance、托盘、设置窗口复用、login startup、便携采集器授权、启动、重试、重启、关闭和断线恢复。
- 生产包包含产品宿主、便携采集器、选定的 WebView2 native dependencies、编译后的 React 资产和产品资源。生产运行只依赖声明的系统与运行时组件。
- Fast 质量门覆盖格式、严格 Debug 构建、核心测试、静态分析、TypeScript typecheck、ESLint、Prettier、前端测试和生产前端构建。Full 追加 MSVC 分析、Release 构建与测试、clean frontend build 和便携包验证。
- 实施队列由四个顺序 Ticket 组成：项目基础设施与实时链路；设置平台与应用外壳；完整贴边胶囊体验；完整监控面板与产品交付。

## Testing Decisions

- 好的自动测试断言用户或相邻模块可以观察到的输入、输出、状态转换、错误和生命周期，不断言私有类形状、内部调用次数、React component tree 或具体 adapter 实现。
- 自动测试优先使用现有最高 seam。新增 seam 仅在无法通过现有公共接口稳定观察行为时引入，并保持接口小而完整。
- 第一条测试 seam 是便携采集器与产品宿主之间的 Protocol V1 seam。它覆盖握手、流量节拍、排行快照、采集状态、乱序、重复帧、非法 frame、payload 上限、断线、重连、ShutdownRequest 和采集会话重置。
- Protocol V1 测试沿用仓库已有 golden fixture、codec round-trip、IPC integration、security policy 和 snapshot publisher 测试先例。
- 第二条测试 seam 是产品宿主与 React 之间的 Native/Web bridge seam。它覆盖消息版本、方向、schema、payload 限制、未知命令、设置白名单、权威设置回送、命令错误、主题、语言、呈现状态和采集会话变化。
- Native/Web bridge 使用跨 C++ 与 TypeScript 的共享契约样例验证相同有效载荷和拒绝行为。测试通过公开 bridge interface 注入和读取消息，不绑定 WebView2 controller 实现。
- 第三条测试 seam 是用户可观察的产品宿主行为 seam。确定性 C++ 模型接收归一化 pointer、timer、monitor、DPI、taskbar、full-screen、menu 和 collector events，并输出可执行 effects。
- 产品宿主行为测试覆盖 Tray Only、Dock Capsule Visible、Monitor Panel Visible、固定显示、自动隐藏、悬停、点击、dismiss、重入、拖动、取消、贴边、显示器移除、混合 DPI、context menu nested loop 和 full-screen topmost yield。
- 行为模型测试沿用仓库已有 config、ranking、runtime 和 integration 测试的值语义与确定性输入方式。Win32 adapter 只验证归一化和 effect 执行，不重复领域行为测试。
- React 测试覆盖 locale catalog 完整性、semantic token 应用、设置表单校验投影、流量节拍隔离、排行 key 稳定性、采集会话重置、Reduced Motion 和可访问状态。测试通过用户可见文本、角色、状态和 bridge events 观察行为。
- 配置测试沿用已有 model、codec、validation 和 store 测试先例，覆盖默认值、范围、单位、迁移、原子保存、damaged-file diagnostics 和 unknown future schema。
- Collector 测试沿用现有 ETW、进程身份、PID reuse、应用合并、ranking window 和 runtime integration 测试先例，覆盖 TCP、UDP、IPv4、IPv6、高速和短连接场景。
- Fast 与 Full 自动门保持 headless：不创建可见窗口，不启动开发服务器，不弹出 UAC，不移动指针，也不依赖人工输入。
- Windows 11 24H2 x64 目标机验收属于第三条用户行为 seam 的真实 adapter 证明，覆盖 native-backed WebView2、desktop backdrop、圆角、hit target、resize、拖动手感、多显示器、混合 DPI、任务栏、全屏、High Contrast、RDP 和 device-loss fallback。
- Ticket 03 记录贴边胶囊与基础监控面板的 surface topology、4 Hz 更新以及 idle process、CPU 和 working-set observations，并形成 surface ADR。Ticket 04 在相同 topology 上完成生产监控面板和最终视觉验收。
- 便携包验收从解压后的声明入口启动，验证首次授权、采集、设置、胶囊、监控面板、重启、退出、便携采集器清理和 ETW session 释放。
- 性能、CPU、内存、体积和动画流畅度先记录测量结果；只有在参考机器上获得可重复数据后才形成数值验收阈值。

## Out of Scope

- Windows 11 24H2 x64 之外平台的首个实现与验收。
- 任务栏嵌入、Explorer 注入和内核驱动。
- Windows 服务形态、跨用户采集和远程采集器控制。
- 网络 payload、报文内容、完整敏感命令行参数和非必要远端地址采集。
- 面向 Web 内容的通用 native RPC、任意文件系统访问、shell 执行和任意进程控制。
- Qt、Electron、WinUI 3、Windows App SDK、WPF 和大型 UI component kit。
- WebView2 Fixed Version Runtime 打包；首个产品使用 Evergreen Runtime。
- 云同步、用户账号、遥测上传和在线后端。
- 可替换皮肤市场、插件系统和第三方脚本扩展。
- 在缺少参考机器测量时承诺固定 CPU、内存、包体积或帧率阈值。

## Further Notes

- 产品宿主、WebView2、React 和便携采集器的产品架构已经接受，实施代理应遵循现有 ADR、领域词汇表和本规格中的权威边界。
- 四个 Ticket 是顺序依赖的纵向切片。每个 Ticket 在依赖满足后可直接由代理执行，并以其 Completion proof、Fast/Full 门和适用的目标机验收共同判断完成。
- 主题、国际化、设置 schema、公共常量归属、工具复用和 dependency direction 属于基础设施合同，不在后续功能中建立平行实现。
- 自动隐藏是 AppConfig 管理的用户设置。
- context menu 的命令合同由 Show/Hide Monitor、Pin/Unpin、Settings、Collector status/Retry Collector 和 Exit 组成。
- 具体工程位置、工具配置文本和打包文件清单由架构、质量、交付文档及对应 Ticket 维护；本规格记录稳定的用户合同、模块责任和测试 seam。
- 当前实现起点是可独立构建的 C++20 便携采集器基础。产品宿主与 React 实现从 Ticket 01 开始。
