# Delivery

The target portable distribution is:

```text
NetworkFlowMonitor.exe
nfm-collector.exe
ui/
  index.html
  assets/*
README.md
data/                      (created at runtime under application root)
  config.json
  webview2/
logs/                      (created at runtime under application root)
  nfm-host.log
  nfm-host.1.log .. nfm-host.5.log (size-capped rotating logs, max 2 MiB per file)
```

The system dependency is WebView2 Evergreen Runtime. Startup detects availability and presents native recovery guidance before WebView creation. WebView2Loader is linked statically (`WebView2LoaderStatic.lib`), so no external loader DLL is required.

All runtime data (`data/config.json`, `data/webview2/`) and rotating diagnostic logs (`logs/nfm-host.log`) reside strictly inside the portable application root directory.

Generated `build/`, `dist/` and frontend `dist/` directories are never source artifacts.
