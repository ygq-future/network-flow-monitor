#include "network_flow_monitor/host/platform/taskbar_speed_window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <dwmapi.h>
#include <string_view>

namespace nfm::host::platform {

namespace {

constexpr wchar_t kTaskbarOverlayClassName[] = L"NetworkFlowMonitor_TaskbarSpeedWindow";
constexpr wchar_t kTaskbarTooltipClassName[] = L"NetworkFlowMonitor_TaskbarTooltipWindow";
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
#else
constexpr DWORD kDwmwaUseImmersiveDarkMode = DWMWA_USE_IMMERSIVE_DARK_MODE;
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kDwmwaWindowCornerPreference = 33;
#else
constexpr DWORD kDwmwaWindowCornerPreference = DWMWA_WINDOW_CORNER_PREFERENCE;
#endif
#ifndef DWMWA_BORDER_COLOR
constexpr DWORD kDwmwaBorderColor = 34;
#else
constexpr DWORD kDwmwaBorderColor = DWMWA_BORDER_COLOR;
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
constexpr DWORD kDwmwaSystemBackdropType = 38;
#else
constexpr DWORD kDwmwaSystemBackdropType = DWMWA_SYSTEMBACKDROP_TYPE;
#endif
constexpr int kDwmwcpRoundSmall = 3;
constexpr int kDwmsbtDisable = 1;
[[nodiscard]] bool is_windows_11_or_greater() noexcept {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    std::array<wchar_t, 32> build_str{};
    auto size = static_cast<DWORD>(build_str.size() * sizeof(wchar_t));
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, &type,
                                            reinterpret_cast<LPBYTE>(build_str.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }
    wchar_t* end_ptr = nullptr;
    const long build_num = std::wcstol(build_str.data(), &end_ptr, 10);
    return build_num >= 22000L;
}

[[nodiscard]] bool is_windows_11_xaml_taskbar(HWND taskbar_hwnd) noexcept {
    if (!is_windows_11_or_greater() || taskbar_hwnd == nullptr) {
        return false;
    }
    return FindWindowExW(taskbar_hwnd, nullptr,
                         L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr) != nullptr;
}

[[nodiscard]] bool query_taskbar_centered(HWND taskbar_hwnd) noexcept {
    if (!is_windows_11_xaml_taskbar(taskbar_hwnd)) {
        return false;
    }
    DWORD align = 1U; // Windows 11 defaults to centered (1)
    DWORD size = sizeof(align);
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, L"TaskbarAl", RRF_RT_REG_DWORD, nullptr, &align,
                     &size) == ERROR_SUCCESS) {
        return align != 0U;
    }
    return true;
}

[[nodiscard]] bool query_taskbar_widgets_enabled(HWND taskbar_hwnd) noexcept {
    if (!is_windows_11_xaml_taskbar(taskbar_hwnd)) {
        return false;
    }
    DWORD widgets = 0U;
    DWORD size = sizeof(widgets);
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, L"TaskbarDa", RRF_RT_REG_DWORD, nullptr, &widgets,
                     &size) == ERROR_SUCCESS) {
        return widgets != 0U;
    }
    return false;
}

[[nodiscard]] std::wstring format_magnitude_value(double val) {
    std::array<wchar_t, 32> buf{};
    if (val >= 100.0) {
        static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.0f", std::round(val)));
    } else {
        static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.1f", val));
    }
    return std::wstring(buf.data());
}

[[nodiscard]] std::wstring format_rounded_int(double val) {
    std::array<wchar_t, 32> buf{};
    const double clamped = std::isfinite(val) ? std::max(0.0, val) : 0.0;
    static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.0f", std::round(clamped)));
    return std::wstring(buf.data());
}

[[nodiscard]] std::wstring format_one_decimal(double val) {
    std::array<wchar_t, 32> buf{};
    const double clamped = std::isfinite(val) ? std::max(0.0, val) : 0.0;
    static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.1f", clamped));
    return std::wstring(buf.data());
}

[[nodiscard]] std::wstring format_power_watts(double val) {
    std::array<wchar_t, 32> buf{};
    const double clamped = std::isfinite(val) ? std::max(0.0, val) : 0.0;
    if (clamped >= 10.0) {
        static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.0f", std::round(clamped)));
    } else {
        static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.1f", clamped));
    }
    return std::wstring(buf.data());
}

[[nodiscard]] std::wstring format_capacity_detail(double pct, double used_gb, double total_gb) {
    std::array<wchar_t, 64> buf{};
    const double safe_pct = std::isfinite(pct) ? std::clamp(pct, 0.0, 100.0) : 0.0;
    if (!std::isfinite(total_gb) || total_gb <= 0.0) {
        static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.1f%%", safe_pct));
        return std::wstring(buf.data());
    }
    const double safe_used = std::isfinite(used_gb) ? std::clamp(used_gb, 0.0, total_gb) : 0.0;
    static_cast<void>(std::swprintf(buf.data(), buf.size(), L"%.1f%% (%.1f / %.1f GB)", safe_pct,
                                    safe_used, total_gb));
    return std::wstring(buf.data());
}

[[nodiscard]] COLORREF sanitize_transparent_color(COLORREF color) noexcept {
    if (color == 0) {
        return RGB(1, 1, 1);
    }
    if (color == RGB(255, 255, 255)) {
        return RGB(254, 254, 254);
    }
    BYTE r = GetRValue(color);
    BYTE g = GetGValue(color);
    BYTE b = GetBValue(color);
    if (r == b) {
        if (b >= 255U) {
            --b;
        } else {
            ++b;
        }
    }
    return RGB(r, g, b);
}

[[nodiscard]] bool is_color_similar(COLORREF c1, COLORREF c2) noexcept {
    constexpr int kDiffThreshold = 24;
    return (std::abs(static_cast<int>(GetRValue(c1)) - static_cast<int>(GetRValue(c2))) +
            std::abs(static_cast<int>(GetGValue(c1)) - static_cast<int>(GetGValue(c2))) +
            std::abs(static_cast<int>(GetBValue(c1)) - static_cast<int>(GetBValue(c2)))) <
           kDiffThreshold;
}

} // namespace

TaskbarSpeedWindow::TaskbarSpeedWindow() = default;

TaskbarSpeedWindow::~TaskbarSpeedWindow() {
    close();
}

