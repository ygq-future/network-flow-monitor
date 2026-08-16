# 02 — 排行流水线、Collector 状态机与会话重置

**What to build:**
Deliver the 1 Hz Top N application ranking stream and collector session lifecycle synchronization. The C++ collector sliding-window ranking engine publishes Top N process bandwidth snapshots and diagnostic events. The WPF client ingests these snapshots into `TelemetryStore`, extracts and caches application metadata, binds ranking rows to presentation ViewModels, and handles connection states (Connected, Disconnected overlay, Warm-up, Idle) and instance resets when the collector restarts.

**Blocked by:** 01 — Protocol V1 基础帧协议与端到端 Tracer Bullet

**Status:** ready-for-agent

- [ ] C++ collector serializes and broadcasts 1 Hz `RankingSnapshot` (Type `0x0004`) containing status, aggregate rates, and Top N applications with process identity (id, name, path, icon_key, download rate, upload rate).
- [ ] C++ collector broadcasts `CollectorStatus` (Type `0x0005`) diagnostic events (ETW active state, dropped packet counters, active tracked PID count).
- [ ] C# `TelemetryStore` atomically replaces the latest ranking snapshot, discarding out-of-order or stale snapshots without queuing obsolete frames.
- [ ] When reconnecting to a collector with a new `collector_instance_id`, `TelemetryStore` and Presentation layer immediately reset sequence expectations, purge stale ranking rows, restart data warm-up state, and discard cross-instance history.
- [ ] Presentation state machine reflects Disconnected (offline overlay with retained Pin state), Warm-up (`Collecting data…`), and Idle (`No network activity`) states cleanly in UI models.
- [ ] Tracer Bullet UI presents the Top N application ranking list updated live at 1 Hz alongside 4 Hz aggregate rates.
- [ ] C++ unit tests and C# unit tests verify ranking serialization, deserialization, rate formatting, and instance-reset transitions.
