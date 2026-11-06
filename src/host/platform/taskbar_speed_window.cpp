#include "network_flow_monitor/host/platform/taskbar_speed_window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string_view>

namespace nfm::host::platform {

namespace {

constexpr wchar_t kTaskbarOverlayClassName[] = L"NetworkFlowMonitor_TaskbarSpeedWindow";

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

TaskbarOverlayPlacement TaskbarSpeedWindow::compute_placement(
    const TaskbarLayoutInput& input) noexcept {
    TaskbarOverlayPlacement result{};
    const int tb_width = input.taskbar_rect.right - input.taskbar_rect.left;
    const int tb_height = input.taskbar_rect.bottom - input.taskbar_rect.top;
    if (tb_width <= 0 || tb_height <= 0 || tb_width < tb_height) {
        return result;
    }

    const int dpi_val = static_cast<int>(input.dpi > 0U ? input.dpi : 96U);
    const int width_px = std::min(tb_width, (kDefaultOverlayWidthDip * dpi_val) / 96);
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
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    attached_parent_hwnd_ = nullptr;
    visible_ = false;
}

void TaskbarSpeedWindow::set_enabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    sync_with_taskbar();
}

void TaskbarSpeedWindow::update_config(config::SpeedUnit speed_unit, config::ThemeMode theme_mode) {
    const bool changed = (speed_unit_ != speed_unit || theme_mode_ != theme_mode);
    speed_unit_ = speed_unit;
    theme_mode_ = theme_mode;
    if (changed && visible_ && hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TaskbarSpeedWindow::update_rates(TaskbarRatePair rates) {
    download_bytes_per_sec_ = rates.download_bytes_per_sec;
    upload_bytes_per_sec_ = rates.upload_bytes_per_sec;
    if (enabled_ && hwnd_ != nullptr) {
        sync_with_taskbar();
        if (visible_) {
            const TaskbarFormattedRate up_parts =
                format_rate_parts(upload_bytes_per_sec_, speed_unit_);
            const TaskbarFormattedRate down_parts =
                format_rate_parts(download_bytes_per_sec_, speed_unit_);
            const std::wstring up_line = up_parts.value + L" " + up_parts.unit;
            const std::wstring down_line = down_parts.value + L" " + down_parts.unit;
            const bool dark_text = resolve_dark_text_mode();
            if (up_line != last_rendered_up_ || down_line != last_rendered_down_ ||
                dark_text != last_rendered_dark_text_) {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }
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

    TaskbarFormattedRate up_parts{};
    TaskbarFormattedRate down_parts{};
    try {
        up_parts = format_rate_parts(upload_bytes_per_sec_, speed_unit_);
        down_parts = format_rate_parts(download_bytes_per_sec_, speed_unit_);
        last_rendered_up_ = up_parts.value + L" " + up_parts.unit;
        last_rendered_down_ = down_parts.value + L" " + down_parts.unit;
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
    HGDIOBJ old_font = (font != nullptr) ? SelectObject(mem_dc, font) : nullptr;

    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, dark_text ? RGB(0, 0, 0) : RGB(255, 255, 255));

    const int half_h = height / 2;
    // Fixed 3-column anchors so digit width changes never shift the unit ("KB/s" / "MB/s"):
    // [pad_x .. label_right] (left-aligned "↑:" / "↓:")
    // [label_right .. val_right] (right-aligned numeric magnitude sized to max 4-char "88.8")
    // [unit_left .. width] (left-aligned unit)
    SIZE label_sz{};
    SIZE max_val_sz{};
    GetTextExtentPoint32W(mem_dc, L"\u2191:", 2, &label_sz);
    GetTextExtentPoint32W(mem_dc, L"88.8", 4, &max_val_sz);
    const int pad_x = (2 * dpi_int) / 96;
    const int label_w = (label_sz.cx > 0) ? label_sz.cx : ((12 * dpi_int) / 96);
    const int max_val_w = (max_val_sz.cx > 0) ? max_val_sz.cx : ((24 * dpi_int) / 96);
    const int colon_gap = (1 * dpi_int) / 96;
    const int val_right = pad_x + label_w + colon_gap + max_val_w;
    const int unit_left = val_right + ((3 * dpi_int) / 96);

    const auto draw_item_row = [&](int top_y, int bottom_y, std::wstring_view label_str,
                                   const TaskbarFormattedRate& parts) {
        RECT rc_label{
            .left = pad_x,
            .top = top_y,
            .right = pad_x + label_w,
            .bottom = bottom_y,
        };
        RECT rc_val{
            .left = pad_x + label_w,
            .top = top_y,
            .right = val_right,
            .bottom = bottom_y,
        };
        RECT rc_unit{
            .left = unit_left,
            .top = top_y,
            .right = width,
            .bottom = bottom_y,
        };
        DrawTextW(mem_dc, label_str.data(), static_cast<int>(label_str.size()), &rc_label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        DrawTextW(mem_dc, parts.value.c_str(), static_cast<int>(parts.value.size()), &rc_val,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        DrawTextW(mem_dc, parts.unit.c_str(), static_cast<int>(parts.unit.size()), &rc_unit,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    draw_item_row(0, half_h, L"\u2191:", up_parts);
    draw_item_row(half_h, height, L"\u2193:", down_parts);
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
    case WM_LBUTTONUP: {
        if (self->on_left_click_) {
            self->on_left_click_();
        }
        return 0;
    }
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU: {
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