TaskbarFormattedRate TaskbarSpeedWindow::format_rate_parts(std::uint64_t bytes_per_sec,
                                                           config::SpeedUnit unit_mode) {
    const auto bytes = static_cast<double>(bytes_per_sec);
    std::wstring value_str = L"0.0";
    std::wstring unit_str = (unit_mode == config::SpeedUnit::Bits) ? L"Kbps" : L"KB/s";

    if (bytes > 0.0) {
        if (unit_mode == config::SpeedUnit::Bits) {
            const double bits = bytes * 8.0;
            constexpr double kKb = 1000.0;
            constexpr double kMb = 1000.0 * kKb;
            constexpr double kGb = 1000.0 * kMb;
            if (bits >= 999.5 * kMb) {
                value_str = format_magnitude_value(bits / kGb);
                unit_str = L"Gbps";
            } else if (bits >= 999.5 * kKb) {
                value_str = format_magnitude_value(bits / kMb);
                unit_str = L"Mbps";
            } else {
                value_str = format_magnitude_value(bits / kKb);
                unit_str = L"Kbps";
            }
        } else {
            constexpr double kKB = 1024.0;
            constexpr double kMB = 1024.0 * kKB;
            constexpr double kGB = 1024.0 * kMB;
            if (bytes >= 999.5 * kMB) {
                value_str = format_magnitude_value(bytes / kGB);
                unit_str = L"GB/s";
            } else if (bytes >= 999.5 * kKB) {
                value_str = format_magnitude_value(bytes / kMB);
                unit_str = L"MB/s";
            } else {
                value_str = format_magnitude_value(bytes / kKB);
                unit_str = L"KB/s";
            }
        }
    }

    return TaskbarFormattedRate{
        .value = std::move(value_str),
        .unit = std::move(unit_str),
    };
}

std::wstring TaskbarSpeedWindow::format_rate_line(std::wstring_view arrow_prefix,
                                                  std::uint64_t bytes_per_sec,
                                                  config::SpeedUnit unit_mode) {
    const TaskbarFormattedRate parts = format_rate_parts(bytes_per_sec, unit_mode);
    std::wstring line;
    line.reserve(16U);
    line.append(arrow_prefix);
    line.append(L": ");
    line.append(parts.value);
    line.push_back(L' ');
    line.append(parts.unit);
    return line;
}

std::vector<TaskbarColumnPair> TaskbarSpeedWindow::build_display_columns(
    const config::DisplayConfig& display_config, TaskbarRatePair rates,
    const SystemTelemetrySnapshot& telemetry) {
    struct HardwareItem {
        TaskbarMetricCell cell;
        int min_width_dip;
    };

    std::vector<HardwareItem> hw_items;
    hw_items.reserve(6U);

    // Canonical sequence requested:
    // 1. CPU: usage%
    // 2. CPU: temperature °C
    // 3. CPU: power W
    // 4. MEM: usage%
    // 5. GPU: usage%
    // 6. VRM: usage%
    if (display_config.show_taskbar_cpu_usage) {
        hw_items.push_back(HardwareItem{
            .cell =
                TaskbarMetricCell{
                    .label = L"CPU:",
                    .value = format_rounded_int(telemetry.cpu_usage_percent),
                    .unit = L"%",
                    .empty = false,
                },
            .min_width_dip = kHardwareColumnWidthDip,
        });
    }
    if (display_config.show_taskbar_cpu_temp) {
        hw_items.push_back(HardwareItem{
            .cell =
                TaskbarMetricCell{
                    .label = L"CPU:",
                    .value = telemetry.cpu_temperature_celsius.has_value()
                                 ? format_rounded_int(*telemetry.cpu_temperature_celsius)
                                 : L"--",
                    .unit = L"\u2103",
                    .empty = false,
                },
            .min_width_dip = kHardwareColumnWidthDip,
        });
    }
    if (display_config.show_taskbar_cpu_power) {
        hw_items.push_back(HardwareItem{
            .cell =
                TaskbarMetricCell{
                    .label = L"CPU:",
                    .value = telemetry.cpu_package_power_watts.has_value()
                                 ? format_power_watts(*telemetry.cpu_package_power_watts)
                                 : L"--",
                    .unit = L"W",
                    .empty = false,
                },
            .min_width_dip = kHardwareColumnWidthDip,
        });
    }
    if (display_config.show_taskbar_memory_usage) {
        hw_items.push_back(HardwareItem{
            .cell =
                TaskbarMetricCell{
                    .label = L"MEM:",
                    .value = format_rounded_int(telemetry.memory_usage_percent),
                    .unit = L"%",
                    .empty = false,
                },
            .min_width_dip = kHardwareColumnWidthDip,
        });
    }
    if (display_config.show_taskbar_gpu_usage) {
        hw_items.push_back(HardwareItem{
            .cell =
                TaskbarMetricCell{
                    .label = L"GPU:",
                    .value = format_rounded_int(telemetry.gpu_usage_percent),
                    .unit = L"%",
                    .empty = false,
                },
            .min_width_dip = kHardwareColumnWidthDip,
        });
    }
    if (display_config.show_taskbar_gpu_memory) {
        hw_items.push_back(HardwareItem{
            .cell =
                TaskbarMetricCell{
                    .label = L"VRM:",
                    .value = format_rounded_int(telemetry.gpu_vram_usage_percent),
                    .unit = L"%",
                    .empty = false,
                },
            .min_width_dip = kHardwareColumnWidthDip,
        });
    }

    std::vector<TaskbarColumnPair> columns;
    columns.reserve(((hw_items.size() + 1U) / 2U) + 1U);

    // Pair hardware items sequentially (top, bottom) so disabled items are filled by subsequent
    // hardware items.
    for (std::size_t idx = 0U; idx < hw_items.size(); idx += 2U) {
        TaskbarColumnPair col{};
        col.top = hw_items[idx].cell;
        col.width_dip = hw_items[idx].min_width_dip;
        if (idx + 1U < hw_items.size()) {
            col.bottom = hw_items[idx + 1U].cell;
            col.width_dip = std::max(col.width_dip, hw_items[idx + 1U].min_width_dip);
        }
        columns.push_back(std::move(col));
    }

    // Upload and download speeds never substitute into hardware slots and always appear as an
    // inseparable vertical pair in the rightmost column.
    if (display_config.show_taskbar_speed) {
        const TaskbarFormattedRate up_parts =
            format_rate_parts(rates.upload_bytes_per_sec, display_config.speed_unit);
        const TaskbarFormattedRate down_parts =
            format_rate_parts(rates.download_bytes_per_sec, display_config.speed_unit);
        columns.push_back(TaskbarColumnPair{
            .top =
                TaskbarMetricCell{
                    .label = L"\u2191:",
                    .value = up_parts.value,
                    .unit = up_parts.unit,
                    .empty = false,
                },
            .bottom =
                TaskbarMetricCell{
                    .label = L"\u2193:",
                    .value = down_parts.value,
                    .unit = down_parts.unit,
                    .empty = false,
                },
            .width_dip = kDefaultOverlayWidthDip,
        });
    }

    return columns;
}

