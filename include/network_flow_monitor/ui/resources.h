#ifndef NETWORK_FLOW_MONITOR_UI_RESOURCES_H
#define NETWORK_FLOW_MONITOR_UI_RESOURCES_H

#include "network_flow_monitor/ui/appearance.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nfm::ui {

// Application-authored visible strings. Every key must be present in both English
// and Simplified Chinese. The brand and standard rate-unit abbreviations are never
// localized and therefore live outside this catalog.
enum class ResourceKey : std::uint8_t {
    StatusCollectorOffline,
    StatusCollectingData,
    StatusNoNetworkActivity,
    StatusDataUnavailable,
    Pin,
    PinOff,
    Pinned,
    MenuShowHideMonitor,
    MenuRetryCollector,
    MenuSettings,
    MenuExit,
    SettingsTitle,
    ErrorCategoryWin32,
};

// The full key list, used by validation and completeness tests.
[[nodiscard]] std::span<const ResourceKey> ResourceKeys() noexcept;

[[nodiscard]] std::wstring_view ResourceText(ResourceKey key, ResolvedLanguage language) noexcept;

// Null-terminated C-string form of a resource. The catalog stores wide string
// literals, so the returned pointer is valid for the process lifetime and safe to
// pass to Win32 APIs that require a zero-terminated string.
[[nodiscard]] const wchar_t* ResourceCString(ResourceKey key, ResolvedLanguage language) noexcept;

// Substitutes {0}, {1}, ... placeholders with the supplied values. A placeholder
// index outside the supplied range is left verbatim rather than dropped.
[[nodiscard]] std::wstring FormatResource(ResourceKey key, ResolvedLanguage language,
                                          std::span<const std::wstring_view> values);

// Returns every key that is missing from one supported language or whose placeholder
// arity differs between English and Simplified Chinese. Empty when the catalog is
// internally consistent.
[[nodiscard]] std::vector<ResourceKey> ValidateResourceCatalog();

// The untranslated product brand.
[[nodiscard]] std::wstring_view BrandName() noexcept;

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_RESOURCES_H
