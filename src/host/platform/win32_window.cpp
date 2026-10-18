#include "network_flow_monitor/host/platform/win32_window.h"

#include <algorithm>
#include <array>
#include <dwmapi.h>
#include <shellapi.h>
#include <utility>
#include <windowsx.h>
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
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;
constexpr int kDwmsbtDisable = 1;
namespace nfm::host::platform {

namespace {

constexpr UINT kMsgPostUiTask = WM_USER + 100;
constexpr UINT kMsgTrayIcon = WM_USER + 101;
constexpr UINT kTrayIconId = 1;

constexpr int kCmdTrayShowMonitor = 1001;
constexpr int kCmdTraySettings = 1002;
constexpr int kCmdTrayRetryCollector = 1003;
constexpr int kCmdTrayExit = 1004;
constexpr int kCmdTrayPinMonitor = 1005;
constexpr UINT_PTR kSurfaceClockTimerId = 1;
constexpr UINT kSurfaceClockPeriodMs = 8;
constexpr int kAutoHideTaskbarReservationDip = 4;

std::string display_device_id(const wchar_t* device_name) {
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, device_name, -1,
                                             nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return "primary";
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, device_name, -1, result.data(), required,
                        nullptr, nullptr);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

// Edge and DPI are both Win32 UINT values with distinct domain meaning.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::int32_t auto_hide_taskbar_reservation_px(const RECT& monitor_rect, UINT edge,
                                              UINT dpi) noexcept {
    APPBARDATA appbar{.cbSize = sizeof(APPBARDATA), .uEdge = edge, .rc = monitor_rect};
    if (SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &appbar) == 0) {
        return 0;
    }
    return MulDiv(kAutoHideTaskbarReservationDip, static_cast<int>(dpi), 96);
}

} // namespace

Win32Window::Win32Window(WindowConfig config) : config_(std::move(config)) {}

Win32Window::~Win32Window() {
    close();
    if (class_registered_) {
        UnregisterClassW(config_.class_name.c_str(), GetModuleHandleW(nullptr));
        class_registered_ = false;
    }
    if (background_brush_ != nullptr) {
        DeleteObject(background_brush_);
        background_brush_ = nullptr;
    }
}

bool Win32Window::create(IWindowEventListener* listener) {
    listener_ = listener;
    HINSTANCE instance = GetModuleHandleW(nullptr);

    auto* app_icon_big = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                   GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED));
    if (app_icon_big == nullptr) {
        app_icon_big = LoadIconW(nullptr, IDI_APPLICATION);
    }

    auto* app_icon_small = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
    if (app_icon_small == nullptr) {
        app_icon_small = app_icon_big;
    }

    if (!config_.transparent_backdrop) {
        background_brush_ = CreateSolidBrush(RGB(15, 23, 42));
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hIcon = app_icon_big;
    wc.hIconSm = app_icon_small;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = background_brush_;
    wc.lpszClassName = config_.class_name.c_str();

    if (RegisterClassExW(&wc) == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    class_registered_ = true;

    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    if (config_.frameless) {
        style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    }
    DWORD ex_style = config_.tool_window ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;

    if (config_.topmost) {
        ex_style |= WS_EX_TOPMOST;
    }
    hwnd_ = CreateWindowExW(ex_style, config_.class_name.c_str(), config_.title.c_str(), style,
                            config_.initial_x, config_.initial_y, config_.initial_width,
                            config_.initial_height, nullptr, nullptr, instance, this);

    if (hwnd_ == nullptr) {
        return false;
    }

    if (config_.frameless) {
        const int glass_extent = config_.transparent_backdrop ? -1 : 1;
        MARGINS margins{.cxLeftWidth = glass_extent,
                        .cxRightWidth = glass_extent,
                        .cyTopHeight = glass_extent,
                        .cyBottomHeight = glass_extent};
        DwmExtendFrameIntoClientArea(hwnd_, &margins);
        configure_surface_chrome(false, true);
    }

    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon_big));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon_small));

    if (listener_ != nullptr) {
        listener_->on_window_created(hwnd_);
    }

    if (config_.tool_window) {
        SetTimer(hwnd_, kSurfaceClockTimerId, kSurfaceClockPeriodMs, nullptr);
    }
    return true;
}

