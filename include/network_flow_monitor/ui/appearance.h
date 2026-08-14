#ifndef NETWORK_FLOW_MONITOR_UI_APPEARANCE_H
#define NETWORK_FLOW_MONITOR_UI_APPEARANCE_H

#include "network_flow_monitor/config/app_config.h"

#include <cstdint>
#include <string_view>

namespace nfm::ui {

// Resolved runtime values. The Win32 adapter normalizes raw system notifications
// (theme, language, accessibility) into plain inputs and this module produces the
// deterministic result the rest of the UI consumes. No Win32 types or side effects.
enum class ResolvedLanguage : std::uint8_t {
    English,
    SimplifiedChinese,
};

enum class ResolvedTheme : std::uint8_t {
    Light,
    Dark,
};

// Maps a configured language mode plus the normalized primary system language tag
// (for example "en-US", "zh-CN", "de-DE") to a concrete supported language. System
// mode resolves supported tags and falls back to English for anything else,
// including Traditional Chinese which this milestone does not support.
[[nodiscard]] ResolvedLanguage ResolveLanguage(config::LanguageMode mode,
                                               std::string_view system_language_tag) noexcept;

// Maps a configured theme mode plus whether the system currently uses a light theme
// to a concrete light/dark theme. Explicit modes ignore the system preference.
[[nodiscard]] ResolvedTheme ResolveTheme(config::ThemeMode mode,
                                         bool system_uses_light_theme) noexcept;

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_APPEARANCE_H
