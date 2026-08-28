# Findings

## Collector baseline

- The implemented Collector uses ETW, PID lifetime tracking, application identity, sliding-window ranking and Protocol V1.
- Protocol V1 in code is a 12-byte little-endian frame with a 256 KiB UTF-8 JSON payload limit.
- `TrafficTick` publishes at 4 Hz and `RankingSnapshot` at 1 Hz.
- Portable lifetime already supports a parent PID and named stop event.
- The former executable combined Collector and presentation responsibilities, so baseline cleanup requires a dedicated `nfm-collector.exe` entry point rather than directory deletion alone.

## Product architecture

- The Product Host must own Windows behavior and security-sensitive capabilities; React owns visual presentation.
- WebView2 local static content requires an explicit production origin and navigation policy.
- A transparent/native-backed WebView surface requires Windows 11 target-host evidence as part of the capsule and monitor feature acceptance.
- Frontend build tooling is absent from production runtime and the packaged application loads only compiled assets.

## Repository cleanup

- The accepted product topology needs one portable Collector lifecycle and one native Product Host lifecycle; additional background lifecycle modes are unnecessary.
- Product behavior is consolidated into the frozen specification and four feature-oriented tickets.
- Historical build trees and package outputs must be removed so later verification cannot execute stale binaries.
