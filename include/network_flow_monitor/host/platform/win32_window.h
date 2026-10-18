#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_WIN32_WINDOW_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_WIN32_WINDOW_H

#include "network_flow_monitor/host/surface/surface_types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
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

inline constexpr UINT kWebSurfacePointerMoveMessage = WM_APP + 0x31;
inline constexpr UINT kWebSurfacePointerDownMessage = WM_APP + 0x32;
inline constexpr UINT kWebSurfacePointerUpMessage = WM_APP + 0x33;
inline constexpr UINT kWebSurfacePointerLeaveMessage = WM_APP + 0x34;

struct ContextMenuOptions {
    int screen_x{-1};
    int screen_y{-1};
    bool is_pinned{false};
    bool is_panel_visible{false};
    std::wstring collector_status{L"Running"};
    bool can_retry_collector{false};
    bool is_chinese{false};
};

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
    virtual void on_display_change() {}

    // Context / Tray Menu events
    virtual void on_tray_show_monitor() {}
    virtual void on_tray_settings() {}
    virtual void on_tray_retry_collector() {}
    virtual void on_tray_exit() {}
    virtual void on_tray_context_menu(int /*screen_x*/, int /*screen_y*/) {}

    virtual void on_context_menu_show_monitor() {}
    virtual void on_context_menu_toggle_pin() {}
    virtual void on_context_menu_settings() {}
    virtual void on_context_menu_retry_collector() {}
    virtual void on_context_menu_exit() {}
    virtual void on_context_menu_opened() {}
    virtual void on_context_menu_closed(bool /*cursor_inside*/) {}
    virtual void on_surface_pointer_enter() {}
    virtual void on_surface_pointer_leave() {}
    virtual void on_surface_pointer_down(surface::ScreenPoint /*point*/) {}
    virtual void on_surface_pointer_move(surface::ScreenPoint /*point*/) {}
    virtual void on_surface_pointer_up(surface::ScreenPoint /*point*/) {}
    virtual void on_surface_pointer_cancel() {}
    virtual void on_surface_clock_tick(std::chrono::milliseconds /*elapsed*/) {}
    virtual void on_surface_outside_click() {}
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

    void set_bounds(int x, int y, int width, int height);
    void set_topmost(bool topmost);
    void set_click_through(bool click_through);
    void set_taskbar_visibility(bool app_window);
    void configure_surface_chrome(bool rounded_frame, bool dark_mode);
    void begin_interactive_drag() noexcept {
        surface_button_down_ = true;
    }
    void show_context_menu(const ContextMenuOptions& options);
    static std::vector<surface::MonitorArea> enumerate_monitors();
    [[nodiscard]] static bool is_foreground_fullscreen_on_same_monitor(HWND product_window);
    [[nodiscard]] static bool is_system_dark_mode() noexcept;
    [[nodiscard]] static surface::DragThresholdPx system_drag_threshold() noexcept;
    bool create_tray_icon();
    void remove_tray_icon();

    void post_ui_task(std::function<void()> task);

    [[nodiscard]] HWND hwnd() const noexcept {
        return hwnd_;
    }
    [[nodiscard]] std::uint32_t dpi() const noexcept;
    [[nodiscard]] double dpi_scale() const noexcept;
    [[nodiscard]] bool has_tray_icon() const noexcept {
        return tray_icon_active_;
    }

    static int run_message_loop();
    static void exit_message_loop(int exit_code = 0);

  private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    WindowConfig config_;
    HWND hwnd_{nullptr};
    IWindowEventListener* listener_{nullptr};
    bool class_registered_{false};
    bool tray_icon_active_{false};
    bool surface_pointer_inside_{false};
    bool surface_button_down_{false};
    bool outside_button_was_down_{false};
    bool last_rounded_frame_{false};
    bool last_dark_mode_{true};
    bool last_app_window_{false};
    bool click_through_enabled_{false};
    bool chrome_initialized_{false};
    HBRUSH background_brush_{nullptr};
    std::mutex task_mutex_;
    std::queue<std::function<void()>> task_queue_;
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_WIN32_WINDOW_H
