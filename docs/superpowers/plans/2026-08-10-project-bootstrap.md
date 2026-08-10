# Project Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Initialize Git and produce a buildable, testable Windows 11 x64 C++20 project with single-binary runtime-mode dispatch and a mandatory local/CI quality gate.

**Architecture:** A root CMake project builds a small Win32 GUI executable and a separately testable runtime-mode library. Shared CMake functions apply strict MSVC options and optional static analysis to every first-party target. One PowerShell entry point locates the Visual Studio toolchain through `vswhere`, formats sources, builds, lints, analyzes, tests, and verifies Release output.

**Tech Stack:** Git 2.47+, CMake 3.28+, Ninja, MSVC C++20, Windows SDK 10.0.26100+, clang-format, clang-tidy, Clang Static Analyzer, MSVC `/analyze`, CTest, PowerShell 7.

## Global Constraints

- Target Windows 11 24H2 x64; do not add Windows 10 or x86 compatibility paths.
- Use C++20, CMake, native Win32, and the static MSVC runtime; do not add Qt, .NET, WinUI, Windows App SDK, package managers, or runtime third-party dependencies.
- Keep the interactive executable at normal privilege; this bootstrap must not install, start, stop, or emulate a Windows service.
- Preserve one executable entry surface for `--collector`, `--service`, `--install`, `--uninstall`, and `--service-control`, but return `ERROR_CALL_NOT_IMPLEMENTED` until their dedicated implementation plans are executed.
- Apply `/W4 /WX /permissive- /sdl /utf-8 /Zc:__cplusplus` to every first-party target.
- Every code-bearing task ends with `pwsh -File tools/quality.ps1 -Mode Full`; no Git commit is allowed after a failed gate.
- Git initialization is authorized. Commit creation remains conditional on the full quality gate passing and must not include `.superpowers/`, build directories, IDE state, ETL, logs, dumps, or local runtime configuration.
- Keep project documentation under `docs/`, except root `README.md` and `AGENTS.md`; keep tool-discovery configuration at the repository root.

---

## File Structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Project identity, C++ standard, static runtime, module loading, source/test subdirectories |
| `CMakePresets.json` | Reproducible Debug and Release Ninja configure/build/test presets |
| `cmake/ProjectOptions.cmake` | Strict first-party compiler definitions, warnings, and language options |
| `cmake/StaticAnalysis.cmake` | Per-target clang-tidy and MSVC `/analyze` activation |
| `src/CMakeLists.txt` | Runtime library and final Win32 executable targets |
| `src/app/main.cpp` | Win32 entry boundary, command-line ownership, top-level exception containment, runtime dispatch |
| `include/network_flow_monitor/app/runtime_mode.h` | Stable runtime-mode enum and parser declaration |
| `src/app/runtime_mode.cpp` | Exact command-line switch parser, independent of Win32 UI |
| `tests/CMakeLists.txt` | CTest executables and registrations |
| `tests/bootstrap_tests.cpp` | Compile/runtime assertions for x64 and C++20 baseline |
| `tests/runtime_mode_tests.cpp` | Table-driven runtime-mode parser tests |
| `tools/quality.ps1` | Single format/fast/full quality command used locally and by future CI |
| `README.md` | Actual configure/build/test/quality commands after bootstrap |
| `docs/development-setup.md` | Confirmed preset and quality command documentation |
| `docs/planning/task_plan.md` | Phase state and next action |
| `docs/planning/progress.md` | Commands, results, files, and commit hashes produced during execution |

---

### Task 1: Initialize Git, CMake, Win32 smoke target, and quality gate

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `cmake/ProjectOptions.cmake`
- Create: `cmake/StaticAnalysis.cmake`
- Create: `src/CMakeLists.txt`
- Create: `src/app/main.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/bootstrap_tests.cpp`
- Create: `tools/quality.ps1`

**Interfaces:**
- Consumes: root `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore`; Visual Studio installation discoverable by `vswhere.exe`.
- Produces: `nfm_configure_target(target)`, `nfm_enable_static_analysis(target)`, CMake presets `windows-x64-debug` and `windows-x64-release`, quality modes `Format`, `Fast`, and `Full`, executable target `NetworkFlowMonitor`.

