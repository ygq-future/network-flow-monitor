# 代码质量与提交门

## 目标

C++ 没有单一工具能完全等价于 ESLint。项目使用互补工具覆盖格式、常见缺陷、标准一致性、静态路径分析、行为测试和最终构建：

| 层次 | 工具 | 发现的问题 |
|---|---|---|
| 格式 | `clang-format` | 缩进、换行、括号、指针和 include 排序差异 |
| Lint | `clang-tidy` | bug-prone 模式、现代 C++、可读性、性能、并发和可移植性问题 |
| 静态分析 | Clang Static Analyzer + MSVC `/analyze` | 生命周期、空指针、资源、控制流和 Windows API 误用 |
| 编译器 | MSVC `/W4 /WX /permissive- /sdl` | 高等级警告、非标准扩展和安全诊断 |
| 行为 | CTest 单元/集成测试 | 排行、聚合、IPC、服务和 UI 逻辑回归 |
| 交付 | Release x64 构建 | 只在 Debug 成功、Release 失败的配置或链接问题 |

代码原则以 C++ Core Guidelines、明确所有权、RAII、值语义和最小权限为基础。Win32 边界允许必要的 C 风格结构和转换，但必须限制在适配层，并通过检查名明确的 `NOLINT` 说明理由。

## 仓库配置

- `.clang-format`：唯一格式规范，基于 Microsoft 风格并使用四空格、100 列限制。
- `.clang-tidy`：启用 `bugprone`、`cert`、`clang-analyzer`、`concurrency`、`cppcoreguidelines`、`modernize`、`performance`、`portability` 和 `readability` 等检查。
- `.editorconfig`：统一 UTF-8、换行、末尾空白和基础缩进。
- CMake 必须生成 `compile_commands.json`，供 clang-tidy 使用；该文件属于构建产物，不提交。

关闭的少数 clang-tidy 检查主要针对 Win32 ABI 必需的数组、指针运算和显式转换，或容易产生高噪声的命名/魔法数字规则。关闭某个检查不等于放弃对应设计约束；例如配置值仍必须集中管理。

## 统一质量命令

实现脚手架时创建 `tools/quality.ps1`。最终接口应至少支持：

```powershell
# 格式化改动文件
pwsh -File tools/quality.ps1 -Mode Format

# 开发中快速检查：格式、Debug 构建、clang-tidy、单元测试
pwsh -File tools/quality.ps1 -Mode Fast

# 提交前全量检查
pwsh -File tools/quality.ps1 -Mode Full
```

`Full` 必须顺序执行：

1. `clang-format --dry-run --Werror` 全仓库格式检查。
2. MSVC Debug x64 构建，警告视为错误。
3. 对第一方代码执行 clang-tidy 和 Clang Static Analyzer。
4. 执行 MSVC `/analyze`。
5. 执行全部 CTest 测试。
6. MSVC Release x64 构建。

脚本必须在任一步失败时返回非零退出码，不得吞掉输出或以日志中的“成功”字符串代替进程退出码。CI 调用相同脚本，避免本地与 CI 规则漂移。

## 每个功能的完成定义

功能不是在“代码能运行”时完成，而是在以下条件同时成立时完成：

- 功能和失败路径已经实现。
- 新逻辑有对应测试；不能测试的 UI 行为有明确的可复现验证记录。
- 改动文件已格式化。
- `Fast` 通过。
- 相关文档、ADR 和规划进度已更新。
- 准备提交前再次运行 `Full` 并通过。

只有 `Full` 通过后才允许创建 Git commit。代理仍需遵守用户授权边界：质量通过不自动授予提交、推送或创建 PR 的权限。

## 诊断处理规则

- 优先修正根因，不以全局禁用警告解决。
- 外部头噪声使用 MSVC external headers 机制隔离，不降低第一方代码警告级别。
- `NOLINT(check-name)` 必须限定检查并写理由；禁止 `NOLINT`、`NOLINTBEGIN` 覆盖大段代码而无批准。
- 若工具版本升级新增大量诊断，单独提交工具链升级和修复，避免与功能改动混在一起。
- 如果发现规则与 Win32 API 确实冲突，先更新 `.clang-tidy`、本文件和 ADR，再修改代码；不能只在本机绕过。

## 当前工具状态

当前 Visual Studio 安装中已经包含：

- `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`
- `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe`

实现质量脚本应通过 `vswhere.exe` 定位工具，不把上述版本路径写死。

## 参考

- [ClangFormatStyleOptions](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [clang-tidy](https://clang.llvm.org/extra/clang-tidy/index.html)
- [MSVC compiler options](https://learn.microsoft.com/en-us/cpp/build/reference/compiler-options-listed-by-category)
- [MSVC `/permissive-`](https://learn.microsoft.com/en-us/cpp/build/reference/permissive-standards-conformance)
- [C++ Core Guidelines](https://github.com/isocpp/CppCoreGuidelines)
