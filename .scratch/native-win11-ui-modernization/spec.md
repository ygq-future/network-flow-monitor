# Native Win11 UI modernization and monitor-panel interaction

Status: ready-for-agent

## Problem Statement

Network Flow Monitor currently exposes its data through visually plain Win32 windows whose layout, terminology, interaction rules, configuration surface, and rendering do not yet form a coherent Windows 11 experience. The current monitor can dock to any work-area edge, its Top application popup can flip to unrelated sides, context menus can race the popup-dismiss timer, and the hidden reveal surface is only a clipped rectangle. The UI also lacks a complete light/dark theme model, bilingual resources, real backdrop materials, application icons, bounded animations, and consistently styled Settings controls.

From the user's perspective, the monitor should behave as one predictable two-level desktop surface: a compact NFM panel for product identity and total rates, with a TopA panel directly below it for per-application rates. It should always settle on the nearest left or right edge, collapse into a useful draggable rate capsule, restore its last location, remain stable while menus are used, and provide explicit controls for TopA visibility and pinning. Its visual language should closely resemble native Windows 11 while retaining the project's small native Win32 footprint.

## Solution

Modernize the existing native Win32, Direct2D, and DirectWrite UI without adopting WinUI 3 or another UI runtime. Replace the current loosely coupled window behavior with a deterministic monitor-panel behavior module behind one high-level seam. Win32 adapters normalize raw window, pointer, timer, display, theme, and data notifications into model events. The behavior module returns the next observable state, layout and animation targets, and explicit effect intents; it never performs Win32 operations, reads the system clock, starts real timers, or accesses configuration storage.

The visible experience consists of an NFM panel, a subordinate TopA panel, and a dock capsule. The monitor panel docks only to the nearest left or right side of the pointer-release monitor. NFM and TopA are independent rounded cards, with TopA fixed below NFM. When vertical space is insufficient, the expanded monitor panel moves by the minimum amount required to remain inside the work area; it never moves TopA to the side or above NFM. When hidden, the monitor becomes a configurable capsule whose upload and download fills approach its center from opposite ends.

The UI supports English, Simplified Chinese, and system-language selection; English is the default. It supports system, light, and dark themes; system theme is the default. NFM and TopA request Acrylic with configurable background opacity, while Settings requests Mica. All application-owned surfaces are rounded, content stays opaque, and readable fallbacks are mandatory when Windows disables composition effects. Settings receives a Windows 11-style information architecture and styled native controls while retaining native keyboard, IME, DPI, accessibility, and selection behavior.

## User Stories

