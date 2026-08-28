#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_WIN32_WINDOW_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_WIN32_WINDOW_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
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

struct WindowConfig {
    std::wstring title{L"Network Flow Monitor"};
    std::wstring class_name{L"NetworkFlowMonitor_HostWindow"};
    int initial_x{CW_USEDEFAULT};
    int initial_y{CW_USEDEFAULT};
    int initial_width{480};
    int initial_height{360};
    bool frameless{false};
    bool topmost{false};
    bool tool_window{false};
    bool transparent_backdrop{false};
};

class IWindowEventListener {
  public:
    IWindowEventListener() = default;
    virtual ~IWindowEventListener() = default;
    IWindowEventListener(const IWindowEventListener&) = delete;
    IWindowEventListener& operator=(const IWindowEventListener&) = delete;
    IWindowEventListener(IWindowEventListener&&) = delete;
    IWindowEventListener& operator=(IWindowEventListener&&) = delete;

    virtual void on_window_created(HWND hwnd) = 0;
    virtual void on_window_size_changed(int width_px, int height_px) = 0;
    virtual void on_window_dpi_changed(std::uint32_t new_dpi, const RECT* suggested_rect) = 0;
    virtual void on_window_close_requested() = 0;
    virtual void on_window_destroyed() = 0;
};

class Win32Window {
  public:
    explicit Win32Window(WindowConfig config = {});
    ~Win32Window();

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    Win32Window(Win32Window&&) = delete;
    Win32Window& operator=(Win32Window&&) = delete;

    bool create(IWindowEventListener* listener = nullptr);
    void show(int cmd_show = SW_SHOW);
    void hide();
    void close();

    void post_ui_task(std::function<void()> task);

    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
    }
    [[nodiscard]] std::uint32_t dpi() const noexcept;
    [[nodiscard]] double dpi_scale() const noexcept;

    static int run_message_loop();
    static void exit_message_loop(int exit_code = 0);

  private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    WindowConfig config_;
    HWND hwnd_{nullptr};
    IWindowEventListener* listener_{nullptr};
    bool class_registered_{false};

    std::mutex task_mutex_;
    std::queue<std::function<void()>> task_queue_;
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_WIN32_WINDOW_H
