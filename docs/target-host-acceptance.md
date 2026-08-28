# Windows 11 Target-Host Acceptance

Target: Windows 11 24H2 x64.

## Collector

- First authorization and subsequent portable start behavior.
- TCP/UDP, IPv4/IPv6, short connections and sustained traffic.
- 4 Hz TrafficTick and 1 Hz RankingSnapshot.
- Process exit, PID reuse and multi-process application merge.
- Host exit/crash terminates the Collector and releases the ETW session.
- Disconnect, retry, restart and collector-instance reset.

## Native surface evidence

- Transparent/native-backed WebView2 content and real desktop backdrop.
- Rounded panel, capsule silhouette and no black gutter during resize.
- Mouse capture, hit testing and context-menu behavior.
- Mixed DPI, multi-monitor placement and display changes.
- 4 Hz updates without a permanent frame loop.
- Idle WebView process, CPU and working-set observations.
- Packaged static `ui/` loading and missing-runtime recovery.
- High Contrast/RDP/composition fallback.

## Product interaction

- TrayOnly, CapsuleVisible and PanelVisible.
- 350 ms hover, immediate click reveal and 500 ms dismiss.
- Drag threshold, cancel/complete, release-point monitor and left/right tie rule.
- Pin, Settings-controlled auto-hide, Hide/Show and context-menu pause.
- Capsule and panel context menus expose Show/Hide Monitor, Pin/Unpin, Settings, Collector status/Retry Collector and Exit with localized state-correct labels.
- Taskbar reservation, work-area correction, monitor fallback and full-screen topmost yield.
- Theme, language, Reduced Motion and settings persistence.

## Delivery

- ZIP contains only declared runtime files.
- Production starts with no Node, Bun, Vite server or network dependency.
- Clean exit leaves no Collector or ETW session owned by the application.
