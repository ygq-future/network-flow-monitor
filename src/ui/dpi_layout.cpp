#include "network_flow_monitor/ui/dpi_layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>

namespace nfm::ui {
namespace {

[[nodiscard]] std::int32_t SaturateToInt32(double value) noexcept {
    if (!std::isfinite(value)) {
        return value < 0.0 ? std::numeric_limits<std::int32_t>::min()
                           : std::numeric_limits<std::int32_t>::max();
    }
    const auto rounded = std::llround(value);
    if (rounded <= std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::min();
    }
    if (rounded >= std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(rounded);
}

[[nodiscard]] bool IsValid(PixelRect rect) noexcept {
    return rect.right >= rect.left && rect.bottom >= rect.top;
}

[[nodiscard]] PixelRect MakeRect(std::int32_t left, std::int32_t top, PixelSize size) noexcept {
    const auto right = static_cast<std::int64_t>(left) + std::max<std::int32_t>(0, size.width);
    const auto bottom = static_cast<std::int64_t>(top) + std::max<std::int32_t>(0, size.height);
    const auto saturate_coordinate = [](std::int64_t value) noexcept {
        if (value <= std::numeric_limits<std::int32_t>::min()) {
            return std::numeric_limits<std::int32_t>::min();
        }
        if (value >= std::numeric_limits<std::int32_t>::max()) {
            return std::numeric_limits<std::int32_t>::max();
        }
        return static_cast<std::int32_t>(value);
    };
    return PixelRect{.left = left,
                     .top = top,
                     .right = saturate_coordinate(right),
                     .bottom = saturate_coordinate(bottom)};
}

[[nodiscard]] std::int64_t IntersectionArea(PixelRect lhs, PixelRect rhs) noexcept {
    const auto left = std::max(lhs.left, rhs.left);
    const auto top = std::max(lhs.top, rhs.top);
    const auto right = std::min(lhs.right, rhs.right);
    const auto bottom = std::min(lhs.bottom, rhs.bottom);
    if (right <= left || bottom <= top) {
        return 0;
    }
    return (static_cast<std::int64_t>(right) - left) * (static_cast<std::int64_t>(bottom) - top);
}

} // namespace

double DpiScale(int dpi) noexcept {
    return static_cast<double>(dpi > 0 ? dpi : 96) / 96.0;
}

std::int32_t DipToPixels(double dip, int dpi) noexcept {
    return SaturateToInt32(dip * DpiScale(dpi));
}

double PixelsToDip(std::int32_t pixels, int dpi) noexcept {
    return static_cast<double>(pixels) / DpiScale(dpi);
}

PixelRect ClampRectToWorkArea(PixelRect candidate, PixelRect work_area) noexcept {
    if (!IsValid(work_area)) {
        return candidate;
    }

    const auto work_width = work_area.Width();
    const auto work_height = work_area.Height();
    auto width = candidate.Width();
    auto height = candidate.Height();
    if (width > work_width) {
        width = work_width;
    }
    if (height > work_height) {
        height = work_height;
    }

    auto result =
        MakeRect(candidate.left, candidate.top, PixelSize{.width = width, .height = height});
    if (result.left < work_area.left) {
        result.left = work_area.left;
        result.right = static_cast<std::int32_t>(static_cast<std::int64_t>(result.left) + width);
    }
    if (result.top < work_area.top) {
        result.top = work_area.top;
        result.bottom = static_cast<std::int32_t>(static_cast<std::int64_t>(result.top) + height);
    }
    if (result.right > work_area.right) {
        result.right = work_area.right;
        result.left = static_cast<std::int32_t>(static_cast<std::int64_t>(result.right) - width);
    }
    if (result.bottom > work_area.bottom) {
        result.bottom = work_area.bottom;
        result.top = static_cast<std::int32_t>(static_cast<std::int64_t>(result.bottom) - height);
    }
    return result;
}

PixelRect SnapRectToWorkArea(PixelRect rect, PixelRect work_area,
                             std::int32_t distance_pixels) noexcept {
    auto result = ClampRectToWorkArea(rect, work_area);
    const auto distance = std::max<std::int32_t>(0, distance_pixels);
    if (result.left - work_area.left <= distance) {
        const auto width = result.Width();
        result.left = work_area.left;
        result.right = static_cast<std::int32_t>(static_cast<std::int64_t>(result.left) + width);
    }
    if (work_area.right - result.right <= distance) {
        const auto width = result.Width();
        result.right = work_area.right;
        result.left = static_cast<std::int32_t>(static_cast<std::int64_t>(result.right) - width);
    }
    if (result.top - work_area.top <= distance) {
        const auto height = result.Height();
        result.top = work_area.top;
        result.bottom = static_cast<std::int32_t>(static_cast<std::int64_t>(result.top) + height);
    }
    if (work_area.bottom - result.bottom <= distance) {
        const auto height = result.Height();
        result.bottom = work_area.bottom;
        result.top = static_cast<std::int32_t>(static_cast<std::int64_t>(result.bottom) - height);
    }
    return ClampRectToWorkArea(result, work_area);
}

PopupPlacementResult ChoosePopupPlacement(PixelRect anchor, PixelSize popup_size,
                                          PixelRect work_area) noexcept {
    const PixelSize size{.width = std::max<std::int32_t>(0, popup_size.width),
                         .height = std::max<std::int32_t>(0, popup_size.height)};
    const std::array placements = {PopupPlacement::Below, PopupPlacement::Above,
                                   PopupPlacement::Right, PopupPlacement::Left};

    const auto candidate_for = [&](PopupPlacement placement) noexcept {
        switch (placement) {
        case PopupPlacement::Above:
            return MakeRect(
                anchor.left,
                static_cast<std::int32_t>(static_cast<std::int64_t>(anchor.top) - size.height),
                size);
        case PopupPlacement::Right:
            return MakeRect(anchor.right, anchor.top, size);
        case PopupPlacement::Left:
            return MakeRect(
                static_cast<std::int32_t>(static_cast<std::int64_t>(anchor.left) - size.width),
                anchor.top, size);
        case PopupPlacement::Below:
        default:
            return MakeRect(anchor.left, anchor.bottom, size);
        }
    };

    PopupPlacementResult best{.rect = ClampRectToWorkArea(candidate_for(placements[0]), work_area),
                              .placement = placements[0],
                              .fully_visible = false};
    auto best_area = IntersectionArea(best.rect, work_area);
    for (const auto placement : placements) {
        const auto candidate = candidate_for(placement);
        if (IsValid(work_area) && candidate.left >= work_area.left &&
            candidate.top >= work_area.top && candidate.right <= work_area.right &&
            candidate.bottom <= work_area.bottom) {
            return PopupPlacementResult{
                .rect = candidate, .placement = placement, .fully_visible = true};
        }
        const auto clamped = ClampRectToWorkArea(candidate, work_area);
        const auto area = IntersectionArea(clamped, work_area);
        if (area > best_area) {
            best = PopupPlacementResult{
                .rect = clamped, .placement = placement, .fully_visible = false};
            best_area = area;
        }
    }
    return best;
}

} // namespace nfm::ui
