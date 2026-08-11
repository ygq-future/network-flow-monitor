# 配置模型与排行核心设计规格

- 日期：2026-08-10
- 状态：已批准
- 对应里程碑：Milestone 1

## 1. 目标

建立两个不依赖窗口、ETW、IPC 或管理员权限的 C++20 模块：

1. `config` 提供完整、带版本、可校验和可持久化的 `AppConfig`。
2. `ranking` 从带单调时间戳的累计应用计数快照计算总速率和确定性的 Top N。

这两个模块是后续 IPC、设置界面和 Collector 的稳定数据边界，必须能在普通 CTest 进程中完整单元测试。

## 2. 非目标

- 本里程碑不启动 ETW、不解析进程身份、不创建命名管道或窗口。
- 不实现服务安装、UAC 或配置界面。
- 不保存长期流量历史。
- 不为尚未发布的旧配置格式虚构迁移。首个公开 schema 为版本 1；以后增加版本时必须在迁移表中显式添加逐版本转换。
- 不引入通用反射、序列化框架或第二个 JSON 库。

## 3. 依赖边界

按 ADR 0007 静态集成 yyjson 0.12.0。第三方文件位于 `third_party/yyjson/`，构建目标与第一方严格诊断隔离。yyjson 只出现在 `config` 的 `.cpp` 持久化实现中；公共头文件只暴露项目自己的值类型和结果类型。

配置读取使用严格 RFC 8259 模式。配置文件必须是 UTF-8，不接受注释、尾随逗号、JSON5、`NaN`、无穷值、重复键或扩展数字。yyjson 本身允许重复对象键，因此适配层必须显式检测并拒绝重复键，避免同一配置出现两种解释。

## 4. `AppConfig` 模型

`AppConfig` 使用嵌套值类型按设置页面分组，当前 `schema_version` 固定为 1：

- `RankingConfig`：`top_n`、`std::chrono::milliseconds window`。
- `DisplayConfig`：`std::chrono::milliseconds refresh_interval`、`SpeedUnit`、上传/下载显示开关。
- `InteractionConfig`：悬停和消失延迟。
- `FloatingConfig`：DIP 吸附距离、自动隐藏、自动隐藏延迟、露出宽度和透明度百分比。
- `StartupConfig`：UI 登录启动开关。
- `CollectorConfig`：`CollectorMode`。
- `ServiceConfig`：`ServiceStartType`。
- `DiagnosticsConfig`：`LogLevel`。

时间跨模块只使用 `std::chrono::milliseconds`。DIP 值使用有符号 32 位整数，百分比使用无符号 8 位整数，Top N 使用无符号 8 位整数；解析前先验证 JSON 数值可无损表示，禁止窄化后再校验。

默认值和合法范围以 `docs/configuration.md` 为唯一产品定义。实现把范围集中成命名常量；其他模块不得复制数值。`DefaultAppConfig()` 返回完整默认模型。

## 5. 配置校验与加载

### 5.1 校验

`ValidateAppConfig()` 对内存模型执行同一套范围校验。排行窗口和刷新间隔各自遵守公开范围，但不强制窗口大于刷新间隔；若刷新比窗口慢，引擎仍按两个快照的真实时间差计算，只是可见时间分辨率受刷新间隔限制。非法值不自动钳制，避免用户保存的值与实际生效值不同。

校验结果包含稳定的字段路径和错误码，面向 UI 的本地化文本以后由 UI 映射。配置模块内部不生成 MessageBox 文案。

### 5.2 JSON 语义

