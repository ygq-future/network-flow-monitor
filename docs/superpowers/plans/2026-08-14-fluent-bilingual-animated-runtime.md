# Fluent Bilingual Animated Runtime (Ticket 03) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the single monitor panel, capsule, and existing Settings surfaces with a reusable Windows 11 runtime-visual capability layer: system/light/dark theme, system/English/Simplified-Chinese language, centralized visual tokens, and bounded animation targets emitted by the deterministic behavior seam.

**Architecture:** A new pure, Win32-free `ui` layer resolves theme/language and emits `VisualTokens` + localized resources. The `MonitorPanelBehavior` seam gains an explicit animation target (easing + duration, reduced-motion aware). Win32 adapters (`UiController`, `FloatingWindow`, `RenderResources`, `SettingsWindow`) consume tokens/resources so the renderer no longer hardcodes colors, fonts, or English strings. The `.ico` resource (item 8) and DWM Acrylic/Mica backdrop requests (items 9-10) are deferred to a follow-up because they require asset generation and target-host validation respectively; this plan covers the deterministic, unit-testable runtime backbone.

**Tech Stack:** C++20, CMake, native Win32, Direct2D/DirectWrite, `std::chrono`, the existing high-level `MonitorPanelBehavior` seam.

---

## File Structure

- Create: `include/network_flow_monitor/ui/appearance.h` — pure theme/language resolution (no Win32).
- Create: `src/ui/appearance.cpp` — resolution implementation.
- Create: `include/network_flow_monitor/ui/visual_tokens.h` — resolved color/typography/spacing/corner/animation tokens.
- Create: `src/ui/visual_tokens.cpp` — light/dark/high-contrast token tables.
- Create: `include/network_flow_monitor/ui/resources.h` — bilingual resource catalog + placeholder substitution + validation.
- Create: `src/ui/resources.cpp` — English/Simplified-Chinese tables.
- Modify: `include/network_flow_monitor/ui/monitor_panel_behavior.h` — add `MonitorPanelAnimation`, `Easing`, `reduced_motion` config.
- Modify: `src/ui/monitor_panel_behavior.cpp` — emit animation targets on surface/theme changes.
- Modify: `include/network_flow_monitor/ui/render_resources.h` / `.cpp` — Segoe UI Variable + fallback, tabular rate numerals.
- Modify: `include/network_flow_monitor/ui/floating_window.h` / `.cpp` — consume `VisualTokens` + `ResolvedLanguage`; render status/pin from resources.
- Modify: `include/network_flow_monitor/ui/ui_controller.h` / `.cpp` — resolve appearance, pass tokens/language, react to `WM_SETTINGCHANGE`.
- Modify: `include/network_flow_monitor/ui/settings_window.h` / `.cpp` — language/theme combo controls.
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt` — register new sources/tests.
- Test: `tests/ui_appearance_tests.cpp`, `tests/ui_resources_tests.cpp`, extend `tests/monitor_panel_behavior_tests.cpp`.

---

## Task 1: Theme resolution (`appearance.h/.cpp`)

**Files:**
- Create: `include/network_flow_monitor/ui/appearance.h`
- Create: `src/ui/appearance.cpp`
- Test: `tests/ui_appearance_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1 — Write the failing test.** Cover: `System` follows the system flag; explicit `Light`/`Dark` ignore it; high contrast forces its own signal upstream (tested as a resolved flag). Language: `System` maps `zh-*`→Chinese, `en-*`→English, unknown→English; explicit modes win regardless of system tag.

**Step 2 — Run to verify it fails** (link error: symbols missing).

**Step 3 — Implement** `ResolveTheme(ThemeMode, bool system_light, bool high_contrast)` and `ResolveLanguage(LanguageMode, std::string_view system_language_tag)`.

**Step 4 — Run to verify it passes.**

**Step 5 — Commit:** `feat(ui): add deterministic theme and language resolution`.

---

## Task 2: Visual tokens (`visual_tokens.h/.cpp`)

**Files:** Create header + impl; extend `tests/ui_appearance_tests.cpp`.