std::vector<TaskbarTooltipRow> TaskbarSpeedWindow::build_tooltip_rows(
    const config::DisplayConfig& display_config, TaskbarRatePair rates,
    const SystemTelemetrySnapshot& telemetry, bool is_chinese) {
    std::vector<TaskbarTooltipRow> rows;
    rows.reserve(8U);

    if (display_config.show_taskbar_cpu_usage) {
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"CPU \u4F7F\u7528\u7387" : L"CPU Usage",
            .value = format_one_decimal(telemetry.cpu_usage_percent) + L"%",
        });
    }
    if (display_config.show_taskbar_cpu_temp) {
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"CPU \u6E29\u5EA6" : L"CPU Temp",
            .value = telemetry.cpu_temperature_celsius.has_value()
                         ? (format_one_decimal(*telemetry.cpu_temperature_celsius) + L" \u2103")
                         : L"--",
        });
    }
    if (display_config.show_taskbar_cpu_power) {
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"CPU \u5C01\u88C5\u529F\u8017" : L"CPU Power",
            .value = telemetry.cpu_package_power_watts.has_value()
                         ? (format_one_decimal(*telemetry.cpu_package_power_watts) + L" W")
                         : L"--",
        });
    }
    if (display_config.show_taskbar_memory_usage) {
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"\u5185\u5B58\u5360\u7528 (MEM)" : L"Memory (MEM)",
            .value = format_capacity_detail(telemetry.memory_usage_percent,
                                            telemetry.memory_used_gb, telemetry.memory_total_gb),
        });
    }
    if (display_config.show_taskbar_gpu_usage) {
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"GPU \u4F7F\u7528\u7387" : L"GPU Usage",
            .value = format_one_decimal(telemetry.gpu_usage_percent) + L"%",
        });
    }
    if (display_config.show_taskbar_gpu_memory) {
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"GPU \u663E\u5B58 (VRM)" : L"GPU VRAM (VRM)",
            .value =
                format_capacity_detail(telemetry.gpu_vram_usage_percent, telemetry.gpu_vram_used_gb,
                                       telemetry.gpu_vram_total_gb),
        });
    }
    if (display_config.show_taskbar_speed) {
        const TaskbarFormattedRate up_parts =
            format_rate_parts(rates.upload_bytes_per_sec, display_config.speed_unit);
        const TaskbarFormattedRate down_parts =
            format_rate_parts(rates.download_bytes_per_sec, display_config.speed_unit);
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"\u4E0A\u4F20\u7F51\u901F (\u2191)" : L"Upload (\u2191)",
            .value = up_parts.value + L" " + up_parts.unit,
        });
        rows.push_back(TaskbarTooltipRow{
            .label = is_chinese ? L"\u4E0B\u8F7D\u7F51\u901F (\u2193)" : L"Download (\u2193)",
            .value = down_parts.value + L" " + down_parts.unit,
        });
    }

    return rows;
}

int TaskbarSpeedWindow::compute_required_width_dip(
    const std::vector<TaskbarColumnPair>& columns) noexcept {
    if (columns.empty()) {
        return kDefaultOverlayWidthDip;
    }
    int total_dip = 0;
    for (std::size_t i = 0U; i < columns.size(); ++i) {
        if (i > 0U) {
            total_dip += kColumnGapDip;
        }
        total_dip += columns[i].width_dip;
    }
    return std::max(kHardwareColumnWidthDip, total_dip);
}

TaskbarOverlayPlacement TaskbarSpeedWindow::compute_placement(
    const TaskbarLayoutInput& input) noexcept {
    TaskbarOverlayPlacement result{};
    const int tb_width = input.taskbar_rect.right - input.taskbar_rect.left;
    const int tb_height = input.taskbar_rect.bottom - input.taskbar_rect.top;
    if (tb_width <= 0 || tb_height <= 0 || tb_width < tb_height) {
        return result;
    }

    const int dpi_val = static_cast<int>(input.dpi > 0U ? input.dpi : 96U);
    const int desired_dip =
        (input.overlay_width_dip > 0) ? input.overlay_width_dip : kDefaultOverlayWidthDip;
    const int width_px = std::min(tb_width, (desired_dip * dpi_val) / 96);
    const int height_px = std::min(tb_height, (kDefaultOverlayHeightDip * dpi_val) / 96);
    const int margin_px = (kHorizontalMarginDip * dpi_val) / 96;

    result.y_px = std::max(0, (tb_height - height_px) / 2);
    result.width_px = width_px;
    result.height_px = height_px;
    result.valid = true;

    if (input.taskbar_centered && !input.widgets_enabled) {
        result.x_px = margin_px;
        result.placed_on_left = true;
        return result;
    }

    if (input.has_notify_rect && input.notify_rect.left > input.taskbar_rect.left + width_px &&
        input.notify_rect.left <= input.taskbar_rect.right) {
        const int notify_rel_x = input.notify_rect.left - input.taskbar_rect.left;
        result.x_px = std::max(0, notify_rel_x - width_px - margin_px);
        result.placed_on_left = false;
        return result;
    }

    if (input.taskbar_centered) {
        const int widget_reserve_px = (160 * dpi_val) / 96;
        if (widget_reserve_px + width_px < tb_width) {
            result.x_px = widget_reserve_px;
            result.placed_on_left = true;
            return result;
        }
    }

    const int fallback_right_reserve_px = (160 * dpi_val) / 96;
    result.x_px = std::max(margin_px, tb_width - fallback_right_reserve_px - width_px - margin_px);
    result.placed_on_left = false;
    return result;
}

bool TaskbarSpeedWindow::is_taskbar_light_theme() noexcept {
    DWORD value = 0; // Default Taskbar Dark = 0, Light = 1
    DWORD size = sizeof(value);
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr,
                     &value, &size) == ERROR_SUCCESS) {
        return value != 0;
    }
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                     &value, &size) == ERROR_SUCCESS) {
        return value != 0;
    }
    return false;
}

bool TaskbarSpeedWindow::resolve_dark_text_mode() const noexcept {
    if (theme_mode_ == config::ThemeMode::Light) {
        return true;
    }
    if (theme_mode_ == config::ThemeMode::Dark) {
        return false;
    }
    return is_taskbar_light_theme();
}

