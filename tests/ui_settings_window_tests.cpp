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
    const auto shown = window.Show(nullptr, nfm::config::DefaultAppConfig());
    if (!shown || window.hwnd() == nullptr) {
        std::cerr << "settings window creation failed\n";
        return 1;
    }
    if (host.query_calls != 1U) {
        std::cerr << "settings window did not query service status\n";
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
