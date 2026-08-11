#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/ui/settings_window.h"

#include <windows.h>

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
        nfm::ui::SettingsServiceAction, nfm::config::ServiceStartType) noexcept override {
        return {.succeeded = true};
    }

    nfm::config::AppConfig saved{};
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

    window.Close();
    if (window.hwnd() != nullptr) {
        std::cerr << "settings window did not close\n";
        return 1;
    }

    std::cout << "settings window smoke passed\n";
    return 0;
}
