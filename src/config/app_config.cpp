#include "network_flow_monitor/config/app_config.h"

namespace nfm::config {

AppConfig DefaultAppConfig() noexcept {
    return AppConfig{
        .schema_version = kCurrentSchemaVersion,
        .ranking = {.top_n = 5U, .window = std::chrono::milliseconds{2000}},
        .display =
            {
                .refresh_interval = std::chrono::milliseconds{500},
                .speed_unit = SpeedUnit::Auto,
                .show_upload = true,
                .show_download = true,
            },
        .interaction =
            {
                .hover_delay = std::chrono::milliseconds{350},
                .dismiss_delay = std::chrono::milliseconds{250},
            },
        .floating =
            {
                .snap_distance_dip = 20,
                .auto_hide_enabled = true,
                .auto_hide_delay = std::chrono::milliseconds{500},
                .reveal_strip_dip = 6,
                .opacity_percent = 92U,
            },
        .startup = {.ui_at_login = false},
        .collector = {.preferred_mode = CollectorMode::Portable},
        .service = {.start_type = ServiceStartType::AutomaticDelayed},
        .diagnostics = {.log_level = LogLevel::Info},
    };
}

} // namespace nfm::config
