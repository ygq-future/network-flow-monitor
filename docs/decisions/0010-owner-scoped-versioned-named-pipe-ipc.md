# ADR 0010：使用登录会话限定的 Protocol V1 命名管道

- 状态：已接受
- 日期：2026-08-11
- 修订：2026-08-28

## 决策

Collector 与 native host 使用本地、双向、byte-mode Windows 命名管道。端点只服务启动 Collector 的当前登录会话：DACL 明确授予 LocalSystem、server logon SID 和相同的 authorized logon SID，不授予 Everyone、Anonymous 或其他登录会话；管道拒绝远程客户端，连接后再次模拟并核验客户端 token group 中的 logon SID。

Protocol V1 固定为：

- 12-byte little-endian header：magic `NFM1`、`uint16` version、`uint16` message type、`uint32` payload length；
- payload 上限 256 KiB，消息正文为严格校验的 UTF-8 JSON；
- 消息集合为 `Hello`、`HelloAck`、`TrafficTick`、`RankingSnapshot`、`CollectorStatus`、`ShutdownRequest`；
- 未知版本、未知类型、非法 UTF-8、字段类型错误和越界长度立即拒绝；
- Collector 统一采样并发布，客户端不得各自驱动 ETW flush。

React 不直接访问命名管道。native host 将已验证、已归一化的消息投影到封闭 Native/Web bridge；完整 bridge 合同见 `.scratch/cpp-webview2-react-ui/spec.md`。

## 后果

- 会话隔离、内核 ACL、禁止远程和连接后身份复核共同形成 IPC 安全边界。
- 协议布局与 C++ ABI 解耦，并由 codec、server/client 和集成测试共同约束。
- 修改 header、消息集合、payload 上限、身份边界或序列化格式必须新增 ADR 并同步跨语言 fixture。
