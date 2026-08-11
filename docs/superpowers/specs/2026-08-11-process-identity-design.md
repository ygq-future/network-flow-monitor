# 进程生命周期与应用身份设计

- 日期：2026-08-11
- 状态：已实现并通过 Milestone 2 规格审计与质量门
- 适用里程碑：Milestone 2 — Process identity

## 1. 目标

建立一个独立、可测试的 `process` 模块，把未来 ETW Collector 提供的进程开始、结束和网络事件时间线转换为稳定的进程实例与应用身份。模块必须防止 PID 复用把旧进程流量记到新进程，并为同一应用的多进程事件生成相同 `ApplicationId`，供现有 `ApplicationAccumulator` 累计。

本里程碑只实现身份类型、生命周期跟踪、身份策略和 Win32 进程探测。它不启动 ETW、不累计网络字节、不创建 IPC、不读取包清单显示资源，也不实现图标渲染。

## 2. 已核实的 Windows 事实

- PID 只在进程生命周期内有效并会复用；不能把 PID 本身当成长期身份。
- ETW 进程开始事件的事件头 PID 可能属于父进程，实际 PID 必须读取事件负载。进程 V2 负载还提供指向内核进程对象的 `UniqueProcessKey`。
- `ProcessTrace` 按时间顺序向消费者交付事件；事件记录具有时间戳。Collector 后续必须把 ETW 时间转换为本模块定义的统一强类型时间点后再调用跟踪器。
- `GetProcessTimes`、`QueryFullProcessImageNameW`、`GetPackageFamilyName` 和 `GetApplicationUserModelId` 可使用 `PROCESS_QUERY_LIMITED_INFORMATION` 句柄；DACL、受保护进程或进程提前退出仍可能使打开或查询失败。
- 包族与应用不是同一概念。一个包可声明多个应用，AUMID 才是包内的应用身份；包族是不含版本的包身份。
- `ZwQueryInformationProcess` 及其返回结构被 Microsoft 标为内部接口并可能变化，本项目不依赖它。

## 3. 方案比较与决策

### 3.1 采用：事件生命周期区间加受限 Win32 探测

进程跟踪器为每次开始事件分配单调递增的 generation，以 `(pid, generation)` 形成 `ProcessInstanceId`。开始和结束时间形成实例区间；ETW `UniqueProcessKey` 仅用于核对开始/结束是否属于同一实例。Win32 探测独立返回包身份、映像路径和创建时间，无论首版同步调用还是以后改为有界工作队列，结果都只能应用到完全匹配的实例。

此方案在进程已退出、路径被拒绝或探测结果迟到时仍能保留正确的生命周期边界。它只使用公开 API，且纯跟踪逻辑不依赖 Windows 句柄。

### 3.2 未采用：每个活动进程长期保留句柄

句柄可以稳定引用一个进程对象，也可等待退出，但会让服务为所有活动进程持有长期内核句柄。它仍不能解决开始事件到首次 `OpenProcess` 之间已经退出并复用的竞态，对受保护进程也不完整，因此不值得作为主身份模型。

### 3.3 未采用：PID、文件名或内部 NT 序列号

PID 加文件名既不能抵御相同文件名，也不能保证复用后归属正确。内部 NT 查询可能提供序列信息，但接口不受支持。两者均不满足项目的可验证性和兼容性约束。

## 4. 公共时间与身份类型

`process` 定义 `TraceTimePoint`，其 duration 是有符号 64 位、每秒 10,000,000 tick 的 `std::chrono::duration`。它表示 Collector 已把 Windows `FILETIME` epoch 转换到 `system_clock` epoch 的 ETW 时间，不提供隐式整数接口。Win32 `FILETIME` 转换必须通过命名函数完成；Collector 后续不得把启用 raw timestamp 的 QPC 值直接传入此接口。

核心值类型：

```cpp
using TraceDuration =
    std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;

using TraceTimePoint = std::chrono::sys_time<TraceDuration>;

struct ProcessInstanceId {
    std::uint32_t pid;
    std::uint64_t generation;
};
```

generation 只在一个 `ProcessTracker` 实例中有意义；值 0 保留为无效值。计数达到 `uint64_t` 上限时跟踪器拒绝新的开始事件并返回明确错误，不回绕。

