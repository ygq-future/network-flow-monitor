#ifndef NETWORK_FLOW_MONITOR_UI_DPI_LAYOUT_H
#define NETWORK_FLOW_MONITOR_UI_DPI_LAYOUT_H

#include <cstdint>
#include <limits>

namespace nfm::ui {

struct PixelPoint {
    std::int32_t x{};
    std::int32_t y{};

    friend constexpr bool operator==(PixelPoint lhs, PixelPoint rhs) noexcept = default;
};

struct PixelSize {
    std::int32_t width{};
    std::int32_t height{};

    friend constexpr bool operator==(PixelSize lhs, PixelSize rhs) noexcept = default;
};

struct PixelRect {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    [[nodiscard]] constexpr std::int32_t Width() const noexcept;
    [[nodiscard]] constexpr std::int32_t Height() const noexcept;
    [[nodiscard]] constexpr PixelSize Size() const noexcept;

    friend constexpr bool operator==(PixelRect lhs, PixelRect rhs) noexcept = default;
};

enum class PopupPlacement : std::uint8_t {
    Below,
    Above,
    Right,
    Left
};

struct PopupPlacementResult {
    PixelRect rect{};
    PopupPlacement placement{PopupPlacement::Below};
    bool fully_visible{};
};

[[nodiscard]] double DpiScale(int dpi) noexcept;
[[nodiscard]] std::int32_t DipToPixels(double dip, int dpi) noexcept;
[[nodiscard]] double PixelsToDip(std::int32_t pixels, int dpi) noexcept;

[[nodiscard]] PixelRect ClampRectToWorkArea(PixelRect candidate, PixelRect work_area) noexcept;
[[nodiscard]] PixelRect SnapRectToWorkArea(PixelRect rect, PixelRect work_area,
                                           std::int32_t distance_pixels) noexcept;
[[nodiscard]] PopupPlacementResult ChoosePopupPlacement(PixelRect anchor, PixelSize popup_size,
                                                        PixelRect work_area) noexcept;

} // namespace nfm::ui

constexpr std::int32_t nfm::ui::PixelRect::Width() const noexcept {
    const auto width = static_cast<std::int64_t>(right) - static_cast<std::int64_t>(left);
    if (width <= 0) {
        return 0;
    }
    if (width >= std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(width);
}

constexpr std::int32_t nfm::ui::PixelRect::Height() const noexcept {
    const auto height = static_cast<std::int64_t>(bottom) - static_cast<std::int64_t>(top);
    if (height <= 0) {
        return 0;
    }
    if (height >= std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(height);
}

constexpr nfm::ui::PixelSize nfm::ui::PixelRect::Size() const noexcept {
    return PixelSize{.width = Width(), .height = Height()};
}

#endif // NETWORK_FLOW_MONITOR_UI_DPI_LAYOUT_H
