#ifndef NETWORK_FLOW_MONITOR_UI_DISPLAY_FORMAT_H
#define NETWORK_FLOW_MONITOR_UI_DISPLAY_FORMAT_H

#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/ranking/ranking_engine.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace nfm::ui {

struct WindowDisplayOptions {
    config::SpeedUnit speed_unit{config::SpeedUnit::Auto};
    bool show_upload{true};
    bool show_download{true};
    std::uint8_t opacity_percent{92U};
    std::uint8_t blur_strength_percent{60U};
    std::uint8_t top_n{5U};

    bool operator==(const WindowDisplayOptions&) const = default;
};

[[nodiscard]] std::wstring Utf8ToWide(std::string_view value);
[[nodiscard]] std::wstring FormatRate(double bytes_per_second, config::SpeedUnit unit);
[[nodiscard]] std::wstring ApplicationLabel(const ranking::RankedApplication& application);
[[nodiscard]] std::wstring FormatApplicationRate(const ranking::RankedApplication& application,
                                                 const WindowDisplayOptions& options);
[[nodiscard]] std::wstring FormatTotalRate(const ranking::TransferRate& rate,
                                           const WindowDisplayOptions& options);

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_DISPLAY_FORMAT_H