1. As a user, I want the monitor panel to dock only to the left or right work-area edge, so that it never unexpectedly attaches to the top or bottom.
2. As a user, I want the panel to choose the edge nearest my pointer when I finish dragging, so that placement follows the screen on which I released it.
3. As a multi-monitor user, I want the release-point monitor to determine placement, so that a window spanning two displays still settles predictably.
4. As a user, I want the panel to dock after every completed drag regardless of its distance from an edge, so that it cannot remain stranded in the middle of the desktop.
5. As a user, I want vertical movement constrained to the active monitor's work area, so that the panel cannot be dragged behind the taskbar or beyond the top edge.
6. As a user, I want the panel to respect taskbars placed on any screen edge, so that behavior is not based on an assumption that the taskbar is always at the bottom.
7. As a user with an auto-hidden taskbar, I want the capsule to preserve the system's taskbar reveal area, so that the monitor does not prevent taskbar access.
8. As a user, I want the NFM panel to show the Network Flow Monitor icon and brand name, so that the compact surface has a clear identity.
9. As a user, I want the NFM panel to show total upload and download rates, so that I can read overall traffic without opening the application ranking.
10. As a user, I want the TopA panel directly below NFM, so that the two surfaces maintain a stable parent-child relationship.
11. As a user, I want TopA never to appear without NFM, so that the subordinate list is never detached from its context.
12. As a user, I want TopA to stay below NFM rather than flip above or sideways, so that opening it has a consistent spatial meaning.
13. As a user near the bottom of a screen, I want the expanded monitor panel to move upward just enough to fit, so that TopA remains visible without changing direction.
14. As a user, I want the panel to return to its stored vertical anchor after temporary expansion correction, so that opening TopA does not silently move my preferred resting position.
15. As a user on a short display, I want an intrinsically over-tall TopA list to scroll internally, so that every configured application remains reachable.
16. As a keyboard user, I want an over-tall TopA list to support arrows, Page Up, Page Down, Home, and End, so that it is usable without a mouse.
17. As a touchpad or mouse user, I want normal wheel and touchpad scrolling inside TopA, so that the list behaves like a native desktop list.
18. As a user, I want the TopA scrollbar to stay visually quiet until I interact with the list, so that the panel remains compact without hiding functionality.
19. As a user, I want each TopA row to show an application icon and name on the left, so that applications are easy to recognize.
20. As a user, I want each TopA row to show upload and download rates on the right, so that I can compare both traffic directions per application.
21. As a user, I do not want total rates repeated in TopA, so that the hierarchy remains concise and NFM owns the aggregate information.
22. As a user, I want the TopA heading to include my configured N, so that the panel accurately describes how many applications it ranks.
23. As an English-language user, I want the TopA title to use correct singular or plural wording, so that `Top 1 Application` is not rendered with an incorrect plural.
24. As a user, I want unavailable application icons to use a consistent fallback, so that inaccessible, exited, packaged, or system processes do not break the layout.
25. As a user, I want application icons populated without blocking the UI or requesting elevation, so that visual enrichment never harms responsiveness or the permission boundary.
26. As a user, I want the hidden monitor represented by a thin rounded capsule, so that it remains discoverable without looking like a clipped rectangular window.
27. As a user, I want the capsule to have a fixed configured size rather than grow with traffic, so that its position and hit target remain stable.
28. As a user, I want to configure capsule width and height, so that I can balance visibility against desktop obstruction.
29. As a user, I want the capsule to remain easy to target even when visually narrow, so that reducing its width does not make it unusable.
30. As a user, I want upload represented from the capsule bottom toward its center, so that its direction has a stable visual location.
31. As a user, I want download represented from the capsule top toward its center, so that upload and download remain distinguishable in a very narrow surface.
32. As a user, I want Byte, KiB, and MiB rate bands to progress continuously, so that crossing a unit boundary does not reset the fill.
33. As a user, I want exceptional GiB rates to saturate at the MiB endpoint, so that rare values remain bounded without requiring another visual band.
34. As a user, I want upload colored yellow through orange to red and download colored cyan through blue to purple, so that the two directions are visually distinct.
35. As a user who cannot rely on color alone, I want direction encoded by top/bottom position as well as color, so that the capsule remains interpretable.
36. As a user, I want to drag the capsule without first expanding NFM, so that relocating a hidden monitor is quick.
37. As a user, I want movement beyond the Windows drag threshold to cancel the capsule hover action, so that a drag is not mistaken for a request to expand.
38. As a user, I want a capsule click without a drag to reveal NFM immediately, so that deliberate clicks do not wait for a hover timer.
39. As a user, I want an untouched capsule to reveal NFM after the configured hover delay, so that passive discovery still works.
40. As a user, I want a dragged capsule to cross displays and switch sides, so that I can relocate it without first expanding the panel.
41. As a returning user, I want the capsule display, side, and vertical position restored after restart, so that the monitor returns to its last resting place.
42. As a user whose saved monitor is disconnected, I want the capsule restored safely to the primary display, so that it never opens off-screen.
43. As a user, I do not want the panel to jump back automatically when a previously missing display reconnects, so that display changes do not unexpectedly move it.
44. As a user, I want a dedicated TopA toggle on NFM, so that manual expansion is explicit and does not conflict with panel dragging.
45. As a user, I want hover over NFM to open TopA after the configured delay, so that the existing lightweight discovery workflow remains available.
46. As a user, I want dragging or right-clicking NFM not to toggle TopA, so that unrelated actions do not change the hierarchy.
47. As a user, I want TopA to auto-dismiss normally when NFM is not pinned, even if I opened TopA manually, so that the unpinned monitor still clears itself.
48. As a user, I want hover-opened TopA to auto-dismiss when NFM is pinned, so that pinning the aggregate panel does not force the ranking list to occupy the desktop.
49. As a user, I want manually opened TopA to remain visible while NFM is pinned, so that I can deliberately keep the ranking visible.
50. As a user, I want manually persistent TopA to close only when I use its toggle or unpin NFM, so that its fixed state is explicit.
51. As a user, I want a dedicated pin control on NFM, so that I can prevent NFM from returning to the capsule.
52. As a returning user, I want the NFM pin state persisted, so that my choice survives restart.
53. As a returning user with NFM pinned, I want TopA to begin collapsed after restart, so that a large ranking list is not restored unexpectedly.
54. As a user, I want a pinned NFM to remain draggable, so that pinning visibility never disables placement.
55. As a user who globally disables auto-hide, I want NFM to remain visible and the pin control to explain that it is currently unnecessary, so that two visibility controls do not contradict one another.
56. As a user, I want my saved pin choice retained while global auto-hide is disabled, so that re-enabling auto-hide restores my prior preference.
57. As a user, I want TopA dismissal and monitor auto-hide paused while an application context menu is open, so that moving into a native menu does not collapse the surface beneath it.
58. As a user, I want normal pointer and delay behavior restored after the context menu closes, so that the panel remains open only when the pointer is still within its interaction area.
59. As a tray user, I want opening the tray menu not to force the current monitor panel open or closed, so that tray operations do not unexpectedly alter desktop state.
60. As a user, I want TopA and NFM to dismiss through one continuous transition after I leave the unpinned monitor, so that the hierarchy does not pause awkwardly between two timers.
61. As a user, I want returning during a dismiss animation to reverse smoothly from the current visual state, so that animations do not jump or restart.
62. As a user, I want ranking changes to animate from their current positions without queueing obsolete snapshots, so that rapid traffic changes remain current rather than playing history.
63. As a user, I want application names truncated predictably and rate columns stable, so that text and changing numbers do not cause horizontal jitter.
64. As a user, I want rate numbers right-aligned with tabular numerals, so that values remain easy to scan.
65. As a user, I want precise rate text to keep the configured bytes/bits unit behavior, so that UI modernization does not change established data semantics.
66. As a user, I want NFM and TopA rendered as separate rounded cards, so that the two-level structure remains visually clear.
67. As a user, I want all application-owned panels and popup surfaces rounded in the Windows 11 style, so that the UI feels coherent.
68. As a user, I want context menus to retain native Windows behavior and system-provided corners, so that keyboard and accessibility behavior remain reliable.
69. As a user, I want NFM and TopA to combine configurable opacity with Acrylic blur when Windows permits it, so that the monitor resembles the requested Windows Terminal-style material.
70. As a user, I want text and icons to remain fully opaque, so that changing background opacity does not reduce content readability.
71. As a user, I want the monitor to remain readable when Acrylic is unavailable, so that Remote Desktop, virtual machines, battery-saving modes, high contrast, or unsupported hardware do not produce a broken surface.
72. As a user, I want Settings to use Mica rather than the monitor opacity value, so that a long-lived configuration window remains readable and visually stable.
73. As a user, I want the application to follow the Windows light or dark theme by default, so that it matches my desktop.
74. As a user, I want to explicitly select light or dark theme, so that I can override the system preference.
75. As a user, I want open windows to respond to system theme changes immediately when system mode is selected, so that a restart is unnecessary.
76. As a high-contrast user, I want system high contrast to override custom tint and material styling, so that accessibility takes priority over appearance.
77. As a user, I want bounded animations for monitor reveal, hide, TopA transitions, theme changes, control feedback, and rate fills, so that state changes feel fluid without causing perpetual redraw.
78. As a user who disables Windows animations, I want transitions to complete immediately, so that the application respects reduced-motion preferences.
79. As a Remote Desktop user, I want animation and material effects to degrade safely, so that the monitor remains responsive and legible.
80. As a user, I want English, Simplified Chinese, and system-language choices, so that I can select either supported language or defer to Windows.
81. As a new user, I want English selected by default, so that startup behavior matches the approved product default.
82. As a user with an unsupported Windows language, I want system-language mode to fall back to English, so that every surface remains complete.
83. As a user, I want language changes to update all open application surfaces without restarting, so that the setting takes effect immediately.
84. As a user, I want tray menus, NFM, TopA, Settings, permission guidance, diagnostics, and application-authored errors localized, so that language selection is comprehensive.
85. As a user, I want Windows error details to retain the system-provided text, localized category, and numeric error code, so that diagnostics remain both understandable and precise.
86. As a user, I want the `Network Flow Monitor` brand name to remain untranslated, so that window, tray, executable, and documentation identity stay consistent.
87. As a user, I want TopA to distinguish data-window warm-up from an idle connection, so that `Collecting data…` and `No network activity` do not describe the same state.
88. As a user, I want stale TopA rows cleared when collection is unavailable, so that old application traffic is not presented as current.
89. As a user, I want NFM to preserve its visibility and pin state when collection temporarily fails, so that the recovery surface does not disappear during a fault.
90. As a user, I want unavailable rates represented by a clear default/no-data state, so that transient collector or IPC failures have a safe fallback.
91. As a user, I want ranking warm-up to restart after reconnection, so that rates are not computed across unrelated collector instances.
92. As a user, I want Settings to display `Settings` or `设置` in the title area and a visible current-section heading, so that the window is identifiable rather than showing only an icon.
93. As a user, I want Settings organized into General, Monitor, Ranking, Collector, and Diagnostics sections, so that the expanded option set remains navigable.
94. As a user with a narrow Settings window, I want navigation collapsed into a top selector, so that controls remain usable without a separate mobile layout.
95. As a keyboard user, I want Settings controls to retain native focus, tab order, editing, selection, and activation semantics, so that visual restyling does not reduce usability.
96. As an IME user, I want text and numeric inputs to retain native input behavior, so that Fluent styling does not replace proven Win32 editing semantics.
97. As a screen-reader user, I want controls, the pin button, TopA toggle, and dynamic states to expose accessible names and state, so that custom visuals remain operable.
98. As a user, I want visible control states for hover, press, focus, selection, enabled, and disabled, so that styled Settings controls remain understandable.
99. As a user, I want a live capsule-size preview and restore-default action, so that width and height changes are safe to explore.
100. As a user, I want exactly one program configuration beside the executable regardless of collector mode, so that switching between portable and service collection never changes my UI settings.
101. As a user, I want saved UI settings and monitor position to travel with the executable, so that the application's portable behavior remains predictable.
102. As a user, I want a clear diagnostic when the executable directory is not writable, so that configuration failures are not silently redirected elsewhere.
103. As a developer before first release, I want obsolete development schemas replaced rather than migrated, so that implementation is not burdened by compatibility for unreleased data.
104. As a user with a malformed configuration, I want the original invalid file preserved with a diagnostic, so that corruption is not silently destroyed or overwritten.
105. As a user, I want position persistence performed only after a completed drag, so that pointer movement does not cause continuous disk writes.
106. As a user, I want a canceled drag to restore its original anchor without saving, so that incomplete interaction does not alter future placement.
107. As a user, I want the current session to retain a successfully completed move even if persistence fails, so that a storage error does not make the panel jump backward.
108. As a user, I want a diagnosable persistence error after such a failure, so that I know the position may not survive restart.

