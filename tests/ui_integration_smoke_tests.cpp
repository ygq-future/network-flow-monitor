#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/ui/ui_controller.h"

#include <windows.h>

int main() {
    auto config = nfm::config::DefaultAppConfig();
    config.collector.preferred_mode = nfm::config::CollectorMode::Service;
    nfm::ui::UiController controller{::GetModuleHandleW(nullptr), config};
    return controller.Run(true);
}