- [x] **Step 1: Initialize the repository on `main` and verify commit identity**

Run:

```powershell
git init -b main
git config --get user.name
git config --get user.email
git status --short
```

Expected: Git initializes `.git`, identity prints `sheepyu` and `3029944576@qq.com`, and existing documentation/configuration appears as untracked without `.superpowers/`.

- [x] **Step 2: Move persistent planning state into implementation**

Update `docs/planning/task_plan.md` before creating code:

- set `Current Phase` to `Phase 4`;
- mark all Phase 3 checkboxes complete and its status `complete`;
- set Phase 4 status to `in_progress`;
- set `Next Step` to `Execute Task 1 of the approved project-bootstrap plan`.

Append this exact section to `docs/planning/progress.md`:

```markdown
### Phase 4: Project bootstrap implementation
- **Status:** in_progress
- **Started:** 2026-08-10
- Actions taken:
  - Initialized Git on branch `main` after explicit user authorization.
  - Began Task 1 of `docs/superpowers/plans/2026-08-10-project-bootstrap.md`.
- Files created/modified:
  - `.git/` (repository metadata, not tracked)
  - `docs/planning/task_plan.md`
  - `docs/planning/progress.md`
```

- [x] **Step 3: Create the root CMake project**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)

project(
    NetworkFlowMonitor
    VERSION 0.1.0
    DESCRIPTION "Lightweight Windows 11 per-application network flow monitor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(StaticAnalysis)
include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

- [x] **Step 4: Create strict first-party compiler options**

Create `cmake/ProjectOptions.cmake`:

```cmake
include_guard(GLOBAL)

function(nfm_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    target_compile_definitions(
        ${target_name}
        PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )

    if(MSVC)
        target_compile_options(
            ${target_name}
            PRIVATE
                /W4
                /WX
                /permissive-
                /sdl
                /utf-8
                /Zc:__cplusplus
                /Zc:preprocessor
                /EHsc
                /external:anglebrackets
                /external:W0
        )
    else()
        message(FATAL_ERROR "Network Flow Monitor currently requires MSVC")
    endif()
endfunction()
```

- [x] **Step 5: Create optional static-analysis integration**

Create `cmake/StaticAnalysis.cmake`:

```cmake
include_guard(GLOBAL)

option(NFM_ENABLE_CLANG_TIDY "Run clang-tidy while compiling first-party targets" OFF)
option(NFM_ENABLE_MSVC_ANALYZE "Run MSVC code analysis while compiling first-party targets" OFF)
set(NFM_CLANG_TIDY_EXECUTABLE "" CACHE FILEPATH "Absolute path to clang-tidy.exe")

function(nfm_enable_static_analysis target_name)
    if(NFM_ENABLE_CLANG_TIDY)
        if(NOT EXISTS "${NFM_CLANG_TIDY_EXECUTABLE}")
            message(FATAL_ERROR "NFM_CLANG_TIDY_EXECUTABLE must point to clang-tidy.exe")
        endif()

        set_property(
            TARGET ${target_name}
            PROPERTY CXX_CLANG_TIDY
                "${NFM_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy;--extra-arg-before=--driver-mode=cl"
        )
    endif()

    if(NFM_ENABLE_MSVC_ANALYZE)
        if(NOT MSVC)
            message(FATAL_ERROR "NFM_ENABLE_MSVC_ANALYZE requires MSVC")
        endif()
        target_compile_options(${target_name} PRIVATE /analyze)
    endif()
endfunction()
```

- [x] **Step 6: Create reproducible Debug and Release presets**

Create `CMakePresets.json`:

```json
{
  "version": 6,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 28,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "windows-x64-base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "BUILD_TESTING": "ON",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "windows-x64-debug",
      "inherits": "windows-x64-base",
      "displayName": "Windows x64 Debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "windows-x64-release",
      "inherits": "windows-x64-base",
      "displayName": "Windows x64 Release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "windows-x64-debug",
      "configurePreset": "windows-x64-debug"
    },
    {
      "name": "windows-x64-release",
      "configurePreset": "windows-x64-release"
    }
  ],
  "testPresets": [
    {
      "name": "windows-x64-debug",
      "configurePreset": "windows-x64-debug",
      "output": {
        "outputOnFailure": true
      }
    },
    {
      "name": "windows-x64-release",
      "configurePreset": "windows-x64-release",
      "output": {
        "outputOnFailure": true
      }
    }
  ]
}
```

- [x] **Step 7: Create the minimal Win32 executable target**

Create `src/CMakeLists.txt`:

```cmake
add_executable(NetworkFlowMonitor WIN32 app/main.cpp)

nfm_configure_target(NetworkFlowMonitor)
nfm_enable_static_analysis(NetworkFlowMonitor)

target_link_libraries(NetworkFlowMonitor PRIVATE User32)
```

Create `src/app/main.cpp`:

```cpp
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    ::MessageBoxW(nullptr,
                  L"Network Flow Monitor project bootstrap is ready.",
                  L"Network Flow Monitor",
                  MB_OK | MB_ICONINFORMATION);
    return 0;
}
```

- [x] **Step 8: Create an offline CTest smoke test**

Create `tests/CMakeLists.txt`:

```cmake
add_executable(nfm_bootstrap_tests bootstrap_tests.cpp)

nfm_configure_target(nfm_bootstrap_tests)
nfm_enable_static_analysis(nfm_bootstrap_tests)

add_test(NAME bootstrap.toolchain COMMAND nfm_bootstrap_tests)
```

Create `tests/bootstrap_tests.cpp`:

```cpp
#include <cstddef>

static_assert(sizeof(void*) == 8, "The first release supports x64 only");
static_assert(__cplusplus >= 202002L, "The project requires C++20 or newer");

int main()
{
    return 0;
}
```

- [x] **Step 9: Create the unified quality runner**

Create `tools/quality.ps1`:

```powershell
[CmdletBinding()]
param(
    [ValidateSet('Format', 'Fast', 'Full')]
    [string]$Mode = 'Fast'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

function Invoke-Native {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,
        [string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Native command failed with exit code $LASTEXITCODE: $FilePath $ArgumentList"
    }
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer was not found at $vswhere"
}

$vsInstall = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($vsInstall)) {
    throw 'Visual Studio with the x64/x86 C++ tools is required'
}

$devShellModule = Join-Path $vsInstall 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule -Force
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'

$cmake = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1)
$ninja = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' | Select-Object -First 1)
$clangFormat = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-format.exe' | Select-Object -First 1)
$clangTidy = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-tidy.exe' | Select-Object -First 1)

foreach ($tool in @($cmake, $ninja, $clangFormat, $clangTidy)) {
    if ([string]::IsNullOrWhiteSpace($tool) -or -not (Test-Path -LiteralPath $tool)) {
        throw "A required build tool could not be located: $tool"
    }
}

$env:PATH = "$(Split-Path -Parent $cmake);$(Split-Path -Parent $ninja);$env:PATH"

$sourceFiles = @(
    Get-ChildItem -Path (Join-Path $projectRoot 'src'),
                        (Join-Path $projectRoot 'include'),
                        (Join-Path $projectRoot 'tests') `
                  -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hxx', '.ixx' } |
        Sort-Object FullName
)