## Implementation Decisions

- Preserve the accepted native stack: C++20, CMake, Win32, Direct2D, and DirectWrite. Do not introduce WinUI 3, Windows App SDK, Qt, Electron, WPF, WebView, or another large UI runtime.
- Use the domain terms NFM panel, TopA panel, monitor panel, dock capsule, and pin. Do not use the legacy terms floating window, ranking popup, or reveal strip for new product concepts, even if existing implementation types retain those names temporarily during refactoring.
- Introduce one high-level seam for monitor-panel behavior. Its interface accepts normalized events plus explicit environment/configuration inputs and returns a transition containing the next model state, observable presentation/layout and animation targets, and an ordered collection of effect intents.
- Keep the behavior module deterministic and in-process. It must not access HWND, DWM, Windows messages, real timers, the system clock, configuration I/O, the filesystem, monitor APIs, theme APIs, icon APIs, or any other Win32 side effect.
- Supply time explicitly through normalized time/deadline events. Tests choose timestamps or elapsed durations; the model never obtains the current time itself.
- Make Win32 and the UI controller adapters at the seam. They normalize raw window messages, pointer input, drag thresholds, context-menu lifetime, timer completion, display/work-area changes, DPI changes, theme/accessibility changes, collector state, and snapshots into behavior events.
- Have Win32 adapters realize model output: create/show/hide/reposition windows, apply DWM attributes and composition materials, schedule/cancel real timers, request redraw, load icons, and execute persistence or diagnostic effects.
- Express external work as explicit effect intents such as persisting a changed anchor/configuration state or reporting a save failure. The behavior module does not execute an effect and does not assume it succeeds; completion or failure returns as another normalized event when behavior depends on the outcome.
- Keep the external behavior interface small. The module may internally split state transition, docking, layout, rate mapping, animation planning, localization selection, or other responsibilities, but callers and primary tests drive only the highest-level seam. Internal seams remain implementation-private.
- Use a state representation that distinguishes at least capsule, NFM-visible, TopA-visible, context-menu-active, disconnected/no-data, pinned NFM, and manually persistent TopA. Avoid deriving manual persistence solely from current visibility.
- Treat NFM pinning and TopA manual persistence as separate state. NFM pinning prevents NFM from auto-hiding. TopA persists only when it was manually opened while NFM was pinned. Hover-opened TopA remains dismissible.
- When NFM is unpinned, clear TopA manual persistence and restore normal dismiss behavior. When global auto-hide is disabled, NFM remains visible and the pin control is disabled without erasing the stored pin preference.
- Use a dedicated TopA toggle control. Clicking arbitrary NFM content does not toggle TopA because the surface is also a drag target. Pointer motion past the system drag threshold cancels click and hover activation for that press.
- Pause dismiss and auto-hide deadlines while an application context menu is active. After menu closure, recompute pointer target and resume normal delay behavior. Opening the tray menu must not itself reveal or hide the monitor panel.
- Dock only to left or right. Choose the monitor from the pointer release point, choose the nearer left/right work-area edge, retain the prior side on an exact tie, and use the right side for the first unresolved tie.
- Constrain capsule and NFM drag targets to the selected monitor work area. Use work-area data rather than assuming taskbar position. Preserve access to an auto-hidden taskbar reveal edge when it shares the capsule side.
- Persist a stable monitor identifier, left/right side, and vertical offset in DIP after a completed drag. On startup, restore the saved display when present; otherwise use the primary display, clamp the anchor, and persist the valid fallback rather than jumping when the missing display later reconnects.
- Temporary vertical correction for NFM/TopA expansion never changes the persisted anchor. Place TopA below NFM. Move the combined expanded bounds by only the minimum vertical distance needed to fit. If the TopA content itself exceeds available height, constrain its viewport and scroll internally.
- Render NFM and TopA as separate application-owned rounded cards with tokenized spacing. Keep spacing and corner radius visually adjustable during target-host acceptance rather than encoding an irreversible one-off constant throughout rendering code.
- Default NFM content size to approximately 260×92 DIP. Content determines the actual minimum. Do not expose NFM width or height as public settings. Increase height for text scaling or localization rather than shrinking fonts or omitting rate content.
- Default the capsule to 10×64 DIP. Expose width from 8–24 DIP and height from 40–160 DIP. Maintain an inward hit target of at least 16 DIP without increasing the visible width.
- Give the capsule two independent half-height tracks. Upload grows from the bottom to the center; download grows from the top to the center. Each direction uses its own rate value and identical unit thresholds but a distinct palette.
- Map rate continuously across Byte, KiB, and MiB regions on a logarithmic scale. Saturate values at and above the GiB endpoint without adding a fourth visual band. Preserve direction through position in addition to color.
- Load application icons asynchronously through an icon cache keyed by existing application identity metadata. Prefer packaged application or executable icons when available; use a consistent fallback for inaccessible, missing, exited, or unresolved identities. Never block painting or elevate for icon extraction.
- Replace the runtime-drawn product/tray bitmap with an appropriate multi-size icon resource and use it consistently for NFM, Settings, windows, and the tray where Windows requires an icon.
- Use localized resources for every application-authored visible string. Supported language modes are system, English, and Simplified Chinese. Default to English; system mode falls back to English when Windows reports an unsupported language. Apply language changes to open surfaces without restart.
- Keep the `Network Flow Monitor` brand untranslated. Localize headings, controls, tooltips, statuses, permission guidance, diagnostics, and application-authored error categories. Retain Windows-supplied error text together with its numeric error code.
- Format the TopA heading with the configured N and language-appropriate singular/plural behavior. Use stable right-aligned rate columns with tabular numerals and ellipsize application names when space is constrained.
- Support system, light, and dark theme modes, defaulting to system. React live to relevant Windows theme changes in system mode. High contrast overrides custom colors, tint, transparency, and animation choices as needed for accessibility.
- Request Acrylic plus configured background opacity for NFM and TopA in active and inactive states. Keep content fully opaque. Request Mica for Settings and do not apply monitor opacity to Settings.
- Treat composition materials as optional capabilities. When Acrylic or Mica is unavailable or disabled, use a readable same-tint fallback; never make blur a precondition for usable rendering.
- Apply Windows 11 corner preferences to application-owned top-level surfaces and render custom bounded surfaces with matching rounded geometry. Continue using native Windows context menus so the operating system owns their corners, keyboard behavior, theme, and accessibility.
- Retain native HWND semantics for edits, combo boxes, buttons, keyboard focus, selection, and IME. Use subclassing, custom draw, theme attributes, and narrowly scoped custom controls to achieve the new style. A Switch may be custom rendered; text editing and selection behavior must not be rewritten from scratch.
- Give Settings a visible localized window title and current-section heading. Organize it into General, Monitor, Ranking, Collector, and Diagnostics. Collapse narrow navigation to a top selector rather than building a separate responsive/mobile UI.
- Centralize visual tokens for color, typography, spacing, corner radius, control states, and animation. Use Segoe UI Variable when available with an appropriate fallback; do not retain a single undifferentiated 12-point text style for the whole UI.
- Start with 100 ms control feedback, 180 ms NFM/capsule and work-area motion, 200 ms TopA transitions, 150 ms theme transitions, and capsule rate interpolation bounded to 200 ms or the active data refresh period, whichever is shorter. These are adjustable design tokens, not performance acceptance thresholds.
- Use ease-out for reveal and ease-in for dismiss. Interrupted animations continue from their current visual values toward the new target; do not restart or queue stale transitions. When reduced motion or a degraded session applies, complete transitions immediately.
- Update ranking order only on received snapshots. Animate retained rows toward their new positions for no more than the ranking transition token, fade entrants and exits, and retarget from current values when another snapshot arrives rather than queueing historical animations.
- Preserve dirty-only redraw. Animation temporarily schedules frames only while a transition is active. An idle monitor must not run a fixed high-frame-rate redraw loop.
- Expand the versioned `AppConfig` model with language, theme, NFM pin state, capsule width, capsule height, and persisted anchor fields. Remove snap distance and replace the reveal-strip setting with capsule dimensions.
- Keep exactly one configuration document adjacent to the executable for the UI and all collector modes. Collector mode selects only the backend; it never selects another configuration path or scope. Do not silently fall back to LocalAppData or ProgramData.
- Before first release, replace obsolete development schemas with the current schema and defaults rather than implementing a migration chain. A recognized old development schema can be overwritten directly. Malformed, truncated, or otherwise invalid configuration remains preserved with a diagnostic and must not be silently overwritten.
- Save position only after a completed drag and emit a persistence intent rather than writing for every movement. A canceled drag restores the prior anchor. A persistence failure leaves the current session at the new location but surfaces a diagnostic that the position may not survive restart.
- Represent collection loss as a fallback presentation, not an alternate UI product mode. Preserve NFM visibility and pin state, show explicit no-data/default rates, clear TopA rows, prevent TopA opening while disconnected, and rebuild ranking warm-up after reconnection or collector-instance change.
- Display `Collecting data…` during ranking-window warm-up and `No network activity` when connected with no traffic. Provide complete Simplified Chinese equivalents. Do not reuse `Starting` for both states.
- Preserve module boundaries: UI behavior and rendering remain in `ui`; configuration schema, validation, and persistence remain in `config`; ranking calculations remain independently testable in `ranking`; collector and service code do not acquire window logic.

