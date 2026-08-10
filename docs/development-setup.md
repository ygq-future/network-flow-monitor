# 开发环境

## 支持基线

- 开发系统：Windows 11 x64。
- 首发运行目标：Windows 11 24H2 x64。
- 语言：C++20。
- 构建系统：CMake + Ninja，生成器和工具链来自 Visual Studio。
- 编译器：稳定版 MSVC。
- SDK：Windows SDK 10.0.26100.0 或兼容的稳定更新。

本项目不需要 WDK，因为不开发内核驱动。也不需要 MFC、ATL、Qt、Windows App SDK、.NET 或 vcpkg。

## Visual Studio Installer

在稳定渠道的 Visual Studio Community 或 Build Tools 中安装 `Desktop development with C++` 工作负载，并确认包含：

- MSVC x64/x86 build tools。
- Windows 11 SDK。
- C++ CMake tools for Windows。
- Ninja。
- C++ Clang tools for Windows（提供 clang-format 和 clang-tidy）。

当前机器已经检测到：

- Visual Studio 18 Community：`C:\Program Files\Microsoft Visual Studio\18\Community`
- MSVC：`14.51.36231`
- Windows SDK：`10.0.26100.0`
- Visual Studio 自带 CMake 和 Ninja。
- Visual Studio 自带 clang-format 和 clang-tidy。
- Git：`D:\environments\git\bin\git.exe`

## 加载开发者环境

普通 PowerShell 中找不到 `cl` 并不代表未安装。运行：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1' `
  -Arch amd64 `
  -HostArch amd64
```

然后验证：

```powershell
cl
cmake --version
ninja --version
git --version
```

Visual Studio 的 LLVM 工具不一定出现在普通 PATH。实现阶段的 `tools/quality.ps1` 将使用 `vswhere.exe` 定位，不写死工具链版本目录。也可直接检查：

```powershell
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-format.exe'
& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'
```

也可以从开始菜单直接打开对应版本的 Developer PowerShell。

## 预期构建方式

CMake 工程尚未生成。实现阶段将提供 `CMakePresets.json`，预期命令形态如下：

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug

cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
```

正式预设名称由实现计划确定；文档不会在工程创建前声称这些命令已经可运行。

工程生成后，每个功能完成时还必须运行格式、lint、静态分析、测试和 Release 构建。完整规则见 [代码质量与提交门](code-quality.md)。

## 可选诊断工具

- Windows Performance Recorder/Analyzer：分析 ETW 开销和 UI/采集器 CPU 使用。
- Process Explorer：检查权限、服务进程和句柄。
- `sc.exe`：仅用于诊断 SCM 状态；正常服务操作由应用设置界面完成。

## 常见问题

### `cl`、CMake 或 Ninja 不在 PATH

先加载 Developer PowerShell。不要为了这个问题重复安装独立 CMake 或修改系统级 PATH。

### SDK 版本高于 26100

可以使用兼容的稳定 SDK，但 CMake 不应自动选择 Preview SDK。使用新 SDK 也不能无意中调用高于最低运行版本的 API；需要时应进行运行时能力检测。

### 是否安装 WDK

不安装。若未来提出内核驱动需求，必须先提交新的 ADR、安全评审和签名部署方案。