if ($Mode -eq 'Format') {
    foreach ($file in $sourceFiles) {
        Invoke-Native -FilePath $clangFormat -ArgumentList @('-i', '--style=file', $file.FullName)
    }
    Write-Host "Formatted $($sourceFiles.Count) C/C++ files."
    exit 0
}

foreach ($file in $sourceFiles) {
    Invoke-Native -FilePath $clangFormat -ArgumentList @('--dry-run', '--Werror', '--style=file', $file.FullName)
}

Invoke-Native -FilePath $cmake -ArgumentList @('--preset', 'windows-x64-debug')
Invoke-Native -FilePath $cmake -ArgumentList @('--build', '--preset', 'windows-x64-debug')
Invoke-Native -FilePath $cmake -ArgumentList @('--build', 'build/windows-x64-debug', '--target', 'test')

$tidyBuild = Join-Path $projectRoot 'build\windows-x64-tidy'
Invoke-Native -FilePath $cmake -ArgumentList @(
    '-S', $projectRoot,
    '-B', $tidyBuild,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Debug',
    '-DBUILD_TESTING=ON',
    '-DNFM_ENABLE_CLANG_TIDY=ON',
    "-DNFM_CLANG_TIDY_EXECUTABLE=$clangTidy",
    "-DCMAKE_MAKE_PROGRAM=$ninja"
)
Invoke-Native -FilePath $cmake -ArgumentList @('--build', $tidyBuild)

