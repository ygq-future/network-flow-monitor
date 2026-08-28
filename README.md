# Network Flow Monitor

Network Flow Monitor is a portable Windows 11 per-application traffic monitor.

Current repository state:

- `nfm-collector.exe`: C++20 ETW collector, process identity, aggregation, ranking and Protocol V1.
- Product architecture: C++20 Win32/WebView2 host with a React frontend.
- UI implementation status: specified and ticketed; implementation begins with the Product Host/WebView2 tracer bullet.

The frozen product specification is [`.scratch/cpp-webview2-react-ui/spec.md`](.scratch/cpp-webview2-react-ui/spec.md). Four feature-oriented implementation tickets are in the adjacent `issues/` directory.

## Build the collector baseline

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

Use `tools/quality.ps1` for the authoritative repository gate. Frontend commands become active when Ticket 01 creates `apps/nfm-ui/`.

## Product topology

```text
NetworkFlowMonitor.exe (C++ Win32/WebView2 Host + packaged React UI)
        |
        | owner-scoped Named Pipe, Protocol V1
        v
nfm-collector.exe (C++20 ETW Collector)
```

Target platform: Windows 11 24H2 x64.