## Testing Decisions

- The primary test surface is the confirmed high-level monitor-panel behavior seam. Tests send normalized events with explicit time and environment inputs, then assert only externally observable next state, presentation/layout and animation targets, and effect intents.
- Good behavior tests do not create HWNDs, call DWM, read the real clock, start real timers, read configuration files, inspect private fields, or assert internal helper-call sequences. They should survive internal decomposition of state, layout, docking, and animation logic.
- Replace overlapping low-level interaction tests when equivalent coverage exists at the high-level seam. Keep focused pure-function tests only when they protect a genuinely reusable mathematical contract such as rate-to-capsule mapping; do not layer duplicate tests merely because old helpers still exist.
- Cover left/right docking for arbitrary drag locations, exact ties, prior-side ties, first-use ties, negative-coordinate displays, release-monitor selection, partial cross-monitor windows, DPI changes, and top/bottom exclusion.
- Cover work-area clamping for all taskbar orientations, capsule and NFM drags, unusually small work areas, auto-hidden taskbar edge reservation, display removal, primary-display fallback, and display reconnection without an unsolicited jump.
- Cover persisted anchors in DIP, completed versus canceled drag, temporary TopA expansion correction, persistence intent emission only after completion, persistence success/failure feedback, and no anchor overwrite from temporary movement.
- Cover capsule press, click, hover, drag-threshold crossing, hover cancellation during drag, cross-display dragging, side switching, configured minimum/maximum dimensions, and the 16-DIP minimum inward hit target.
- Cover the two independent capsule tracks at zero, Byte, KiB, MiB, GiB, above-saturation, threshold boundaries, monotonicity, upload-only, download-only, simultaneous traffic, and high-contrast/reduced-effects presentation selection.
- Cover NFM hidden, visible, pinned, globally non-auto-hidden, disconnected, and transitioning states. Cover persisted pin preference and the rule that startup restores pinned NFM with TopA collapsed.
- Cover TopA opened by hover and by its explicit toggle under pinned and unpinned NFM. Assert manual persistence only for manual opening while pinned, its removal on manual close or unpin, and normal dismissal for all unpinned states.
- Cover pointer transitions between NFM and TopA, leaving the combined monitor panel, dismiss delay, auto-hide delay, re-entry during animation, and retargeting from current visual progress.
- Cover context menus opened from NFM, TopA, and tray; deadlines that would otherwise expire during the nested menu loop; pointer state after menu closure; and the invariant that opening a tray menu does not itself reveal or hide the monitor.
- Cover TopA placement below NFM, minimum vertical correction near the bottom, return to the persisted anchor, extremely short work areas, viewport height constraints, and the absence of above/left/right placement fallbacks.
- Cover ranking row presentation for configured N, stable order input, application-name truncation, fixed rate columns, bytes/bits units, singular/plural English titles, Chinese titles, fallback icons, delayed icon availability, and icon-cache invalidation without blocking the behavior seam.
- Cover warm-up, connected idle, active ranking, disconnect, stale-row removal, reconnect, and collector-instance reset. Do not use a healthy data path to conceal missing no-data behavior.
- Cover system/English/Simplified Chinese selection, unsupported-system-language fallback, runtime switching, resource-key completeness across supported languages, placeholder substitution, and the invariant that the brand and technical rate units remain stable.
- Cover system/light/dark selection, live system-theme changes only in system mode, explicit-theme stability, high-contrast override, Acrylic/Mica capability fallback selection, and opacity applying to backgrounds rather than content.
- Cover animation plans for initial and final values, duration tokens, easing choice, interruption and reversal, retargeting on a new snapshot, no queued obsolete transitions, immediate completion under reduced motion, and absence of animation deadlines in an idle stable state.
- Extend configuration tests for new defaults and legal ranges, removed legacy fields, schema replacement before release, single executable-adjacent path behavior, deterministic serialization, atomic save behavior, malformed-file preservation, and diagnosable write failures.
- Use Win32 integration smoke tests above the adapters to verify that UI controller normalization reaches the behavior seam and that returned targets/effects are realized: window creation/destruction, message dispatch, timer scheduling/cancellation, context-menu lifecycle, DPI notification, theme notification, icon resource loading, Settings creation, and clean shutdown.
- Smoke tests may assert DWM calls are attempted and fallback paths remain usable, but they do not claim that blur, corner quality, animation fluidity, or mixed-DPI placement looks correct.
- Target-host manual acceptance is final for actual Acrylic/Mica appearance and degradation, rounded corners, animation quality, native menu behavior, keyboard/focus/accessibility behavior, multi-monitor and mixed-DPI movement, every taskbar orientation, auto-hidden taskbar access, capsule hit testing, text scaling, English/Chinese layouts, and icon quality.
- Run the repository's unified quality sequence after each implementation issue and the complete Full gate before any authorized commit: formatting, full format check, strict Debug build, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, all tests, Release x64, and packaging.
- Record target-host observations as measurements and evidence rather than inventing CPU, memory, frame-rate, blur-quality, or latency thresholds. Preserve dirty-only redraw as a functional invariant and measure any performance claim before treating it as acceptance.