bool TaskbarSpeedWindow::resolve_tooltip_light_mode() const noexcept {
    if (theme_mode_ == config::ThemeMode::Light) {
        return true;
    }
    if (theme_mode_ == config::ThemeMode::Dark) {
        return false;
    }
    DWORD value = 1;
    DWORD size = sizeof(value);
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                     &value, &size) == ERROR_SUCCESS) {
        return value != 0;
    }
    return is_taskbar_light_theme();
}
bool TaskbarSpeedWindow::resolve_chinese_locale() const noexcept {
    if (language_mode_ == config::LanguageMode::SimplifiedChinese) {
        return true;
    }
    if (language_mode_ == config::LanguageMode::English) {
        return false;
    }
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

bool TaskbarSpeedWindow::create(ClickCallback on_left_click, ContextMenuCallback on_right_click) {
    if (hwnd_ != nullptr) {
        return true;
    }
    on_left_click_ = std::move(on_left_click);
    on_right_click_ = std::move(on_right_click);
    taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &TaskbarSpeedWindow::window_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kTaskbarOverlayClassName;
    RegisterClassExW(&wc);

    WNDCLASSEXW tip_wc{};
    tip_wc.cbSize = sizeof(WNDCLASSEXW);
    tip_wc.style = CS_HREDRAW | CS_VREDRAW;
    tip_wc.lpfnWndProc = &TaskbarSpeedWindow::tooltip_window_proc;
    tip_wc.hInstance = hinst;
    tip_wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    tip_wc.lpszClassName = kTaskbarTooltipClassName;
    RegisterClassExW(&tip_wc);

    // Create hidden at 1x1 as WS_POPUP with WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
    // (and no independent WS_EX_TOPMOST) so that once parented to Shell_TrayWnd, the window
    // inherits the taskbar's exact Z-order and fullscreen/auto-hide behavior. Starting at 1x1 (and
    // shrinking to 1x1 prior to any SetParent) ensures the subsequent MoveWindow after SetParent
    // always resizes the window to its target dimensions, forcing Win32/DWM to allocate a valid
    // child layered redirection bitmap even at 96 DPI (80x32).
    constexpr DWORD kExStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    hwnd_ = CreateWindowExW(kExStyle, kTaskbarOverlayClassName, L"NFM Taskbar Speed", WS_POPUP, 0,
                            0, 1, 1, nullptr, nullptr, hinst, this);
    if (hwnd_ == nullptr) {
        return false;
    }

    constexpr DWORD kTipExStyle =
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    tooltip_hwnd_ = CreateWindowExW(kTipExStyle, kTaskbarTooltipClassName, L"", WS_POPUP, 0, 0, 1,
                                    1, nullptr, nullptr, hinst, this);
    if (tooltip_hwnd_ != nullptr) {
        const int corner_pref = kDwmwcpRoundSmall;
        static_cast<void>(DwmSetWindowAttribute(tooltip_hwnd_, kDwmwaWindowCornerPreference,
                                                &corner_pref, sizeof(corner_pref)));
        const int backdrop_type = kDwmsbtDisable;
        static_cast<void>(DwmSetWindowAttribute(tooltip_hwnd_, kDwmwaSystemBackdropType,
                                                &backdrop_type, sizeof(backdrop_type)));
    }
    taskbar_back_color_ =
        sanitize_transparent_color(resolve_dark_text_mode() ? RGB(210, 210, 211) : RGB(32, 33, 33));
    SetLayeredWindowAttributes(hwnd_, taskbar_back_color_, 0, LWA_COLORKEY);

    sync_with_taskbar();
    return true;
}

void TaskbarSpeedWindow::set_target_taskbar_for_testing(HWND taskbar_hwnd) noexcept {
    target_taskbar_override_hwnd_ = taskbar_hwnd;
    attached_parent_hwnd_ = nullptr;
    current_relative_bounds_ = RECT{};
}

void TaskbarSpeedWindow::close() noexcept {
    hide_tooltip();
    if (tooltip_hwnd_ != nullptr) {
        DestroyWindow(tooltip_hwnd_);
        tooltip_hwnd_ = nullptr;
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    attached_parent_hwnd_ = nullptr;
    visible_ = false;
    mouse_tracking_ = false;
}

void TaskbarSpeedWindow::set_enabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled_) {
        hide_tooltip();
    }
    if (enabled_ && !config::HasAnyTaskbarMonitorItem(display_config_)) {
        display_config_.show_taskbar_speed = true;
    }
    sync_with_taskbar();
}