if ($Mode -eq 'Full') {
    $analyzeBuild = Join-Path $projectRoot 'build\windows-x64-analyze'
    Invoke-Native -FilePath $cmake -ArgumentList @(
        '-S', $projectRoot,
        '-B', $analyzeBuild,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        '-DBUILD_TESTING=ON',
        '-DNFM_ENABLE_MSVC_ANALYZE=ON',
        "-DCMAKE_MAKE_PROGRAM=$ninja"
    )
    Invoke-Native -FilePath $cmake -ArgumentList @('--build', $analyzeBuild)

    Invoke-Native -FilePath $cmake -ArgumentList @('--preset', 'windows-x64-release')
    Invoke-Native -FilePath $cmake -ArgumentList @('--build', '--preset', 'windows-x64-release')
    Invoke-Native -FilePath $cmake -ArgumentList @('--build', 'build/windows-x64-release', '--target', 'test')
}

Write-Host "Quality gate '$Mode' passed."
```

- [x] **Step 10: Format all bootstrap C/C++ files**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Format
```

Expected: `Formatted 2 C/C++ files.` and exit code 0.

- [x] **Step 11: Run the complete quality gate**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

Expected: format check, Debug build, `bootstrap.toolchain`, clang-tidy build, MSVC `/analyze` build, Release build, and Release test pass; final line is `Quality gate 'Full' passed.`

- [x] **Step 12: Record the successful full gate before staging**

Append this exact entry to the current Phase 4 section in `docs/planning/progress.md` after Step 10 passes:

```markdown
- Project bootstrap quality gate: Pass
  - Command: `pwsh -NoProfile -File tools/quality.ps1 -Mode Full`
  - clang-format: Pass
  - MSVC Debug `/W4 /WX /permissive- /sdl`: Pass
  - CTest Debug `bootstrap.toolchain`: 1/1 passed
  - clang-tidy and Clang Static Analyzer: Pass
  - MSVC `/analyze`: Pass
  - MSVC Release x64: Pass
  - CTest Release `bootstrap.toolchain`: 1/1 passed
```

- [x] **Step 13: Verify ignore behavior and review the staged scope**

Run:

```powershell
git status --short --ignored
git check-ignore -v .superpowers build/windows-x64-debug build/windows-x64-release
git add .clang-format .clang-tidy .editorconfig .gitignore AGENTS.md README.md cmake CMakeLists.txt CMakePresets.json docs src tests tools
git diff --cached --check
git status --short
```

Expected: `.superpowers/` and `build/` are ignored, no whitespace errors are reported, and only repository documents, root tooling configuration, CMake files, source, tests, and quality tooling are staged.

- [x] **Step 14: Commit the verified bootstrap baseline**

Run:

```powershell
git commit -m "build: initialize native Windows project"
```

Expected: initial commit succeeds on branch `main`. If Git reports identity or signing configuration errors, stop without changing global Git settings and report the exact error.

---

### Task 2: Add test-first single-binary runtime-mode parsing

**Files:**
- Create: `include/network_flow_monitor/app/runtime_mode.h`
- Create: `src/app/runtime_mode.cpp`
- Create: `tests/runtime_mode_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `src/app/main.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `nfm_configure_target(target)`, `nfm_enable_static_analysis(target)`, Win32 `CommandLineToArgvW`.
- Produces: `nfm::app::RuntimeMode` and `nfm::app::ParseRuntimeMode(std::span<const std::wstring_view>) noexcept`; static library target `nfm_runtime` linked by the executable and tests.

- [x] **Step 1: Declare the runtime-mode contract and an intentionally empty implementation unit**

Create `include/network_flow_monitor/app/runtime_mode.h`:

```cpp
#pragma once

#include <span>
#include <string_view>

namespace nfm::app {

enum class RuntimeMode {
    Ui,
    Collector,
    Service,
    Install,
    Uninstall,
    ServiceControl,
    Invalid,
};

[[nodiscard]] RuntimeMode
ParseRuntimeMode(std::span<const std::wstring_view> arguments) noexcept;

} // namespace nfm::app
```

Create `src/app/runtime_mode.cpp`:

```cpp
#include "network_flow_monitor/app/runtime_mode.h"
```

- [x] **Step 2: Add the runtime library to CMake**

Replace `src/CMakeLists.txt` with:

```cmake
add_library(nfm_runtime STATIC app/runtime_mode.cpp)

target_include_directories(
    nfm_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

nfm_configure_target(nfm_runtime)
nfm_enable_static_analysis(nfm_runtime)

add_executable(NetworkFlowMonitor WIN32 app/main.cpp)

nfm_configure_target(NetworkFlowMonitor)
nfm_enable_static_analysis(NetworkFlowMonitor)

target_link_libraries(NetworkFlowMonitor PRIVATE nfm_runtime Shell32 User32)
```

- [x] **Step 3: Write table-driven parser tests before the implementation**

Create `tests/runtime_mode_tests.cpp`:

```cpp
#include "network_flow_monitor/app/runtime_mode.h"

#include <initializer_list>
#include <iostream>
#include <span>
#include <string_view>

namespace {

bool ExpectMode(std::initializer_list<std::wstring_view> arguments,
                nfm::app::RuntimeMode expected,
                std::wstring_view case_name)
{
    const auto actual = nfm::app::ParseRuntimeMode(
        std::span<const std::wstring_view>{arguments.begin(), arguments.size()});
    if (actual == expected) {
        return true;
    }

    std::wcerr << L"Case failed: " << case_name << L'\n';
    return false;
}

} // namespace

int main()
{
    int failures = 0;
    failures += !ExpectMode({}, nfm::app::RuntimeMode::Ui, L"empty arguments select UI");
    failures += !ExpectMode({L"--collector"}, nfm::app::RuntimeMode::Collector, L"collector");
    failures += !ExpectMode({L"--service"}, nfm::app::RuntimeMode::Service, L"service");
    failures += !ExpectMode({L"--install"}, nfm::app::RuntimeMode::Install, L"install");
    failures += !ExpectMode({L"--uninstall"}, nfm::app::RuntimeMode::Uninstall, L"uninstall");
    failures += !ExpectMode(
        {L"--service-control"}, nfm::app::RuntimeMode::ServiceControl, L"service control");
    failures += !ExpectMode({L"--unknown"}, nfm::app::RuntimeMode::Invalid, L"unknown switch");
    failures += !ExpectMode(
        {L"--service", L"extra"}, nfm::app::RuntimeMode::Invalid, L"multiple arguments");
    return failures;
}
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(nfm_runtime_mode_tests runtime_mode_tests.cpp)

nfm_configure_target(nfm_runtime_mode_tests)
nfm_enable_static_analysis(nfm_runtime_mode_tests)

target_link_libraries(nfm_runtime_mode_tests PRIVATE nfm_runtime)

add_test(NAME app.runtime_mode COMMAND nfm_runtime_mode_tests)
```

- [x] **Step 4: Build the parser test and verify the missing implementation fails**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Format
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

Expected: the Debug build inside `Fast` fails with an unresolved external for `nfm::app::ParseRuntimeMode`; the failure proves the test calls the declared contract.

- [x] **Step 5: Implement the exact switch parser**

Replace `src/app/runtime_mode.cpp` with:

```cpp
#include "network_flow_monitor/app/runtime_mode.h"

namespace nfm::app {

RuntimeMode ParseRuntimeMode(std::span<const std::wstring_view> arguments) noexcept
{
    if (arguments.empty()) {
        return RuntimeMode::Ui;
    }
    if (arguments.size() != 1) {
        return RuntimeMode::Invalid;
    }

    const auto argument = arguments.front();
    if (argument == L"--collector") {
        return RuntimeMode::Collector;
    }
    if (argument == L"--service") {
        return RuntimeMode::Service;
    }
    if (argument == L"--install") {
        return RuntimeMode::Install;
    }
    if (argument == L"--uninstall") {
        return RuntimeMode::Uninstall;
    }
    if (argument == L"--service-control") {
        return RuntimeMode::ServiceControl;
    }
    return RuntimeMode::Invalid;
}

} // namespace nfm::app
```

