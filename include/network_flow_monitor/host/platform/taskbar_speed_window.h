#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_TASKBAR_SPEED_WINDOW_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_TASKBAR_SPEED_WINDOW_H

#include "network_flow_monitor/config/app_config.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace nfm::host::platform {

struct TaskbarRatePair {
    std::uint64_t download_bytes_per_sec{0};
    std::uint64_t upload_bytes_per_sec{0};
};

struct TaskbarFormattedRate {
    std::wstring value;
    std::wstring unit;
};

struct TaskbarLayoutInput {
    RECT taskbar_rect{};
    RECT notify_rect{};
    bool has_notify_rect{false};
    bool taskbar_centered{false};
    bool widgets_enabled{false};
    std::uint32_t dpi{96U};
};

struct TaskbarOverlayPlacement {
    int x_px{0};
    int y_px{0};
    int width_px{0};
    int height_px{0};
    bool valid{false};
    bool placed_on_left{false};
};

class TaskbarSpeedWindow {
  public:
    using ClickCallback = std::function<void()>;
    using ContextMenuCallback = std::function<void(int screen_x, int screen_y)>;

    static constexpr int kDefaultOverlayWidthDip = 74;
    static constexpr int kDefaultOverlayHeightDip = 32;
    static constexpr int kHorizontalMarginDip = 2;

    TaskbarSpeedWindow();
    ~TaskbarSpeedWindow();

    TaskbarSpeedWindow(const TaskbarSpeedWindow&) = delete;
    TaskbarSpeedWindow& operator=(const TaskbarSpeedWindow&) = delete;
    TaskbarSpeedWindow(TaskbarSpeedWindow&&) = delete;
    TaskbarSpeedWindow& operator=(TaskbarSpeedWindow&&) = delete;

    bool create(ClickCallback on_left_click, ContextMenuCallback on_right_click);
    void close() noexcept;

    void set_enabled(bool enabled);
    [[nodiscard]] bool is_enabled() const noexcept {
        return enabled_;
    }

    void update_config(config::SpeedUnit speed_unit, config::ThemeMode theme_mode);
    void update_rates(TaskbarRatePair rates);
    void sync_with_taskbar();
    void set_target_taskbar_for_testing(HWND taskbar_hwnd) noexcept;

    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
    }
    [[nodiscard]] bool contains_screen_point(POINT pt) const noexcept;

    [[nodiscard]] static TaskbarFormattedRate format_rate_parts(std::uint64_t bytes_per_sec,
                                                                config::SpeedUnit unit_mode);
    [[nodiscard]] static std::wstring format_rate_line(std::wstring_view arrow_prefix,
                                                       std::uint64_t bytes_per_sec,
                                                       config::SpeedUnit unit_mode);
    [[nodiscard]] static TaskbarOverlayPlacement compute_placement(
        const TaskbarLayoutInput& input) noexcept;
    [[nodiscard]] static bool is_taskbar_light_theme() noexcept;

  private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    [[nodiscard]] bool resolve_dark_text_mode() const noexcept;
    void sample_and_apply_taskbar_background_color(HWND taskbar_hwnd, const RECT& taskbar_rect,
                                                   bool placed_on_left) noexcept;
    void paint_window(HDC paint_dc) noexcept;

    HWND hwnd_{nullptr};
    HWND attached_parent_hwnd_{nullptr};
    HWND target_taskbar_override_hwnd_{nullptr};
    UINT taskbar_created_msg_{0};

    ClickCallback on_left_click_;
    ContextMenuCallback on_right_click_;

    bool enabled_{false};
    bool visible_{false};
    config::SpeedUnit speed_unit_{config::SpeedUnit::Auto};
    config::ThemeMode theme_mode_{config::ThemeMode::System};
    std::uint64_t download_bytes_per_sec_{0};
    std::uint64_t upload_bytes_per_sec_{0};

    RECT current_relative_bounds_{};
    std::uint32_t current_dpi_{96U};
    COLORREF taskbar_back_color_{RGB(1, 1, 1)};
    std::wstring last_rendered_up_;
    std::wstring last_rendered_down_;
    bool last_rendered_dark_text_{false};
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_TASKBAR_SPEED_WINDOW_H
