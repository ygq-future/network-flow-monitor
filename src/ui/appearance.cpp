#include "network_flow_monitor/ui/appearance.h"

#include <algorithm>
#include <cctype>

namespace nfm::ui {
namespace {

[[nodiscard]] std::string_view PrimarySubtag(std::string_view tag) noexcept {
    const auto separator = tag.find('-');
    return tag.substr(0, separator);
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsSimplifiedChineseTag(std::string_view tag) noexcept {
    // Windows reports a BCP-47 tag like "zh", "zh-CN", "zh-Hans", or "zh-TW".
    // Only Simplified Chinese is supported; Traditional Chinese subtags fall through
    // to the English fallback in the caller.
    const auto primary = PrimarySubtag(tag);
    if (!EqualsIgnoreCase(primary, "zh")) {
        return false;
    }
    if (tag.size() == primary.size()) {
        return true;
    }
    const auto remainder = tag.substr(primary.size() + 1U);
    const auto first_subtag = PrimarySubtag(remainder);
    return !EqualsIgnoreCase(first_subtag, "hant") && !EqualsIgnoreCase(first_subtag, "tw") &&
           !EqualsIgnoreCase(first_subtag, "hk") && !EqualsIgnoreCase(first_subtag, "mo");
}

} // namespace

ResolvedLanguage ResolveLanguage(config::LanguageMode mode,
                                 std::string_view system_language_tag) noexcept {
    switch (mode) {
    case config::LanguageMode::English:
        return ResolvedLanguage::English;
    case config::LanguageMode::SimplifiedChinese:
        return ResolvedLanguage::SimplifiedChinese;
    case config::LanguageMode::System:
        if (IsSimplifiedChineseTag(system_language_tag)) {
            return ResolvedLanguage::SimplifiedChinese;
        }
        if (EqualsIgnoreCase(PrimarySubtag(system_language_tag), "en")) {
            return ResolvedLanguage::English;
        }
        return ResolvedLanguage::English;
    }
    return ResolvedLanguage::English;
}

ResolvedTheme ResolveTheme(config::ThemeMode mode, bool system_uses_light_theme) noexcept {
    switch (mode) {
    case config::ThemeMode::Light:
        return ResolvedTheme::Light;
    case config::ThemeMode::Dark:
        return ResolvedTheme::Dark;
    case config::ThemeMode::System:
        return system_uses_light_theme ? ResolvedTheme::Light : ResolvedTheme::Dark;
    }
    return ResolvedTheme::Light;
}

} // namespace nfm::ui
