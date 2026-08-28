# Delivery

The target portable distribution is:

```text
NetworkFlowMonitor.exe
nfm-collector.exe
required WebView2 loader/native dependencies
ui/
  index.html
  assets/*
resources/
README.md
```

The system dependency is WebView2 Evergreen Runtime. Startup detects availability and presents native recovery guidance before WebView creation.

The current repository stage can package only the implemented Collector baseline. The complete product package becomes an acceptance artifact in Ticket 04 after the Product Host and frontend exist.

Generated `build/`, `dist/` and frontend `dist/` directories are never source artifacts.
