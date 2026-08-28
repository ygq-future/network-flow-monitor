# 01 — 项目基础设施与实时链路

**Blocked by:** None

**Status:** ready-for-agent

建立可长期扩展的产品宿主、React 工程与质量基础，并证明 `便携采集器 -> Named Pipe -> 产品宿主 -> Native/Web bridge -> React` 实时链路。

## 工程与模块

- [ ] 建立普通权限 `NetworkFlowMonitor.exe` C++20 Win32 产品宿主，完成 entry、composition root 和退出路径。
- [ ] 按 `host/app`、`bridge`、`lifecycle`、`platform`、`webview`、`windows` 划分产品宿主；Win32/WebView2 通过窄 adapter 接入领域接口。
- [ ] 建立 `apps/nfm-ui/`，采用 `app`、`bridge`、`features`、`shared` 和 `test` 分层，依赖方向为 `app -> features -> shared`。
- [ ] 集成 WebView2 Evergreen Runtime 检测、环境、controller、导航策略与 packaged `dist/` 虚拟 HTTPS host mapping。
- [ ] 建立 React、TypeScript、Vite、Tailwind CSS、Bun authoritative lockfile、ESLint flat config 和精确 Prettier 配置。

## 代码规范与复用

- [ ] 在仓库规范和 lint/review gate 中落实模块归属、接口、实现、seam 与 adapter 规则。
- [ ] 建立按领域归属的常量结构：协议常量、配置 schema/default/range、语义 theme token、feature 常量和真正跨 feature 的 `shared/constants`。
- [ ] 时间、DIP、字节、速率、容量和协议限制使用强类型或带单位名称，产品行为值不得散落为裸字面量。
- [ ] 建立纯函数型 `shared/utils` 约束；平台 helper 位于 adapter，feature helper 位于其所属 feature。
- [ ] 同一校验、转换、格式化和状态规则只有一个权威实现；自动检查与评审阻止循环依赖、catch-all `Utils`/`Common` 和重复逻辑。

## 跨层基础与实时证明

- [ ] 产品宿主使用 Protocol V1 完成 `Hello`/`HelloAck` 和 4 Hz 流量节拍，遥测线程不直接操作 WebView/UI。
- [ ] Native/Web bridge 使用版本化封闭联合、方向校验、payload 限制和结构化错误，`trafficTick` DTO 具有 C++/TypeScript 对应契约测试。
- [ ] React tracer 稳定显示下载与上传速率，并隔离高频状态更新。
- [ ] 从第一天建立 System/Light/Dark 语义 theme token、System/English/Simplified Chinese locale catalog、locale key 与运行时 provider。
- [ ] 建立应用路由骨架与 Settings 路由占位，使 Ticket 02 可直接扩展应用外壳。
- [ ] `tools/quality.ps1` Fast/Full 接入 C++ 与 frontend headless gate；测试链路无窗口、UAC、开发服务器和人工输入。

**Completion proof:** 合成便携采集器与真实产品宿主/WebView2/React 链路均持续显示流量节拍；生产静态资产可加载；tracer 可实时切换主题和语言；目录/依赖/常量规则有自动检查或可复现评审证据；Fast 与 Full 门通过。
