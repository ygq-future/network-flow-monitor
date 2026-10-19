# ADR 0012：使用可审计的 x64 ZIP 交付

- 状态：已接受
- 日期：2026-08-12
- 修订：2026-08-28

## 决策

Release 使用静态 MSVC 运行库与静态 `WebView2LoaderStatic.lib`，通过 CMake/CPack 生成单层目录结构的 x64 便携 ZIP，包含 `NetworkFlowMonitor.exe`、`nfm-collector.exe`、`ui/` 静态资产和 `README.md`。

生产 host 使用 WebView2 Evergreen Runtime，并通过虚拟主机目录映射加载包内资产。所有运行期数据（`<app_root>/data/config.json`、`<app_root>/data/webview2`）与滚动诊断日志（`<app_root>/logs/nfm-host.log` 及最多 5 个 2 MiB 历史分卷）全部归口在应用根目录下。

## 后果

- 交付内容可枚举、可哈希且不依赖开发机可选 DLL。
- `tools/package.ps1` 始终从干净 Release 构建调用 CPack。
- WebView2 host 尚未实现期间，collector-only ZIP 只是核心验证产物，不是完整产品发行版。
