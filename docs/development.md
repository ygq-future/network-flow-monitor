# Development, Quality Gate & Delivery

## Prerequisites

- **OS**: Windows 11 24H2 x64
- **C++ Toolchain**: Visual Studio 2022 (Desktop development with C++ x64 workload), CMake 3.28+, Ninja, LLVM (`clang-format`, `clang-tidy`)
- **Runtime**: Microsoft Edge WebView2 Evergreen Runtime
- **Frontend Toolchain**: [Bun](https://bun.sh/) (manages React 19, TypeScript, Vite, Vitest, ESLint, and Prettier)

---

## Building & Running Tests

### 1. Authoritative Quality Gate (`tools/quality.ps1`)

`tools/quality.ps1` is the unified entry point for formatting, building, static analysis, testing, and packaging. It automatically locates Visual Studio C++ tools via `vswhere.exe` and caches the x64 developer environment in `build/.vsenv-x64.json`.

```powershell
# Format all C++ and frontend source files
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Format

# Fast gate (headless daily development check)
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Fast

# Full gate (Fast + clang-tidy + MSVC /analyze + Release build/tests + CPack ZIP)
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Full

# Commit message & range validation (Conventional Commits)
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Commit -CommitMessage "fix(host): example summary"
powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Commit -CommitRange "HEAD~1..HEAD"
```

#### Gate Modes
- **Fast Mode**:
  1. Deprecation policy verification (rejects deprecated TS/CMake options and blanket suppressions).
  2. Version consistency check across `CMakeLists.txt`, `apps/nfm-ui/package.json`, `resources/network_flow_monitor.rc`, and `.github/release-note/v<version>.md`.
  3. `clang-format --dry-run --Werror` across all C/C++ headers, sources, and tests.
  4. Frontend `bun install --frozen-lockfile`, Prettier check (`tailwindStylesheet: ./src/app/index.css`), ESLint (`--max-warnings 0` including `@typescript-eslint/no-deprecated`), `tsc --noEmit`, Vitest suite, and Vite production build.
  5. C++ Debug x64 configure + LSP compilation database generation (`build/windows-x64-tidy/compile_commands.json` consumed by `.clangd` without MSVC PCH flags).
  6. C++ Debug x64 build (`/W4 /WX /permissive- /sdl`, 0 warnings).
  7. C++ automated headless test suites (`ctest --output-on-failure -LE requires-admin`).
- **Full Mode**:
  - Runs everything in **Fast Mode**, plus:
  - `clang-tidy` & Clang Static Analyzer (`WarningsAsErrors: '*'`, 0 diagnostics enforced on exit code and output).
  - MSVC `/analyze` static analysis (0 warnings enforced on exit code and output).
  - C++ Release x64 build and test suites.
  - CPack portable ZIP archive generation.
- **Commit Mode**:
  - Validates commit message files (`-CommitMsgFile`), literal messages (`-CommitMessage`), or commit ranges (`-CommitRange`) against Conventional Commits (`feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert`).

### 2. Capability Matrix & Diagnostic Parity

| Capability | Provider / Configuration | Command / Task | Scope | Enforcement Phase / Severity | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Formatting / format check** | `clang-format` + Prettier (`prettier-plugin-tailwindcss`, `tailwindStylesheet`) | `tools/quality.ps1 -Mode Fast` | `src/`, `include/`, `tests/`, `apps/nfm-ui/` | Pre-commit (`Fast`) & CI (`Full`) / Error | Verified |
| **Syntax / compile** | MSVC C++20 (`cl.exe`) + Vite / Rollup | `tools/quality.ps1 -Mode Fast` | All C++ targets & React UI | Pre-commit (`Fast`) & CI (`Full`) / Error | Verified |
| **Type check** | MSVC C++20 + `tsc --noEmit` (`strict: true`) | `tools/quality.ps1 -Mode Fast` | `src/`, `include/`, `tests/`, `apps/nfm-ui/src/` | Pre-commit (`Fast`) & CI (`Full`) / Error | Verified |
| **Lint & LSP parity** | `.clangd` + `.clang-tidy` (`WarningsAsErrors: '*'`) + ESLint Flat Config (`--max-warnings 0`) | `tools/quality.ps1 -Mode Full` | All first-party C++ TUs & TS/TSX | Pre-push / CI (`Full`) / Error (0 warnings/diagnostics) | Verified |
| **Compiler warnings** | MSVC `/W4 /WX /permissive- /sdl` | `tools/quality.ps1 -Mode Fast` | All first-party C++ targets | Pre-commit (`Fast`) & CI (`Full`) / Error | Verified |
| **Deprecated / obsolete API** | Deprecation policy + MSVC `/W4 /WX` + `@typescript-eslint/no-deprecated` | `tools/quality.ps1 -Mode Fast` | Configs, C++, TS/TSX | Pre-commit (`Fast`) & CI (`Full`) / Error | Verified |
| **Static analysis** | `clang-tidy` (`clang-analyzer-*`, `bugprone-*`, `concurrency-*`) + MSVC `/analyze` | `tools/quality.ps1 -Mode Full` | All first-party C++ targets | Pre-push / CI (`Full`) / Error | Verified |
| **Unused / unreachable code** | `.clangd` (`UnusedIncludes: Strict`), `misc-unused-*`, TS `noUnusedLocals`/`noUnusedParameters`, ESLint `no-unused-vars` | `tools/quality.ps1 -Mode Fast` / `Full` | C++ & TS/TSX | Pre-commit (`Fast`) & `Full` / Error | Verified |
| **Tests** | CTest (`nfm_tests`, 8 headless suites) + Vitest (10 suites) | `tools/quality.ps1 -Mode Fast` | Debug & Release C++, React UI | Pre-commit (`Fast`) & CI (`Full`) / Error | Verified |
| **Build validation** | CMake Release x64 + Vite production bundle + CPack ZIP | `tools/quality.ps1 -Mode Full` | `NetworkFlowMonitor.exe`, `nfm-collector.exe`, `ui/` | Pre-push / CI (`Full`) / Error | Verified |
| **Dependency / manifest / config** | `bun install --frozen-lockfile`, vendored `yyjson 0.12.0`, CMake presets | `tools/quality.ps1 -Mode Fast` | Root & `apps/nfm-ui/` | Pre-commit (`Fast`) & CI (`Full`) / Error | Verified |
| **Application version consistency** | `CMakeLists.txt` SSOT (`0.3.4`) vs `package.json`, `.rc`, `.github/release-note/v<ver>.md`, Git tag | `tools/quality.ps1 -Mode Fast` + CI `meta` | All manifests & release notes | Pre-commit (`Fast`) & CI Release / Error | Verified |
| **Commit message validation** | Conventional Commits validator in `tools/quality.ps1 -Mode Commit` | `tools/quality.ps1 -Mode Commit` | `commit-msg` hook & CI commit range | `commit-msg` hook & CI / Error | Verified |
| **Git hooks** | `.githooks/pre-commit` (`Fast`), `commit-msg` (`Commit`), `pre-push` (`Full`) | `git config --local core.hooksPath .githooks` | Local repository | Local Git lifecycle | Verified |
| **CI Quality Gate** | `.github/workflows/quality-and-publish.yml` | `tools/quality.ps1 -Mode Full -Clean` | Windows x64 full matrix & release | Push, PR, Release tag | Verified |

#### Diagnostic Limits & Baseline Policy
- **Zero Historical Debt Baseline**: The repository maintains 0 accepted warnings and 0 static-analysis suppressions in baseline. All newly introduced compiler, linter, or static-analyzer diagnostics block completion.
- **LSP (`clangd`) Compilation Database**: `.clangd` points to `build/windows-x64-tidy/compile_commands.json` (which disables MSVC `/Yu`/`/Fp`/`/FI` precompiled headers that `clangd` cannot parse) and strips MSVC-only driver flags (`/Zc:preprocessor`, `/external:anglebrackets`, `/sdl`, `/RTC1`). Running `tools/quality.ps1 -Mode Fast` automatically generates this compilation database on a fresh clone.
- **Admin-Only Integration Suites**: `collector.etw_integration` and `ipc.integration` require an elevated token (`requires-admin` CTest label) and are verified via `tools/portable-acceptance.ps1` on target hosts.

### 3. Direct CMake, Frontend & Git Hook Commands

```powershell
# Activate versioned Git hooks on a fresh clone
git config --local core.hooksPath .githooks

# C++ Debug build & tests
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure -LE requires-admin

# Run a single C++ test suite directly
.\build\windows-x64-debug\nfm_tests.exe config.suite

# Frontend checks (under apps/nfm-ui/)
cd apps/nfm-ui
bun install --frozen-lockfile
bun run typecheck
bun run lint
bun run test
bun run build
```

---

## Portable Packaging & Delivery Layout

Run `tools/package.ps1` to produce a clean Release portable archive in `build/dist/`:

```powershell
powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Configuration Release
```

The resulting `NetworkFlowMonitor-<version>-win-x64.zip` is self-contained (statically linking MSVC runtime and `WebView2LoaderStatic.lib`) and keeps all runtime state inside the portable application directory:

```text
NetworkFlowMonitor/
├── NetworkFlowMonitor.exe     # C++20 Win32/WebView2 Product Host (Standard User)
├── nfm-collector.exe          # C++20 ETW Traffic Collector Companion
├── ui/                        # Compiled React static assets mapped to https://nfm.local/
│   ├── index.html
│   └── assets/*
├── README.md
├── LICENSE
├── data/                      # Created at runtime under application root
│   ├── config.json            # Atomic user configuration
│   └── webview2/              # WebView2 user data folder
└── logs/                      # Created at runtime under application root
    ├── nfm-host.log           # Active diagnostic log (max 2 MiB per file)
    └── nfm-host.1.log .. 5    # Size-capped rotating log archives
```

---

## Target-Host Acceptance Checklist (Windows 11 x64)

### 1. Collector & Elevation
- Standard-user launch of `NetworkFlowMonitor.exe` automatically prompts UAC (`runas`) on first run if ETW privileges are not yet configured, and persists one-time ETW authorization (`Performance Log Users` + `SeSystemProfilePrivilege` + ETW GUID DACL).
- Subsequent standard-user launches and `HKCU\Run` login startups connect without UAC prompts.
- Collector lifecycle probe (`tools/portable-acceptance.ps1` run as Administrator) confirms clean startup, stop-event termination, exit code `0`, and removal of `NetworkFlowMonitor.SystemTrace` ETW session.

### 2. Surface & Interaction
- **Dock Capsule**: Renders bidirectional logarithmic download/upload gradient fills, supports custom two-tone color presets, and respects `prefers-reduced-motion`.
- **Hover & Click**: 350 ms hover or immediate click expands the 340 DIP Monitor Panel with smooth morphing transition; pointer leave collapses back after dismiss delay unless Pinned.
- **Native Drag & Edge Snap**: Dragging capsule or panel header beyond `SM_CXDRAG`/`SM_CYDRAG` invokes smooth native system move and snaps to the left or right edge of the release monitor's work area (`rcWork`), preserving a 4 DIP margin for auto-hide taskbars.
- **Fullscreen Yield**: Active fullscreen applications on the same monitor trigger click-through lock (`ignore_hover_when_fullscreen`) or capsule hiding (`hide_capsule_when_fullscreen`).
- **Settings Center**: Opens instantaneously from the pre-warmed window, synchronizes General / Capsule / Collector settings in real time, and cleans up stale cross-path `HKCU\Run` entries when toggling login startup.
