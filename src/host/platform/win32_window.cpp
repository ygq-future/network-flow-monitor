#include "network_flow_monitor/host/platform/win32_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <utility>
#include <windowsx.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace nfm::host::platform {

namespace {

constexpr UINT kMsgPostUiTask = WM_USER + 100;
constexpr UINT kMsgTrayIcon = WM_USER + 101;
constexpr UINT kTrayIconId = 1;

constexpr int kCmdTrayShowMonitor = 1001;
constexpr int kCmdTraySettings = 1002;
constexpr int kCmdTrayRetryCollector = 1003;
constexpr int kCmdTrayExit = 1004;

} // namespace

Win32Window::Win32Window(WindowConfig config) : config_(std::move(config)) {}

Win32Window::~Win32Window() {
    close();
    if (class_registered_) {
        UnregisterClassW(config_.class_name.c_str(), GetModuleHandleW(nullptr));
        class_registered_ = false;
    }
}

bool Win32Window::create(IWindowEventListener* listener) {
    listener_ = listener;
    HINSTANCE instance = GetModuleHandleW(nullptr);

    HICON app_icon_big = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                   GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED));
    if (app_icon_big == nullptr) {
        app_icon_big = LoadIconW(nullptr, IDI_APPLICATION);
    }

    HICON app_icon_small = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
    if (app_icon_small == nullptr) {
        app_icon_small = app_icon_big;
    }

    HBRUSH bg_brush = CreateSolidBrush(RGB(15, 23, 42)); // #0F172A slate-900
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hIcon = app_icon_big;
    wc.hIconSm = app_icon_small;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = bg_brush;
    wc.lpszClassName = config_.class_name.c_str();

    if (RegisterClassExW(&wc) == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    class_registered_ = true;

    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    DWORD ex_style = WS_EX_APPWINDOW;

    if (config_.topmost) {
        ex_style |= WS_EX_TOPMOST;
    }
    if (config_.tool_window) {
        ex_style |= WS_EX_TOOLWINDOW;
    }

    hwnd_ = CreateWindowExW(ex_style, config_.class_name.c_str(), config_.title.c_str(), style,
                            config_.initial_x, config_.initial_y, config_.initial_width,
                            config_.initial_height, nullptr, nullptr, instance, this);

    if (hwnd_ == nullptr) {
        return false;
    }

    if (config_.frameless) {
        // Enable Win11 DWM native rounded corners
        const DWM_WINDOW_CORNER_PREFERENCE corner_pref = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_pref,
                              sizeof(corner_pref));

        // Enable DWM dark mode for window frame/shadow
        const BOOL dark_mode = TRUE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));

        // Extend frame into client area so DWM renders hardware drop shadow
        MARGINS margins = {1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(hwnd_, &margins);

        // Force frame recalculation
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon_big));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon_small));

    if (listener_ != nullptr) {
        listener_->on_window_created(hwnd_);
    }

    return true;
}

void Win32Window::show(int cmd_show) {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, cmd_show);
        UpdateWindow(hwnd_);
    }
}

void Win32Window::hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
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
    HICON tray_icon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                   LR_DEFAULTCOLOR | LR_SHARED));
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
            if (IsZoomed(hwnd)) {
                HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(MONITORINFO);
                if (GetMonitorInfoW(mon, &mi)) {
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
        if (self->config_.frameless && !IsZoomed(hwnd)) {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            RECT rc{};
            GetWindowRect(hwnd, &rc);
            constexpr int border = 8;
            constexpr int corner_width = 16;
            constexpr int corner_height = 16;

            const bool is_corner_left = (pt.x >= rc.left && pt.x < rc.left + corner_width);
            const bool is_corner_right = (pt.x < rc.right && pt.x >= rc.right - corner_width);
            const bool is_corner_top = (pt.y >= rc.top && pt.y < rc.top + corner_height);
            const bool is_corner_bottom = (pt.y < rc.bottom && pt.y >= rc.bottom - corner_height);

            if (is_corner_top && is_corner_left)
                return HTTOPLEFT;
            if (is_corner_top && is_corner_right)
                return HTTOPRIGHT;
            if (is_corner_bottom && is_corner_left)
                return HTBOTTOMLEFT;
            if (is_corner_bottom && is_corner_right)
                return HTBOTTOMRIGHT;

            const bool left = pt.x >= rc.left && pt.x < rc.left + border;
            const bool right = pt.x < rc.right && pt.x >= rc.right - border;
            const bool top = pt.y >= rc.top && pt.y < rc.top + border;
            const bool bottom = pt.y < rc.bottom && pt.y >= rc.bottom - border;

            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
        mmi->ptMinTrackSize.x = 640;
        mmi->ptMinTrackSize.y = 420;
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
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            if (self->listener_ != nullptr) {
                self->listener_->on_tray_show_monitor();
            }
            break;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            if (menu != nullptr) {
                AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTrayShowMonitor),
                            L"Show Monitor");
                AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTraySettings), L"Settings");
                AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTrayRetryCollector),
                            L"Retry Collector");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTrayExit), L"Exit");

                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x,
                                         pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);

                if (self->listener_ != nullptr) {
                    if (cmd == kCmdTrayShowMonitor) {
                        self->show(SW_RESTORE);
                        SetForegroundWindow(hwnd);
                        self->listener_->on_tray_show_monitor();
                    } else if (cmd == kCmdTraySettings) {
                        self->show(SW_RESTORE);
                        SetForegroundWindow(hwnd);
                        self->listener_->on_tray_settings();
                    } else if (cmd == kCmdTrayRetryCollector) {
                        self->listener_->on_tray_retry_collector();
                    } else if (cmd == kCmdTrayExit) {
                        self->listener_->on_tray_exit();
                    }
                }
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
        if (suggested_rect != nullptr) {
            SetWindowPos(hwnd, nullptr, suggested_rect->left, suggested_rect->top,
                         suggested_rect->right - suggested_rect->left,
                         suggested_rect->bottom - suggested_rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
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
        PostQuitMessage(0);
        return 0;
    }
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace nfm::host::platform