void TaskbarSpeedWindow::update_config(const config::DisplayConfig& display_config,
                                       config::ThemeMode theme_mode,
                                       config::LanguageMode language_mode) {
    const bool changed = (display_config_ != display_config || theme_mode_ != theme_mode ||
                          language_mode_ != language_mode);
    display_config_ = display_config;
    theme_mode_ = theme_mode;
    language_mode_ = language_mode;
    const bool next_enabled = config::HasAnyTaskbarMonitorItem(display_config_);
    if (enabled_ != next_enabled) {
        enabled_ = next_enabled;
        if (!enabled_) {
            hide_tooltip();
        }
        sync_with_taskbar();
        return;
    }
    if (changed && enabled_ && hwnd_ != nullptr) {
        sync_with_taskbar();
        if (visible_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (tooltip_visible_) {
            const bool light_mode = resolve_tooltip_light_mode();
            const BOOL use_dark = light_mode ? FALSE : TRUE;
            static_cast<void>(DwmSetWindowAttribute(tooltip_hwnd_, kDwmwaUseImmersiveDarkMode,
                                                    &use_dark, sizeof(use_dark)));
            const COLORREF dwm_border = light_mode ? RGB(224, 224, 224) : RGB(51, 51, 51);
            static_cast<void>(DwmSetWindowAttribute(tooltip_hwnd_, kDwmwaBorderColor, &dwm_border,
                                                    sizeof(dwm_border)));
            show_or_update_tooltip();
            RedrawWindow(tooltip_hwnd_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
        }
    }
}

void TaskbarSpeedWindow::refresh_if_needed() {
    if (!enabled_ || hwnd_ == nullptr) {
        return;
    }
    sync_with_taskbar();
    if (!visible_) {
        return;
    }
    try {
        const auto next_cols =
            build_display_columns(display_config_,
                                  TaskbarRatePair{.download_bytes_per_sec = download_bytes_per_sec_,
                                                  .upload_bytes_per_sec = upload_bytes_per_sec_},
                                  telemetry_);
        const bool dark_text = resolve_dark_text_mode();
        if (next_cols != last_rendered_columns_ || dark_text != last_rendered_dark_text_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (tooltip_visible_) {
            POINT pt{};
            if (GetCursorPos(&pt) != FALSE && !contains_screen_point(pt)) {
                mouse_tracking_ = false;
                hide_tooltip();
            } else {
                show_or_update_tooltip();
            }
        }
    } catch (...) {
    }
}

void TaskbarSpeedWindow::update_rates(TaskbarRatePair rates) {
    download_bytes_per_sec_ = rates.download_bytes_per_sec;
    upload_bytes_per_sec_ = rates.upload_bytes_per_sec;
    refresh_if_needed();
}

void TaskbarSpeedWindow::update_telemetry(const SystemTelemetrySnapshot& telemetry) {
    telemetry_ = telemetry;
    refresh_if_needed();
}

void TaskbarSpeedWindow::hide_tooltip() noexcept {
    if (tooltip_hwnd_ != nullptr && tooltip_visible_) {
        ShowWindow(tooltip_hwnd_, SW_HIDE);
    }
    tooltip_visible_ = false;
}

void TaskbarSpeedWindow::show_or_update_tooltip() {
    if (tooltip_hwnd_ == nullptr || hwnd_ == nullptr || !enabled_ || !visible_) {
        hide_tooltip();
        return;
    }

    std::vector<TaskbarTooltipRow> rows;
    try {
        rows = build_tooltip_rows(display_config_,
                                  TaskbarRatePair{.download_bytes_per_sec = download_bytes_per_sec_,
                                                  .upload_bytes_per_sec = upload_bytes_per_sec_},
                                  telemetry_, resolve_chinese_locale());
    } catch (...) {
        return;
    }
    if (rows.empty()) {
        hide_tooltip();
        return;
    }

    last_tooltip_rows_ = std::move(rows);

    const int dpi_int = static_cast<int>(current_dpi_ > 0U ? current_dpi_ : 96U);
    const int pad_x = (12 * dpi_int) / 96;
    const int pad_y = (8 * dpi_int) / 96;
    const int row_h = (20 * dpi_int) / 96;
    const int mid_gap = (18 * dpi_int) / 96;

    HDC screen_dc = GetDC(nullptr);
    int max_lbl_w = (60 * dpi_int) / 96;
    int max_val_w = (118 * dpi_int) / 96;
    if (screen_dc != nullptr) {
        const int lf_height = -MulDiv(9, dpi_int, 72);
        HFONT font =
            CreateFontW(lf_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"\u5FAE\u8F6F\u96C5\u9ED1");
        HGDIOBJ old_font = (font != nullptr) ? SelectObject(screen_dc, font) : nullptr;
        for (const auto& row : last_tooltip_rows_) {
            SIZE lsz{};
            SIZE vsz{};
            GetTextExtentPoint32W(screen_dc, row.label.c_str(), static_cast<int>(row.label.size()),
                                  &lsz);
            GetTextExtentPoint32W(screen_dc, row.value.c_str(), static_cast<int>(row.value.size()),
                                  &vsz);
            max_lbl_w = std::max(max_lbl_w, static_cast<int>(lsz.cx));
            max_val_w = std::max(max_val_w, static_cast<int>(vsz.cx));
        }
        if (old_font != nullptr) {
            SelectObject(screen_dc, old_font);
        }
        if (font != nullptr) {
            DeleteObject(font);
        }
        ReleaseDC(nullptr, screen_dc);
    }

    const int tip_w = (pad_x * 2) + max_lbl_w + mid_gap + max_val_w;
    const int tip_h = (pad_y * 2) + (static_cast<int>(last_tooltip_rows_.size()) * row_h);

    RECT overlay_rc{};
    GetWindowRect(hwnd_, &overlay_rc);

    MONITORINFO mi{};
    mi.cbSize = sizeof(MONITORINFO);
    HMONITOR hmon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    RECT mon_rc{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (hmon != nullptr && GetMonitorInfoW(hmon, &mi) != FALSE) {
        mon_rc = mi.rcMonitor;
    }

    const int overlay_center_x = (overlay_rc.left + overlay_rc.right) / 2;
    const int screen_margin = (6 * dpi_int) / 96;
    int tip_x = overlay_center_x - (tip_w / 2);
    tip_x = std::clamp(tip_x, static_cast<int>(mon_rc.left) + screen_margin,
                       std::max(static_cast<int>(mon_rc.left) + screen_margin,
                                static_cast<int>(mon_rc.right) - tip_w - screen_margin));

    int tip_y = overlay_rc.top - tip_h - screen_margin;
    if (tip_y < mon_rc.top + screen_margin) {
        tip_y = overlay_rc.bottom + screen_margin;
    }

    if (!tooltip_visible_) {
        const bool light_mode = resolve_tooltip_light_mode();
        const BOOL use_dark = light_mode ? FALSE : TRUE;
        static_cast<void>(DwmSetWindowAttribute(tooltip_hwnd_, kDwmwaUseImmersiveDarkMode,
                                                &use_dark, sizeof(use_dark)));
        const COLORREF dwm_border = light_mode ? RGB(224, 224, 224) : RGB(51, 51, 51);
        static_cast<void>(DwmSetWindowAttribute(tooltip_hwnd_, kDwmwaBorderColor, &dwm_border,
                                                sizeof(dwm_border)));
        SetWindowPos(tooltip_hwnd_, HWND_TOPMOST, tip_x, tip_y, tip_w, tip_h,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        tooltip_visible_ = true;
        ShowWindow(tooltip_hwnd_, SW_SHOWNOACTIVATE);
        RedrawWindow(tooltip_hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
    } else {
        RECT cur_rc{};
        const bool bounds_changed =
            (GetWindowRect(tooltip_hwnd_, &cur_rc) == FALSE || cur_rc.left != tip_x ||
             cur_rc.top != tip_y || (cur_rc.right - cur_rc.left) != tip_w ||
             (cur_rc.bottom - cur_rc.top) != tip_h);
        if (bounds_changed) {
            SetWindowPos(tooltip_hwnd_, HWND_TOPMOST, tip_x, tip_y, tip_w, tip_h,
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        InvalidateRect(tooltip_hwnd_, nullptr, FALSE);
    }
}

void TaskbarSpeedWindow::paint_tooltip_window(HDC paint_dc) noexcept {
    if (tooltip_hwnd_ == nullptr || paint_dc == nullptr) {
        return;
    }
    RECT client_rc{};
    if (GetClientRect(tooltip_hwnd_, &client_rc) == FALSE) {
        return;
    }
    const int width = client_rc.right - client_rc.left;
    const int height = client_rc.bottom - client_rc.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    HDC mem_dc = CreateCompatibleDC(paint_dc);
    if (mem_dc == nullptr) {
        return;
    }
    HBITMAP mem_bmp = CreateCompatibleBitmap(paint_dc, width, height);
    if (mem_bmp == nullptr) {
        DeleteDC(mem_dc);
        return;
    }
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    const bool light_mode = resolve_tooltip_light_mode();
    const COLORREF bg_color = light_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    const COLORREF border_color = light_mode ? RGB(224, 224, 224) : RGB(51, 51, 51);
    const COLORREF label_color = light_mode ? RGB(102, 102, 102) : RGB(166, 166, 166);
    const COLORREF value_color = light_mode ? RGB(0, 0, 0) : RGB(255, 255, 255);
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    FillRect(mem_dc, &client_rc, bg_brush);
    DeleteObject(bg_brush);

    HBRUSH border_brush = CreateSolidBrush(border_color);
    FrameRect(mem_dc, &client_rc, border_brush);
    DeleteObject(border_brush);

    const int dpi_int = static_cast<int>(current_dpi_ > 0U ? current_dpi_ : 96U);
    const int lf_height = -MulDiv(9, dpi_int, 72);
    HFONT font = CreateFontW(lf_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"\u5FAE\u8F6F\u96C5\u9ED1");
    HGDIOBJ old_font = (font != nullptr) ? SelectObject(mem_dc, font) : nullptr;

    SetBkMode(mem_dc, TRANSPARENT);
    const int pad_x = (12 * dpi_int) / 96;
    const int pad_y = (8 * dpi_int) / 96;
    const int row_h = (20 * dpi_int) / 96;

    for (std::size_t idx = 0U; idx < last_tooltip_rows_.size(); ++idx) {
        const auto& row = last_tooltip_rows_[idx];
        const int top_y = pad_y + (static_cast<int>(idx) * row_h);
        RECT rc_row{
            .left = pad_x,
            .top = top_y,
            .right = width - pad_x,
            .bottom = top_y + row_h,
        };
        SetTextColor(mem_dc, label_color);
        DrawTextW(mem_dc, row.label.c_str(), static_cast<int>(row.label.size()), &rc_row,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(mem_dc, value_color);
        DrawTextW(mem_dc, row.value.c_str(), static_cast<int>(row.value.size()), &rc_row,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    BitBlt(paint_dc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);

    if (old_font != nullptr) {
        SelectObject(mem_dc, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

bool TaskbarSpeedWindow::contains_screen_point(POINT pt) const noexcept {
    if (hwnd_ == nullptr || !visible_ || IsWindowVisible(hwnd_) == FALSE) {
        return false;
    }
    RECT rc{};
    if (GetWindowRect(hwnd_, &rc) == FALSE) {
        return false;
    }
    return PtInRect(&rc, pt) != FALSE;
}

void TaskbarSpeedWindow::sample_and_apply_taskbar_background_color(HWND taskbar_hwnd,
                                                                   const RECT& taskbar_rect,
                                                                   bool placed_on_left) noexcept {
    if (hwnd_ == nullptr || taskbar_hwnd == nullptr) {
        return;
    }
    // Only sample when taskbar is topmost (i.e. not covered by a fullscreen window)
    if ((GetWindowLongPtrW(taskbar_hwnd, GWL_EXSTYLE) & static_cast<LONG_PTR>(WS_EX_TOPMOST)) ==
        0) {
        return;
    }
    RECT wnd_rc{};
    if (GetWindowRect(hwnd_, &wnd_rc) == FALSE) {
        return;
    }
    const int candidate_x =
        static_cast<int>(placed_on_left ? (wnd_rc.right + 2) : (wnd_rc.left - 2));
    const int sample_x = std::clamp(candidate_x, static_cast<int>(taskbar_rect.left + 1),
                                    static_cast<int>(taskbar_rect.right - 2));
    const int sample_y =
        std::clamp(static_cast<int>(wnd_rc.bottom - 1), static_cast<int>(taskbar_rect.top + 1),
                   static_cast<int>(taskbar_rect.bottom - 2));

    HDC desktop_dc = GetDC(nullptr);
    if (desktop_dc == nullptr) {
        return;
    }
    const COLORREF raw_color = GetPixel(desktop_dc, sample_x, sample_y);
    ReleaseDC(nullptr, desktop_dc);

    if (raw_color == CLR_INVALID || raw_color == 0) {
        return;
    }
    const COLORREF sanitized = sanitize_transparent_color(raw_color);
    if (!is_color_similar(sanitized, taskbar_back_color_)) {
        taskbar_back_color_ = sanitized;
        SetLayeredWindowAttributes(hwnd_, taskbar_back_color_, 0, LWA_COLORKEY);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TaskbarSpeedWindow::sync_with_taskbar() {
    if (hwnd_ == nullptr) {
        return;
    }
    if (!enabled_) {
        hide_tooltip();
        if (visible_ || IsWindowVisible(hwnd_) != FALSE) {
            ShowWindow(hwnd_, SW_HIDE);
            visible_ = false;
        }
        return;
    }

    HWND taskbar_hwnd = (target_taskbar_override_hwnd_ != nullptr)
                            ? target_taskbar_override_hwnd_
                            : FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar_hwnd == nullptr || IsWindow(taskbar_hwnd) == FALSE) {
        hide_tooltip();
        if (visible_) {
            ShowWindow(hwnd_, SW_HIDE);
            visible_ = false;
        }
        attached_parent_hwnd_ = nullptr;
        return;
    }

    RECT taskbar_rect{};
    if (GetWindowRect(taskbar_hwnd, &taskbar_rect) == FALSE) {
        return;
    }

    const UINT dpi_val = GetDpiForWindow(taskbar_hwnd);
    current_dpi_ = (dpi_val > 0U) ? dpi_val : 96U;

    // Attach directly to Shell_TrayWnd and position relative to native taskbar components
    // (TrayNotifyWnd, Start, Widgets) while preserving sibling taskbar container geometry.
    if (attached_parent_hwnd_ != taskbar_hwnd || GetAncestor(hwnd_, GA_PARENT) != taskbar_hwnd) {
        MoveWindow(hwnd_, 0, 0, 1, 1, FALSE);
        SetLayeredWindowAttributes(hwnd_, taskbar_back_color_, 0, LWA_COLORKEY);
        static_cast<void>(SetParent(hwnd_, taskbar_hwnd));
        attached_parent_hwnd_ = taskbar_hwnd;
        current_relative_bounds_ = RECT{};
    }

    int required_width_dip = kDefaultOverlayWidthDip;
    try {
        const auto cols =
            build_display_columns(display_config_,
                                  TaskbarRatePair{.download_bytes_per_sec = download_bytes_per_sec_,
                                                  .upload_bytes_per_sec = upload_bytes_per_sec_},
                                  telemetry_);
        required_width_dip = compute_required_width_dip(cols);
    } catch (...) {
    }

    HWND notify_hwnd = FindWindowExW(taskbar_hwnd, nullptr, L"TrayNotifyWnd", nullptr);
    RECT notify_rect{};
    const bool has_notify =
        (notify_hwnd != nullptr && GetWindowRect(notify_hwnd, &notify_rect) != FALSE);

    const TaskbarLayoutInput layout_input{
        .taskbar_rect = taskbar_rect,
        .notify_rect = notify_rect,
        .has_notify_rect = has_notify,
        .taskbar_centered = query_taskbar_centered(taskbar_hwnd),
        .widgets_enabled = query_taskbar_widgets_enabled(taskbar_hwnd),
        .dpi = current_dpi_,
        .overlay_width_dip = required_width_dip,
    };
    const TaskbarOverlayPlacement placement = compute_placement(layout_input);
    if (!placement.valid) {
        return;
    }

    int rel_y = placement.y_px;
    HWND start_hwnd = FindWindowExW(taskbar_hwnd, nullptr, L"Start", nullptr);
    RECT start_rect{};
    if (start_hwnd != nullptr && GetWindowRect(start_hwnd, &start_rect) != FALSE) {
        const int start_h = start_rect.bottom - start_rect.top;
        const int tb_h = taskbar_rect.bottom - taskbar_rect.top;
        if (start_h > 0 && tb_h >= start_h) {
            rel_y = ((start_h - placement.height_px) / 2) + (tb_h - start_h);
        }
    }

    const RECT target_rel{
        .left = placement.x_px,
        .top = rel_y,
        .right = placement.x_px + placement.width_px,
        .bottom = rel_y + placement.height_px,
    };
    if (current_relative_bounds_.left != target_rel.left ||
        current_relative_bounds_.top != target_rel.top ||
        current_relative_bounds_.right != target_rel.right ||
        current_relative_bounds_.bottom != target_rel.bottom) {
        current_relative_bounds_ = target_rel;
        MoveWindow(hwnd_, placement.x_px, rel_y, placement.width_px, placement.height_px, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (!visible_ || IsWindowVisible(hwnd_) == FALSE) {
        visible_ = true;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (GetWindow(hwnd_, GW_HWNDPREV) != nullptr) {
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    sample_and_apply_taskbar_background_color(taskbar_hwnd, taskbar_rect, placement.placed_on_left);
}

void TaskbarSpeedWindow::paint_window(HDC paint_dc) noexcept {
    if (hwnd_ == nullptr || paint_dc == nullptr) {
        return;
    }
    RECT client_rc{};
    if (GetClientRect(hwnd_, &client_rc) == FALSE) {
        return;
    }
    const int width = client_rc.right - client_rc.left;
    const int height = client_rc.bottom - client_rc.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    std::vector<TaskbarColumnPair> columns;
    try {
        columns =
            build_display_columns(display_config_,
                                  TaskbarRatePair{.download_bytes_per_sec = download_bytes_per_sec_,
                                                  .upload_bytes_per_sec = upload_bytes_per_sec_},
                                  telemetry_);
        last_rendered_columns_ = columns;
    } catch (...) {
        return;
    }
    const bool dark_text = resolve_dark_text_mode();
    last_rendered_dark_text_ = dark_text;

    HDC mem_dc = CreateCompatibleDC(paint_dc);
    if (mem_dc == nullptr) {
        return;
    }
    HBITMAP mem_bmp = CreateCompatibleBitmap(paint_dc, width, height);
    if (mem_bmp == nullptr) {
        DeleteDC(mem_dc);
        return;
    }
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    // Fill background with the sampled taskbar color (which also matches LWA_COLORKEY).
    // This gives GDI ClearType subpixel rendering the exact taskbar background luminance so
    // font strokes are 100% razor-sharp with zero color fringing.
    HBRUSH bg_brush = CreateSolidBrush(taskbar_back_color_);
    FillRect(mem_dc, &client_rc, bg_brush);
    DeleteObject(bg_brush);

    // Match TrafficMonitor's default font: 9pt Microsoft YaHei ("微软雅黑"), FW_NORMAL,
    // DEFAULT_QUALITY (ClearType enabled).
    const int dpi_int = static_cast<int>(current_dpi_ > 0U ? current_dpi_ : 96U);
    const int lf_height = -MulDiv(9, dpi_int, 72);
    HFONT font = CreateFontW(lf_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"\u5FAE\u8F6F\u96C5\u9ED1");
    // Scale "MEM:" (~80% of 9pt), "VRM:" / "MB/s" (~90% of 9pt), and 3-digit hardware values
    // (~68% of 9pt) so "MEM:" matches "CPU:", "VRM:" matches "GPU:", "MB/s" matches "KB/s", and
    // 3-digit hardware values ("100") fit cleanly inside a 2-digit ("88") slot.
    const int mem_lf_height = -std::max(8, MulDiv(-lf_height, 4, 5));
    HFONT mem_label_font =
        CreateFontW(mem_lf_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                    DEFAULT_PITCH | FF_SWISS, L"\u5FAE\u8F6F\u96C5\u9ED1");
    const int vrm_lf_height = -std::max(8, MulDiv(-lf_height, 9, 10));
    HFONT vrm_label_font =
        CreateFontW(vrm_lf_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                    DEFAULT_PITCH | FF_SWISS, L"\u5FAE\u8F6F\u96C5\u9ED1");
    const int compact_3digit_lf_height = -std::max(7, MulDiv(-lf_height, 13, 20));
    HFONT compact_3digit_font =
        CreateFontW(compact_3digit_lf_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                    DEFAULT_PITCH | FF_SWISS, L"\u5FAE\u8F6F\u96C5\u9ED1");
    HGDIOBJ old_font = (font != nullptr) ? SelectObject(mem_dc, font) : nullptr;

    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, dark_text ? RGB(0, 0, 0) : RGB(255, 255, 255));

    const int half_h = height / 2;
    const int pad_x = (2 * dpi_int) / 96;
    const int col_gap_px = std::max(3, (kColumnGapDip * dpi_int) / 96);
    const int colon_gap_px = std::max(1, (1 * dpi_int) / 96);
    const int rate_unit_gap_px = std::max(2, (2 * dpi_int) / 96);
    const int hw_unit_gap_px = std::max(1, (1 * dpi_int) / 96);

    SIZE cpu_lbl_sz{};
    GetTextExtentPoint32W(mem_dc, L"CPU:", 4, &cpu_lbl_sz);
    const int cpu_label_w = (cpu_lbl_sz.cx > 0) ? cpu_lbl_sz.cx : ((27 * dpi_int) / 96);

    SIZE gpu_lbl_sz{};
    GetTextExtentPoint32W(mem_dc, L"GPU:", 4, &gpu_lbl_sz);
    const int gpu_label_w = (gpu_lbl_sz.cx > 0) ? gpu_lbl_sz.cx : ((28 * dpi_int) / 96);

    // Fixed 2-digit slot ("88") for hardware values (3-digit values scale down to fit this exact
    // 2-digit width) and fixed 4-char slot ("88.8") for network rates so units stay stationary.
    SIZE max_hw_val_sz{};
    GetTextExtentPoint32W(mem_dc, L"88", 2, &max_hw_val_sz);
    const int max_hw_val_w = (max_hw_val_sz.cx > 0) ? max_hw_val_sz.cx : ((14 * dpi_int) / 96);

    SIZE max_rate_val_sz{};
    GetTextExtentPoint32W(mem_dc, L"88.8", 4, &max_rate_val_sz);
    const int max_rate_val_w =
        (max_rate_val_sz.cx > 0) ? max_rate_val_sz.cx : ((24 * dpi_int) / 96);

    const auto draw_metric_cell = [&](RECT row_bounds, const TaskbarMetricCell& cell,
                                      bool is_rate_col) {
        if (cell.empty) {
            return;
        }
        const bool is_mem_label = (cell.label == L"MEM:");
        const bool is_vrm_label = (cell.label == L"VRM:");
        SIZE lbl_sz{};
        GetTextExtentPoint32W(mem_dc, cell.label.c_str(), static_cast<int>(cell.label.size()),
                              &lbl_sz);
        int label_w = (lbl_sz.cx > 0) ? lbl_sz.cx : ((12 * dpi_int) / 96);
        if (is_mem_label) {
            label_w = cpu_label_w;
        } else if (is_vrm_label) {
            label_w = gpu_label_w;
        }

        RECT rc_label{
            .left = row_bounds.left,
            .top = row_bounds.top,
            .right = row_bounds.left + label_w,
            .bottom = row_bounds.bottom,
        };
        if (is_mem_label && mem_label_font != nullptr) {
            SelectObject(mem_dc, mem_label_font);
        } else if (is_vrm_label && vrm_label_font != nullptr) {
            SelectObject(mem_dc, vrm_label_font);
        }
        DrawTextW(mem_dc, cell.label.c_str(), static_cast<int>(cell.label.size()), &rc_label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if ((is_mem_label || is_vrm_label) && font != nullptr) {
            SelectObject(mem_dc, font);
        }

        const int slot_val_w = is_rate_col ? max_rate_val_w : max_hw_val_w;
        const int unit_gap_px = is_rate_col ? rate_unit_gap_px : hw_unit_gap_px;
        const int val_left = static_cast<int>(rc_label.right) + colon_gap_px;
        const int val_right = std::min(static_cast<int>(row_bounds.right), val_left + slot_val_w);
        RECT rc_val{
            .left = val_left,
            .top = row_bounds.top,
            .right = val_right,
            .bottom = row_bounds.bottom,
        };
        const bool shrink_3digit_hw = (!is_rate_col && cell.value.size() >= 3U);
        if (shrink_3digit_hw && compact_3digit_font != nullptr) {
            SelectObject(mem_dc, compact_3digit_font);
        }
        DrawTextW(mem_dc, cell.value.c_str(), static_cast<int>(cell.value.size()), &rc_val,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (shrink_3digit_hw && font != nullptr) {
            SelectObject(mem_dc, font);
        }

        RECT rc_unit{
            .left = val_right + unit_gap_px,
            .top = row_bounds.top,
            .right = row_bounds.right,
            .bottom = row_bounds.bottom,
        };
        const bool is_mb_unit =
            (is_rate_col && !cell.unit.empty() && (cell.unit[0] == L'M' || cell.unit[0] == L'm'));
        if (is_mb_unit && vrm_label_font != nullptr) {
            SelectObject(mem_dc, vrm_label_font);
        }
        DrawTextW(mem_dc, cell.unit.c_str(), static_cast<int>(cell.unit.size()), &rc_unit,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (is_mb_unit && font != nullptr) {
            SelectObject(mem_dc, font);
        }
    };

    int cursor_x = pad_x;
    for (std::size_t idx = 0U; idx < columns.size(); ++idx) {
        const auto& col = columns[idx];
        const int col_w_px = (col.width_dip * dpi_int) / 96;
        const int col_right =
            (idx + 1U == columns.size()) ? width : std::min(width, cursor_x + col_w_px);
        const bool is_rate_col = (col.top.unit.size() > 2U || col.bottom.unit.size() > 2U);

        draw_metric_cell(RECT{cursor_x, 0, col_right, half_h}, col.top, is_rate_col);
        draw_metric_cell(RECT{cursor_x, half_h, col_right, height}, col.bottom, is_rate_col);
        cursor_x = col_right + col_gap_px;
    }

    BitBlt(paint_dc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);

    if (old_font != nullptr) {
        SelectObject(mem_dc, old_font);
    }
    if (compact_3digit_font != nullptr) {
        DeleteObject(compact_3digit_font);
    }
    if (vrm_label_font != nullptr) {
        DeleteObject(vrm_label_font);
    }
    if (mem_label_font != nullptr) {
        DeleteObject(mem_label_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

LRESULT CALLBACK TaskbarSpeedWindow::tooltip_window_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                                         LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* self = reinterpret_cast<TaskbarSpeedWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = reinterpret_cast<TaskbarSpeedWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        self->paint_tooltip_window(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK TaskbarSpeedWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                                 LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* self = reinterpret_cast<TaskbarSpeedWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = reinterpret_cast<TaskbarSpeedWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (self->taskbar_created_msg_ != 0U && msg == self->taskbar_created_msg_) {
        self->attached_parent_hwnd_ = nullptr;
        self->sync_with_taskbar();
        return 0;
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        self->paint_window(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    case WM_MOUSEMOVE: {
        if (!self->mouse_tracking_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            if (TrackMouseEvent(&tme) != FALSE) {
                self->mouse_tracking_ = true;
            }
        }
        if (!self->tooltip_visible_) {
            self->show_or_update_tooltip();
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        self->mouse_tracking_ = false;
        POINT pt{};
        if (GetCursorPos(&pt) != FALSE && self->contains_screen_point(pt)) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            if (TrackMouseEvent(&tme) != FALSE) {
                self->mouse_tracking_ = true;
            }
            return 0;
        }
        self->hide_tooltip();
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        return 0;
    case WM_LBUTTONUP: {
        if (self->on_left_click_) {
            self->on_left_click_();
        }
        if (self->tooltip_visible_ && self->tooltip_hwnd_ != nullptr) {
            SetWindowPos(self->tooltip_hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        return 0;
    }
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU: {
        self->hide_tooltip();
        POINT pt{};
        if (GetCursorPos(&pt) != FALSE && self->on_right_click_) {
            self->on_right_click_(pt.x, pt.y);
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: {
        self->sync_with_taskbar();
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace nfm::host::platform