void Win32Window::show(int cmd_show) {
    if (hwnd_ != nullptr) {
        if (IsIconic(hwnd_) != 0 && cmd_show != SW_HIDE && cmd_show != SW_MINIMIZE) {
            ShowWindow(hwnd_, SW_RESTORE);
        }
        ShowWindow(hwnd_, cmd_show);
        UpdateWindow(hwnd_);
    }
}

void Win32Window::hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Win32Window::set_bounds(int x, int y, int width, int height) {
    if (hwnd_ != nullptr) {
        if (IsIconic(hwnd_) != 0) {
            ShowWindow(hwnd_, SW_RESTORE);
        }
        SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void Win32Window::set_topmost(bool topmost) {
    if (hwnd_ != nullptr) {
        SetWindowPos(hwnd_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
void Win32Window::set_taskbar_visibility(bool app_window) {
    if (hwnd_ == nullptr || (chrome_initialized_ && last_app_window_ == app_window)) {
        return;
    }
    last_app_window_ = app_window;
    LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (app_window) {
        ex_style = (ex_style & ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW)) |
                   static_cast<LONG_PTR>(WS_EX_APPWINDOW);
    } else {
        ex_style = (ex_style & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW)) |
                   static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex_style);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void Win32Window::configure_surface_chrome(bool rounded_frame, bool dark_mode) {
    if (hwnd_ == nullptr || !config_.frameless) {
        return;
    }
    if (chrome_initialized_ && last_rounded_frame_ == rounded_frame &&
        last_dark_mode_ == dark_mode) {
        return;
    }
    chrome_initialized_ = true;
    last_rounded_frame_ = rounded_frame;
    last_dark_mode_ = dark_mode;

    const BOOL use_dark = dark_mode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmwaUseImmersiveDarkMode, &use_dark, sizeof(use_dark));

    const DWM_WINDOW_CORNER_PREFERENCE corner_pref =
        rounded_frame ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd_, kDwmwaWindowCornerPreference, &corner_pref, sizeof(corner_pref));

    const COLORREF border_color = rounded_frame ? kDwmColorDefault : kDwmColorNone;
    DwmSetWindowAttribute(hwnd_, kDwmwaBorderColor, &border_color, sizeof(border_color));

    const int backdrop_type = kDwmsbtDisable;
    DwmSetWindowAttribute(hwnd_, kDwmwaSystemBackdropType, &backdrop_type, sizeof(backdrop_type));

    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

bool Win32Window::is_system_dark_mode() noexcept {
    DWORD value = 1; // Default Light = 1, Dark = 0
    DWORD size = sizeof(value);
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                     &value, &size) == ERROR_SUCCESS) {
        return value == 0;
    }
    return true;
}

void Win32Window::show_context_menu(const ContextMenuOptions& options) {
    if (hwnd_ == nullptr) {
        return;
    }
    POINT pt{};
    if (GetCursorPos(&pt) == FALSE && options.screen_x >= 0 && options.screen_y >= 0) {
        pt.x = options.screen_x;
        pt.y = options.screen_y;
    }
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    std::wstring show_monitor_label = L"Show Monitor";
    if (options.is_chinese) {
        show_monitor_label = options.is_panel_visible ? L"隐藏监控" : L"显示监控";
    } else {
        show_monitor_label = options.is_panel_visible ? L"Hide Monitor" : L"Show Monitor";
    }

    std::wstring pin_label = L"Pin";
    if (options.is_chinese) {
        pin_label = options.is_pinned ? L"取消固定" : L"固定显示";
    } else {
        pin_label = options.is_pinned ? L"Unpin" : L"Pin";
    }

    const std::wstring settings_label = options.is_chinese ? L"设置" : L"Settings";
    std::wstring collector_label;
    if (options.can_retry_collector) {
        collector_label = options.is_chinese ? L"重试采集器" : L"Retry Collector";
    } else {
        collector_label =
            (options.is_chinese ? L"采集器: " : L"Collector: ") + options.collector_status;
    }
    const std::wstring exit_label = options.is_chinese ? L"退出" : L"Exit";

    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTrayShowMonitor),
                show_monitor_label.c_str());
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTrayPinMonitor), pin_label.c_str());
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTraySettings), settings_label.c_str());
    AppendMenuW(menu, options.can_retry_collector ? MF_STRING : MF_STRING | MF_GRAYED,
                static_cast<UINT_PTR>(kCmdTrayRetryCollector), collector_label.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTrayExit), exit_label.c_str());

    if (listener_ != nullptr) {
        listener_->on_context_menu_opened();
    }

    SetForegroundWindow(hwnd_);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                             hwnd_, nullptr);
    DestroyMenu(menu);

    POINT end_pt{};
    GetCursorPos(&end_pt);
    RECT win_rc{};
    GetWindowRect(hwnd_, &win_rc);
    bool cursor_inside = PtInRect(&win_rc, end_pt) != 0;

    outside_button_was_down_ = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
                               ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    if (listener_ != nullptr) {
        listener_->on_context_menu_closed(cursor_inside);
        if (cmd == kCmdTrayShowMonitor) {
            listener_->on_context_menu_show_monitor();
        } else if (cmd == kCmdTrayPinMonitor) {
            listener_->on_context_menu_toggle_pin();
        } else if (cmd == kCmdTraySettings) {
            listener_->on_context_menu_settings();
        } else if (cmd == kCmdTrayRetryCollector) {
            listener_->on_context_menu_retry_collector();
        } else if (cmd == kCmdTrayExit) {
            listener_->on_context_menu_exit();
        }
    }
}

