#include "network_flow_monitor/ui/appearance.h"
#include "network_flow_monitor/ui/visual_tokens.h"

#include <chrono>
#include <iostream>

namespace {
using namespace nfm::ui;
using namespace nfm::config;

struct Test {
    int failed{};
    void Check(bool value, const char* name) {
        if (!value) {
            std::cerr << "FAIL: " << name << '\n';
            ++failed;
        }
    }
};

void LanguageResolution(Test& test) {
    test.Check(ResolveLanguage(LanguageMode::English, "zh-CN") == ResolvedLanguage::English,
               "explicit English ignores the system tag");
    test.Check(ResolveLanguage(LanguageMode::SimplifiedChinese, "de-DE") ==
                   ResolvedLanguage::SimplifiedChinese,
               "explicit Simplified Chinese ignores the system tag");
    test.Check(ResolveLanguage(LanguageMode::System, "en-US") == ResolvedLanguage::English,
               "system mode maps en-US to English");
    test.Check(ResolveLanguage(LanguageMode::System, "zh-CN") ==
                   ResolvedLanguage::SimplifiedChinese,
               "system mode maps zh-CN to Simplified Chinese");
    test.Check(ResolveLanguage(LanguageMode::System, "zh-Hans") ==
                   ResolvedLanguage::SimplifiedChinese,
               "system mode maps zh-Hans to Simplified Chinese");
    test.Check(ResolveLanguage(LanguageMode::System, "zh-SG") ==
                   ResolvedLanguage::SimplifiedChinese,
               "system mode maps zh-SG to Simplified Chinese");
    test.Check(ResolveLanguage(LanguageMode::System, "de-DE") == ResolvedLanguage::English,
               "unsupported system language falls back to English");
    test.Check(ResolveLanguage(LanguageMode::System, "zh-TW") == ResolvedLanguage::English,
               "traditional Chinese is unsupported and falls back to English");
    test.Check(ResolveLanguage(LanguageMode::System, "zh-Hant") == ResolvedLanguage::English,
               "zh-Hant is unsupported and falls back to English");
    test.Check(ResolveLanguage(LanguageMode::System, "") == ResolvedLanguage::English,
               "empty system language falls back to English");
    test.Check(ResolveLanguage(LanguageMode::System, "EN-gb") == ResolvedLanguage::English,
               "language matching is case-insensitive");
}

void ThemeResolution(Test& test) {
    test.Check(ResolveTheme(ThemeMode::System, true) == ResolvedTheme::Light,
               "system light follows the light system preference");
    test.Check(ResolveTheme(ThemeMode::System, false) == ResolvedTheme::Dark,
               "system dark follows the dark system preference");
    test.Check(ResolveTheme(ThemeMode::Light, false) == ResolvedTheme::Light,
               "explicit light ignores a dark system preference");
    test.Check(ResolveTheme(ThemeMode::Dark, true) == ResolvedTheme::Dark,
               "explicit dark ignores a light system preference");
}

void VisualTokenResolution(Test& test) {
    const auto light = ResolveVisualTokens(ResolvedTheme::Light, false);
    const auto dark = ResolveVisualTokens(ResolvedTheme::Dark, false);
    const auto contrast = ResolveVisualTokens(ResolvedTheme::Light, true);
    test.Check(light.background != dark.background && light.text != dark.text,
               "light and dark themes use distinct background and text colors");
    test.Check(contrast.background != light.background && contrast.background != dark.background &&
                   contrast.accent != light.accent && contrast.accent != dark.accent,
               "high contrast overrides the light and dark palettes");
    test.Check(dark.accent == 0x4CC2FFU && dark.background == 0x202020U && dark.dark,
               "dark palette uses the Fluent charcoal surface and light-blue accent");
    test.Check(light.background == 0xFFFFFFU && light.accent == 0x0067C0U && !light.dark,
               "light palette uses a clean white surface and Windows 11 accent");
    test.Check(light.control_feedback == std::chrono::milliseconds{100} &&
                   light.monitor_motion == std::chrono::milliseconds{180} &&
                   light.ranking_row == std::chrono::milliseconds{200} &&
                   light.theme_transition == std::chrono::milliseconds{150} &&
                   light.capsule_rate_interpolation == std::chrono::milliseconds{200},
               "animation duration tokens match the specification");
    test.Check(dark.monitor_motion == light.monitor_motion,
               "both themes share the same animation durations");
    test.Check(MotionDuration(std::chrono::milliseconds{180}, false) ==
                   std::chrono::milliseconds{180},
               "motion duration passes through the token when animations are enabled");
    test.Check(MotionDuration(std::chrono::milliseconds{180}, true) == std::chrono::milliseconds{0},
               "reduced motion completes transitions immediately");
}
} // namespace

int main() {
    Test test;
    LanguageResolution(test);
    ThemeResolution(test);
    VisualTokenResolution(test);
    return test.failed == 0 ? 0 : 1;
}