- [x] **Step 6: Run the focused parser test and verify it passes**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Format
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

Expected: both `bootstrap.toolchain` and `app.runtime_mode` pass, and the final line is `Quality gate 'Fast' passed.`

- [x] **Step 7: Replace the bootstrap entry point with bounded runtime dispatch**

Replace `src/app/main.cpp` with:

```cpp
#include "network_flow_monitor/app/runtime_mode.h"

#include <shellapi.h>
#include <windows.h>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

struct LocalFreeDeleter {
    void operator()(wchar_t** value) const noexcept
    {
        if (value != nullptr) {
            ::LocalFree(value);
        }
    }
};

int RunMode(nfm::app::RuntimeMode mode)
{
    if (mode == nfm::app::RuntimeMode::Invalid) {
        ::MessageBoxW(nullptr,
                      L"Unsupported command-line arguments.",
                      L"Network Flow Monitor",
                      MB_OK | MB_ICONERROR);
        return ERROR_INVALID_PARAMETER;
    }

    if (mode != nfm::app::RuntimeMode::Ui) {
        return ERROR_CALL_NOT_IMPLEMENTED;
    }

    ::MessageBoxW(nullptr,
                  L"Network Flow Monitor project bootstrap is ready.",
                  L"Network Flow Monitor",
                  MB_OK | MB_ICONINFORMATION);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try {
        int argument_count = 0;
        wchar_t** raw_arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
        if (raw_arguments == nullptr || argument_count < 1) {
            return ERROR_INVALID_PARAMETER;
        }

        const std::unique_ptr<wchar_t*, LocalFreeDeleter> arguments_owner{raw_arguments};
        std::vector<std::wstring_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argument_count - 1));
        for (int index = 1; index < argument_count; ++index) {
            arguments.emplace_back(raw_arguments[index]);
        }

        return RunMode(nfm::app::ParseRuntimeMode(arguments));
    } catch (...) {
        return ERROR_UNHANDLED_EXCEPTION;
    }
}
```

- [x] **Step 8: Run the complete quality gate**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Format
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

Expected: both CTest cases pass in Debug and Release; clang-tidy and MSVC `/analyze` produce no gate failures; final line is `Quality gate 'Full' passed.`

- [x] **Step 9: Record the successful runtime-mode full gate**

Append this exact entry to the current Phase 4 section in `docs/planning/progress.md` after Step 8 passes:

```markdown
- Runtime-mode quality gate: Pass
  - Command: `pwsh -NoProfile -File tools/quality.ps1 -Mode Full`
  - clang-format: Pass
  - MSVC Debug `/W4 /WX /permissive- /sdl`: Pass
  - CTest Debug: 2/2 passed
  - clang-tidy and Clang Static Analyzer: Pass
  - MSVC `/analyze`: Pass
  - MSVC Release x64: Pass
  - CTest Release: 2/2 passed
```

- [x] **Step 10: Commit the runtime-mode boundary**

Run:

```powershell
git add include src tests docs/planning/progress.md
git diff --cached --check
git commit -m "feat: add runtime mode dispatch"
```

Expected: commit succeeds and contains only the runtime contract, parser, entry-point dispatch, CMake target changes, and tests.

---

### Task 3: Synchronize project documentation and planning state

**Files:**
- Modify: `README.md`
- Modify: `docs/development-setup.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`

**Interfaces:**
- Consumes: verified preset names, quality modes, executable target, CTest names, Git commit hashes from Tasks 1 and 2.
- Produces: accurate developer onboarding and a persistent record that bootstrap implementation is complete.

- [x] **Step 1: Replace the README status and add verified commands**

In `README.md`, replace the sentence stating that production code has not started with:

```markdown
项目已完成原生 C++20/CMake 初始化和单一二进制运行模式骨架。ETW、IPC、服务和最终 UI 仍按独立实现计划推进。
```

Add this section before “首版非目标”:

````markdown
## 构建与检查

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug

pwsh -NoProfile -File tools/quality.ps1 -Mode Format
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