std::vector<surface::MonitorArea> Win32Window::enumerate_monitors() {
    std::vector<surface::MonitorArea> result;
    struct EnumData {
        std::vector<surface::MonitorArea>* monitors{nullptr};
    };
    EnumData data{.monitors = &result};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hmon, HDC /*hdc*/, LPRECT /*lprc*/, LPARAM lparam) -> BOOL {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            auto* d = reinterpret_cast<EnumData*>(lparam);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(MONITORINFOEXW);
            if (GetMonitorInfoW(hmon, &mi) != 0) {
                UINT dpi_x = 96;
                UINT dpi_y = 96;
                HMODULE shcore = LoadLibraryW(L"shcore.dll");
                if (shcore != nullptr) {
                    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
                    // NOLINTNEXTLINE(clang-diagnostic-cast-function-type)
                    auto* get_dpi_fn = reinterpret_cast<GetDpiForMonitorFn>(
                        GetProcAddress(shcore, "GetDpiForMonitor"));
                    if (get_dpi_fn != nullptr) {
                        get_dpi_fn(hmon, 0 /*MDT_EFFECTIVE_DPI*/, &dpi_x, &dpi_y);
                    }
                    FreeLibrary(shcore);
                }
                bool is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                d->monitors->push_back(surface::MonitorArea{
                    .monitor_id = display_device_id(mi.szDevice),
                    .left_px = mi.rcMonitor.left,
                    .top_px = mi.rcMonitor.top,
                    .right_px = mi.rcMonitor.right,
                    .bottom_px = mi.rcMonitor.bottom,
                    .work_left_px = mi.rcWork.left,
                    .work_top_px = mi.rcWork.top,
                    .work_right_px = mi.rcWork.right,
                    .work_bottom_px = mi.rcWork.bottom,
                    .auto_hide_left_reservation_px =
                        auto_hide_taskbar_reservation_px(mi.rcMonitor, ABE_LEFT, dpi_x),
                    .auto_hide_top_reservation_px =
                        auto_hide_taskbar_reservation_px(mi.rcMonitor, ABE_TOP, dpi_x),
                    .auto_hide_right_reservation_px =
                        auto_hide_taskbar_reservation_px(mi.rcMonitor, ABE_RIGHT, dpi_x),
                    .auto_hide_bottom_reservation_px =
                        auto_hide_taskbar_reservation_px(mi.rcMonitor, ABE_BOTTOM, dpi_x),
                    .is_primary = is_primary,
                    .dpi = dpi_x,
                });
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));

    if (result.empty()) {
        RECT fallback_work_area{.left = 0,
                                .top = 0,
                                .right = GetSystemMetrics(SM_CXSCREEN),
                                .bottom = GetSystemMetrics(SM_CYSCREEN)};
        static_cast<void>(SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback_work_area, 0));
        result.push_back(surface::MonitorArea{
            .monitor_id = "primary",
            .left_px = 0,
            .top_px = 0,
            .right_px = GetSystemMetrics(SM_CXSCREEN),
            .bottom_px = GetSystemMetrics(SM_CYSCREEN),
            .work_left_px = fallback_work_area.left,
            .work_top_px = fallback_work_area.top,
            .work_right_px = fallback_work_area.right,
            .work_bottom_px = fallback_work_area.bottom,
            .is_primary = true,
            .dpi = 96,
        });
    }
    return result;
}

