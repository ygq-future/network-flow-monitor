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
                .auto_hide_enabled = true,
                .auto_hide_delay = std::chrono::milliseconds{500},
                .opacity_percent = 92U,
                .blur_strength_percent = 60U,
                .capsule_width_dip = 10,
                .capsule_height_dip = 64,
                .nfm_pinned = false,
                .yield_topmost_when_fullscreen = true,
                .anchor_monitor_id = {},
                .anchor_side = DockSide::Right,
                .anchor_vertical_offset_dip = 64,
            },
        .appearance = {.language = LanguageMode::English, .theme = ThemeMode::System},
        .startup = {.ui_at_login = false},
        .diagnostics = {.log_level = LogLevel::Info},
    };
}

} // namespace nfm::config
