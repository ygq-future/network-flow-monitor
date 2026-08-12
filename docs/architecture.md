# 架构说明

## 总览

系统由普通权限 UI 和高权限采集器组成。两者来自同一个可执行文件，但运行在不同进程和权限上下文中。

```text
ETW 网络/进程事件
        │
        ▼
高权限 Collector ──累计应用上下行字节──► 命名管道
                                              │
                                              ▼
普通权限 UI ──滑动窗口/速率/Top N──► 托盘、悬浮窗、排行面板
        │
        └──提权服务管理代理──► Windows Service Control Manager
```

采集器不决定 Top N，也不保存某个用户的排行窗口。它维护应用累计计数，并按每个 UI 客户端请求的刷新间隔发送快照。UI 保存短期历史，根据自己的配置计算滑动窗口速率和排行。

## 单一可执行文件的运行模式

| 模式 | 权限 | 用途 |
|---|---|---|
| 默认 | 普通用户 | 托盘、悬浮窗、排行、设置和服务管理界面 |
| `--collector` | UAC 提权 | 绿色模式的临时采集器；运行时只启动 CollectorHost，响应控制台/会话停止事件 |
| `--service` | 服务账户 | SCM 启动的长期采集服务 |
| `--install` | UAC 提权 | 使用当前稳定副本的绝对路径和固定 `--service` 入口注册服务 |
| `--uninstall` | UAC 提权 | 停止、注销并移除安装副本和机器级配置 |
| `--service-control` | UAC 提权 | 由设置 UI 调用的受限、类型化服务查询代理 |

命令行入口不得替代设置界面。安装路径必须稳定，不能把服务注册到可能被移动或删除的绿色版路径。

运行时装配位于 `app/collector_runtime`，不在 `wWinMain` 中直接拼接 ETW、IPC 或 SCM 对象：
`--collector` 使用当前登录会话的 logon SID 建立 owner-scoped 管道，`--service` 使用
LocalSystem 服务端和本机 Users 客户端 SID 建立服务端点；默认 UI 分支不会构造
`WindowsEtwSession`。服务入口只把同一个 `CollectorRuntime` 交给 `ServiceEntry`，因此
SCM 回调不创建窗口。安装、卸载和查询分别调用固定服务名的 `ServiceControl` 类型化操作，
不接受任意服务名或命令行参数。

## Collector

### ETW Session

- `WindowsEtwSession` 拥有固定名称/GUID 的 QPC 实时 SystemTraceProvider logger，启用 TCP/IP、UDP/IP 和进程生命周期事件；controller、consumer 和处理线程分别由 RAII 管理。
- TCP/UDP 热路径只接受 version 2 的已知 opcode，并在边界检查后从 payload 前缀读取 PID 和字节大小；不使用可能属于记录线程的事件头 PID。
- 进程 Start、End 和 DCStart 只接受 version 2/3/4，通过 TDH 读取 `UniqueProcessKey`、`ProcessId` 和 `ImageFileName`。25H2 build 26200 实测生命周期事件为 version 4；24H2 仍需目标机确认。
- `FlushAndQuery` 发布实际 buffer 数量/尺寸及 `EventsLost`、`LogBuffersLost`、`RealTimeBuffersLost`；任何可见丢失都会降低健康状态，不能通过重启伪装为完整计数。
- 会话不默认抢占同名 logger；部分启动、重复停止、callback/worker 失败和明确 recovery policy 均有测试覆盖。
- 只统计归属、字节和健康元数据，不抓取或保存报文载荷、远端地址、端口、SID 或命令行。

### Process Tracker

- Collector 在采集启动时消费进程 Rundown，随后持续消费启动和结束事件；实际 PID 必须来自 ETW 事件负载，不能把事件头 PID 当作目标进程。
- 每次开始事件分配 tracker-local generation，以 `(pid, generation)` 构成 `ProcessInstanceId`。ETW kernel process key 只用于核对生命周期事件，不单独充当长期应用身份。
- 生命周期采用事件时间的半开区间 `[started_at, ended_at)`；网络事件只匹配覆盖该事件时间的实例，不因当前活动进程 PID 相同而回退猜测。
- 已结束实例默认保留 30 秒且最多 65,536 个；先按水位清理到期项，仍超限则确定性驱逐最早结束项。两者都是内部保护边界，不属于用户配置。
- Win32 探测结果按完整实例 ID 回写。若探测到的创建时间晚于 Start 或 Rundown 观察时间，则把结果判为 PID 已可能复用并拒绝应用。

