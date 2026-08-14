#ifndef NETWORK_FLOW_MONITOR_UI_MONITOR_PANEL_BEHAVIOR_H
#define NETWORK_FLOW_MONITOR_UI_MONITOR_PANEL_BEHAVIOR_H

#include "network_flow_monitor/ui/dpi_layout.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nfm::ui {

// This is the sole public seam for monitor-panel interaction.  It deliberately
// contains no Win32 types or clock/configuration dependencies.
enum class DockSide : std::uint8_t {
    Left,
    Right
};
enum class MonitorPanelSurface : std::uint8_t {
    Capsule,
    Nfm
};
enum class MonitorPanelEventKind : std::uint8_t {
    Start,
    ShowCapsule,
    ShowNfm,
    CapsulePointerEnter,
    CapsulePointerLeave,
    PanelPointerEnter,
    PanelPointerLeave,
    PanelPress,
    PointerMove,
    PanelRelease,
    HoverDeadline,
    ContextMenuOpened,
    ContextMenuClosed,
    DragCancelled,
    EnvironmentChanged,
    PersistSucceeded,
    PersistFailed,
};
enum class MonitorPanelEffectKind : std::uint8_t {
    PersistAnchor,
    ReportPersistenceFailure
};

struct MonitorWorkArea {
    std::string id;
    PixelRect rect;
    std::uint32_t dpi{96};
    bool primary{};
    bool auto_hide_reveal_on_left{};
    bool auto_hide_reveal_on_right{};
};

struct MonitorPanelEnvironment {
    std::vector<MonitorWorkArea> monitors;
};
struct MonitorPanelConfig {
    std::chrono::milliseconds hover_delay{350};
    std::int32_t capsule_width_dip{10};
    std::int32_t capsule_height_dip{64};
    std::int32_t minimum_drag_distance_x_pixels{4};
    std::int32_t minimum_drag_distance_y_pixels{4};
    std::chrono::milliseconds auto_hide_delay{500};
    bool auto_hide_enabled{true};
    bool pinned{};
};
struct PersistedMonitorAnchor {
    std::string monitor_id;
    DockSide side{DockSide::Right};
    std::int32_t vertical_offset_dip{};

    bool operator==(const PersistedMonitorAnchor&) const = default;
};
struct MonitorPanelEvent {
    MonitorPanelEventKind kind{};
    PixelPoint point{};
    std::chrono::milliseconds elapsed{};
    std::optional<PersistedMonitorAnchor> anchor;
};
struct CapsuleTrack {
    float fill{};
    // RGB is intentionally presentation data, not a renderer resource.
    std::uint32_t color{};
};
struct CapsulePresentation {
    CapsuleTrack download;
    CapsuleTrack upload;
};
struct MonitorPanelState {
    MonitorPanelSurface surface{MonitorPanelSurface::Capsule};
    PersistedMonitorAnchor anchor{};
    PixelRect bounds{};
    PixelPoint press_point{};
    PixelPoint press_offset{};
    PersistedMonitorAnchor drag_origin{};
    bool pressing{};
    bool dragging{};
    bool hover_pending{};
    bool auto_hide_pending{};
    bool context_menu_active{};
    bool persistence_failed{};
};
struct MonitorPanelEffect {
    MonitorPanelEffectKind kind{};
    PersistedMonitorAnchor anchor{};
};
struct MonitorPanelTransition {
    MonitorPanelState state{};
    PixelRect target_bounds{};
    PixelRect hit_bounds{};
    std::optional<std::chrono::milliseconds> next_deadline;
    std::vector<MonitorPanelEffect> effects;
};

class MonitorPanelBehavior final {
  public:
    [[nodiscard]] static MonitorPanelTransition Transition(
        const MonitorPanelState& state, const MonitorPanelEvent& event,
        const MonitorPanelEnvironment& environment, const MonitorPanelConfig& config);
    [[nodiscard]] static CapsulePresentation MapCapsuleRates(
        std::uint64_t download_bytes_per_second, std::uint64_t upload_bytes_per_second) noexcept;
};

} // namespace nfm::ui

#endif
