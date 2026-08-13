# ADR 0015：便携采集器通过一次性授权免 UAC

- 状态：已接受
- 日期：2026-08-13

## 背景

ADR-0002 把「每次启动高权限采集都会出现 UAC」当作便携模式的固有代价,并让服务模式承担免 UAC 职责。但便携模式是主要使用形态,每次双击弹 UAC 严重破坏首启体验。ETW 事实(ADR-0009):按进程网络字节计数只能由 system logger 会话产生,启动它需要 `SeSystemProfilePrivilege`;普通 per-user 会话根本采不到这些内核事件(不是覆盖变窄,而是采不到)。

## 决策

便携采集器不再每次启动用 `runas` 弹 UAC,改为**一次性授权**:管理员执行一次「授权免 UAC」动作,授予当前用户 `SeSystemProfilePrivilege`(加入 `Performance Log Users` 组),并用 `EventAccessControl` 把该用户加进会话 GUID 与 SystemTraceProvider GUID 的 DACL。此后采集器以普通权限启动,免 UAC,采集覆盖与提权时一致。授权未完成时,退回 `runas` 提权路径(每次启动 UAC)。

## 后果

- 便携模式首启体验顺滑;授权是一次性、可持久化的系统改动,设置界面需能查询授权状态并允许撤销。
- UI 进程保持普通权限,采集器仍通过命名管道与 UI 解耦(ADR-0010);`AGENTS.md` 的权限边界不变。
- 授权动作本身仍需一次 UAC;「授权状态」成为配置/设置的一个可诊断字段。
- 不取代服务模式(ADR-0002):服务仍是「开机无感采集」的答案;本 ADR 只让便携模式也获得免 UAC 能力。
