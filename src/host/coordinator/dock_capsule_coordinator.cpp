#include "network_flow_monitor/host/coordinator/dock_capsule_coordinator.h"

#include <cmath>

namespace nfm::host::coordinator {

namespace {

MonitorArea make_default_monitor() {
    return MonitorArea{
        .monitor_id = "primary",
        .left_px = 0,
        .top_px = 0,
        .right_px = 1920,
        .bottom_px = 1080,
        .work_left_px = 0,
        .work_top_px = 0,
        .work_right_px = 1920,
        .work_bottom_px = 1040,
        .is_primary = true,
        .dpi = 96,
    };
}

} // namespace

DockCapsuleCoordinator::DockCapsuleCoordinator(config::AppConfig config,
                                               std::vector<MonitorArea> monitors)
    : config_(config), monitors_(std::move(monitors)) {
    if (monitors_.empty()) {
        monitors_.push_back(make_default_monitor());
    }

    state_.presentation_state = PresentationState::CapsuleVisible;
    state_.is_pinned = config_.floating.nfm_pinned;
    state_.is_topmost = true;
    state_.anchor_side = config_.floating.anchor_side;
    state_.anchor_vertical_offset_dip = config_.floating.anchor_vertical_offset_dip;
    state_.anchor_monitor_id = config_.floating.anchor_monitor_id.data();

    if (state_.is_pinned) {
        state_.presentation_state = PresentationState::PanelVisible;
    } else if (config_.floating.auto_hide_enabled) {
        state_.is_auto_hide_timer_active = true;
        auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
    }

    recalculate_geometry();
}

const MonitorArea* DockCapsuleCoordinator::active_monitor() const {
    return &resolve_active_monitor();
}

const MonitorArea& DockCapsuleCoordinator::resolve_active_monitor() const {
    if (monitors_.empty()) {
        static const MonitorArea kFallback = make_default_monitor();
        return kFallback;
    }

    if (!state_.anchor_monitor_id.empty()) {
        for (const auto& mon : monitors_) {
            if (mon.monitor_id == state_.anchor_monitor_id) {
                return mon;
            }
        }
    }

    // Fallback to primary monitor
    for (const auto& mon : monitors_) {
        if (mon.is_primary) {
            return mon;
        }
    }
    return monitors_.front();
}

const MonitorArea& DockCapsuleCoordinator::find_monitor_for_point(ScreenPoint pt) const {
    if (monitors_.empty()) {
        static const MonitorArea kFallback = make_default_monitor();
        return kFallback;
    }

    for (const auto& mon : monitors_) {
        if (pt.x_px >= mon.left_px && pt.x_px < mon.right_px && pt.y_px >= mon.top_px &&
            pt.y_px < mon.bottom_px) {
            return mon;
        }
    }

    // If point is outside all monitors, find closest monitor by distance to center
    double min_dist_sq = 1e18;
    const MonitorArea* best_mon = &monitors_.front();
    for (const auto& mon : monitors_) {
        const double center_x = static_cast<double>(mon.left_px + mon.right_px) / 2.0;
        const double center_y = static_cast<double>(mon.top_px + mon.bottom_px) / 2.0;
        const double dx = static_cast<double>(pt.x_px) - center_x;
        const double dy = static_cast<double>(pt.y_px) - center_y;
        const double dist_sq = (dx * dx) + (dy * dy);
        if (dist_sq < min_dist_sq) {
            min_dist_sq = dist_sq;
            best_mon = &mon;
        }
    }
    return *best_mon;
}

void DockCapsuleCoordinator::update_config(const config::AppConfig& new_config) {
    config_ = new_config;
    state_.is_pinned = config_.floating.nfm_pinned;
    state_.anchor_side = config_.floating.anchor_side;
    state_.anchor_vertical_offset_dip = config_.floating.anchor_vertical_offset_dip;
    state_.anchor_monitor_id = config_.floating.anchor_monitor_id.data();

    if (state_.is_pinned && state_.presentation_state == PresentationState::CapsuleVisible) {
        state_.presentation_state = PresentationState::PanelVisible;
    }
    if (!config_.floating.auto_hide_enabled) {
        state_.is_auto_hidden = false;
        state_.is_auto_hide_timer_active = false;
        auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
    } else if (state_.presentation_state == PresentationState::CapsuleVisible &&
               !pointer_in_capsule_ && !state_.is_auto_hidden) {
        state_.is_auto_hide_timer_active = true;
        auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
    }
    set_fullscreen_active_on_same_monitor(fullscreen_active_);
    recalculate_geometry();
}

void DockCapsuleCoordinator::update_monitors(std::vector<MonitorArea> monitors) {
    monitors_ = std::move(monitors);
    if (monitors_.empty()) {
        monitors_.push_back(make_default_monitor());
    }
    recalculate_geometry();
}

void DockCapsuleCoordinator::update_drag_threshold(DragThresholdPx threshold) noexcept {
    drag_threshold_.horizontal_px = std::max(1, threshold.horizontal_px);
    drag_threshold_.vertical_px = std::max(1, threshold.vertical_px);
}

void DockCapsuleCoordinator::set_panel_height_dip(std::int32_t height_dip) {
    const std::int32_t clamped = std::clamp(height_dip, 80, 640);
    if (panel_height_dip_ != clamped) {
        panel_height_dip_ = clamped;
        if (state_.presentation_state == PresentationState::PanelVisible && !state_.is_dragging) {
            recalculate_geometry();
        }
    }
}

void DockCapsuleCoordinator::recalculate_geometry() {
    if (state_.presentation_state == PresentationState::TrayOnly) {
        state_.geometry = WindowGeometry{.x_px = 0, .y_px = 0, .width_px = 0, .height_px = 0};
        return;
    }

    const MonitorArea& mon = resolve_active_monitor();
    const double scale = mon.dpi_scale();

    const auto min_y = mon.work_top_px + mon.auto_hide_top_reservation_px;

    if (state_.presentation_state == PresentationState::CapsuleVisible) {
        // Capsule width with inward hit target (at least 16 DIP)
        const std::int32_t visual_width_dip = config_.floating.capsule_width_dip;
        const std::int32_t hit_width_dip = std::max(visual_width_dip, kInwardHitTargetDip);
        const auto width_px = static_cast<std::int32_t>(std::round(hit_width_dip * scale));
        const auto height_px =
            static_cast<std::int32_t>(std::round(config_.floating.capsule_height_dip * scale));

        const auto max_y =
            std::max(min_y, mon.work_bottom_px - height_px - mon.auto_hide_bottom_reservation_px);
        const auto desired_y =
            mon.work_top_px +
            static_cast<std::int32_t>(std::round(state_.anchor_vertical_offset_dip * scale));
        const auto clamped_y = std::clamp(desired_y, min_y, max_y);

        std::int32_t x = 0;
        if (state_.anchor_side == config::DockSide::Right) {
            x = mon.work_right_px - width_px - mon.auto_hide_right_reservation_px;
        } else {
            x = mon.work_left_px + mon.auto_hide_left_reservation_px;
        }
        state_.geometry = WindowGeometry{
            .x_px = x,
            .y_px = clamped_y,
            .width_px = width_px,
            .height_px = height_px,
        };
    } else if (state_.presentation_state == PresentationState::PanelVisible) {
        const auto width_px = static_cast<std::int32_t>(std::round(kDefaultPanelWidthDip * scale));
        const auto height_px = static_cast<std::int32_t>(std::round(panel_height_dip_ * scale));
        const auto max_y =
            std::max(min_y, mon.work_bottom_px - height_px - mon.auto_hide_bottom_reservation_px);
        const auto desired_y =
            mon.work_top_px +
            static_cast<std::int32_t>(std::round(state_.anchor_vertical_offset_dip * scale));
        const auto clamped_y = std::clamp(desired_y, min_y, max_y);

        std::int32_t x = 0;
        if (state_.anchor_side == config::DockSide::Right) {
            x = mon.work_right_px - width_px - mon.auto_hide_right_reservation_px;
        } else {
            x = mon.work_left_px + mon.auto_hide_left_reservation_px;
        }

        state_.geometry = WindowGeometry{
            .x_px = x,
            .y_px = clamped_y,
            .width_px = width_px,
            .height_px = height_px,
        };
    }
}

void DockCapsuleCoordinator::on_pointer_enter_capsule() {
    pointer_in_capsule_ = true;
    state_.is_auto_hidden = false;
    auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_auto_hide_timer_active = false;
    recalculate_geometry();

    if (state_.presentation_state == PresentationState::CapsuleVisible &&
        !state_.is_context_menu_open) {
        state_.is_hover_timer_active = true;
        hover_timer_elapsed_ = std::chrono::milliseconds{0};
    }

    if (state_.is_dismiss_timer_active) {
        state_.is_dismiss_timer_active = false;
        dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
    }
}

void DockCapsuleCoordinator::on_pointer_leave_capsule() {
    pointer_in_capsule_ = false;
    state_.is_hover_timer_active = false;
    hover_timer_elapsed_ = std::chrono::milliseconds{0};

    if (state_.is_dragging) {
        return;
    }

    if (state_.presentation_state == PresentationState::PanelVisible && !state_.is_pinned &&
        !pointer_in_panel_ && !state_.is_context_menu_open) {
        state_.is_dismiss_timer_active = true;
        dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
    } else if (state_.presentation_state == PresentationState::CapsuleVisible &&
               config_.floating.auto_hide_enabled) {
        state_.is_auto_hide_timer_active = true;
        auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
    }
}

void DockCapsuleCoordinator::on_pointer_enter_panel() {
    pointer_in_panel_ = true;
    state_.is_dismiss_timer_active = false;
    dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
}

void DockCapsuleCoordinator::on_pointer_leave_panel() {
    pointer_in_panel_ = false;
    pointer_in_capsule_ = false;
    if (state_.presentation_state == PresentationState::PanelVisible && !state_.is_pinned &&
        !state_.is_context_menu_open) {
        state_.is_dismiss_timer_active = true;
        dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
    }
}

void DockCapsuleCoordinator::on_pointer_click_capsule() {
    state_.is_hover_timer_active = false;
    hover_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_dismiss_timer_active = false;
    dismiss_timer_elapsed_ = std::chrono::milliseconds{0};

    if (state_.presentation_state == PresentationState::CapsuleVisible) {
        state_.presentation_state = PresentationState::PanelVisible;
        recalculate_geometry();
    }
}

void DockCapsuleCoordinator::on_pointer_down_capsule(ScreenPoint pt) {
    pointer_down_candidate_ = true;
    drag_start_pt_ = pt;
    drag_grab_offset_x_px_ = pt.x_px - state_.geometry.x_px;
    drag_grab_offset_y_px_ = pt.y_px - state_.geometry.y_px;
}

void DockCapsuleCoordinator::on_pointer_move(ScreenPoint pt) {
    if (pointer_down_candidate_ && !state_.is_dragging) {
        const auto dx = std::abs(pt.x_px - drag_start_pt_.x_px);
        const auto dy = std::abs(pt.y_px - drag_start_pt_.y_px);

        if (dx >= drag_threshold_.horizontal_px || dy >= drag_threshold_.vertical_px) {
            state_.is_dragging = true;
            // Cancel hover timer when drag starts
            state_.is_hover_timer_active = false;
            hover_timer_elapsed_ = std::chrono::milliseconds{0};
        }
    }
    if (state_.is_dragging) {
        if (state_.presentation_state == PresentationState::PanelVisible) {
            state_.geometry.x_px = pt.x_px - drag_grab_offset_x_px_;
        } else {
            state_.geometry.x_px = pt.x_px - (state_.geometry.width_px / 2);
        }
        state_.geometry.y_px = pt.y_px - drag_grab_offset_y_px_;
    }
}

void DockCapsuleCoordinator::on_pointer_up(ScreenPoint pt) {
    if (state_.is_dragging) {
        // Complete drag
        state_.is_dragging = false;
        pointer_down_candidate_ = false;

        const MonitorArea& target_mon = find_monitor_for_point(pt);
        const double scale = target_mon.dpi_scale();

        // Left / Right Snapping and Tie-Breaking Rule
        const auto mid_x = target_mon.work_left_px + (target_mon.work_width_px() / 2);
        config::DockSide new_side = config::DockSide::Right;
        if (pt.x_px < mid_x) {
            new_side = config::DockSide::Left;
        } else if (pt.x_px > mid_x) {
            new_side = config::DockSide::Right;
        } else {
            // Tie-break rule: keep existing side; if first time default to Right
            new_side = (state_.anchor_side == config::DockSide::Left) ? config::DockSide::Left
                                                                      : config::DockSide::Right;
        }

        // Clamp vertical position
        const auto min_y = target_mon.work_top_px + target_mon.auto_hide_top_reservation_px;
        const auto active_height_dip =
            (state_.presentation_state == PresentationState::PanelVisible)
                ? panel_height_dip_
                : config_.floating.capsule_height_dip;
        const auto height_px = static_cast<std::int32_t>(std::round(active_height_dip * scale));
        const auto max_y = std::max(min_y, target_mon.work_bottom_px - height_px -
                                               target_mon.auto_hide_bottom_reservation_px);

        const auto raw_y = pt.y_px - drag_grab_offset_y_px_;
        const auto clamped_y = std::clamp(raw_y, min_y, max_y);
        const auto offset_dip = static_cast<std::int32_t>(
            std::round(static_cast<double>(clamped_y - target_mon.work_top_px) / scale));

        state_.anchor_side = new_side;
        state_.anchor_vertical_offset_dip = offset_dip;
        state_.anchor_monitor_id = target_mon.monitor_id;
        state_.anchor_updated = true;

        recalculate_geometry();
    } else if (pointer_down_candidate_) {
        // It was a click (distance below drag threshold)
        pointer_down_candidate_ = false;
        on_pointer_click_capsule();
    }
}

void DockCapsuleCoordinator::on_cancel_drag() {
    state_.is_dragging = false;
    pointer_down_candidate_ = false;
    recalculate_geometry();
}

void DockCapsuleCoordinator::advance_time(std::chrono::milliseconds elapsed) {
    if (state_.is_hover_timer_active) {
        if (state_.is_context_menu_open) {
            state_.is_hover_timer_active = false;
            hover_timer_elapsed_ = std::chrono::milliseconds{0};
        } else {
            hover_timer_elapsed_ += elapsed;
            if (hover_timer_elapsed_ >= config_.interaction.hover_delay) {
                state_.is_hover_timer_active = false;
                hover_timer_elapsed_ = std::chrono::milliseconds{0};
                if (state_.presentation_state == PresentationState::CapsuleVisible) {
                    state_.presentation_state = PresentationState::PanelVisible;
                    recalculate_geometry();
                }
            }
        }
    }

    if (state_.is_auto_hide_timer_active) {
        auto_hide_timer_elapsed_ += elapsed;
        if (auto_hide_timer_elapsed_ >= std::chrono::milliseconds{500}) {
            state_.is_auto_hide_timer_active = false;
            auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
            if (state_.presentation_state == PresentationState::CapsuleVisible &&
                config_.floating.auto_hide_enabled) {
                state_.is_auto_hidden = true;
                recalculate_geometry();
            }
        }
    }

    if (state_.is_dismiss_timer_active) {
        dismiss_timer_elapsed_ += elapsed;
        if (dismiss_timer_elapsed_ >= config_.interaction.dismiss_delay) {
            state_.is_dismiss_timer_active = false;
            dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
            if (state_.presentation_state == PresentationState::PanelVisible && !state_.is_pinned &&
                !state_.is_context_menu_open) {
                state_.presentation_state = PresentationState::CapsuleVisible;
                if (config_.floating.auto_hide_enabled && !pointer_in_capsule_) {
                    state_.is_auto_hide_timer_active = true;
                    auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
                }
                recalculate_geometry();
            }
        }
    }
}

void DockCapsuleCoordinator::show_capsule() {
    state_.is_hover_timer_active = false;
    hover_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_dismiss_timer_active = false;
    dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.presentation_state =
        state_.is_pinned ? PresentationState::PanelVisible : PresentationState::CapsuleVisible;
    if (state_.presentation_state == PresentationState::CapsuleVisible &&
        config_.floating.auto_hide_enabled && !pointer_in_capsule_) {
        state_.is_auto_hide_timer_active = true;
        auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
    }
    recalculate_geometry();
}
void DockCapsuleCoordinator::show_monitor() {
    state_.is_hover_timer_active = false;
    hover_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_dismiss_timer_active = false;
    dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_auto_hide_timer_active = false;
    auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_auto_hidden = false;
    state_.presentation_state = PresentationState::PanelVisible;
    recalculate_geometry();
}

void DockCapsuleCoordinator::hide_monitor() {
    state_.presentation_state = PresentationState::TrayOnly;
    recalculate_geometry();
}

void DockCapsuleCoordinator::toggle_monitor() {
    if (state_.presentation_state == PresentationState::TrayOnly) {
        state_.presentation_state = PresentationState::CapsuleVisible;
    } else {
        state_.presentation_state = PresentationState::TrayOnly;
    }
    recalculate_geometry();
}

void DockCapsuleCoordinator::set_pin(bool pinned) {
    state_.is_pinned = pinned;
    if (state_.is_pinned) {
        state_.is_dismiss_timer_active = false;
        dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
        state_.presentation_state = PresentationState::PanelVisible;
    } else {
        if (!pointer_in_capsule_ && !pointer_in_panel_ && !state_.is_context_menu_open) {
            state_.is_dismiss_timer_active = true;
            dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
        }
    }
    recalculate_geometry();
}

void DockCapsuleCoordinator::toggle_pin() {
    set_pin(!state_.is_pinned);
}

void DockCapsuleCoordinator::on_context_menu_opened() {
    state_.is_context_menu_open = true;
    state_.is_hover_timer_active = false;
    hover_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_dismiss_timer_active = false;
    dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
    state_.is_auto_hide_timer_active = false;
    auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
}

void DockCapsuleCoordinator::on_context_menu_closed(bool cursor_inside) {
    state_.is_context_menu_open = false;
    state_.is_hover_timer_active = false;
    hover_timer_elapsed_ = std::chrono::milliseconds{0};
    if (!cursor_inside) {
        pointer_in_capsule_ = false;
        pointer_in_panel_ = false;
        if (!state_.is_pinned && state_.presentation_state == PresentationState::PanelVisible) {
            state_.is_dismiss_timer_active = true;
            dismiss_timer_elapsed_ = std::chrono::milliseconds{0};
        } else if (state_.presentation_state == PresentationState::CapsuleVisible &&
                   config_.floating.auto_hide_enabled) {
            state_.is_auto_hide_timer_active = true;
            auto_hide_timer_elapsed_ = std::chrono::milliseconds{0};
        }
    }
}

void DockCapsuleCoordinator::set_fullscreen_active_on_same_monitor(bool active) {
    fullscreen_active_ = active;
    state_.is_topmost = !(config_.floating.yield_topmost_when_fullscreen && fullscreen_active_);
}

} // namespace nfm::host::coordinator
