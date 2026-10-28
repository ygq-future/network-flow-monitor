# AGENTS.md

本文件约束 Network Flow Monitor 的开发代理和维护者。开始设计或修改前，必须阅读：

- `docs/architecture.md`（系统架构、核心设计决策、Protocol V1、Bridge 与配置契约）
- `docs/development.md`（构建、质量门、打包与验收规范）
- `docs/progress.md`（阶段演进与交付记录）

## 代码索引

XERJ prefix: "nfm"

## 产品架构

- 目标系统：Windows 11 24H2 x64。
- Collector (`nfm-collector.exe`)：C++20、CMake、ETW、进程身份、聚合、排行、Protocol V1。
- Product Host (`NetworkFlowMonitor.exe`)：C++20、Win32、Microsoft WebView2。
- Frontend (`apps/nfm-ui`)：React 19、TypeScript、Vite、Tailwind CSS。
- Frontend 工具：Bun、ESLint、Prettier、`prettier-plugin-tailwindcss`。
- 生产运行只加载打包静态资产 (`ui/`)，不运行 Node、Bun、Vite server 或 TypeScript compiler。
- 交付形态是普通权限 Host + portable Collector companion；Collector 通过父进程句柄和停止信号结束，并支持按需动态原生 UAC 提权与一次性 ETW 持久授权。
- 不实现任务栏嵌入、Explorer 注入、内核驱动或通用 Native RPC。

## 模块职责

- `app`: Collector 可执行入口与运行时参数组装。
- `collector`: ETW 会话、事件解码、特权激活/一次性授权和累计计数。
- `process`: 进程生命周期、PID 防复用和应用身份。
- `ranking`: 滑动窗口、速率、应用合并和 Top N。
- `ipc`: Protocol V1、命名管道、ACL 和客户端/服务端传输。
- `config`: 带版本的 `AppConfig`、默认值、校验、迁移和持久化。
- `host`: HWND、WebView2、托盘、贴边胶囊协调器 (`DockCapsuleCoordinator`)、独立设置窗口、滚动日志、开机启动和 Native/Web bridge。
- `apps/nfm-ui`: React 视觉呈现（贴边胶囊、340 DIP 监控面板、3 大分类设置中心）。

Collector 不包含 UI。React 不直接调用 Win32、命名管道、文件系统、shell 或授权接口。Host 解析并验证 Collector 数据后才形成 Web DTO。

## 工程结构与复用约束

- 模块拥有自己的策略、类型、常量、测试和特性专用 helper；只有已有多个真实调用方的代码才能进入 shared/common。
- 公共常量按领域归属：协议常量属于 `ipc`/`bridge`，配置默认值与范围属于 `config`，视觉值属于语义化 theme token，特性几何与时序属于对应 feature。
- `shared/constants` 只承载真正跨特性的不可变常量，禁止成为无归属的常量堆放目录。
- 表达时间、DIP、字节、速率、容量、重试次数和协议限制的值必须使用强类型或带单位名称；禁止在业务代码中散落代表产品规则的裸字面量。
- `shared/utils` 只接受纯函数、平台无关且具有多个调用方的工具；Win32 能力必须位于窄 adapter，feature 私有 helper 保留在 feature 内。
- 同一规则、转换、校验或格式化逻辑只有一个权威实现。发现重复实现时必须抽取到其最窄共同所有者，并补充针对该接口的测试。
- 禁止只做转发的工具类、无内聚职责的 `Utils`/`Common` 大类，以及为了复用而暴露实现细节的公共接口。
- React 依赖方向为 `app -> features -> shared`；C++ 依赖方向为 composition root -> domain interface -> adapter。禁止循环依赖。

## 安全与协议

- Protocol V1 使用 12-byte little-endian header、256 KiB payload 上限和 UTF-8 JSON。
- 命名管道必须使用显式 owner-scoped security descriptor 并拒绝远程客户端。
- 未知版本、类型、命令、非法 UTF-8 和越界长度必须拒绝。
- Bridge 是版本化 allow-list union，不提供任意方法调用、文件访问或 shell 执行。
- 日志不记录网络载荷、敏感命令行参数或非必要远端地址。

## C++ 质量

- 使用 RAII 管理 Windows handle、COM、ETW session、thread 和 memory。
- 优先值语义、`std::unique_ptr`、`std::span`、`std::string_view` 和 `std::chrono`。
- 禁止裸 `new`/`delete`，除非封装 ABI 且说明所有权。
- 异常不得穿过 Win32 callback、thread entry、collector entry 或 IPC ABI。
- 第一方 Debug、Release 和 CI 启用 `/W4 /WX /permissive- /sdl`。
- `.clang-format` 和 `.clang-tidy` 是唯一 C/C++ 格式与 lint 配置。

## Frontend 质量

- TypeScript strict mode，并提供 `tsc --noEmit` typecheck。
- ESLint 只负责 correctness/code quality；Prettier 负责 formatting。
- Bun lockfile 是唯一 package lockfile。
- 稳定状态不得运行永久 JavaScript animation loop。
- 所有用户可见文本使用 locale key，所有主题相关颜色使用语义 token。
- 禁止使用已弃用的配置（如 tsconfig 的 `baseUrl`、`ignoreDeprecations`）或 API，禁止复制过时模板；该策略由 Fast 门的 Deprecation policy check 强制。

## 质量门

统一入口是 `tools/quality.ps1`，Fast/Full 的检查项以 `docs/development.md` 为准。

Fast/Full 必须 headless：不显示 HWND/WebView、无 UAC、无人工交互、无 Vite server。

## 完成与交付

功能或修复完成后按 `docs/development.md` 运行适用质量门并更新 `docs/progress.md`；交付前汇报修改、质量结果、目标机验收步骤和待验收项。