提交前必须通过 `Full`。详细规则见 [代码质量与提交门](docs/code-quality.md)。
````

- [x] **Step 2: Make the development setup commands authoritative**

In `docs/development-setup.md`, replace the paragraph saying the CMake project has not been generated with:

```markdown
项目提供以下稳定预设：

- `windows-x64-debug`：Debug、测试和开发检查。
- `windows-x64-release`：Release 交付配置。

所有命令既可在 Developer PowerShell 中直接运行，也可通过 `tools/quality.ps1` 自动加载 Visual Studio 开发环境后运行。
```

Keep the existing preset command block, and add:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

- [x] **Step 3: Record completed bootstrap work in persistent planning files**

Update `docs/planning/task_plan.md` so Phase 4 reports the bootstrap implementation complete and the next action is to choose the next subsystem plan. Add these checked Phase 4 items:

```markdown
- [x] Initialize Git on `main`
- [x] Scaffold the C++20/CMake/CTest project
- [x] Add the single-binary runtime mode boundary
- [x] Add and pass the unified full quality gate
```

Append a Phase 4 entry to `docs/planning/progress.md` listing:

- the two commit hashes and messages;
- `pwsh -NoProfile -File tools/quality.ps1 -Mode Full`;
- the Debug and Release CTest pass counts;
- successful clang-format, clang-tidy, MSVC `/analyze`, Debug, and Release results;
- every created or modified source/build-tool file from Tasks 1 and 2.

- [x] **Step 4: Verify documentation links, whitespace, and repository scope**

Run:

```powershell
git diff --check
$missing = @()
Get-ChildItem -Recurse -Filter *.md |
    Where-Object {
        $_.FullName -notlike '*\.superpowers\*' -and
        $_.FullName -notlike '*\docs\superpowers\plans\*'
    } |
    ForEach-Object {
        $file = $_
        $text = Get-Content -Raw -LiteralPath $file.FullName
        [regex]::Matches($text, '\[[^\]]+\]\(([^)]+)\)') | ForEach-Object {
            $target = $_.Groups[1].Value
            if ($target -notmatch '^(https?://|#)' -and $target -notmatch '^D:/') {
                $clean = ($target -split '#')[0]
                $resolved = [System.IO.Path]::GetFullPath((Join-Path $file.DirectoryName $clean))
                if (-not (Test-Path -LiteralPath $resolved)) {
                    $missing += "$($file.FullName): $target"
                }
            }
        }
    }
if ($missing.Count -ne 0) { throw ($missing -join [Environment]::NewLine) }
```

Expected: no whitespace errors and no missing relative Markdown links.

- [x] **Step 5: Re-run the full quality gate after documentation changes**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

Expected: final line is `Quality gate 'Full' passed.` and no tracked file changes are generated by the check.

- [x] **Step 6: Commit the synchronized documentation**

Run:

```powershell
git add README.md docs/development-setup.md docs/planning/task_plan.md docs/planning/progress.md docs/superpowers/plans/2026-08-10-project-bootstrap.md
git diff --cached --check
git commit -m "docs: record project bootstrap workflow"
git status --short
git log --oneline --decorate -3
```

Expected: documentation commit succeeds, working tree is clean, and the three bootstrap commits are visible on `main`.

---

## Plan Self-Review Record

- **Spec coverage:** This bootstrap plan covers Git initialization, CMake/Ninja/MSVC setup, strict compiler options, format/lint/static-analysis/test/Release gates, x64/C++20 assertions, a Win32 executable, and the approved single-binary argument surface. ETW collection, secure IPC, service lifecycle, settings UI, tray/floating UI, and ranking are intentionally assigned to separate future plans because each is independently testable and reviewable.
- **Type consistency:** `nfm::app::RuntimeMode` and `ParseRuntimeMode(std::span<const std::wstring_view>) noexcept` use identical names and signatures in the header, implementation, executable, tests, and CMake target graph.
- **Quality consistency:** Every source target calls both `nfm_configure_target` and `nfm_enable_static_analysis`; every commit step follows a successful full gate or documentation-only recheck after a previously successful full gate.
- **Dependency consistency:** The plan introduces no package manager or third-party runtime/test dependency.
