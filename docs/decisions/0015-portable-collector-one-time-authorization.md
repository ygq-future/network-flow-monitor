# ADR 0015：portable collector 支持一次性 ETW 授权

- 状态：已接受
- 日期：2026-08-13
- 修订：2026-08-28

## 决策

Collector 启动 system logger 所需的 `SeSystemProfilePrivilege` 通过一次性授权获得：管理员动作把当前用户加入 `Performance Log Users`，并通过 `EventAccessControl` 为会话 GUID 与 SystemTraceProvider GUID 增加该用户访问权。授权完成后 Collector 以普通权限运行；未授权时 native host 可明确请求一次 UAC 启动或引导用户完成授权。

授权查询、执行、撤销与错误信息属于 native host 的封闭系统能力，React 只能调用明确的 bridge 方法并展示结果。

## 后果

- 日常运行保持普通权限，授权动作本身仍需要 UAC。
- 授权状态必须可诊断、可撤销，并在 Windows 11 24H2 真机验证。
- 权限模型变更必须更新 ADR、实现测试和设置页验收步骤。
