#ifndef NETWORK_FLOW_MONITOR_HOST_COORDINATOR_DOCK_CAPSULE_COORDINATOR_H
#define NETWORK_FLOW_MONITOR_HOST_COORDINATOR_DOCK_CAPSULE_COORDINATOR_H

#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/host/surface/surface_types.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nfm::host::coordinator {

enum class PresentationState : std::uint8_t {
    TrayOnly,
    CapsuleVisible,
    PanelVisible,
};

using surface::DragThresholdPx;
using surface::MonitorArea;
using surface::ScreenPoint;
using surface::WindowGeometry;

struct CoordinatorState {
    PresentationState presentation_state{PresentationState::CapsuleVisible};
    WindowGeometry geometry{};
    bool is_topmost{true};
    bool is_pinned{false};
    bool is_dragging{false};
    bool is_auto_hidden{false};
    bool is_context_menu_open{false};
    bool is_hover_timer_active{false};
    bool is_dismiss_timer_active{false};
    bool is_auto_hide_timer_active{false};
    bool is_fullscreen_hidden{false};
    bool is_click_through{false};
    // Configuration updates emitted by interaction
    bool anchor_updated{false};
    bool pin_updated{false};
    config::DockSide anchor_side{config::DockSide::Right};
    std::int32_t anchor_vertical_offset_dip{0};
    std::string anchor_monitor_id;
};

class DockCapsuleCoordinator {
  public:
    static constexpr std::int32_t kDefaultPanelWidthDip = 340;
    static constexpr std::int32_t kDefaultPanelHeightDip = 128;
    static constexpr std::int32_t kInwardHitTargetDip = 16;
    explicit DockCapsuleCoordinator(config::AppConfig config = {},
                                    std::vector<MonitorArea> monitors = {});

    // Configuration updates
    void update_config(const config::AppConfig& new_config);
    void update_monitors(std::vector<MonitorArea> monitors);
    void update_drag_threshold(DragThresholdPx threshold) noexcept;

    void set_panel_height_dip(std::int32_t height_dip);
    // Pointer events
    void on_pointer_enter_capsule();
    void on_pointer_leave_capsule();
    void on_pointer_enter_panel();
    void on_pointer_leave_panel();
    void on_pointer_click_capsule();

    // Dragging events
    void on_pointer_down_capsule(ScreenPoint pt);
    void on_pointer_move(ScreenPoint pt);
    void on_pointer_up(ScreenPoint pt);
    void on_cancel_drag();

    // Timers
    void advance_time(std::chrono::milliseconds elapsed);

    // Visibility & Pin
    void show_capsule();
    void show_monitor();
    void hide_monitor();
    void toggle_monitor();
    void set_pin(bool pinned);
    void toggle_pin();

    // Context Menu
    void on_context_menu_opened();
    void on_context_menu_closed(bool cursor_inside);

    // Fullscreen Topmost Yield
    void set_fullscreen_active_on_same_monitor(bool active);

    // State Inspection
    [[nodiscard]] const CoordinatorState& state() const noexcept {
        return state_;
    }
    [[nodiscard]] const config::AppConfig& config() const noexcept {
        return config_;
    }
    [[nodiscard]] const std::vector<MonitorArea>& monitors() const noexcept {
        return monitors_;
    }
    [[nodiscard]] const MonitorArea* active_monitor() const;

    // Reset pending emitted updates
    void clear_emitted_updates() noexcept {
        state_.anchor_updated = false;
        state_.pin_updated = false;
    }

  private:
    void recalculate_geometry();
    [[nodiscard]] const MonitorArea& find_monitor_for_point(ScreenPoint pt) const;
    [[nodiscard]] const MonitorArea& resolve_active_monitor() const;

    config::AppConfig config_;
    std::vector<MonitorArea> monitors_;
    CoordinatorState state_;

    // Timers tracking
    std::chrono::milliseconds hover_timer_elapsed_{0};
    std::chrono::milliseconds dismiss_timer_elapsed_{0};
    std::chrono::milliseconds auto_hide_timer_elapsed_{0};

    // Drag tracking
    bool pointer_down_candidate_{false};
    ScreenPoint drag_start_pt_{};
    std::int32_t drag_grab_offset_y_px_{0};

    std::int32_t panel_height_dip_{kDefaultPanelHeightDip};
    std::int32_t drag_grab_offset_x_px_{0};
    // Fullscreen state
    bool fullscreen_active_{false};

    // Pointer presence
    bool pointer_in_capsule_{false};
    bool pointer_in_panel_{false};
    DragThresholdPx drag_threshold_{};
};

} // namespace nfm::host::coordinator

#endif // NETWORK_FLOW_MONITOR_HOST_COORDINATOR_DOCK_CAPSULE_COORDINATOR_H
