#ifndef NETWORK_FLOW_MONITOR_UI_VISUAL_TOKENS_H
#define NETWORK_FLOW_MONITOR_UI_VISUAL_TOKENS_H

#include "network_flow_monitor/ui/appearance.h"

#include <chrono>
#include <cstdint>

namespace nfm::ui {

// Centralized visual tokens for all application-owned surfaces. Colors are opaque
// 0xRRGGBB values resolved per theme; opacity is applied to the background material
// only, never to text or icons. Durations are adjustable design tokens, not
// performance acceptance thresholds.
struct VisualTokens {
    std::uint32_t background{};
    std::uint32_t text{};
    std::uint32_t accent{};
    std::uint32_t border{};
    // True when the resolved theme is dark, so the Win32 adapter can request the
    // matching immersive dark mode for DWM system backdrops.
    bool dark{};
    // Preferred family; the renderer falls back to Segoe UI when unavailable.
    const wchar_t* text_family{L"Segoe UI Variable"};
    float text_size_dip{12.0F};
    float corner_radius_dip{8.0F};
    std::chrono::milliseconds control_feedback{100};
    std::chrono::milliseconds monitor_motion{180};
    std::chrono::milliseconds ranking_row{200};
    std::chrono::milliseconds theme_transition{150};
    std::chrono::milliseconds capsule_rate_interpolation{200};
};

[[nodiscard]] VisualTokens ResolveVisualTokens(ResolvedTheme theme, bool high_contrast) noexcept;

// A motion transition completes immediately under reduced motion and uses the token
// duration otherwise. Kept as a pure helper so both the behavior model and the
// Win32 adapter select the same duration.
[[nodiscard]] std::chrono::milliseconds MotionDuration(std::chrono::milliseconds token,
                                                       bool reduced_motion) noexcept;

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_VISUAL_TOKENS_H