### Application Identity Resolver

应用身份按以下优先级构造：

1. 非空 AUMID：`package-app:<aumid>`。
2. 没有 AUMID 但有包族：`package-family:<package-family-name>`。
3. 结构规范化后的可执行文件完整路径：`executable:<normalized-path>`。
4. 已验证的 PID 0 Idle 和带 `System` 映像提示的 PID 4：固定系统身份。
5. 其他情况：实例专属 `unresolved:<pid>:<generation>`。

同一稳定 Application ID 的多个 PID 合并。不能仅按文件名合并不同目录中的同名程序，也不能把一个包内不同 AUMID 合并。路径规范化不访问文件系统并保留大小写，可能产生可诊断的 false split，但优先避免 false merge。`svchost.exe` 等系统宿主只显示为宿主项，不虚构到具体服务；无法解析的不同进程实例也不互相合并。

属性探测每次只短暂打开一个不可继承的 `PROCESS_QUERY_LIMITED_INFORMATION` 句柄，并用 RAII 关闭；不启用 `SeDebugPrivilege`、不读取进程内存/token/命令行，也不长期缓存句柄。命令行、SID、远端地址和报文载荷不进入身份模型或普通日志；完整映像路径只留在 Collector 身份数据及未来确有必要且受 ACL 保护的 IPC 字段中。

### Counter Store

- `ApplicationAccumulator` 是纯逻辑的应用合并边界：接收 Collector 已归属到 Application ID 的字节增量，为每个 ID 维护单调递增的上传和下载 `uint64` 计数。
- 同一 ID 的多个进程事件在累计阶段合并；空 ID 和累计溢出被拒绝且不修改状态，元数据冲突保留首个有效值并计入诊断。
- 计数更新与 IPC 快照解耦，避免 UI 刷新频率影响 ETW 消费。
- 累计器输出按 ID 确定性排序的快照；IPC 后续在其外层补充协议版本和采集健康状态。

## IPC

- Windows 命名管道使用显式自描述 DACL、`PIPE_REJECT_REMOTE_CLIENTS` 和 owner-scoped 名称；服务端不创建 UI。
- 帧格式包含魔数、协议版本、消息类型、负载长度和请求 ID，所有长度、方向、版本和序列均在边界处校验。
- 初始握手先完成授权，再协商版本和订阅刷新间隔；服务端的初始订阅确认与后续 Subscribe 确认都必须回显正确 request ID。
- 服务端按目录序列先发完整/增量目录，再发快照；客户端只在完整目录可用后构建 `ipc:<instance-guid>:<handle>` 排行身份。
- UI 客户端使用有限退避和抖动重连；断线、采集器实例变化、版本不兼容或未授权都会清除旧目录/快照，禁止把陈旧数据显示为实时值。
- 服务端回收已结束会话后才创建下一 pipe 实例；达到实例上限属于可恢复 Busy，不会让接收循环永久退出。

## UI 与排行

### Rate History

`RankingEngine` 由每个 UI 客户端独立拥有，只保存最长 10 秒的短期累计快照历史，不启动计时器，也不依赖窗口或 IPC。每次新快照到达时：

1. 找到窗口起点附近的基准快照。
2. 用累计计数差除以真实单调时间差，计算上传和下载速率。
3. 按上传加下载总速率排序。
4. 取 `ranking.top_n`，但继续显示所有正在传输项的正确总速率。
5. 标记总速率和排行区域为脏并请求一次重绘。

时间戳必须严格递增，快照中的 Application ID 必须非空且唯一；非法输入不修改历史。计数下降按应用和方向建立新纪元，只抑制该方向一次，避免回绕或 Collector 重启产生虚假巨速。排序依次使用总速率、下载速率、上传速率和 Application ID；Top N 只截断列表，不截断系统总速率。窗口或 Top N 热更新会复用仍保留的历史，不足新窗口时明确回到预热状态。