bool Win32Window::is_foreground_fullscreen_on_same_monitor(HWND product_window) {
    if (product_window == nullptr) {
        return false;
    }
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || foreground == product_window || IsWindowVisible(foreground) == 0 ||
        IsIconic(foreground) != 0) {
        return false;
    }

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked != 0) {
        return false;
    }

    std::array<wchar_t, 64> class_name{};
    if (GetClassNameW(foreground, class_name.data(), static_cast<int>(class_name.size())) > 0) {
        const std::wstring_view cls{class_name.data()};
        if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" ||
            cls == L"Shell_SecondaryTrayWnd" || cls == L"Windows.UI.Core.CoreWindow" ||
            cls == L"XamlExplorerHostIslandWindow" || cls == L"ApplicationFrameWindow") {
            return false;
        }
    }

    HMONITOR product_monitor = MonitorFromWindow(product_window, MONITOR_DEFAULTTONEAREST);
    HMONITOR foreground_monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    if (product_monitor != foreground_monitor) {
        return false;
    }

    MONITORINFO monitor_info{.cbSize = sizeof(MONITORINFO)};
    RECT foreground_rect{};
    if (GetMonitorInfoW(foreground_monitor, &monitor_info) == 0 ||
        GetWindowRect(foreground, &foreground_rect) == 0) {
        return false;
    }
    return foreground_rect.left <= monitor_info.rcMonitor.left &&
           foreground_rect.top <= monitor_info.rcMonitor.top &&
           foreground_rect.right >= monitor_info.rcMonitor.right &&
           foreground_rect.bottom >= monitor_info.rcMonitor.bottom;
}

surface::DragThresholdPx Win32Window::system_drag_threshold() noexcept {
    return surface::DragThresholdPx{
        .horizontal_px = std::max(1, GetSystemMetrics(SM_CXDRAG)),
        .vertical_px = std::max(1, GetSystemMetrics(SM_CYDRAG)),
    };
}