现有 `ApplicationId` 与 `ApplicationMetadata` 从 `ranking/counter_types.h` 移到 `process/application_identity.h`。`ranking` 继续把 ID 视作区分大小写的 UTF-8 不透明值，不自行规范化。

## 5. 生命周期状态机

### 5.1 开始事件

`ObserveStart` 接收 PID、事件时间、可选的 64 位 kernel process key、进程映像名提示，以及 `StartKind::Started` 或 `StartKind::Rundown`。

- PID 0 和 PID 4 允许进入预定义系统身份路径；它们直接获得固定身份，不产生 Win32 探测请求。这两个目标 Windows 11 常量集中定义，集成测试必须验证本机 PID 4 的 ETW 映像提示为 `System`；若验证不成立则使用实例专属 unresolved 身份，不能仅凭 PID 虚构系统身份。
- 同一 PID 没有活动实例时创建新 generation。
- 同一 PID 已有活动实例，而 kernel key 与新事件相同且事件时间相同或更晚时，视作重复/补充观察，不创建 generation，只补充空元数据。
- kernel key 不同，或任一事件没有可比较 key 且新开始时间晚于旧开始时间时，把旧实例在新开始时间隐式结束，记录 `implicit_pid_reuse`，再创建新 generation。
- 时间早于现有开始时间的开始观察被拒绝，且不修改状态。

成功创建实例后返回 `ProcessProbeRequest`。请求包含实例 ID、PID、开始时间和开始类型；Win32 适配器不得只凭 PID 回写结果。

### 5.2 结束事件

`ObserveEnd` 先按 PID 查活动实例。若结束事件和实例都含 kernel key，而两者不同，则把它记为 stale end，不结束当前实例。结束时间早于开始时间同样拒绝且不修改状态。有效结束把区间关闭并移入 retired cache。

生命周期区间按 `[started_at, ended_at)` 解释。网络事件恰好落在新实例开始时间时优先匹配新实例；这使 PID 复用边界确定且可测试。

### 5.3 网络事件解析与迟到缓存

`ResolveAt(pid, event_time)` 先选择开始时间不晚于事件且区间包含事件的最新实例。活动和 retired 实例使用同一规则。没有区间匹配时返回 `NotFound`，不猜测当前 PID。

已结束实例默认保留 30 秒 ETW 时间。该值是带单位的内部常量，不进入用户配置或设置 UI；它只保护 ETW 缓冲交付和回调处理造成的迟到事件，不承诺恢复任意长时间的乱序数据。`AdvanceWatermark` 单调推进清理水位；倒退水位被拒绝。清理后到达的旧事件返回 `NotFound` 并增加诊断，不归到当前同 PID 进程。

缓存必须有条目上限 65,536。超限时优先清除已超过保留时间的实例；仍超限则按最早结束时间驱逐并记录诊断，避免异常进程 churn 造成无界内存。

## 6. Win32 探测边界

`WindowsProcessProbe` 每个请求只短暂打开一次 `PROCESS_QUERY_LIMITED_INFORMATION`、不可继承的进程句柄，并使用 RAII 关闭。它不启用 `SeDebugPrivilege`，不读取进程内存，不读取命令行，不打开 token，也不长期缓存句柄。

同一句柄上查询：

1. `GetProcessTimes` 创建时间。
2. `GetApplicationUserModelId`。
3. `GetPackageFamilyName`。
4. `QueryFullProcessImageNameW` 的 Win32 路径。

`APPMODEL_ERROR_NO_APPLICATION` 和 `APPMODEL_ERROR_NO_PACKAGE` 是正常缺失状态。其他失败保留 Win32 错误码与查询阶段，但不得把未初始化缓冲区当结果。

如果查询到的创建时间晚于 ETW Start 或 Rundown 观察时间，则说明 PID 在探测前可能已复用，返回 `PidReusedBeforeProbe`，整个身份结果不应用。Rundown 进程正常情况下创建时间早于 rundown 观察时间；这不是跳过复用检查的理由。探测回写时跟踪器还必须验证 `(pid, generation)`；实例已被清理时返回 stale result。

所有双调用字符串 API 都设明确上限：AUMID 使用 SDK 常量，包族使用 SDK 常量，映像路径最多 32,768 个 UTF-16 code unit。越界或无效 UTF-16 返回明确错误。UTF-8 转换使用 `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ...)`。

