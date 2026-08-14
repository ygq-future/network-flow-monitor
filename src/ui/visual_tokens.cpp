#include "network_flow_monitor/ui/visual_tokens.h"

namespace nfm::ui {
namespace {

struct Palette {
    std::uint32_t background;
    std::uint32_t text;
    std::uint32_t accent;
    std::uint32_t border;
};

// Windows 11 Fluent palettes. Light uses a clean near-white surface (not gray),
// dark uses a neutral charcoal surface (not navy blue), with the system accent
// pair. High contrast uses the standard black/white/yellow accessibility palette.
constexpr Palette kDarkPalette{0x202020U, 0xFFFFFFU, 0x4CC2FFU, 0x383838U};
constexpr Palette kLightPalette{0xFFFFFFU, 0x1B1B1BU, 0x0067C0U, 0xE0E0E0U};
constexpr Palette kHighContrastPalette{0x000000U, 0xFFFFFFU, 0xFFFF00U, 0xFFFFFFU};

[[nodiscard]] const Palette& PaletteFor(ResolvedTheme theme, bool high_contrast) noexcept {
    if (high_contrast) {
        return kHighContrastPalette;
    }
    return theme == ResolvedTheme::Light ? kLightPalette : kDarkPalette;
}

} // namespace

VisualTokens ResolveVisualTokens(ResolvedTheme theme, bool high_contrast) noexcept {
    const auto& palette = PaletteFor(theme, high_contrast);
    return VisualTokens{.background = palette.background,
                        .text = palette.text,
                        .accent = palette.accent,
                        .border = palette.border,
                        .dark = high_contrast || theme == ResolvedTheme::Dark};
}

std::chrono::milliseconds MotionDuration(std::chrono::milliseconds token,
                                         bool reduced_motion) noexcept {
    return reduced_motion ? std::chrono::milliseconds{0} : token;
}

} // namespace nfm::ui
