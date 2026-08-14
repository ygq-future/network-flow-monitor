#ifndef NETWORK_FLOW_MONITOR_UI_MONITOR_PANEL_LAYOUT_H
#define NETWORK_FLOW_MONITOR_UI_MONITOR_PANEL_LAYOUT_H

#include <cstdint>

namespace nfm::ui {

// Centralized visual tokens for the single monitor panel. These are adjustable
// design tokens (see the Native Win11 UI specification), not performance or
// acceptance thresholds. The panel uses one compact fixed design width; there
// is no user-configurable width and no reserved column for untruncated names.
struct MonitorPanelLayout final {
    static constexpr std::int32_t kWidthDip = 400;
    static constexpr std::int32_t kHeaderRowDip = 36;
    static constexpr std::int32_t kRowDip = 28;
    static constexpr std::int32_t kVerticalPaddingDip = 5;
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_MONITOR_PANEL_LAYOUT_H
