# ADR 0010：使用所有者限定的版本化命名管道 IPC

- 状态：已接受
- 日期：2026-08-11

## 背景

普通权限 UI 需要从绿色提权 Collector 或 LocalSystem 服务接收累计应用计数与健康状态。ETW 数据覆盖整机，而当前隐私模型有意不采集进程用户 SID，因此 Collector 无法可靠生成按 Windows 用户隔离的流量视图。默认命名管道安全描述符还会给 Everyone 和匿名身份读权限；服务创建的命名管道若不额外限制，也可能接受远程连接。

协议需要同时满足小体积、多个 UI 客户端、100–5000ms 订阅刷新、版本不兼容诊断、恶意长度拒绝、慢客户端隔离和可取消关闭。它不能直接序列化 C++ 对象布局，也不能让每个客户端独立触发 ETW flush。

## 决策

采用本地、双向、byte-mode Windows 命名管道和显式小端二进制帧协议：

- 绿色模式的 DACL 只允许 LocalSystem、创建 server 的 logon SID 和当前 UI logon SID；服务模式只允许 LocalSystem 和安装服务的用户 SID。不给 Everyone、Anonymous、Builtin Administrators group 或其他本地用户数据访问权。同一登录会话内的进程属于同一信任边界。
- 管道使用 `PIPE_REJECT_REMOTE_CLIENTS`，首个实例使用 `FILE_FLAG_FIRST_PIPE_INSTANCE`。客户端只请求具体读写数据权限，并以 `SECURITY_IDENTIFICATION` 连接；服务端在读取 Hello 后短暂模拟客户端、查询 SID、严格比对预期主体并总是恢复自身令牌。
- 每帧使用固定 24-byte header，包含 magic、header/version、message type、flags、payload length 和 request ID。单帧 payload 最多 1 MiB；所有整数显式按 little-endian 编解码，未知版本、类型、flag、越界长度或非法 UTF-8 均拒绝。
- Collector 每次启动生成 instance GUID；每个应用在该实例内分配不复用的 `uint64` handle。快照只重复发送 handle 与累计字节，显示名和 icon key 通过可分块 catalog 发送，完整内部 Application ID 不跨 IPC。
- 最多四个客户端。一个集中 publisher 使用所有订阅中的最短刷新间隔执行一次 flush/snapshot，各客户端按自己的到期时间获得数据。每个客户端只保留一个尚未开始发送的最新快照；写入超过固定安全时限即取消并断开该慢客户端。
- 客户端使用有上限退避重连；未授权和协议不兼容是终止状态。Collector instance 变化会清空 UI 的 catalog 与排行历史。
- 在 IPC 之前补齐 Collector 活跃应用状态的有界性：应用在最大排行窗口 10 秒加最大刷新间隔 5 秒后可过期，最多保留 4096 个同时活跃应用。容量耗尽时新身份归入明确的 `collector:other` 桶并把健康状态降级，不能静默丢字节。

完整协议与错误行为见 `docs/superpowers/specs/2026-08-11-secure-ipc-design.md`。

## 后果

- UI 和高权限 Collector 的权限边界由内核 DACL、禁止远程、主体复核和协议校验共同保护，不依赖管道名称保密。
- 单次采样可服务多个 UI，慢客户端不会阻塞 ETW worker 或其他客户端。
- 数字 application handle 和 catalog 降低重复路径数据量；Collector 重启通过 instance GUID 明确形成计数新纪元。
- 每客户端一个有界 session worker 比 IOCP 多少量线程，但最多四个客户端，状态机和可测试性明显更简单。
- 服务模式只服务安装者账户。若未来要支持多个 Windows 用户，必须新增 ADR，并先解决整机 ETW 数据无法按用户可靠过滤的隐私问题。
- 4096 活跃应用是内存安全边界而非性能承诺；触发时必须可见降级，后续可以依据实测调整。

## 未采用方案

- **默认命名管道安全描述符：**会扩大到 Everyone/Anonymous，不满足项目安全约束。
- **允许所有本地交互用户：**任何获准用户都会看到整机应用流量，而 Collector 没有可验证的按用户过滤数据。
- **message-mode + IOCP：**扩展性更高，但对最多四个本地客户端增加过多完成端口、partial-message 和关闭状态复杂度。
- **同步 I/O：**慢读写可能无限阻塞，可靠停止需要额外线程取消边界。
- **loopback TCP：**增加端口、认证、防火墙和服务暴露面，没有本机 IPC 收益。
- **直接发送 C++ struct 或 JSON：**前者存在 ABI/padding 风险，后者增加体积与解析成本；两者都不如固定有界 codec 易于做恶意输入测试。
