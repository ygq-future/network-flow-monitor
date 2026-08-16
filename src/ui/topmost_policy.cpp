#include "network_flow_monitor/ui/topmost_policy.h"

namespace nfm::ui {

bool ShouldStayTopmost(bool yield_when_fullscreen,
                       const ForegroundWindowState& foreground) noexcept {
    if (!yield_when_fullscreen || !foreground.present || !foreground.visible ||
        foreground.minimized || foreground.belongs_to_current_process || foreground.shell_window ||
        foreground.monitor_bounds.Width() <= 0 || foreground.monitor_bounds.Height() <= 0) {
        return true;
    }

    constexpr auto kFrameTolerancePixels = 2;
    return foreground.bounds.left > foreground.monitor_bounds.left + kFrameTolerancePixels ||
           foreground.bounds.top > foreground.monitor_bounds.top + kFrameTolerancePixels ||
           foreground.bounds.right < foreground.monitor_bounds.right - kFrameTolerancePixels ||
           foreground.bounds.bottom < foreground.monitor_bounds.bottom - kFrameTolerancePixels;
}

} // namespace nfm::ui
