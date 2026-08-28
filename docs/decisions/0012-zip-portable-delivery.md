# ADR 0012：使用可审计的 x64 ZIP 交付

- 状态：已接受
- 日期：2026-08-12
- 修订：2026-08-28

## 决策

Release 使用静态 MSVC 运行库并通过 CMake/CPack 生成 x64 ZIP。当前仓库可交付的验证基线仅包含 `nfm-collector.exe` 与 README；WebView2 host 完成后，正式产品包必须同时包含 `NetworkFlowMonitor.exe`、`nfm-collector.exe` 和相邻的 `ui/` 静态资产。

生产 host 使用 WebView2 Evergreen Runtime，并通过虚拟主机目录映射加载包内资产。ZIP 不写入注册表、不创建安装记录，也不声称具备自动更新能力。

## 后果

- 交付内容可枚举、可哈希且不依赖开发机可选 DLL。
- `tools/package.ps1` 始终从干净 Release 构建调用 CPack。
- WebView2 host 尚未实现期间，collector-only ZIP 只是核心验证产物，不是完整产品发行版。
