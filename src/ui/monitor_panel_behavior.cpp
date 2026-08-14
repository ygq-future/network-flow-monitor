#include "network_flow_monitor/ui/monitor_panel_behavior.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nfm::ui {
namespace {

[[nodiscard]] const MonitorWorkArea* Primary(const MonitorPanelEnvironment& environment) {
    for (const auto& monitor : environment.monitors) {
        if (monitor.primary) {
            return &monitor;
        }
    }
    return environment.monitors.empty() ? nullptr : &environment.monitors.front();
}

[[nodiscard]] const MonitorWorkArea* MonitorForPoint(const MonitorPanelEnvironment& environment,
                                                     PixelPoint point) {
    for (const auto& monitor : environment.monitors) {
        if (point.x >= monitor.rect.left && point.x < monitor.rect.right &&
            point.y >= monitor.rect.top && point.y < monitor.rect.bottom) {
            return &monitor;
        }
    }
    const MonitorWorkArea* nearest = nullptr;
    std::int64_t nearest_distance = std::numeric_limits<std::int64_t>::max();
    for (const auto& monitor : environment.monitors) {
        const auto dx = point.x < monitor.rect.left
                            ? static_cast<std::int64_t>(monitor.rect.left) - point.x
                        : point.x >= monitor.rect.right
                            ? static_cast<std::int64_t>(point.x) - monitor.rect.right + 1
                            : 0;
        const auto dy = point.y < monitor.rect.top
                            ? static_cast<std::int64_t>(monitor.rect.top) - point.y
                        : point.y >= monitor.rect.bottom
                            ? static_cast<std::int64_t>(point.y) - monitor.rect.bottom + 1
                            : 0;
        const auto distance = (dx * dx) + (dy * dy);
        if (distance < nearest_distance) {
            nearest = &monitor;
            nearest_distance = distance;
        }
    }
    return nearest;
}

[[nodiscard]] const MonitorWorkArea* MonitorForAnchor(const MonitorPanelEnvironment& environment,
                                                      const PersistedMonitorAnchor& anchor) {
    for (const auto& monitor : environment.monitors) {
        if (monitor.id == anchor.monitor_id) {
            return &monitor;
        }
    }
    return Primary(environment);
}

[[nodiscard]] PixelRect BoundsFor(const PersistedMonitorAnchor& anchor,
                                  const MonitorWorkArea& monitor, const MonitorPanelConfig& config,
                                  MonitorPanelSurface surface) noexcept {
    const auto dpi = static_cast<int>(monitor.dpi);
    const auto desired_width = surface == MonitorPanelSurface::Capsule
                                   ? DipToPixels(config.capsule_width_dip, dpi)
                                   : DipToPixels(260, dpi);
    const auto desired_height = surface == MonitorPanelSurface::Capsule
                                    ? DipToPixels(config.capsule_height_dip, dpi)
                                    : DipToPixels(92, dpi);
    const auto reserve_reveal =
        (anchor.side == DockSide::Left && monitor.auto_hide_reveal_on_left) ||
        (anchor.side == DockSide::Right && monitor.auto_hide_reveal_on_right);
    constexpr auto kAutoHideRevealPixels = 2;
    const auto available_width =
        std::max(1, monitor.rect.Width() - (reserve_reveal ? kAutoHideRevealPixels : 0));
    const auto width = std::max(1, std::min(desired_width, available_width));
    const auto height = std::max(1, std::min(desired_height, monitor.rect.Height()));
    const auto maximum_top = std::max(monitor.rect.top, monitor.rect.bottom - height);
    const auto top = std::clamp(monitor.rect.top + DipToPixels(anchor.vertical_offset_dip, dpi),
                                monitor.rect.top, maximum_top);
    auto left = anchor.side == DockSide::Left ? monitor.rect.left : monitor.rect.right - width;
    if (anchor.side == DockSide::Left && monitor.auto_hide_reveal_on_left) {
        left += std::min(kAutoHideRevealPixels, std::max(0, monitor.rect.Width() - width));
    } else if (anchor.side == DockSide::Right && monitor.auto_hide_reveal_on_right) {
        left -= std::min(kAutoHideRevealPixels, std::max(0, monitor.rect.Width() - width));
    }
    return {.left = left, .top = top, .right = left + width, .bottom = top + height};
}

[[nodiscard]] PersistedMonitorAnchor DockAt(const MonitorWorkArea& monitor, PixelPoint release,
                                            PixelPoint press_offset, DockSide previous_side,
                                            const MonitorPanelConfig& config,
                                            MonitorPanelSurface surface) {
    const auto left_distance = static_cast<std::int64_t>(release.x) - monitor.rect.left;
    const auto right_distance = static_cast<std::int64_t>(monitor.rect.right) - release.x;
    auto side = previous_side;
    if (left_distance < right_distance) {
        side = DockSide::Left;
    } else if (right_distance < left_distance) {
        side = DockSide::Right;
    }
    const auto desired_height =
        surface == MonitorPanelSurface::Capsule
            ? DipToPixels(config.capsule_height_dip, static_cast<int>(monitor.dpi))
            : DipToPixels(92, static_cast<int>(monitor.dpi));
    const auto height = std::max(1, std::min(desired_height, monitor.rect.Height()));
    const auto top = std::clamp(release.y - press_offset.y, monitor.rect.top,
                                std::max(monitor.rect.top, monitor.rect.bottom - height));
    return {.monitor_id = monitor.id,
            .side = side,
            .vertical_offset_dip = static_cast<std::int32_t>(
                std::lround(PixelsToDip(top - monitor.rect.top, static_cast<int>(monitor.dpi))))};
}

struct ColorRange {
    std::uint32_t from;
    std::uint32_t to;
};

[[nodiscard]] std::uint32_t InterpolateColor(ColorRange range, float progress) noexcept {
    const auto channel = [progress](std::uint32_t first, std::uint32_t second) {
        return static_cast<std::uint32_t>(
            std::lround(static_cast<float>(first) +
                        ((static_cast<float>(second) - static_cast<float>(first)) * progress)));
    };
    const auto red = channel((range.from >> 16U) & 0xFFU, (range.to >> 16U) & 0xFFU);
    const auto green = channel((range.from >> 8U) & 0xFFU, (range.to >> 8U) & 0xFFU);
    const auto blue = channel(range.from & 0xFFU, range.to & 0xFFU);
    return (red << 16U) | (green << 8U) | blue;
}

[[nodiscard]] CapsuleTrack Track(std::uint64_t bytes, bool download) noexcept {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    const auto fill =
        bytes == 0U ? 0.0F
                    : static_cast<float>(std::clamp(
                          std::log1p(static_cast<double>(bytes)) / std::log1p(kGiB), 0.0, 1.0));
    const auto low = download ? 0x19D8E6U : 0xF4D03FU;
    const auto middle = download ? 0x2878D0U : 0xF39C32U;
    const auto high = download ? 0x8146B8U : 0xD93636U;
    const auto color = fill <= 0.5F
                           ? InterpolateColor({.from = low, .to = middle}, fill * 2.0F)
                           : InterpolateColor({.from = middle, .to = high}, (fill - 0.5F) * 2.0F);
    return {.fill = fill, .color = color};
}

void ApplyStart(MonitorPanelTransition& result, const MonitorPanelEvent& event,
                const MonitorPanelEnvironment& environment, const MonitorPanelConfig& config) {
    const auto requested = event.anchor.value_or(result.state.anchor);
    const auto* const monitor = MonitorForAnchor(environment, requested);
    if (monitor == nullptr) {
        return;
    }
    result.state.anchor = requested;
    result.state.surface = config.pinned ? MonitorPanelSurface::Nfm : MonitorPanelSurface::Capsule;
    if (requested.monitor_id != monitor->id) {
        result.state.anchor.monitor_id = monitor->id;
        result.effects.push_back(
            {.kind = MonitorPanelEffectKind::PersistAnchor, .anchor = result.state.anchor});
    }
}

void ApplyPointerEvent(MonitorPanelTransition& result, const MonitorPanelEvent& event,
                       const MonitorPanelEnvironment& environment,
                       const MonitorPanelConfig& config) {
    if (result.state.context_menu_active &&
        event.kind != MonitorPanelEventKind::ContextMenuClosed &&
        event.kind != MonitorPanelEventKind::ContextMenuOpened) {
        return;
    }
    switch (event.kind) {
    case MonitorPanelEventKind::ShowCapsule:
        result.state.surface = MonitorPanelSurface::Capsule;
        result.state.hover_pending = false;
        result.state.auto_hide_pending = false;
        break;
    case MonitorPanelEventKind::ShowNfm:
        result.state.surface = MonitorPanelSurface::Nfm;
        result.state.hover_pending = false;
        result.state.auto_hide_pending = false;
        break;
    case MonitorPanelEventKind::CapsulePointerEnter:
        result.state.auto_hide_pending = false;
        if (result.state.surface == MonitorPanelSurface::Capsule) {
            result.state.hover_pending = true;
            result.next_deadline = config.hover_delay;
        }
        break;
    case MonitorPanelEventKind::CapsulePointerLeave:
        result.state.hover_pending = false;
        break;
    case MonitorPanelEventKind::PanelPointerEnter:
        result.state.auto_hide_pending = false;
        break;
    case MonitorPanelEventKind::PanelPointerLeave:
        if (result.state.surface == MonitorPanelSurface::Nfm && config.auto_hide_enabled &&
            !config.pinned) {
            result.state.auto_hide_pending = true;
            result.next_deadline = config.auto_hide_delay;
        }
        break;
    case MonitorPanelEventKind::HoverDeadline:
        if (result.state.hover_pending && !result.state.dragging) {
            result.state.surface = MonitorPanelSurface::Nfm;
            result.state.hover_pending = false;
        } else if (result.state.auto_hide_pending && !result.state.dragging) {
            result.state.surface = MonitorPanelSurface::Capsule;
            result.state.auto_hide_pending = false;
        }
        break;
    case MonitorPanelEventKind::ContextMenuOpened:
        result.state.context_menu_active = true;
        result.state.hover_pending = false;
        result.state.auto_hide_pending = false;
        break;
    case MonitorPanelEventKind::ContextMenuClosed: {
        result.state.context_menu_active = false;
        result.state.hover_pending = false;
        result.state.auto_hide_pending = false;
        const auto pointer_inside = event.point.x >= result.state.bounds.left &&
                                    event.point.x < result.state.bounds.right &&
                                    event.point.y >= result.state.bounds.top &&
                                    event.point.y < result.state.bounds.bottom;
        if (!pointer_inside && result.state.surface == MonitorPanelSurface::Nfm &&
            config.auto_hide_enabled && !config.pinned) {
            result.state.auto_hide_pending = true;
            result.next_deadline = config.auto_hide_delay;
        }
        break;
    }
    case MonitorPanelEventKind::PanelPress:
        result.state.pressing = true;
        result.state.press_point = event.point;
        result.state.press_offset = {.x = event.point.x - result.state.bounds.left,
                                     .y = event.point.y - result.state.bounds.top};
        result.state.drag_origin = result.state.anchor;
        break;
    case MonitorPanelEventKind::PointerMove:
        if (result.state.pressing && (std::abs(event.point.x - result.state.press_point.x) >=
                                          config.minimum_drag_distance_x_pixels ||
                                      std::abs(event.point.y - result.state.press_point.y) >=
                                          config.minimum_drag_distance_y_pixels)) {
            result.state.dragging = true;
            result.state.hover_pending = false;
            if (const auto* monitor = MonitorForPoint(environment, event.point);
                monitor != nullptr) {
                result.state.anchor =
                    DockAt(*monitor, event.point, result.state.press_offset,
                           result.state.drag_origin.side, config, result.state.surface);
            }
        }
        break;
    case MonitorPanelEventKind::PanelRelease:
        if (result.state.pressing && !result.state.dragging) {
            result.state.surface = MonitorPanelSurface::Nfm;
        } else if (result.state.dragging) {
            const auto* const release_monitor = MonitorForPoint(environment, event.point);
            if (release_monitor != nullptr) {
                result.state.anchor =
                    DockAt(*release_monitor, event.point, result.state.press_offset,
                           result.state.drag_origin.side, config, result.state.surface);
                result.effects.push_back(
                    {.kind = MonitorPanelEffectKind::PersistAnchor, .anchor = result.state.anchor});
            }
        }
        result.state.pressing = false;
        result.state.dragging = false;
        result.state.hover_pending = false;
        result.state.auto_hide_pending = false;
        break;
    case MonitorPanelEventKind::DragCancelled:
        if (result.state.dragging) {
            result.state.anchor = result.state.drag_origin;
        }
        result.state.pressing = false;
        result.state.dragging = false;
        result.state.hover_pending = false;
        result.state.auto_hide_pending = false;
        break;
    default:
        break;
    }
}
} // namespace

