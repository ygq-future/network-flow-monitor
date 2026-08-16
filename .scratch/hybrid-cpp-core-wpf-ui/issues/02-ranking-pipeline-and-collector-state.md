# 02 — 排行流水线、Collector 状态机与会话重置

**What to build:**
Deliver the 1 Hz Top N application ranking stream, collector diagnostics, and session lifecycle synchronization. The C++ collector sliding-window ranking engine publishes Top N process bandwidth snapshots and `CollectorStatus` events. The WPF client ingests these into `TelemetryStore`, extracts application metadata, binds ranking rows to presentation ViewModels, and manages pure telemetry connection/data states (Connected, Disconnected data clearing, Warm-up, Idle) and instance resets when the collector restarts (decoupled from window Pin/visibility presentation behavior, which belongs to Ticket 03).

**Blocked by:** 01 — Protocol V1 基础帧协议与端到端 Tracer Bullet

**Status:** completed

- [x] C++ collector serializes and broadcasts 1 Hz `RankingSnapshot` (Type `0x0004`) containing status, aggregate rates, and Top N applications with process identity (id, name, path, icon_key, download rate, upload rate).
- [x] C++ collector broadcasts `CollectorStatus` (Type `0x0005`) diagnostic events (ETW active state, dropped packet counters, active tracked PID count).
- [x] Owns and implements cross-language golden protocol fixtures in `tests/fixtures/` for `RankingSnapshot` and `CollectorStatus`.
- [x] C# `TelemetryStore` atomically replaces the latest ranking snapshot, discarding out-of-order or stale snapshots without queuing obsolete frames.
- [x] When reconnecting to a collector with a new `collector_instance_id`, `TelemetryStore` immediately resets sequence expectations, purges stale ranking rows, restarts data warm-up state, and discards cross-instance history.
- [x] Telemetry data state reflects Disconnected (data cleared), Warm-up (`Collecting data…`), and Idle (`No network activity`) states cleanly in `TelemetryStore` models (window Pin/visibility state remains owned by `MonitorBehaviorModel` in Ticket 03).
- [x] Tracer Bullet UI presents the Top N application ranking list updated live at 1 Hz alongside 4 Hz aggregate rates.
- [x] C++ unit tests and C# unit tests verify ranking serialization, deserialization, rate formatting, and instance-reset transitions.
