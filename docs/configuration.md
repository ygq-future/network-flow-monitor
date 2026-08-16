# 配置模型

## 三个容易混淆的概念

网络事件由 ETW 事件驱动地持续进入采集器，不存在用户可见的“每隔多久抓一次包”。用户需要理解并配置的是：

1. **排行窗口**：计算速率时回看多长时间。例如 2000ms 表示用最近两秒的字节增量计算速率。
2. **数据显示刷新间隔**：UI 订阅的采集器多久发送一次累计计数快照，UI 就多久更新一次总速率和排行。例如 500ms。断线重连后会重新协商该间隔。
3. **悬停延迟**：鼠标停留多久才打开 TopA 面板。例如 350ms。

产品中不再提供独立“UI 刷新率”。收到新快照时 UI 标记数据区域为脏并重绘；配置、DPI、主题或交互状态变化也会触发重绘。空闲时不持续高帧率刷新。

## 首版公开配置

| 配置键 | 默认值 | 合法范围/选项 | 作用域 |
|---|---:|---|---|
| `ranking.top_n` | 5 | 1–20 | 程序 |
| `ranking.window_ms` | 2000 | 500–10000ms | 程序 |
| `display.refresh_interval_ms` | 500 | 100–5000ms | 程序/IPC 订阅 |
| `display.speed_unit` | `auto` | `auto`, `bytes`, `bits` | 程序 |
| `display.show_upload` | `true` | 布尔值 | 程序 |
| `display.show_download` | `true` | 布尔值 | 程序 |
| `appearance.language` | `english` | `system`, `english`, `simplified_chinese` | 程序 |
| `appearance.theme` | `system` | `system`, `light`, `dark` | 程序 |
| `interaction.hover_delay_ms` | 350 | 0–2000ms | 程序 |
| `interaction.dismiss_delay_ms` | 250 | 0–2000ms | 程序 |
| `floating.auto_hide_enabled` | `true` | 布尔值 | 程序 |
| `floating.auto_hide_delay_ms` | 500 | 0–5000ms | 程序 |
| `floating.nfm_pinned` | `false` | 布尔值 | 程序 |
| `floating.capsule_width_dip` | 10 | 8–24 DIP | 程序 |
| `floating.capsule_height_dip` | 64 | 40–160 DIP | 程序 |
| `floating.opacity_percent` | 92 | 0–100 | 程序 |
| `floating.blur_strength_percent` | 60 | 0–100（映射到 0–30 DIP Gaussian 标准差） | 程序 |
| `floating.yield_topmost_when_fullscreen` | `true` | 布尔值 | 程序 |
| `startup.ui_at_login` | `false` | 布尔值 | 程序 |
| `collector.preferred_mode` | `portable` | `portable`, `service` | 程序 |
| `service.start_type` | `automatic_delayed` | `manual`, `automatic`, `automatic_delayed` | 程序 |
| `diagnostics.log_level` | `info` | `off`, `error`, `info`, `debug` | 程序 |

合法范围用于保护 CPU、IPC 消息量和交互可用性，并非承诺所有极端组合具有相同性能。设置 UI 应显示单位和简短影响说明。

## 设置界面

### 显示与排行

- Top N。
- 排行窗口。
- 数据显示刷新间隔。
- 速率单位和上下行显示开关。

### 监控面板与交互

- 悬停延迟和面板消失延迟。
- 仅吸附工作区左右边;拖动结束总是吸附到鼠标松开点所在显示器的最近一侧,不提供贴边距离。
- 自动隐藏、自动隐藏延迟、固定显示,以及胶囊宽度和高度。胶囊可见宽度无论多窄,向屏幕内侧的实际指针命中宽度都不得小于 16 DIP。
- 监控面板背景透明度与毛玻璃模糊强度。两者相互独立，透明度不作用于文字和图标；Composition 不可用时使用可读的同色降级背景。
- “让全屏应用显示在监控面板之上”。启用后，当前台外部窗口覆盖整块显示器时，监控面板从 topmost 降为普通层级；离开全屏后恢复。判定不依赖游戏进程名，因此也覆盖 Windows 截图遮罩。