void Win32Window::close() {
    remove_tray_icon();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Win32Window::create_tray_icon() {
    if (hwnd_ == nullptr || tray_icon_active_) {
        return false;
    }
    auto* tray_icon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
    if (tray_icon == nullptr) {
        tray_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kMsgTrayIcon;
    nid.hIcon = tray_icon;
    wcsncpy_s(nid.szTip, config_.title.c_str(), _TRUNCATE);

    if (Shell_NotifyIconW(NIM_ADD, &nid) == TRUE) {
        tray_icon_active_ = true;
        return true;
    }
    return false;
}

void Win32Window::remove_tray_icon() {
    if (hwnd_ != nullptr && tray_icon_active_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hwnd_;
        nid.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        tray_icon_active_ = false;
    }
}

void Win32Window::post_ui_task(std::function<void()> task) {
    if (hwnd_ == nullptr || !task) {
        return;
    }
    {
        const std::scoped_lock lock(task_mutex_);
        task_queue_.push(std::move(task));
    }
    static_cast<void>(PostMessageW(hwnd_, kMsgPostUiTask, 0, 0));
}

std::uint32_t Win32Window::dpi() const noexcept {
    if (hwnd_ != nullptr) {
        return GetDpiForWindow(hwnd_);
    }
    return 96U;
}

double Win32Window::dpi_scale() const noexcept {
    return static_cast<double>(dpi()) / 96.0;
}

int Win32Window::run_message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void Win32Window::exit_message_loop(int exit_code) {
    PostQuitMessage(exit_code);
}

LRESULT CALLBACK Win32Window::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = reinterpret_cast<Win32Window*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    switch (msg) {
    case WM_NCCALCSIZE: {
        if (self->config_.frameless && wparam == TRUE) {
            if (IsZoomed(hwnd) != 0) {
                HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(MONITORINFO);
                if (GetMonitorInfoW(mon, &mi) != 0) {
                    // NOLINTNEXTLINE(performance-no-int-to-ptr)
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                    params->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        break;
    }
    case WM_NCHITTEST: {
        if (self->config_.frameless) {
            return HTCLIENT;
        }
        break;
    }
    case WM_SYSCOMMAND: {
        if (self->config_.tool_window && (wparam & 0xFFF0U) == SC_MINIMIZE) {
            return 0;
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        return 0;
    }
    case kWebSurfacePointerMoveMessage:
    case WM_MOUSEMOVE: {
        if (!self->surface_pointer_inside_) {
            self->surface_pointer_inside_ = true;
            if (self->listener_ != nullptr) {
                self->listener_->on_surface_pointer_enter();
            }
        }
        POINT point{};
        GetCursorPos(&point);
        const bool left_pressed = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
        if (self->surface_button_down_ && !left_pressed) {
            self->surface_button_down_ = false;
            if (self->listener_ != nullptr) {
                self->listener_->on_surface_pointer_up(
                    surface::ScreenPoint{.x_px = point.x, .y_px = point.y});
            }
            return 0;
        }
        if (self->listener_ != nullptr) {
            self->listener_->on_surface_pointer_move(
                surface::ScreenPoint{.x_px = point.x, .y_px = point.y});
        }
        return 0;
    }
    case kWebSurfacePointerDownMessage:
    case WM_LBUTTONDOWN: {
        POINT point{};
        GetCursorPos(&point);
        self->surface_button_down_ = true;
        if (msg == WM_LBUTTONDOWN) {
            SetCapture(hwnd);
        }
        if (self->listener_ != nullptr) {
            self->listener_->on_surface_pointer_down(
                surface::ScreenPoint{.x_px = point.x, .y_px = point.y});
        }
        return 0;
    }
    case kWebSurfacePointerUpMessage:
    case WM_LBUTTONUP: {
        POINT point{};
        GetCursorPos(&point);
        self->surface_button_down_ = false;
        if (self->listener_ != nullptr) {
            self->listener_->on_surface_pointer_up(
                surface::ScreenPoint{.x_px = point.x, .y_px = point.y});
        }
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        return 0;
    }
    case kWebSurfacePointerLeaveMessage:
    case WM_MOUSELEAVE: {
        if (!self->surface_button_down_) {
            POINT pt{};
            RECT rc{};
            if (GetCursorPos(&pt) != FALSE && GetWindowRect(hwnd, &rc) != FALSE &&
                PtInRect(&rc, pt) != FALSE) {
                return 0;
            }
            if (self->surface_pointer_inside_) {
                self->surface_pointer_inside_ = false;
                if (self->listener_ != nullptr) {
                    self->listener_->on_surface_pointer_leave();
                }
            }
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE: {
        self->surface_button_down_ = false;
        if (self->listener_ != nullptr) {
            self->listener_->on_surface_pointer_cancel();
        }
        return 0;
    }
    case WM_TIMER: {
        if (wparam == kSurfaceClockTimerId && self->listener_ != nullptr) {
            if (IsWindowVisible(hwnd) != FALSE) {
                POINT pt{};
                RECT rc{};
                if (GetCursorPos(&pt) != FALSE && GetWindowRect(hwnd, &rc) != FALSE) {
                    const bool inside = (PtInRect(&rc, pt) != FALSE);
                    const SHORT left_state = GetAsyncKeyState(VK_LBUTTON);
                    const SHORT right_state = GetAsyncKeyState(VK_RBUTTON);
                    const bool left_pressed = ((left_state & 0x8000) != 0);
                    const bool right_pressed = ((right_state & 0x8000) != 0);
                    const bool any_pressed = (left_pressed || right_pressed);
                    if (self->surface_button_down_) {
                        if (left_pressed) {
                            self->listener_->on_surface_pointer_move(
                                surface::ScreenPoint{.x_px = pt.x, .y_px = pt.y});
                        } else {
                            self->surface_button_down_ = false;
                            self->listener_->on_surface_pointer_up(
                                surface::ScreenPoint{.x_px = pt.x, .y_px = pt.y});
                            self->surface_pointer_inside_ = inside;
                            if (!inside) {
                                self->listener_->on_surface_pointer_leave();
                            }
                        }
                    } else {
                        if (inside && !self->surface_pointer_inside_) {
                            self->surface_pointer_inside_ = true;
                            self->listener_->on_surface_pointer_enter();
                        } else if (!inside && self->surface_pointer_inside_) {
                            self->surface_pointer_inside_ = false;
                            self->listener_->on_surface_pointer_leave();
                        }
                        if (!inside && any_pressed && !self->outside_button_was_down_) {
                            self->listener_->on_surface_outside_click();
                        }
                    }
                    self->outside_button_was_down_ = any_pressed;
                }
            }
            self->listener_->on_surface_clock_tick(
                std::chrono::milliseconds{kSurfaceClockPeriodMs});
        }
        return 0;
    }
    case kMsgPostUiTask: {
        std::function<void()> task;
        {
            const std::scoped_lock lock(self->task_mutex_);
            if (!self->task_queue_.empty()) {
                task = std::move(self->task_queue_.front());
                self->task_queue_.pop();
            }
        }
        if (task) {
            try {
                task();
            } catch (...) {
            }
        }
        return 0;
    }
    case kMsgTrayIcon: {
        switch (LOWORD(lparam)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK: {
            self->outside_button_was_down_ = true;
            if (self->listener_ != nullptr) {
                self->listener_->on_tray_show_monitor();
            }
            break;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt{};
            GetCursorPos(&pt);
            if (self->listener_ != nullptr) {
                self->listener_->on_tray_context_menu(pt.x, pt.y);
            }
            break;
        }
        default:
            break;
        }
        return 0;
    }
    case WM_SIZE: {
        const int width = LOWORD(lparam);
        const int height = HIWORD(lparam);
        if (self->listener_ != nullptr) {
            self->listener_->on_window_size_changed(width, height);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        auto new_dpi = static_cast<std::uint32_t>(HIWORD(wparam));
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        const auto* suggested_rect = reinterpret_cast<const RECT*>(lparam);
        if (self->listener_ != nullptr) {
            self->listener_->on_window_dpi_changed(new_dpi, suggested_rect);
        }
        if (self->listener_ == nullptr && suggested_rect != nullptr) {
            SetWindowPos(hwnd, nullptr, suggested_rect->left, suggested_rect->top,
                         suggested_rect->right - suggested_rect->left,
                         suggested_rect->bottom - suggested_rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_DISPLAYCHANGE: {
        if (self->listener_ != nullptr) {
            self->listener_->on_display_change();
        }
        return 0;
    }
    case WM_CLOSE: {
        if (self->listener_ != nullptr) {
            self->listener_->on_window_close_requested();
        } else {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_DESTROY: {
        if (self->listener_ != nullptr) {
            self->listener_->on_window_destroyed();
        }
        self->hwnd_ = nullptr;
        if (self->config_.class_name == L"NetworkFlowMonitor_HostWindow") {
            PostQuitMessage(0);
        }
        return 0;
    }
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace nfm::host::platform
