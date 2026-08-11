# 配置模型

## 三个容易混淆的概念

网络事件由 ETW 事件驱动地持续进入采集器，不存在用户可见的“每隔多久抓一次包”。用户需要理解并配置的是：

1. **排行窗口**：计算速率时回看多长时间。例如 2000ms 表示用最近两秒的字节增量计算速率。
2. **数据显示刷新间隔**：UI 订阅的采集器多久发送一次累计计数快照，UI 就多久更新一次总速率和排行。例如 500ms。断线重连后会重新协商该间隔。
3. **悬停延迟**：鼠标停留多久才打开排行面板。例如 350ms。

产品中不再提供独立“UI 刷新率”。收到新快照时 UI 标记数据区域为脏并重绘；配置、DPI、主题或交互状态变化也会触发重绘。空闲时不持续高帧率刷新。

## 首版公开配置

| 配置键 | 默认值 | 合法范围/选项 | 作用域 |
|---|---:|---|---|
| `ranking.top_n` | 5 | 1–20 | 每用户 |
| `ranking.window_ms` | 2000 | 500–10000ms | 每用户 |
| `display.refresh_interval_ms` | 500 | 100–5000ms | 每用户/IPC 订阅 |
| `display.speed_unit` | `auto` | `auto`, `bytes`, `bits` | 每用户 |
| `display.show_upload` | `true` | 布尔值 | 每用户 |
| `display.show_download` | `true` | 布尔值 | 每用户 |
| `interaction.hover_delay_ms` | 350 | 0–2000ms | 每用户 |
| `interaction.dismiss_delay_ms` | 250 | 0–2000ms | 每用户 |
| `floating.snap_distance_dip` | 12 | 0–64 DIP | 每用户 |
| `floating.auto_hide_enabled` | `true` | 布尔值 | 每用户 |
| `floating.auto_hide_delay_ms` | 500 | 0–5000ms | 每用户 |
| `floating.reveal_strip_dip` | 3 | 1–16 DIP | 每用户 |
| `floating.opacity_percent` | 92 | 30–100 | 每用户 |
| `startup.ui_at_login` | `false` | 布尔值 | 每用户 |
| `collector.preferred_mode` | `portable` | `portable`, `service` | 每用户 |
| `service.start_type` | `automatic_delayed` | `manual`, `automatic`, `automatic_delayed` | 机器 |
| `diagnostics.log_level` | `info` | `off`, `error`, `info`, `debug` | 每用户/机器 |

合法范围用于保护 CPU、IPC 消息量和交互可用性，并非承诺所有极端组合具有相同性能。设置 UI 应显示单位和简短影响说明。

## 设置界面

### 显示与排行

- Top N。
- 排行窗口。
- 数据显示刷新间隔。
- 速率单位和上下行显示开关。

### 悬浮窗与交互

- 悬停延迟和面板消失延迟。
- 贴边距离、自动隐藏、自动隐藏延迟和保留露出宽度。
- 透明度、字体和颜色。字体与颜色可后续扩展，但必须进入统一配置模型。

### 采集与服务

- 当前采集模式、连接状态、服务安装状态和 SCM 状态。
- “使用绿色采集器”与“使用后台服务”模式切换。
- 安装服务、启动、停止、重启、卸载。
- 服务启动类型。
- 错误详情和重新连接。

需要修改机器状态的按钮触发一次明确的 UAC 操作。界面在操作完成后重新查询真实 SCM 状态，不凭本地按钮状态推断成功。

### 常规与诊断

- UI 登录自启。
- 日志级别、打开日志目录和导出无敏感载荷的诊断摘要。
- 恢复默认设置。

## 持久化

- 绿色模式：配置默认写在可执行文件旁的 `NetworkFlowMonitor.json`。若目录不可写，UI 必须告知用户并允许明确选择 `%LocalAppData%`，不能静默改变“绿色”语义。
- 安装模式：每用户设置位于 `%LocalAppData%\NetworkFlowMonitor\config.json`。
- 机器级服务设置位于 `%ProgramData%\NetworkFlowMonitor\service.json`，通过提权操作修改。
- 路径策略只组合调用方提供的基础目录，不探测可写性、不创建父目录，也不静默切换到其他位置；这些交互和目录准备由上层应用负责。
- 所有文件包含 `schema_version`。当前只支持 schema 1；schema 0 返回“旧版本暂不支持”，未来版本返回“不兼容”。两者都保留原文件。后续加入迁移时必须使用显式、可测试的版本步骤。
- JSON 严格遵循 RFC 8259 和 UTF-8。未知键、重复键、错误类型、无效枚举、越界数值及损坏 JSON 都会被拒绝并返回可诊断错误，不会触发写回。
- schema 1 中缺少普通字段时使用该字段的安全默认值，并将加载状态标记为“使用了默认值”；用户文档与机器服务文档只接受各自允许的配置组。
- 单个配置文件的内部读取上限为 1 MiB；这不是用户可配置项。
- 保存时先在目标目录创建独占临时文件，完整写入并调用 `FlushFileBuffers`，关闭句柄后再发布：已有目标使用 `ReplaceFileW`，首次保存使用带 `MOVEFILE_WRITE_THROUGH` 的 `MoveFileExW`。发布失败时保留原文件，仅清理本次临时文件。

## 内部常量

ETW 缓冲区尺寸、IPC 最大消息长度、安全协议版本等不是普通用户配置。它们应集中定义、测试并记录，但不进入设置页面，避免把实现细节暴露为不安全选项。