窗口长度与刷新间隔不同：窗口决定平滑程度，刷新间隔决定可见数据更新频率。不存在第二个固定 UI 刷新循环。

### Tray Controller

- 创建通知区图标和上下文菜单。
- 左键切换悬浮窗，右键打开菜单，悬停达到延迟后显示排行面板。
- Windows 可能把图标放入通知区溢出区域；应用不能强制常驻显示，应在首次引导中提示用户自行固定。

### Floating Window

- 无边框、可拖动、按显示器 DPI 感知。
- 显示系统总上传和下载速率。
- 根据 DIP 距离吸附当前显示器工作区边缘。
- 自动隐藏时保留可配置的露出条；鼠标进入后恢复。
- 工作区、DPI、显示器或任务栏状态变化时重新约束位置。

### Ranking Popup

- 从托盘图标或悬浮窗锚点打开。
- 先测量目标面板尺寸，再比较当前显示器工作区四个方向的可用空间。
- 优先不遮挡锚点且完整可见的方向；若均不足则选择空间最大方向并钳制到工作区。
- 鼠标从锚点移动到面板时保持打开，离开两者后按消失延迟关闭。

### Settings Window

- 普通权限运行。
- 展示配置页面和实时服务状态。
- 服务动作通过受限的提权管理代理完成，然后查询 SCM 验证结果。
- 详细配置项见 `docs/configuration.md`。

## 服务生命周期

- 默认先以绿色模式运行，避免未经用户同意修改系统。
- 用户选择后台服务后，设置 UI 发起提权安装；Release ZIP 应先解压到用户确认的稳定目录，提权动作再把该副本的绝对路径和固定 `--service` 入口注册到 SCM。当前实现不暗中复制或移动用户文件，安装服务后不得移动该目录。
- UI 登录自启与服务开机启动是两个独立开关。
- 切回绿色模式时先连接临时采集器，确认可用后再停止服务，减少数据中断。
- 卸载服务不会自动删除每用户 UI 设置；界面提供单独的“删除个人设置”。

## 错误处理

- ETW 权限不足：提示切换到提权采集器或安装服务。
- ETW 会话启动失败：显示系统错误码、停止显示实时标志并允许重试。
- 丢失事件：在健康状态中计数，并在超阈值时标记排行可能不完整。
- 进程路径拒绝访问：使用明确的未知/系统宿主身份，不丢弃字节。
- IPC 版本不兼容：拒绝连接并提示 UI/服务版本不一致。
- 配置损坏：保留损坏文件、加载默认值并显示恢复入口。
- 服务操作失败：重新查询 SCM，以真实状态和系统错误码更新界面。

## 测试边界

- 纯逻辑：滑动窗口、累计计数差、排序、Top N、PID 复用和应用合并。
- Collector 集成：TCP/UDP、IPv4/IPv6、短连接、并发进程、事件丢失报告。
- IPC：ACL、协议版本、截断/超长消息、断线和重连。
- UI：混合 DPI、多显示器、工作区改变、贴边隐藏、托盘溢出。
- 服务：安装、启停、重启、自动/延迟启动、模式切换和卸载。
- 性能：Release x64 下测量可执行文件大小、空闲/高流量 CPU、工作集和快照延迟；第一版原型测量前不写死宣传数字。

## 工程质量边界

- 格式由仓库根目录 `.clang-format` 唯一定义。
- Lint 和 Clang Static Analyzer 由 `.clang-tidy` 定义，仅检查第一方代码。
- MSVC 第一方代码使用 `/W4 /WX /permissive- /sdl`，并运行独立 `/analyze` 检查。
- 实现阶段提供 `tools/quality.ps1`，本地和 CI 共享同一检查入口。
- 每个功能先格式化，再通过 Debug 构建、clang-tidy、MSVC `/analyze`、测试和 Release 构建；全量门未通过不得提交。

详细规则见 `docs/code-quality.md`。
