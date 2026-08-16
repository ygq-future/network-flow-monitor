# 04 — 胶囊现代流体动效与监控面板交互质感

**What to build:**
Deliver the complete, production-grade visual experience for the dock capsule and single monitor panel. The dock capsule renders 4 Hz fluid rate dynamics via GPU composition-accelerated `RenderTransform` (`ScaleTransform`) and dynamic `ColorAnimation`, featuring logarithmic rate-to-fill scaling, fixed gradient topology, and 250ms attack / 450ms decay asymmetric ballistics. The single monitor panel renders the header, tabular rate columns, virtualized Top N ranking rows with smooth reordering, asynchronous icon extraction cache (`SHGetFileInfo`), over-tall scrolling, and the shared native context menu emitting state persistence intents.

**Blocked by:** 02 — 排行流水线、Collector 状态机与会话重置, 03 — WPF 窗口宿主预研 (Spike) 与确定性行为模型集成

**Status:** ready-for-agent

- [ ] Rate-to-fill pure math function $F(R) = \operatorname{clamp}(\log_2(R)/30.0, 0.0, 1.0)$ maps Byte, KiB, MiB, and GiB saturation identically and monotonically for download and upload.
- [ ] Capsule geometry is fixed; fill height animates strictly via `RenderTransform` (`ScaleTransform`) without modifying `Width`, `Height`, or layout grid definitions.
- [ ] Asymmetric ballistics: rate growth animates with 250 ms Fluent Cubic Ease-Out ($1 - (1-t)^3$); rate decay animates with 450 ms natural momentum decay ($1 - (1-t)^2$).
- [ ] Fixed gradient stop collection continuously interpolates colors: download transitions cyan (`#19D8E6`) $\to$ blue (`#2878D0`) $\to$ purple (`#8146B8`); upload transitions yellow (`#F4D03F`) $\to$ orange (`#F39C32`) $\to$ red (`#D93636`).
- [ ] New telemetry arriving mid-animation retargets smoothly from the current rendered visual value ($v_{\text{current}} \to v_{\text{target}}$) without snapping or queuing stale frames; identical targets do not restart animations.
- [ ] Stable idle presentation does not execute active render loops or repeatedly invalidate the visual tree.
- [ ] Single monitor panel renders product brand, tabular right-aligned total download and upload rates, and virtualized Top N application rows.
- [ ] Application icons are extracted asynchronously via `SHGetFileInfo`, cached in memory with explicit `HICON` ownership / `DestroyIcon` leak prevention, and use consistent fallback icons for inaccessible/system processes without shifting row geometry.
- [ ] Over-tall ranking viewport scrolls smoothly via mouse wheel, touchpad, and keyboard; ranking rows reorder smoothly without pop-in.
- [ ] Shared native context menu provides Pin toggle (disabled with retained check when auto-hide is off, emits persistence intents for Ticket 05), pause-dismiss on open, and direct Settings access.
