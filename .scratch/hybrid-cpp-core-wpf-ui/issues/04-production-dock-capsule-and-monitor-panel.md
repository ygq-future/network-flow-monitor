# 04 — 胶囊现代流体动效与监控面板交互质感

**What to build:**
Deliver the complete, production-grade visual experience for the dock capsule and single monitor panel. The dock capsule renders 4 Hz fluid rate dynamics via layout-free `RenderTransform` (`ScaleTransform`) and dynamic `ColorAnimation`, featuring logarithmic rate-to-fill scaling, fixed gradient topology, continuous RGB color morphing, and 250ms attack / 450ms decay asymmetric ballistics. The single monitor panel renders the header (`[NFM] Network Flow Monitor` $\to$ total download $\to$ total upload), shared tabular rate columns, virtualized Top N ranking rows with smooth Y translation / entrant fade / exit fade transitions, asynchronous icon extraction cache (`SHGetFileInfo`) with managed handle cleanup, over-tall scrolling, and the shared native context menu with dynamic Show/Hide items and persistence intent emission.

**Blocked by:** 02 — 排行流水线、Collector 状态机与会话重置, 03 — WPF 窗口宿主预研 (Spike) 与确定性行为模型集成

**Status:** ready-for-agent

- [ ] Rate-to-fill pure math function $F(R) = \operatorname{clamp}(\log_2(R)/30.0, 0.0, 1.0)$ maps Byte, KiB, MiB, and GiB saturation identically and monotonically for download and upload.
- [ ] Capsule geometry is fixed; fill height animates strictly via `RenderTransform` (`ScaleTransform`) without modifying `Width`, `Height`, or layout grid definitions.
- [ ] Asymmetric ballistics: rate growth animates with 250 ms Fluent Cubic Ease-Out ($1 - (1-t)^3$); rate decay animates with 450 ms natural momentum decay ($1 - (1-t)^2$).
- [ ] Fixed gradient stop collection continuously interpolates colors across RGB channels (download: `#19D8E6` Cyan $\to$ `#2878D0` Blue $\to$ `#8146B8` Purple; upload: `#F4D03F` Yellow $\to$ `#F39C32` Orange $\to$ `#D93636` Red) without abrupt snapping at threshold boundaries.
- [ ] New telemetry arriving mid-animation retargets smoothly from the current rendered visual value ($v_{\text{current}} \to v_{\text{target}}$) without snapping or queuing stale frames; identical targets do not restart animations.
- [ ] Stable idle presentation does not execute active render loops or repeatedly invalidate the visual tree.
- [ ] Single monitor panel renders untranslated brand `Network Flow Monitor`, right-aligned tabular total download and upload rates, and virtualized Top N ranking rows in a compact 340 DIP card.
- [ ] Ranking row transitions smoothly translate existing rows to new Y positions, fade in new entrants, fade out exiting rows, and retarget immediately upon new snapshots without queuing stale history.
- [ ] Application icons are extracted asynchronously via `SHGetFileInfo`, cached in memory with explicit `HICON` ownership / `DestroyIcon` GDI cleanup, and use consistent fallback icons for inaccessible/system processes without shifting row geometry.
- [ ] Over-tall ranking viewport scrolls smoothly via mouse wheel, touchpad, and keyboard while keeping the header row pinned; application names ellipsize without widening the card.
- [ ] Shared native context menu renders dynamic Show/Hide Monitor items based on `TrayOnly` / visible state, Pin toggle (disabled with retained check when auto-hide is off, emits `EffectIntent.PersistPin`), pause-dismiss on open, and Settings forward activation.
- [ ] **Live-Host Human Visual Acceptance (Mandatory Gate)**: Production WPF visual quality requires live Target-Host Acceptance. Automated unit tests prove state, math, and contracts, but do not certify visual polish. Ticket 04 is only complete after running real `NetworkFlowMonitor.exe` and human passes visual/interaction evaluation for:
  - Capsule 350ms hover reveal feel
  - Capsule click reveal immediate transition
  - Auto-hide 500ms dismiss smoothness and mid-flight re-entry reversal
  - Dragging smoothness across display edges
  - Fluid rate fill animation without 250ms stepping
  - Continuous RGB gradient color transitions
  - Ranking row reordering animation feel
  - Shared context menu presentation and dismiss pausing
  - Compact 340 DIP card layout and typography alignment
  - DWM backdrop material and Windows 11 rounded corners
  - Theme switching (System, Light, Dark)
  - Language switching (System, English, Simplified Chinese)
