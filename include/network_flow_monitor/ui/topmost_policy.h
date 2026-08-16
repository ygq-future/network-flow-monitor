#ifndef NETWORK_FLOW_MONITOR_UI_TOPMOST_POLICY_H
#define NETWORK_FLOW_MONITOR_UI_TOPMOST_POLICY_H

#include "network_flow_monitor/ui/dpi_layout.h"

namespace nfm::ui {

struct ForegroundWindowState {
    bool present{};
    bool visible{};
    bool minimized{};
    bool belongs_to_current_process{};
    bool shell_window{};
    PixelRect bounds{};
    PixelRect monitor_bounds{};
};

// Full-screen detection deliberately uses the monitor rectangle rather than the work area:
// borderless games and capture overlays cover the taskbar as well as the desktop work area.
[[nodiscard]] bool ShouldStayTopmost(bool yield_when_fullscreen,
                                     const ForegroundWindowState& foreground) noexcept;

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_TOPMOST_POLICY_H
