#include "network_flow_monitor/host/platform/win32_window.h"

#include <utility>

namespace nfm::host::platform {

namespace {

constexpr UINT kMsgPostUiTask = WM_USER + 100;

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

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); // NOLINT(performance-no-int-to-ptr)
    wc.lpszClassName = config_.class_name.c_str();

    if (RegisterClassExW(&wc) == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    class_registered_ = true;

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD ex_style = WS_EX_APPWINDOW;

    if (config_.frameless) {
        style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    }
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
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
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
