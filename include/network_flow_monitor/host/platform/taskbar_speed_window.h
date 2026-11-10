#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_TASKBAR_SPEED_WINDOW_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_TASKBAR_SPEED_WINDOW_H

#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/host/platform/system_telemetry_sampler.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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

struct TaskbarMetricCell {
    std::wstring label;
    std::wstring value;
    std::wstring unit;
    bool empty{true};

    bool operator==(const TaskbarMetricCell&) const = default;
};

struct TaskbarColumnPair {
    TaskbarMetricCell top;
    TaskbarMetricCell bottom;
    int width_dip{74};

    bool operator==(const TaskbarColumnPair&) const = default;
};

struct TaskbarTooltipRow {
    std::wstring label;
    std::wstring value;

    bool operator==(const TaskbarTooltipRow&) const = default;
};

struct TaskbarLayoutInput {
    RECT taskbar_rect{};
    RECT notify_rect{};
    bool has_notify_rect{false};
    bool taskbar_centered{false};
    bool widgets_enabled{false};
    std::uint32_t dpi{96U};
    int overlay_width_dip{74};
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
    static constexpr int kHardwareColumnWidthDip = 58;
    static constexpr int kColumnGapDip = 4;
    static constexpr int kDefaultOverlayHeightDip = 32;
    static constexpr int kHorizontalMarginDip = 2;
    // Minimum pointer dwell time before the metrics tooltip is allowed to appear.
    static constexpr std::chrono::milliseconds kTooltipHoverDelay{500};
    static constexpr UINT_PTR kTooltipHoverTimerId = 1;

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

    void update_config(const config::DisplayConfig& display_config, config::ThemeMode theme_mode,
                       config::LanguageMode language_mode = config::LanguageMode::System);
    void update_rates(TaskbarRatePair rates);
    void update_telemetry(const SystemTelemetrySnapshot& telemetry);
    void sync_with_taskbar();
    void set_target_taskbar_for_testing(HWND taskbar_hwnd) noexcept;
    // While a native popup menu is open, clicks on the overlay only dismiss that menu; they must
    // not toggle the monitor panel or open a second menu.
    void set_context_menu_open(bool open) noexcept;

    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
    }
    [[nodiscard]] bool contains_screen_point(POINT pt) const noexcept;
    [[nodiscard]] bool is_tooltip_visible() const noexcept {
        return tooltip_visible_;
    }
    [[nodiscard]] bool is_tooltip_hover_timer_active() const noexcept {
        return tooltip_hover_timer_active_;
    }

    [[nodiscard]] static TaskbarFormattedRate format_rate_parts(std::uint64_t bytes_per_sec,
                                                                config::SpeedUnit unit_mode);
    [[nodiscard]] static std::wstring format_rate_line(std::wstring_view arrow_prefix,
                                                       std::uint64_t bytes_per_sec,
                                                       config::SpeedUnit unit_mode);
    [[nodiscard]] static std::vector<TaskbarColumnPair> build_display_columns(
        const config::DisplayConfig& display_config, TaskbarRatePair rates,
        const SystemTelemetrySnapshot& telemetry);
    [[nodiscard]] static std::vector<TaskbarTooltipRow> build_tooltip_rows(
        const config::DisplayConfig& display_config, TaskbarRatePair rates,
        const SystemTelemetrySnapshot& telemetry, bool is_chinese);
    [[nodiscard]] static int compute_required_width_dip(
        const std::vector<TaskbarColumnPair>& columns) noexcept;
    [[nodiscard]] static TaskbarOverlayPlacement compute_placement(
        const TaskbarLayoutInput& input) noexcept;
    [[nodiscard]] static bool is_taskbar_light_theme() noexcept;

  private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK tooltip_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    [[nodiscard]] bool resolve_dark_text_mode() const noexcept;
    [[nodiscard]] bool resolve_tooltip_light_mode() const noexcept;
    [[nodiscard]] bool resolve_chinese_locale() const noexcept;
    void refresh_if_needed();
    void start_tooltip_hover_timer();
    void cancel_tooltip_hover_timer() noexcept;
    void on_tooltip_hover_timer_fired();
    void show_or_update_tooltip();
    void hide_tooltip() noexcept;
    void sample_and_apply_taskbar_background_color(HWND taskbar_hwnd, const RECT& taskbar_rect,
                                                   bool placed_on_left) noexcept;
    void paint_window(HDC paint_dc) noexcept;
    void paint_tooltip_window(HDC paint_dc) noexcept;

    HWND hwnd_{nullptr};
    HWND tooltip_hwnd_{nullptr};
    HWND attached_parent_hwnd_{nullptr};
    HWND target_taskbar_override_hwnd_{nullptr};
    UINT taskbar_created_msg_{0};

    ClickCallback on_left_click_;
    ContextMenuCallback on_right_click_;

    bool enabled_{false};
    bool visible_{false};
    bool mouse_tracking_{false};
    bool tooltip_visible_{false};
    bool tooltip_hover_timer_active_{false};
    bool context_menu_open_{false};
    bool left_button_down_{false};
    bool right_button_down_{false};
    std::uint64_t suppress_left_click_until_{0};
    std::uint64_t suppress_right_click_until_{0};
    config::DisplayConfig display_config_{};
    config::ThemeMode theme_mode_{config::ThemeMode::System};
    config::LanguageMode language_mode_{config::LanguageMode::System};
    std::uint64_t download_bytes_per_sec_{0};
    std::uint64_t upload_bytes_per_sec_{0};
    SystemTelemetrySnapshot telemetry_{};

    RECT current_relative_bounds_{};
    std::uint32_t current_dpi_{96U};
    COLORREF taskbar_back_color_{RGB(1, 1, 1)};
    std::vector<TaskbarColumnPair> last_rendered_columns_;
    std::vector<TaskbarTooltipRow> last_tooltip_rows_;
    bool last_rendered_dark_text_{false};
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_TASKBAR_SPEED_WINDOW_H