## 7. 应用身份策略

按以下顺序生成稳定前缀化 ID：

1. AUMID 成功且非空：`package-app:<aumid>`。
2. 无 AUMID 但包族成功：`package-family:<package-family-name>`。
3. 映像路径成功：`executable:<normalized-path>`。
4. PID 0：`system:idle`；PID 4：`system:kernel`。
5. 其他无法解析实例：`unresolved:<pid>:<generation>`。

不能只按包族处理已有 AUMID 的进程，因为一个包可以包含多个应用。不能只按文件名处理桌面应用。unresolved 身份按进程实例隔离，宁可少合并，也不能把无证据的同名进程合并。

路径规范化只做不会访问文件系统的结构转换：接受绝对 DOS、UNC 与扩展长度路径；把 `/` 转成 `\`；把 `\\?\UNC\` 转成普通 UNC，把 `\\?\C:\` 转成 DOS 形式；删除根路径以外的结尾分隔符；拒绝相对路径、NUL 和空结果。保留 Windows 返回的字符大小写，不解析硬链接、符号链接或文件 ID。相同文件若因别名产生两个 ID属于可诊断的 false split；本里程碑明确优先避免 false merge。

显示元数据是非身份信息：优先使用映像名提示，其次取规范化路径 basename，再使用包族/AUMID 的稳定字符串。`icon_key` 仅对已解析映像路径设置为该路径；本里程碑不加载图标或包清单资源。

同一 `ApplicationId` 的多个 `ProcessInstanceId` 交给现有 `ApplicationAccumulator` 后自然合并。系统宿主如 `svchost.exe` 只显示为宿主项，不尝试把流量虚构到具体 Windows 服务。

## 8. 错误、诊断与隐私

公开结果区分：无错误、无匹配实例、重复观察、时间倒退、generation 耗尽、探测访问被拒、进程已退出、PID 在探测前复用、字符串越界/编码错误和其他 Win32 失败。

跟踪器诊断至少包含：隐式 PID 复用、stale end、stale probe result、无匹配网络事件、retired cache 到期和容量驱逐。计数采用饱和增长。

命令行、远端地址、报文载荷和 SID 不属于模型。完整映像路径只存在于 Collector 内存中的身份数据和未来受 ACL 保护的必要 IPC 字段；不得写入普通日志。错误日志只记录阶段、PID、generation 和错误码。

## 9. 并发与所有权

`ProcessTracker` 是单线程纯状态机，由未来 Collector 的 ETW 消费线程拥有；其 API 不自行加锁。Win32 探测可在同一线程同步执行，首版先采用同步探测以保证顺序和简化生命周期；若实测显示回调阻塞不可接受，后续可增加有界工作队列，但回写仍使用实例 ID 校验。

所有容器拥有值对象。Windows 句柄使用明确 RAII 类型。异常不得穿过 ETW 回调或线程边界；公共操作返回状态值，分配失败在未来 Collector 边界转换为健康错误并停止采集，而不是继续不完整归属。

## 10. 测试与完成条件

纯单元测试至少覆盖：

- 开始、结束、重复开始、显式和隐式 PID 复用。
- stale end 的 kernel key 不结束新实例。
- 旧/新实例在复用边界前后的事件时间匹配。
- 结束后迟到事件、TTL 到期、容量驱逐和水位倒退。
- stale probe result 不覆盖复用后的实例。
- AUMID、包族、路径、系统和 unresolved 五级身份优先级。
- 同路径多进程相同 ID、同文件名不同路径不同 ID、不同 AUMID 不合并。
- 扩展 DOS/UNC 路径转换、相对/空/含 NUL 路径拒绝。
- generation 与所有诊断计数不回绕。

Win32 集成测试至少覆盖当前测试进程：受限权限打开、创建时间、映像路径和 UTF-8 转换；再启动一个受控短命子进程验证进程提前退出返回可分类结果。测试不得假定开发机进程具有包身份，也不得要求访问受保护系统进程。

完成条件：所有第一方代码通过仓库格式检查、严格 MSVC Debug、clang-tidy/Clang Static Analyzer、MSVC `/analyze`、全部 Debug/Release CTest 和 Release x64 构建；架构、ADR、规划与进度文档同步；没有新增运行时 DLL 依赖或未经批准的第三方库。
