# Architecture

## Runtime

```text
NetworkFlowMonitor.exe
  C++20 Win32 Product Host
  WebView2 controller
  tray, window behavior, config, Protocol V1 client
  packaged React frontend
          |
          | owner-scoped local Named Pipe
          v
nfm-collector.exe
  C++20 ETW collector
  process identity, aggregation, ranking, Protocol V1 server
```

The Product Host is the authority for Windows behavior, configuration, authorization and bridge validation. React is the authority for visual presentation. The Collector is the authority for traffic and ranking facts.

## Runtime data flow

1. Host starts the portable Collector with a parent PID and stop-event name.
2. Collector creates an owner-scoped local named pipe and begins ETW collection.
3. Host completes Protocol V1 `Hello`/`HelloAck`.
4. Collector publishes 4 Hz `TrafficTick`, 1 Hz `RankingSnapshot` and diagnostic `CollectorStatus`.
5. Host validates and converts messages to bridge DTOs.
6. React renders the latest declared state and owns only visual interpolation.

## Native/web seam

The bridge exposes fixed messages documented in the frozen spec. Web commands are allow-listed, versioned and schema-validated. The frontend has no raw pipe, filesystem, shell, monitor-enumeration or authorization capability.

## Module and reuse seams

The Native Host is divided into composition, bridge, lifecycle, platform, WebView and window modules. React is divided into app composition, typed bridge, capsule/monitor/settings features and narrowly curated shared modules. Domain interfaces do not depend on Win32, WebView2 or storage implementations; adapters implement those seams.

Configuration defaults and ranges belong to `config`, protocol constants belong to `ipc`/`bridge`, visual values belong to semantic theme tokens and feature behavior values belong to the owning feature. Shared constants and utilities exist only for multiple real consumers, preserving one canonical implementation of each rule.

## Local frontend content

Production maps the packaged `ui/` directory to an application-owned HTTPS-style virtual hostname through WebView2. Development may use an explicit Vite origin. These paths are separate and testable.

## Window behavior

The host owns a deterministic C++ behavior model for `TrayOnly`, `CapsuleVisible` and `PanelVisible`, including docking, drag, hover, dismiss, Pin, DPI, display fallback, taskbar reservation, context-menu pause and full-screen topmost yield.

The complete contract is [the frozen specification](../.scratch/cpp-webview2-react-ui/spec.md).