- 根必须是对象且必须包含整数 `schema_version`。
- 编解码调用必须显式指定 `ConfigDocumentKind::User` 或 `ConfigDocumentKind::Service`。用户文档只允许 `ranking`、`display`、`interaction`、`floating`、`startup`、`collector` 和 `diagnostics`；服务文档只允许 `service` 和 `diagnostics`。不得把整个复合模型无差别写进两个位置。
- 版本 1 中各文档的分组和键使用 `docs/configuration.md` 的点路径展开为嵌套对象。
- 缺失的普通字段使用版本 1 默认值，使新增可选字段可以向后补齐；加载状态记录 `loaded_with_defaults`。
- 类型错误、越界、重复键或未知键视为损坏，不静默忽略拼写错误。
- `schema_version > 1` 返回 `future_version`，保留原文件且不尝试降级解释。
- `schema_version < 1` 返回 `unsupported_legacy_version`。当前没有已发布旧版本，因此不伪造迁移；未来 schema 2 必须新增 1 -> 2 的独立转换和测试。

### 5.3 加载结果

加载返回一个带状态的值对象，至少区分：`loaded`、`loaded_with_defaults`、`missing`、`damaged`、`future_version`、`unsupported_legacy_version` 和 `io_error`。除 `loaded`/`loaded_with_defaults` 外，结果携带安全默认 `AppConfig` 和可诊断错误，但不修改来源文件。

“保留损坏文件”指加载路径不覆盖、不重命名也不删除原文件。只有用户随后明确保存或恢复默认值时才写入新内容；UI 可根据结果提供打开位置和恢复入口。

## 6. 路径选择与持久化

路径策略与 JSON 编解码分离：

- 绿色模式候选路径是 EXE 同目录 `NetworkFlowMonitor.json`。
- 安装模式每用户路径是 `%LocalAppData%\NetworkFlowMonitor\config.json`。
- 机器级服务路径是 `%ProgramData%\NetworkFlowMonitor\service.json`，本里程碑只提供路径值，不执行提权写入。

绿色目录不可写时返回明确错误，不自动切换到 LocalAppData。后续 UI 必须让用户明确选择新位置。

加载和保存 API 都必须接收文档类型；进程只读取与自身作用域对应的字段，未属于该文档的内存字段保持默认值。保存先在目标目录创建唯一临时文件，写入完整 UTF-8 JSON，刷新文件缓冲，然后使用 Windows 原子替换语义更新目标。目标已存在时优先 `ReplaceFileW`；首次创建使用带写穿标志的 `MoveFileExW`。失败时保留原目标，清理本次临时文件，并返回 Win32 错误码。序列化键顺序固定，输出带两个空格缩进和结尾换行，便于诊断与差异审查。

## 7. 应用累计与排行输入模型

`ApplicationAccumulator` 接收已经解析出 `ApplicationId` 的上传/下载字节增量，并为每个应用维护单调递增的 `std::uint64_t` 累计值。来自多个进程但具有相同 `ApplicationId` 的事件写入同一个应用槽，因此多进程合并发生在累计阶段，而不是对可能随进程退出而下降的进程快照做临时求和。

单次增量导致累计溢出时返回明确错误且不修改该应用原值。显示元数据优先采用非空值；同一应用出现冲突元数据时稳定保留已有有效值并增加诊断计数。后续 Collector 使用此纯逻辑类型，但 PID 生命周期和 `ApplicationId` 的解析仍属于 `process`/`collector` 里程碑。

`CounterSnapshot` 包含：

- `std::chrono::steady_clock::time_point captured_at`。
- 零个或多个 `ApplicationCounters`：稳定的 `ApplicationId`、显示元数据、累计上传和下载 `std::uint64_t`。

`ApplicationId` 在本里程碑中是区分大小写的 UTF-8 不透明值。后续 `process` 模块负责规范化路径或包身份；`ranking` 不猜测两个字符串是否属于同一应用。

`ApplicationAccumulator` 生成的快照对每个 `ApplicationId` 最多包含一个条目。排行引擎也验证此不变量；外部构造的快照若含重复 ID，则拒绝整个快照且不修改历史，避免重复累计值产生双重计数。

快照时间必须严格递增。重复或倒退时间戳被拒绝，不修改已有历史。

## 8. 窗口和速率算法

