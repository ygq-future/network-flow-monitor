# 03 — 完整贴边胶囊体验

**Blocked by:** 02 — 设置平台与应用外壳

**Status:** ready-for-agent

交付从可配置贴边胶囊到基础监控面板、右键菜单和 Settings 的完整用户路线；贴边胶囊相关视觉、动画、窗口行为与目标机 surface 证据在同一 Ticket 内验收。

## 贴边胶囊本体

- [ ] 实现默认 10 × 64 DIP 贴边胶囊，宽度 8–24 DIP、高度 40–160 DIP，end rounding 受当前宽度一半约束，至少提供 16 DIP inward hit target。
- [ ] width、height 与 end rounding 修改实时生效，DPI 变化后保持 DIP 语义和可点击性。
- [ ] 下载从上端向中心填充，上传从下端向中心填充，采用规格中的固定渐变和对数 rate mapping。
- [ ] 4 Hz 新目标从当前视觉值重定向；increase 250 ms、decrease 450 ms；Reduced Motion 立即完成。
- [ ] 断线、重连与 collector instance epoch 切换清理陈旧值并呈现明确状态。

## 贴边胶囊交互与窗口行为

- [ ] 贴边胶囊遵循 Windows drag threshold，支持捕获、取消、完成、release-point monitor 选择和位置持久化。
- [ ] 支持左右侧吸附、tie rule、work-area clamp、auto-hidden taskbar reservation、monitor fallback 与 mixed-DPI/display-change 重算。
- [ ] hover delay 后显示基础监控面板，click 立即显示；离开后按 dismiss delay 隐藏，重入取消隐藏。
- [ ] Pin、Settings 中的 auto-hide 配置、TrayOnly/CapsuleVisible/PanelVisible 和 Hide/Show Monitor 由确定性 C++ behavior model 协调。
- [ ] 贴边胶囊与基础监控面板均提供 retained native context menu：Show/Hide Monitor、Pin/Unpin、Settings、Collector status/Retry Collector、Exit。
- [ ] context menu 打开期间暂停 dismiss，关闭后重新核对 pointer state；命令与 tray/application action 使用同一实现。
- [ ] 可从贴边胶囊、基础监控面板和 context menu 正常进入已有 Settings 页面。

## Surface 与验收

- [ ] 在 Windows 11 24H2 x64 验证 transparent/native-backed WebView2、desktop backdrop、贴边胶囊轮廓、圆角、命中、拖动与无黑色 gutter。
- [ ] 验证 production `dist/`、4 Hz 更新、idle process/CPU/working-set observations、High Contrast、RDP、composition/device-loss fallback。
- [ ] 以 ADR 记录选定的 WebView2 HWND/controller/composition topology 和实测证据。
- [ ] 将状态、拖动、停靠、hover/dismiss、菜单嵌套循环、DPI、显示器变化与全屏 topmost yield 纳入确定性 headless tests。

**Completion proof:** 在目标机上可调整贴边胶囊几何、观察实时双向渐变、拖动并左右吸附、hover/click 打开基础面板、使用完整右键菜单并进入 Settings；自动测试、surface ADR、Fast/Full gate 和本 Ticket 人工验收全部通过。
