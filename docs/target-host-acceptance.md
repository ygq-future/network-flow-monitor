# Windows 11 24H2 目标机验收清单

本清单用于首发目标 Windows 11 24H2 x64。当前开发机是 Windows 11 25H2 build 26200，不能用它替代本清单的目标机结论。

## 1. 包和系统基线

1. 将 `NetworkFlowMonitor-0.1.0-win-x64.zip` 解压到一个稳定目录，例如 `C:\Tools\NetworkFlowMonitor-0.1.0-win-x64`。安装服务后不要移动该目录。
2. 在项目 `tools/` 目录运行只读探针：

   ```powershell
   pwsh -NoProfile -File tools/acceptance.ps1 `
     -PackageRoot 'C:\Tools\NetworkFlowMonitor-0.1.0-win-x64'
   ```

3. 保存输出到 `docs/evidence/win11-24h2-<date>-acceptance.json`。确认 `os_build` 为 26100 系列、`ui_smoke_exit_code` 为 0，并记录 `service_query_exit_code` 的初始值。
4. 确认解压目录只有 `NetworkFlowMonitor.exe` 和 `README.md`，没有可选运行库 DLL。

## 2. ETW 和协议矩阵

如果目标机具备仓库和 Visual Studio 开发环境，在管理员 PowerShell 中运行：

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

记录 Debug/Release 的 CTest 总数和以下测试结果：

- `collector.etw_integration`：TCP、UDP、IPv4、IPv6、短连接、高速 burst、进程生命周期、停止清理和 ETW 丢失统计。
- `ipc.integration`：真实命名管道、显式 ACL、恶意帧、慢客户端隔离、重连和停止清理。

任何失败都必须记录完整输出、Windows build、是否管理员令牌和测试退出码；不得仅重跑到通过后删除失败记录。

## 3. 设置窗口和 SCM/UAC

在稳定目录启动普通 UI（不要用 `--collector` 或 `--service`）：

1. 打开托盘菜单中的设置，确认服务状态初始值与 `--service-query` 一致。
2. 在设置中执行“安装”，确认出现 UAC，确认返回后状态为已安装/已停止。
3. 依次执行查询、启动、停止、重启；每一步都记录界面状态、SCM 状态和错误码。
4. 修改服务启动类型为手动、自动、自动（延迟），重新打开设置确认值保持。
5. 切换到便携模式，确认 UAC 启动采集器，确认 UI 能连接；再切回服务模式，确认旧采集器/服务按设计停止且失败时配置回滚。
6. 执行卸载，确认服务消失、用户配置文件仍按文档保留，且稳定目录中的程序未被隐式移动或删除。
7. 将新版本解压到另一个稳定目录，安装/升级服务，确认 SCM 二进制路径指向新目录；完成后按产品决定保留旧目录或卸载旧服务并记录结果。

## 4. 托盘、悬浮窗和设置视觉检查

在单显示器和多显示器各执行一次，至少覆盖 100%、125% 和 150% DPI；记录截图或短视频：

- 托盘左键显示/隐藏悬浮窗，右键菜单可打开设置和退出。
- 悬停达到配置的延迟后出现排行面板；离开后按消失延迟关闭。
- 悬浮窗拖动释放后按工作区边缘吸附；开启自动隐藏后只留下露出条，鼠标移入可恢复。
- 显示器有负坐标、任务栏自动隐藏、工作区尺寸变化和 shell 托盘溢出时，窗口仍在正确工作区内。
- 修改 Top N、排行窗口、数据显示刷新间隔、方向、单位、不透明度和贴边参数，保存后立即生效，重启后仍保持。
- 无采集连接时，悬浮窗显示断线状态，不显示伪造流量。

## 5. 性能和稳定性记录

不要预先写入阈值。分别记录空闲 5 分钟、普通使用 5 分钟和高流量 5 分钟的：

- UI 进程 CPU、工作集峰值和趋势；
- 采集器/服务 CPU、工作集峰值；
- 排行快照到可见数据的延迟；
- ETW lost events、活动连接数和异常日志；
- UI 退出后是否残留采集器进程、管道或服务句柄。

将原始截图、日志和 JSON 放在不提交 Git 的 `docs/evidence/`，只把摘要和环境信息写入 `docs/planning/progress.md`。

## 6. 通过标准

只有当 24H2 ETW/IPC、真实 UAC/SCM 生命周期、视觉矩阵和性能记录都完成，才能把 Phase 5 标记为 complete。25H2 自动化 Full 门通过只能证明当前主机的实现质量，不能单独关闭这些目标机项目。