## Out of Scope

- Portable/service ETW session coexistence, shared-session naming, service action status repair, and any other service coexistence work. That effort begins only after this UI work.
- Runtime verification of one-time no-UAC authorization on a target machine, except that its existing Settings action must remain reachable and visually restyled.
- WinUI 3, Windows App SDK, Qt, Electron, WPF, WebView, XAML Islands, or another new UI framework/runtime.
- Taskbar embedding, Explorer injection, a kernel driver, packet-payload capture, or moving ETW collection into the UI process.
- Replacing native Windows context menus with an application-owned menu implementation.
- Reimplementing edit controls, combo-box selection, IME, or accessibility semantics from first principles.
- Supporting languages other than English and Simplified Chinese in this milestone.
- Translating the `Network Flow Monitor` brand name or standard technical rate-unit abbreviations.
- A separate per-user, per-service, LocalAppData, or ProgramData UI configuration. Collector mode does not select a configuration scope.
- Migration compatibility for unreleased development schemas. This does not waive corrupted-file diagnostics or atomic-save requirements.
- User-configurable NFM panel width/height, arbitrary undocked placement, top/bottom docking, taskbar integration, TopA side/above placement, or persistent TopA expansion across restart.
- Guaranteed Acrylic/Mica blur under every Windows environment. The UI must degrade correctly when the operating system disables composition effects.
- Advertising or enforcing unmeasured performance, footprint, frame-rate, or animation-latency thresholds.
- Full installer, automatic update, Program Files deployment workflow, service binary copying, signing, or upgrade migration work.

