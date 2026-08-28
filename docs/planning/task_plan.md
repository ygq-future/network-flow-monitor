# Task Plan: Network Flow Monitor

## Goal

Deliver a portable Windows 11 per-application network monitor using a C++20 Collector, C++20 Win32/WebView2 Product Host and packaged React frontend.

## Current state

- Collector, process identity, ranking and Protocol V1 foundations exist.
- Product architecture is frozen in `.scratch/cpp-webview2-react-ui/spec.md`.
- Product UI implementation has not started.
- The repository is a clean, independently buildable Collector baseline ready for Ticket 01.

## Execution queue

| Order | Ticket | Status | Completion gate |
|---:|---|---|---|
| 0 | Collector baseline cleanup | complete | Independent `nfm-collector.exe`, core tests and quality gate pass |
| 1 | Project foundation and live tracer | ready | Planned module structure, maintainability rules, theme/i18n foundation, Collector-to-React TrafficTick and combined gate |
| 2 | Settings platform and application shell | blocked by 01 | Authoritative live settings, tray/single-instance shell and Collector lifecycle |
| 3 | Complete Dock Capsule experience | blocked by 02 | Configurable Dock Capsule, traffic visuals, drag/dock, basic panel, context menu, Settings route and surface ADR |
| 4 | Monitor panel and product delivery | blocked by 03 | Production ranking panel, complete interactions, portable package and target-host acceptance |

The implementation queue contains four sequential, feature-oriented vertical slices.

## Immediate work

Ticket 01 is the next implementation unit. It establishes the C++ Win32/WebView2 host, React workspace, module/reuse constraints, theme/localization foundations and the live-rate tracer.

## Completion discipline

Each ticket follows: approved plan, tests, implementation, Fast gate, Full gate, planning update, target-host acceptance where required, and Git commit only after explicit user authorization.
