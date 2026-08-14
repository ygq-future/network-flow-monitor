#include "network_flow_monitor/ui/resources.h"

#include <array>
#include <cstddef>

namespace nfm::ui {
namespace {

struct Entry {
    ResourceKey key;
    const wchar_t* english;
    const wchar_t* chinese;
};

constexpr std::array kEntries{
    Entry{ResourceKey::StatusCollectorOffline, L"Collector offline", L"采集器离线"},
    Entry{ResourceKey::StatusCollectingData, L"Collecting data…", L"正在采集数据…"},
    Entry{ResourceKey::StatusNoNetworkActivity, L"No network activity", L"无网络活动"},
    Entry{ResourceKey::StatusDataUnavailable, L"Data unavailable", L"数据不可用"},
    Entry{ResourceKey::Pin, L"Pin", L"固定"},
    Entry{ResourceKey::PinOff, L"Pin off", L"固定已禁用"},
    Entry{ResourceKey::Pinned, L"Pinned", L"已固定"},
    Entry{ResourceKey::MenuShowHideMonitor, L"Show/hide monitor", L"显示/隐藏监控面板"},
    Entry{ResourceKey::MenuRetryCollector, L"Retry collector", L"重试采集器"},
    Entry{ResourceKey::MenuSettings, L"Settings", L"设置"},
    Entry{ResourceKey::MenuExit, L"Exit", L"退出"},
    Entry{ResourceKey::SettingsTitle, L"Settings", L"设置"},
    Entry{ResourceKey::ErrorCategoryWin32, L"Win32 error {0}", L"Win32 错误 {0}"},
};

constexpr std::array kKeys{
    ResourceKey::StatusCollectorOffline,
    ResourceKey::StatusCollectingData,
    ResourceKey::StatusNoNetworkActivity,
    ResourceKey::StatusDataUnavailable,
    ResourceKey::Pin,
    ResourceKey::PinOff,
    ResourceKey::Pinned,
    ResourceKey::MenuShowHideMonitor,
    ResourceKey::MenuRetryCollector,
    ResourceKey::MenuSettings,
    ResourceKey::MenuExit,
    ResourceKey::SettingsTitle,
    ResourceKey::ErrorCategoryWin32,
};

const Entry* FindEntry(ResourceKey key) noexcept {
    for (const auto& entry : kEntries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

std::size_t PlaceholderCount(std::wstring_view text) noexcept {
    std::size_t count = 0U;
    for (std::size_t index = 0; index + 2U < text.size(); ++index) {
        if (text[index] == L'{' && text[index + 1] >= L'0' && text[index + 1] <= L'9' &&
            text[index + 2] == L'}') {
            ++count;
        }
    }
    return count;
}

} // namespace

std::span<const ResourceKey> ResourceKeys() noexcept {
    return kKeys;
}

std::wstring_view ResourceText(ResourceKey key, ResolvedLanguage language) noexcept {
    const auto* entry = FindEntry(key);
    if (entry == nullptr) {
        return {};
    }
    return language == ResolvedLanguage::SimplifiedChinese ? std::wstring_view{entry->chinese}
                                                           : std::wstring_view{entry->english};
}

const wchar_t* ResourceCString(ResourceKey key, ResolvedLanguage language) noexcept {
    const auto* entry = FindEntry(key);
    if (entry == nullptr) {
        return L"";
    }
    return language == ResolvedLanguage::SimplifiedChinese ? entry->chinese : entry->english;
}

std::wstring FormatResource(ResourceKey key, ResolvedLanguage language,
                            std::span<const std::wstring_view> values) {
    const auto text = ResourceText(key, language);
    std::wstring result;
    result.reserve(text.size() + 8U);
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == L'{' && index + 2U < text.size() && text[index + 1] >= L'0' &&
            text[index + 1] <= L'9' && text[index + 2] == L'}') {
            const auto position = static_cast<std::size_t>(text[index + 1] - L'0');
            if (position < values.size()) {
                result += values[position];
            } else {
                result.append(text.substr(index, 3U));
            }
            index += 3U;
            continue;
        }
        result += text[index];
        ++index;
    }
    return result;
}

std::vector<ResourceKey> ValidateResourceCatalog() {
    std::vector<ResourceKey> problems;
    for (const auto& entry : kEntries) {
        const auto english = std::wstring_view{entry.english};
        const auto chinese = std::wstring_view{entry.chinese};
        if (english.empty() || chinese.empty() ||
            PlaceholderCount(english) != PlaceholderCount(chinese)) {
            problems.push_back(entry.key);
        }
    }
    return problems;
}

std::wstring_view BrandName() noexcept {
    return L"Network Flow Monitor";
}

} // namespace nfm::ui
