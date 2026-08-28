# 04 — 完整监控面板与产品交付

**Blocked by:** 03 — 完整贴边胶囊体验

**Status:** ready-for-agent

在已验证的贴边胶囊路线和 native surface 上完成生产级监控面板、排行体验、面板交互、便携打包与最终验收。

## 数据与会话

- [ ] Host 解码并验证 `RankingSnapshot` 与 `CollectorStatus`，维护 Connected、Disconnected、Warm-up、Idle 和 collector instance epoch。
- [ ] 新 `collector_instance_id` 重置序列、陈旧排行和跨会话动画历史；重连、乱序、重复帧和实例切换具有确定性测试。
- [ ] 4 Hz aggregate rate 更新不重建排行树，1 Hz 排行快照替换旧目标且不排队。

## 面板设计与呈现

- [ ] 完成 340 DIP 生产面板、背景材质、产品 header、总下载/上传速率、Top N、状态行和内部 ranking viewport。
- [ ] header 与 rows 共享 rate column width、右对齐、tabular numerals；长应用名 ellipsis，列表过高时仅排行 viewport 滚动。
- [ ] 应用图标异步解析与缓存，fallback 保持相同几何；warm-up、idle、offline 和 error 状态不留下无解释空白。
- [ ] 排行 reorder、entry、exit 从当前视觉位置重定向；Reduced Motion、System/Light/Dark、High Contrast 和全部语言即时正确呈现。
- [ ] 使用既有 theme token、locale catalog、共享 controls、constants 和 utilities，新增规则归属到 monitor feature 或其最窄共同模块。

## 面板交互与交付

- [ ] 面板支持拖动、位置持久化、显示器/DPI/work-area 变化、Pin、离开隐藏、全屏 topmost yield 和稳定的贴边胶囊往返过渡。
- [ ] 面板 context menu 与贴边胶囊、tray 和 application actions 保持状态一致，Settings 激活复用已有窗口。
- [ ] resize、展开/收起、背景、圆角和动画期间无黑色 gutter、错误底色或永久 JavaScript frame loop。
- [ ] portable ZIP 包含产品宿主、便携采集器、选定 WebView2 loader/native dependencies、`ui/` 和 resources；生产运行不依赖 Node、Bun、Vite server、网络或开发工具。
- [ ] 完成完整 Fast/Full gate、package verification 和 Windows 11 24H2 x64 Target-Host Acceptance。

**Completion proof:** 解压 ZIP 后可完成首次授权、启动、贴边胶囊监控、完整面板排行、设置、拖动/停靠、重启和干净退出；生产视觉、交互、动画、资源回退和生命周期通过自动门及人工验收。