`RankingEngine` 保存覆盖配置窗口所需的短期历史，并保留窗口边界前的一个快照。收到新快照时：

1. 验证快照内应用 ID 唯一且累计值类型有效。
2. 若从首个有效快照到当前的时间小于 `ranking.window`，返回 `warming_up`，不把不完整窗口伪装成稳定排行。
3. 选择时间戳不晚于 `current - window` 的最新快照作为全局基线；用真实时间差而非配置毫秒数做除数。
4. 对每个当前应用，从基线或其首次观测快照计算累计差。首次出现且没有两个观测点的应用暂不产生速率。
5. 若当前计数小于基线计数，将其视为 Collector 重启、计数复位或 `uint64` 回绕造成的不连续；该方向本次不产生巨幅速率，并记录按应用、按方向的新纪元基线。后续快照从这个新基线按真实时间差恢复计算，不会反复与旧纪元的高计数比较。
6. 使用 `double` 表示每秒字节数。上传与下载分别计算，总速率使用两者相加。
7. 系统总速率包含所有具有有效差值的当前应用；Top N 只截断展示列表，不影响总数。
8. 零总速率应用不进入 Top N。排序依次按总速率降序、下载速率降序、上传速率降序、`ApplicationId` 升序，确保测试和 UI 不因哈希遍历顺序抖动。

配置窗口或 Top N 改变时不清空可复用历史；引擎按新的窗口裁剪。如果新窗口大于现有历史，则重新进入 `warming_up`。显示刷新间隔不属于排行引擎定时器，只决定上游快照到达频率。

## 9. 输出模型与错误处理

每次接受快照后输出 `RankingSnapshot`：

- 状态：`warming_up` 或 `ready`。
- 实际基线到当前的时间跨度。
- 总上传、下载和合计字节每秒速率。
- 最多 `top_n` 个 `RankedApplication`。
- 非致命诊断：元数据冲突数、计数不连续数和被忽略的单点应用数。

无效配置、非递增时间和重复应用 ID 返回明确错误且保持引擎原状态，提供强失败语义。普通的应用计数复位是数据诊断，不导致整个快照失败。应用累计器自身的 `uint64` 溢出同样返回错误并保持对应槽位原值。

## 10. 测试范围

配置单元测试至少覆盖：完整默认值、每个边界的最小/最大与越界、窗口小于刷新间隔、缺失字段补默认、错误类型、未知/重复键、损坏 JSON、UTF-8 错误、schema 0、schema 1、未来 schema、`uint64`/窄化边界、稳定序列化、写入失败和原文件保留。

排行单元测试至少覆盖：窗口预热、真实时间差、不规则快照间隔、Top N、确定性并列排序、上传/下载分离、零速率过滤、同应用多进程事件累计、累计溢出、重复快照 ID 拒绝、应用首次出现/消失、计数下降/回绕后的新纪元、时间戳重复/倒退、窗口增减和总速率不受 Top N 截断。

第三方 yyjson 不进入第一方 clang-tidy 或 `/analyze`；项目适配层和所有公共逻辑仍必须通过格式检查、MSVC `/W4 /WX /permissive- /sdl`、clang-tidy/Clang Static Analyzer、MSVC `/analyze`、Debug/Release 测试和 Release x64 构建。

## 11. 完成条件

- ADR 0007、第三方来源/许可记录和固定版本一致。
- 公共接口不包含 yyjson 或 Win32 UI 类型。
- 配置默认值、范围和 JSON 键与 `docs/configuration.md` 一致。
- 损坏、旧版和未来版配置不会被静默覆盖。
- 排行在预热、计数不连续和时间异常时不会显示虚假的极高速率。
- 多进程的同应用事件累计后只产生一个应用快照和排行项。
- 全量质量门通过，结果记录到 `docs/planning/progress.md`。
- Release 体积测量记录 yyjson 引入前后数据，但本里程碑不预设未经测量的尺寸阈值。
