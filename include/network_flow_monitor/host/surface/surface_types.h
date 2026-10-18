#ifndef NETWORK_FLOW_MONITOR_HOST_SURFACE_SURFACE_TYPES_H
#define NETWORK_FLOW_MONITOR_HOST_SURFACE_SURFACE_TYPES_H

#include <cstdint>
#include <string>

namespace nfm::host::surface {

struct MonitorArea {
    std::string monitor_id;
    std::int32_t left_px{0};
    std::int32_t top_px{0};
    std::int32_t right_px{1920};
    std::int32_t bottom_px{1080};
    std::int32_t work_left_px{0};
    std::int32_t work_top_px{0};
    std::int32_t work_right_px{1920};
    std::int32_t work_bottom_px{1040};
    std::int32_t auto_hide_left_reservation_px{0};
    std::int32_t auto_hide_top_reservation_px{0};
    std::int32_t auto_hide_right_reservation_px{0};
    std::int32_t auto_hide_bottom_reservation_px{0};
    bool is_primary{true};
    std::uint32_t dpi{96};

    [[nodiscard]] std::int32_t width_px() const noexcept {
        return right_px - left_px;
    }
    [[nodiscard]] std::int32_t height_px() const noexcept {
        return bottom_px - top_px;
    }
    [[nodiscard]] std::int32_t work_width_px() const noexcept {
        return work_right_px - work_left_px;
    }
    [[nodiscard]] std::int32_t work_height_px() const noexcept {
        return work_bottom_px - work_top_px;
    }
    [[nodiscard]] double dpi_scale() const noexcept {
        return static_cast<double>(dpi) / 96.0;
    }
};

struct ScreenPoint {
    std::int32_t x_px{0};
    std::int32_t y_px{0};
    bool operator==(const ScreenPoint&) const = default;
};

struct DragThresholdPx {
    std::int32_t horizontal_px{4};
    std::int32_t vertical_px{4};
};

struct WindowGeometry {
    std::int32_t x_px{0};
    std::int32_t y_px{0};
    std::int32_t width_px{0};
    std::int32_t height_px{0};
    bool operator==(const WindowGeometry&) const = default;
};

} // namespace nfm::host::surface

#endif // NETWORK_FLOW_MONITOR_HOST_SURFACE_SURFACE_TYPES_H
