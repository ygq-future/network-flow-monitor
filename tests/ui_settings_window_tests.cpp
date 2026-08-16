#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/ui/settings_window.h"

#include <windows.h>

#include <cstddef>
#include <iostream>

namespace {

class FakeSettingsHost final : public nfm::ui::SettingsWindowHost {
  public:
    [[nodiscard]] nfm::ui::SettingsSaveResult SaveSettings(
        const nfm::config::AppConfig& value) noexcept override {
        saved = value;
        return {.saved = true};
    }

    [[nodiscard]] nfm::ui::SettingsActionResult ExecuteServiceAction(
        nfm::ui::SettingsServiceAction action, nfm::config::ServiceStartType) noexcept override {
        if (action == nfm::ui::SettingsServiceAction::Query) {
            ++query_calls;
        }
        return {.succeeded = true, .state = nfm::service::ServiceState::Running};
    }

    nfm::config::AppConfig saved{};
    std::size_t query_calls{};
};

} // namespace

int main() {
    FakeSettingsHost host;
    nfm::ui::SettingsWindow window{::GetModuleHandleW(nullptr), host};
    auto original = nfm::config::DefaultAppConfig();
    original.appearance.language = nfm::config::LanguageMode::SimplifiedChinese;
    original.appearance.theme = nfm::config::ThemeMode::Dark;
    original.floating.nfm_pinned = true;
    original.floating.blur_strength_percent = 73U;
    original.floating.yield_topmost_when_fullscreen = false;
    original.floating.anchor_monitor_id[0] = 'X';
    original.floating.anchor_side = nfm::config::DockSide::Left;
    original.floating.anchor_vertical_offset_dip = 123;
    const auto shown = window.Show(nullptr, original);
    if (!shown || window.hwnd() == nullptr) {
        std::cerr << "settings window creation failed\n";
        return 1;
    }
    if (host.query_calls != 1U) {
        std::cerr << "settings window did not query service status\n";
        return 1;
    }
    constexpr auto kSaveControlId = 117;
    ::SendMessageW(window.hwnd(), WM_COMMAND, MAKEWPARAM(kSaveControlId, BN_CLICKED), 0);
    if (host.saved.appearance != original.appearance ||
        host.saved.floating.nfm_pinned != original.floating.nfm_pinned ||
        host.saved.floating.blur_strength_percent != original.floating.blur_strength_percent ||
        host.saved.floating.yield_topmost_when_fullscreen !=
            original.floating.yield_topmost_when_fullscreen ||
        host.saved.floating.anchor_monitor_id != original.floating.anchor_monitor_id ||
        host.saved.floating.anchor_side != original.floating.anchor_side ||
        host.saved.floating.anchor_vertical_offset_dip !=
            original.floating.anchor_vertical_offset_dip) {
        std::cerr << "settings save did not preserve hidden ticket 01 state\n";
        return 1;
    }

    window.Close();
    if (window.hwnd() != nullptr) {
        std::cerr << "settings window did not close\n";
        return 1;
    }

    std::cout << "settings window smoke passed\n";
    return 0;
}
