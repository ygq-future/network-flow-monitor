# 打包与交付验收

## 当前交付形态

首版提供一个 x64 ZIP 绿色包。包内只有 `NetworkFlowMonitor.exe` 和 `README.md`；Release 使用静态 MSVC 运行库，不随包携带可选开发 DLL。解压目录必须保持稳定，若用户在设置界面安装 Windows 服务，SCM 会把当前可执行文件的绝对路径和固定 `--service` 入口注册为服务命令行，移动或删除该目录会使服务无法启动。

生成包：

```powershell
pwsh -NoProfile -File tools/package.ps1 -Configuration Release
```

脚本会重新构建 Release、执行 CMake 安装目标、拒绝安装树中的 DLL，并生成：

```text
dist/NetworkFlowMonitor-<version>-win-x64/
dist/NetworkFlowMonitor-<version>-win-x64.zip
```

CPack ZIP 目标也由 `tools/quality.ps1 -Mode Full` 验证；质量门不把生成的 `dist/` 或 `build/` 内容提交到 Git。

## 当前主机测量

以下数据来自 Windows 11 25H2 x64（build 26200）的 Release 构建，只是开发测量，不是宣传阈值：

- 可执行文件：775,168 bytes。
- ZIP 包：376,536 bytes；SHA-256 由每次打包脚本输出（`Compress-Archive` 包含时间元数据，因此哈希可能变化）。
- `--ui-smoke-test` 退出码：0；最近一次墙钟时间约 98.754 ms（短烟测路径，重复运行会波动，不是性能阈值）。
- 只读验收脚本的同批短 smoke 采样：峰值工作集约 12.9 MiB、CPU 时间约 46.9 ms；采样仅覆盖启动/退出路径，不代表高流量运行性能。
- 依赖为 Windows 系统组件：`ADVAPI32.dll`、`DWrite.dll`、`d2d1.dll`、`GDI32.dll`、`KERNEL32.dll`、`SHELL32.dll`、`tdh.dll`、`USER32.dll` 及同步 API-set。

重新测量并写入被忽略的 `dist/measurements-release.json`：

```powershell
pwsh -NoProfile -File tools/measure-release.ps1 -Configuration Release
```

目标机验收建议从解压后的稳定目录运行以下只读检查；它不会安装、启动、停止或删除服务：

```powershell
pwsh -NoProfile -File tools/acceptance.ps1 -PackageRoot 'C:\Tools\NetworkFlowMonitor-0.1.0-win-x64'
```

该脚本记录系统版本、包哈希、UI smoke 墙钟时间/峰值工作集/CPU 时间、SCM 当前状态和退出码；这些资源值只描述短 smoke 路径，不是高流量运行阈值。真实 UAC/SCM 变更仍必须由设置界面逐项确认并记录。

需要自动记录并清理 SCM 生命周期时，可在确认同名服务尚未安装后使用 `tools/service-acceptance.ps1 -MutateScm`；该脚本默认拒绝修改，且不会替代设置界面的 UAC 和视觉验收。

完整目标机步骤见 [`docs/target-host-acceptance.md`](target-host-acceptance.md)。

内存、CPU、ETW 丢失率和排行延迟必须在目标 Windows 11 24H2 x64 机器上按相同场景测量后才能形成验收结论；当前 25H2 数据不能替代该结果。

## 发布前人工矩阵

| 场景 | 证据要求 | 当前状态 |
|---|---|---|
| 托盘/悬浮窗、多显示器、混合 DPI、任务栏自动隐藏 | 截图或人工记录，确认位置、贴边、悬停和关闭延迟 | 待目标机人工验收 |
| 设置窗口配置往返 | 修改全部公开字段，重启 UI 后值保持，非法值显示错误；服务状态和登录自启状态可见/可操作 | 原生创建/关闭、服务状态查询和登录自启逻辑自动化通过；完整往返待人工验收 |
| 服务安装/启动/停止/重启/卸载 | UAC 确认、SCM 实际状态、错误码和断线重连记录 | 待管理员目标机验收 |
| 绿色采集器 | 普通 UI 启动 UAC 采集器，退出 UI 后采集器句柄清理 | 自动化逻辑通过；人工交互待验收 |
| Windows 11 24H2 x64 | ETW schema、权限、IPv4/IPv6/TCP/UDP 和高流量矩阵 | 当前仅有 25H2 证据 |
| 升级/卸载 | 保留或清理用户配置的行为、旧服务路径和旧进程 | 服务控制升级分支已有单元覆盖；实际 SCM 路径待目标机验收 |

当前 UI 绘制检查点已完成：悬浮窗显示真实总上传/下载速率，排行面板显示经过单位和方向配置处理的 Top N 应用；拖动释放会按当前显示器工作区贴边，自动隐藏会留下可恢复的露出条。该行为已由 `ui.display_format` 和 `ui.window_logic` 自动化覆盖；截图、多显示器和混合 DPI 仍需目标机人工验收。