### 外观与语言

- 语言提供跟随系统、English 和简体中文;默认 English,系统语言不受支持时回退 English。
- 主题提供跟随系统、浅色和深色;默认跟随系统。系统主题变更实时生效,高对比度覆盖应用材质和配色。
- 单一监控面板使用原生 Windows.UI.Composition HostBackdrop 毛玻璃、可调模糊和背景透明度；设置窗口使用 Mica。所有应用自有面板使用圆角，右键菜单继续使用 Windows 原生菜单。
- 动画只在有界状态转换期间运行并响应系统减少动画设置;空闲时不运行固定高帧率循环。

### 采集与服务

- 当前采集模式、连接状态、服务安装状态和 SCM 状态。
- “使用绿色采集器”与“使用后台服务”模式切换。
- 安装服务、启动、停止、重启、卸载。
- 服务启动类型。
- 错误详情和重新连接。

绿色采集器由 UI 在明确的 UAC 边界后启动 `--collector --stop-event <内部事件名>`；采集进程不会通过固定高帧率循环
刷新 UI，而是由 IPC 订阅的数据显示刷新间隔驱动快照。采集进程收到控制台关闭、注销或关机
控制事件时请求有序停止；设置 UI 管理 UI 侧的进程句柄，并在切回绿色模式后确认连接成功再
停止旧的后台服务。`--stop-event` 是 UI 与临时采集器之间的内部参数，不作为用户配置项开放。

需要修改机器状态的按钮触发一次明确的 UAC 操作。界面在操作完成后重新查询真实 SCM 状态，不凭本地按钮状态推断成功。

### 常规与诊断

- UI 登录自启；保存时写入或移除当前用户 `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run` 中的 `NetworkFlowMonitor` 项，不需要管理员权限。
- 日志级别、打开日志目录和导出无敏感载荷的诊断摘要。
- 恢复默认设置。

## 持久化

- 程序只有一份配置,固定写在可执行文件旁的 `NetworkFlowMonitor.json`;进程当前工作目录不参与路径计算。便携采集器与服务采集器只是后端运行形态,切换采集模式不改变 UI、配置作用域或配置路径。
- 不使用 `%LocalAppData%` 或 `%ProgramData%` 作为静默回退。可执行文件目录不可写时,UI 必须报告可诊断的保存错误。
- 这项单文件便携语义意味着从共享目录运行的多个 Windows 用户会读写同一配置,安装到普通用户不可写目录也无法保存;首版接受这些明确限制。
- 路径策略只根据可执行文件路径计算相邻配置路径,不探测可写性、不创建父目录,也不静默切换到其他位置。
- 所有配置包含 `schema_version`。产品尚未发布期间不维护旧开发 schema 的迁移链;识别到旧开发 schema 时使用当前安全默认值并以当前 schema 直接替换。损坏、截断或非法配置仍保留原文件和可诊断错误,不得静默覆盖。
- JSON 严格遵循 RFC 8259 和 UTF-8。未知键、重复键、错误类型、无效枚举、越界数值及损坏 JSON 都会被拒绝并返回可诊断错误，不会触发写回。
- 当前 schema 中缺少普通字段时使用该字段的安全默认值,并将加载状态标记为“使用了默认值”。
- 单个配置文件的内部读取上限为 1 MiB；这不是用户可配置项。
- 保存时先在目标目录创建独占临时文件，完整写入并调用 `FlushFileBuffers`，关闭句柄后再发布：已有目标使用 `ReplaceFileW`，首次保存使用带 `MOVEFILE_WRITE_THROUGH` 的 `MoveFileExW`。发布失败时保留原文件，仅清理本次临时文件。

## 内部常量

ETW 缓冲区尺寸、IPC 最大消息长度、安全协议版本等不是普通用户配置。它们应集中定义、测试并记录，但不进入设置页面，避免把实现细节暴露为不安全选项。