MonitorPanelTransition MonitorPanelBehavior::Transition(const MonitorPanelState& state,
                                                        const MonitorPanelEvent& event,
                                                        const MonitorPanelEnvironment& environment,
                                                        const MonitorPanelConfig& config) {
    MonitorPanelTransition result{
        .state = state, .target_bounds = state.bounds, .hit_bounds = state.bounds};
    if (event.kind == MonitorPanelEventKind::Start) {
        ApplyStart(result, event, environment, config);
    } else if (event.kind == MonitorPanelEventKind::EnvironmentChanged) {
        const auto* const changed_monitor = MonitorForAnchor(environment, result.state.anchor);
        if (changed_monitor != nullptr && result.state.anchor.monitor_id != changed_monitor->id) {
            result.state.anchor.monitor_id = changed_monitor->id;
            result.effects.push_back(
                {.kind = MonitorPanelEffectKind::PersistAnchor, .anchor = result.state.anchor});
        }
    } else if (event.kind == MonitorPanelEventKind::PersistFailed) {
        result.state.persistence_failed = true;
        result.effects.push_back({.kind = MonitorPanelEffectKind::ReportPersistenceFailure,
                                  .anchor = result.state.anchor});
    } else if (event.kind == MonitorPanelEventKind::PersistSucceeded) {
        result.state.persistence_failed = false;
    } else {
        ApplyPointerEvent(result, event, environment, config);
    }
    const auto* const monitor = MonitorForAnchor(environment, result.state.anchor);
    if (monitor != nullptr) {
        result.state.bounds =
            BoundsFor(result.state.anchor, *monitor, config, result.state.surface);
        if (result.state.dragging && event.kind == MonitorPanelEventKind::PointerMove) {
            const auto width = result.state.bounds.Width();
            const auto height = result.state.bounds.Height();
            const auto left =
                std::clamp(event.point.x - result.state.press_offset.x, monitor->rect.left,
                           std::max(monitor->rect.left, monitor->rect.right - width));
            const auto top =
                std::clamp(event.point.y - result.state.press_offset.y, monitor->rect.top,
                           std::max(monitor->rect.top, monitor->rect.bottom - height));
            result.state.bounds = {
                .left = left, .top = top, .right = left + width, .bottom = top + height};
        }
    }
    result.target_bounds = result.state.bounds;
    result.hit_bounds = result.state.bounds;
    if (result.state.surface == MonitorPanelSurface::Capsule && monitor != nullptr) {
        const auto hit_width = DipToPixels(16, static_cast<int>(monitor->dpi));
        if (result.state.anchor.side == DockSide::Left) {
            result.hit_bounds.right =
                std::max(result.hit_bounds.right, result.hit_bounds.left + hit_width);
        } else {
            result.hit_bounds.left =
                std::min(result.hit_bounds.left, result.hit_bounds.right - hit_width);
        }
    }
    return result;
}

CapsulePresentation MonitorPanelBehavior::MapCapsuleRates(
    std::uint64_t download_bytes_per_second, std::uint64_t upload_bytes_per_second) noexcept {
    return {.download = Track(download_bytes_per_second, true),
            .upload = Track(upload_bytes_per_second, false)};
}
} // namespace nfm::ui
