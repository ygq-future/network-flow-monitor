# Development Setup

## Collector baseline

Required: Windows 11 x64, Visual Studio 2022 C++ workload, CMake 3.28+, Ninja and the LLVM tools used by the repository quality gate.

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

Run `tools/quality.ps1 -Mode Fast` during development and `tools/quality.ps1 -Mode Full` before requesting a commit.

## Frontend and Product Host

Ticket 01 adds Microsoft WebView2 SDK/loader integration, WebView2 Evergreen Runtime, Bun, React, TypeScript, Vite, Tailwind CSS, ESLint, Prettier and `prettier-plugin-tailwindcss`.

Development navigation to Vite is explicit. Production validation always uses static `dist/` assets.
