#include "network_flow_monitor/ui/resources.h"

#include <array>
#include <iostream>
#include <string_view>

namespace {
using namespace nfm::ui;

struct Test {
    int failed{};
    void Check(bool value, const char* name) {
        if (!value) {
            std::cerr << "FAIL: " << name << '\n';
            ++failed;
        }
    }
};

void BilingualCoverage(Test& test) {
    const auto keys = ResourceKeys();
    test.Check(!keys.empty(), "resource catalog exposes its keys");
    test.Check(ResourceText(ResourceKey::StatusCollectingData, ResolvedLanguage::English) ==
                   L"Collecting data…",
               "English status uses the approved warm-up wording");
    test.Check(ResourceText(ResourceKey::StatusCollectingData,
                            ResolvedLanguage::SimplifiedChinese) == L"正在采集数据…",
               "Chinese status provides a Simplified Chinese equivalent");
    test.Check(ResourceText(ResourceKey::StatusNoNetworkActivity, ResolvedLanguage::English) !=
                   ResourceText(ResourceKey::StatusCollectingData, ResolvedLanguage::English),
               "warm-up and idle keep distinct English wording");
    test.Check(ResourceText(ResourceKey::SettingsTitle, ResolvedLanguage::English) == L"Settings" &&
                   ResourceText(ResourceKey::SettingsTitle, ResolvedLanguage::SimplifiedChinese) ==
                       L"设置",
               "settings title is localized in both languages");
}

void BrandAndUnits(Test& test) {
    test.Check(BrandName() == L"Network Flow Monitor", "the product brand remains untranslated");
    test.Check(
        ResourceText(ResourceKey::MenuShowHideMonitor, ResolvedLanguage::English) !=
            ResourceText(ResourceKey::MenuShowHideMonitor, ResolvedLanguage::SimplifiedChinese),
        "menu entries are localized");
    test.Check(std::wstring_view{ResourceCString(ResourceKey::Pin,
                                                 ResolvedLanguage::SimplifiedChinese)} == L"固定",
               "null-terminated resource form matches the localized string");
}

void PlaceholderSubstitution(Test& test) {
    const std::array<std::wstring_view, 1> values{L"5"};
    const auto english =
        FormatResource(ResourceKey::ErrorCategoryWin32, ResolvedLanguage::English, values);
    test.Check(english == L"Win32 error 5", "English error category substitutes the numeric code");
    const auto chinese = FormatResource(ResourceKey::ErrorCategoryWin32,
                                        ResolvedLanguage::SimplifiedChinese, values);
    test.Check(chinese == L"Win32 错误 5", "Chinese error category retains the numeric code");
}

void CatalogValidation(Test& test) {
    const auto problems = ValidateResourceCatalog();
    test.Check(problems.empty(), "resource catalog is complete and placeholder-consistent");
    for (const auto key : ResourceKeys()) {
        test.Check(!ResourceText(key, ResolvedLanguage::English).empty() &&
                       !ResourceText(key, ResolvedLanguage::SimplifiedChinese).empty(),
                   "every key is non-empty in both languages");
    }
}
} // namespace

int main() {
    Test test;
    BilingualCoverage(test);
    BrandAndUnits(test);
    PlaceholderSubstitution(test);
    CatalogValidation(test);
    return test.failed == 0 ? 0 : 1;
}