Define `struct VisualTokens` with `std::uint32_t` colors (`0xRRGGBB`), text family string, text size DIP, corner radius DIP, and `std::chrono::milliseconds` durations (control feedback 100ms, monitor motion 180ms, ranking row 200ms, theme 150ms, capsule rate interpolation 200ms). `ResolveVisualTokens(ResolvedTheme, bool high_contrast)` returns the palette; high contrast uses the documented system-ish palette.

Test: light vs dark differ on background/text; high-contrast differs from both; durations match the spec tokens; both themes share the same duration values.

---

## Task 3: Bilingual resource catalog (`resources.h/.cpp`)

**Files:** Create header + impl; Test `tests/ui_resources_tests.cpp`.

`enum class ResourceKey` covers the monitor-panel statuses (`CollectorOffline`, `CollectingData`, `NoNetworkActivity`, `DataUnavailable`), pin labels (`Pin`, `PinOff`, `Pinned`), tray menu entries, and Settings title. `ResourceText(key, ResolvedLanguage)` returns the string; `FormatResource(key, language, placeholders...)` substitutes `{0}`/`{1}`. `ValidateResourceCatalog()` returns the set of keys missing per language or with mismatched placeholder counts.

Test: every key present in both languages; brand and rate units stay untranslated (returned by dedicated non-localized accessors); placeholder substitution works and rejects wrong arity; English default for `System`-resolved English.

---

## Task 4: Animation targets at the behavior seam

**Files:** Modify `monitor_panel_behavior.h/.cpp`; extend `tests/monitor_panel_behavior_tests.cpp`.

Add `enum class Easing { Linear, EaseOut, EaseIn }`, `enum class MonitorPanelAnimationKind { None, Reveal, Dismiss, Theme }`, and `struct MonitorPanelAnimation { kind, easing, duration }`. Add `bool reduced_motion{false}` to `MonitorPanelConfig`. Emit `Reveal`/`EaseOut` on Capsule→Panel, `Dismiss`/`EaseIn` on Panel→Capsule, `Theme` on configuration-driven theme change. Reduced motion ⇒ `duration == 0ms`.

Test: reveal uses ease-out with the motion token duration; dismiss uses ease-in; reduced-motion emits zero-duration targets; no animation in the idle stable state.

---

## Task 5: Tokenize the renderer

**Files:** Modify `render_resources.h/.cpp`, `floating_window.h/.cpp`.

`RenderResources` builds text formats with `Segoe UI Variable` (fallback `Segoe UI`) and enables tabular numerals for the rate format. `FloatingWindow` gains `SetAppearance(VisualTokens, ResolvedLanguage)` and uses token colors/radius/family plus resource strings for status and pin labels.

Test: pure helpers only (font family selection, radius/color selection); the HWND paint path is covered by existing smoke tests.

---

## Task 6: Settings language/theme controls

**Files:** Modify `settings_window.h/.cpp`.

Add `kLanguage` and `kTheme` combo controls (`System/English/Simplified Chinese`, `System/Light/Dark`), populate from `config.appearance`, and read back into `AppConfig.appearance`. Keep native combo behavior (no custom control).

---

## Task 7: Wire appearance into the UI controller

**Files:** Modify `ui_controller.h/.cpp`.

`UiController` derives system light/dark from the registry/`DwmGetColorizationColor`-free path, resolves language/theme via `appearance.h`, builds `VisualTokens`, passes them plus `ResolvedLanguage` to `FloatingWindow`, and reacts to `WM_SETTINGCHANGE` (theme) by re-resolving and repainting. `SaveSettings` applies a changed `appearance` immediately.

---

## Final gate

Run `tools/quality.ps1 -Mode Full`, record results in `docs/planning/progress.md`, then commit with `feat(ui): implement fluent bilingual animated runtime (ticket 03)`.

## Deferred (documented, not implemented here)

- Multi-size `.ico` resource + `.rc` replacing the runtime-drawn 32×32 DIB (spec item 8).
- DWM Acrylic/Mica backdrop requests + fallback (spec items 9-10) — requires target-host validation.
- Surface rect-interpolation frame loop in the Win32 adapter (adapter realization of the animation targets emitted by Task 4); reduced-motion immediate completion is honored.
