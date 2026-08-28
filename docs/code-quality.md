# Code Quality

`tools/quality.ps1` is the authoritative local and CI entry point.

## Fast gate

- clang-format repository check.
- Strict C++ Debug x64 build.
- Core, configuration, Collector and Protocol V1 tests.
- clang-tidy and enabled Clang Static Analyzer checks.
- When `apps/nfm-ui/package.json` exists: Bun deterministic install validation, TypeScript typecheck, ESLint, Prettier check, Vite production build and registered frontend tests.

## Full gate

- Everything in Fast.
- MSVC `/analyze`.
- Strict C++ Release x64 build and tests.
- Clean production frontend build when present.
- Portable staging verification for the implemented runtime components.

## Headless invariant

Fast and Full display no HWND, WebView, context menu or UAC prompt; start no development server; move no pointer; require no human input.

## Deprecation & API Freshness Contract

The repository enforces a strict deprecation and freshness policy across all layers:

1. **General Principle**:
   - `deprecated -> actionable finding -> migrate to supported replacement`.
   - Blanket suppression (`ignoreDeprecations`, `_CRT_SECURE_NO_WARNINGS` on first-party code, global warning disables) is strictly forbidden.

2. **TypeScript & Toolchain Freshness**:
   - `tsconfig.json` adheres to the installed TypeScript major version. Deprecated compiler options like `baseUrl` are removed in favor of modern relative path mappings (`"@/*": ["./src/*"]`).
   - `ignoreDeprecations` is forbidden.
   - ESLint enables type-aware `@typescript-eslint/no-deprecated` to surface deprecated source APIs.
   - React, Vite, Tailwind CSS, Prettier, and Bun toolchains must follow their currently installed versions without legacy compatibility shims.

3. **C++ & Windows SDK / WebView2 Freshness**:
   - C++ compiler and SDK deprecation warnings are actionable findings requiring migration rather than global silencing.
   - WebView2 integrations target APIs in the repository's active WebView2 SDK version.

4. **Quality Gate Deprecation Policy**:
   - Fast gate starts with an authoritative `Deprecation policy` check on configuration files.
   - Deprecation diagnostics from compilers, linters, and static analyzers are never filtered out in compact mode.

## Structure and maintainability gate

- Product rules, limits, timings, geometry, protocol identifiers and theme values have one named owner and are not repeated as unexplained literals.
- Shared constants expose only cross-feature values. Configuration defaults/ranges, protocol constants, design tokens and feature constants remain in their domain modules.
- Time, DIP, bytes and rates use strong types or unit-bearing names at interfaces.
- Shared utilities are pure, cohesive and used by multiple consumers; platform behavior stays behind adapters and feature helpers stay local.
- Repeated validation, conversion, formatting and state-transition logic is consolidated behind the narrowest common interface and tested once at that seam.
- Dependency checks reject React feature-to-feature cycles and C++ adapter dependencies flowing back into domain interfaces.
- Review rejects catch-all `Utils`/`Common` classes, pass-through wrappers and public interfaces that expose WebView2, Win32 or storage implementation details unnecessarily.

## Target-host gate

Windows 11 24H2 x64 interactive acceptance verifies ETW authorization, native surface evidence, native backdrop, mixed DPI, docking, taskbar behavior, full-screen yield, animation quality, WebView2 Runtime handling and clean Collector shutdown.

## C++ order

1. clang-format changed C/C++ files.
2. repository format check.
3. Debug build and tests.
4. clang-tidy/Clang Static Analyzer.
5. MSVC `/analyze` for Full.
6. Release build and tests for Full.

No quality result grants permission to create a Git commit.
