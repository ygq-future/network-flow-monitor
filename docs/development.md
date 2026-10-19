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
```

#### Gate Modes
- **Fast Mode**:
  1. Deprecation policy verification (rejects deprecated TS/CMake options and blanket suppressions).
  2. `clang-format --dry-run --Werror` across all C/C++ headers, sources, and tests.
  3. C++ Debug x64 build (`/W4 /WX /permissive- /sdl`).
  4. C++ automated headless test suites (`ctest --output-on-failure -LE requires-admin`).
  5. Frontend `bun install --frozen-lockfile`, Prettier check, ESLint (`--max-warnings 0` including `@typescript-eslint/no-deprecated`), `tsc --noEmit`, Vitest suite, and Vite production build.
- **Full Mode**:
  - Runs everything in **Fast Mode**, plus:
  - `clang-tidy` & Clang Static Analyzer (0 diagnostics).
  - MSVC `/analyze` static analysis (0 warnings).
  - C++ Release x64 build and test suites.
  - CPack portable ZIP archive generation.

### 2. Direct CMake & Frontend Commands

```powershell
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

Run `tools/package.ps1` to produce a clean Release portable archive in `dist/`:

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