## Further Notes

- The existing UI already has useful prior art: a UI controller seam, a deterministic interaction state machine, pure DPI/layout helpers, display-format tests, window-logic tests, Settings and UI integration smoke tests, and dirty-only invalidation. The implementation should deepen and consolidate these behaviors behind the confirmed monitor-panel seam rather than build an unrelated parallel stack.
- Current code behavior that this spec deliberately replaces includes four-edge docking/hiding, multi-direction TopA placement, a runtime-drawn 32×32 tray bitmap, a single Segoe UI Regular text format, hard-coded English strings, flat rectangle rendering, and opacity implemented only as paint alpha.
- Microsoft describes Mica as an opaque material suited to long-lived base surfaces and Acrylic as a semi-transparent blurred material commonly used for transient surfaces. The choice to request Acrylic for the persistent monitor panel is a product decision made to obtain the requested Terminal-like appearance; it requires careful fallback and target-host validation. See [Microsoft system backdrops guidance](https://learn.microsoft.com/en-us/windows/apps/develop/ui/system-backdrops).
- Microsoft documents that Acrylic can be disabled in environments such as Remote Desktop, virtual machines, unsupported hardware, or power-saving conditions. The spec therefore requires graceful degradation rather than promising blur. See [Windows Terminal troubleshooting](https://learn.microsoft.com/en-us/windows/terminal/troubleshooting).
- All public configuration fields require explicit defaults, units, legal ranges, validation, deterministic serialization, and Settings exposure. Time values remain strongly typed or use `std::chrono` across interfaces.
- No Git commit is authorized by publication of this spec. Implementation work must still follow the repository's test-first and quality-gate rules, and commits require explicit user authorization.
