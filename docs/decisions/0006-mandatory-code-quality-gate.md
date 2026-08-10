# ADR 0006：强制多工具 C++ 质量门

- 状态：已接受
- 日期：2026-08-10

## 背景

C++ 没有一个工具能同时可靠覆盖格式、语言规范、API 误用、控制流缺陷、资源生命周期、行为回归和 Release 链接问题。只运行编译器或只运行 clang-tidy 都会留下明显盲区。

## 决策

采用以下提交门：

1. `.clang-format` 格式化及全仓库差异检查。
2. MSVC `/W4 /WX /permissive- /sdl` Debug 构建。
3. `.clang-tidy` 驱动的 lint 和 Clang Static Analyzer。
4. MSVC `/analyze`。
5. 全部 CTest 测试。
6. Release x64 构建。

实现阶段提供 `tools/quality.ps1`，本地和 CI 使用同一入口。功能完成后运行快速门，每次 Git commit 前必须运行全量门。

## 后果

- 提交反馈时间增加，但格式、静态缺陷和配置回归能在进入历史前暴露。
- Win32 ABI 可能与部分 Core Guidelines 检查冲突；只允许针对具体检查、有理由的局部抑制。
- 工具链升级可能带来新诊断，因此工具升级和对应修复应作为独立变更处理。
- 质量门通过只是提交的必要条件，不构成代理自行提交或推送的授权。
