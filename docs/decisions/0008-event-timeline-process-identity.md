# ADR 0008：用事件时间线和公开 Win32 API 构造进程身份

- 状态：已接受
- 日期：2026-08-11

## 背景

ETW 网络事件以 PID 关联流量，但 Windows 会复用 PID。进程可能在 Collector 探测映像路径前退出，路径查询也可能因 DACL 或受保护进程失败。仅按 PID、文件名或一次 `OpenProcess` 结果归属会把旧流量错误记到新进程。

## 决策

`process` 模块使用 ETW 进程开始/结束事件时间线，为每次开始分配 tracker-local generation，并以 `(pid, generation)` 表示进程实例。事件负载中的 kernel process key 用作开始/结束交叉核对；已结束实例短期保留，以事件时间解析迟到网络事件。

进程属性只通过 `PROCESS_QUERY_LIMITED_INFORMATION` 和公开的 `GetProcessTimes`、`GetApplicationUserModelId`、`GetPackageFamilyName`、`QueryFullProcessImageNameW` 获取。创建时间用于拒绝“开始事件之后 PID 已复用”的探测结果。不使用 `ZwQueryInformationProcess` 等内部接口，也不长期保留每个进程的句柄。

应用身份优先使用 AUMID，其次包族，再其次完整映像路径；无法获得可靠身份时生成进程实例专属 unknown ID，避免按文件名误合并。

## 后果

- PID 复用和迟到事件可由纯状态机确定性测试。
- 访问被拒或进程提前退出不会丢弃流量，但会降低到明确的 unknown/host 身份。
- 同一路径的桌面多进程可合并，不同目录中的同名程序不会合并；路径别名可能产生 false split，但不会为了减少条目而冒 false merge 风险。
- 包含多个应用的同一包不会因只使用包族而被错误合并。
- Collector 必须把 ETW 时间规范化后按顺序驱动跟踪器，并记录时间线与缓存诊断。
