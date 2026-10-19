#ifndef NETWORK_FLOW_MONITOR_HOST_APP_PRODUCT_HOST_H
#define NETWORK_FLOW_MONITOR_HOST_APP_PRODUCT_HOST_H

#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/config/config_store.h"
#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"
#include "network_flow_monitor/host/bridge/native_bridge.h"
#include "network_flow_monitor/host/coordinator/dock_capsule_coordinator.h"
#include "network_flow_monitor/host/lifecycle/collector_companion.h"
#include "network_flow_monitor/host/platform/app_updater.h"
#include "network_flow_monitor/host/platform/rotating_file_logger.h"
#include "network_flow_monitor/host/platform/startup_registration.h"
#include "network_flow_monitor/host/platform/win32_window.h"
#include "network_flow_monitor/host/webview/webview_controller.h"

#include <filesystem>
#include <memory>
#include <string>

namespace nfm::host::app {

struct ProductHostOptions {
    std::filesystem::path static_assets_dir;
    std::filesystem::path collector_executable_path;
    std::filesystem::path config_file_path;
    std::filesystem::path log_directory_path;
    platform::StartupRegistrationOptions startup_options;
    bool enable_dev_tools{false};
    std::wstring dev_server_url; // Optional: e.g. L"http://localhost:5173"
};

// NOLINTNEXTLINE(misc-multiple-inheritance)
class ProductHost : public platform::IWindowEventListener,
                    public bridge::IBridgeCommandHandler,
                    public std::enable_shared_from_this<ProductHost> {
  public:
    explicit ProductHost(ProductHostOptions options = {});
    ~ProductHost() override;

    ProductHost(const ProductHost&) = delete;
    ProductHost& operator=(const ProductHost&) = delete;
    ProductHost(ProductHost&&) = delete;
    ProductHost& operator=(ProductHost&&) = delete;

    bool initialize();
    int run();
    void shutdown();

    // IWindowEventListener implementation
    void on_window_created(HWND hwnd) override;
    void on_window_size_changed(int width_px, int height_px) override;
    void on_window_position_changed() override;
    void on_window_dpi_changed(std::uint32_t new_dpi, const RECT* suggested_rect) override;
    void on_window_close_requested() override;
    void on_window_destroyed() override;
    void on_display_change() override;
    void on_tray_show_monitor() override;
    void on_tray_settings() override;
    void on_tray_retry_collector() override;
    void on_tray_exit() override;
    void on_tray_context_menu(int screen_x, int screen_y) override;

    void on_context_menu_show_monitor() override;
    void on_context_menu_toggle_pin() override;
    void on_context_menu_settings() override;
    void on_context_menu_retry_collector() override;
    void on_context_menu_exit() override;
    void on_context_menu_opened() override;
    void on_context_menu_closed(bool cursor_inside) override;
    void on_surface_pointer_enter() override;
    void on_surface_pointer_leave() override;
    void on_surface_pointer_down(surface::ScreenPoint point) override;
    void on_surface_pointer_move(surface::ScreenPoint point) override;
    void on_surface_pointer_up(surface::ScreenPoint point) override;
    void on_surface_pointer_cancel() override;
    void on_surface_clock_tick(std::chrono::milliseconds elapsed) override;
    void on_surface_outside_click() override;
    // IBridgeCommandHandler implementation
    void on_show_monitor() override;
    void on_hide_monitor() override;
    void on_set_pin(bool pinned) override;
    void on_open_settings(std::string_view route) override;
    void on_request_config() override;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void on_update_setting(std::string_view key, std::string_view value_json) override;
    void on_retry_collector() override;
    void on_restart_collector() override;
    void on_minimize_window() override;
    void on_toggle_maximize() override;
    void on_start_window_drag() override;
    void on_exit_app() override;
    void on_set_theme(std::string_view theme) override;
    void on_set_locale(std::string_view locale) override;
    void on_open_context_menu(int screen_x, int screen_y) override;
    void on_resize_panel(int height_dip) override;
    void on_resize_settings_window(int height_dip) override;
    void on_open_log_directory() override;
    void on_check_for_update() override;
    void on_download_and_apply_update() override;
    [[nodiscard]] std::shared_ptr<bridge::INativeBridge> bridge() const noexcept {
        return native_bridge_;
    }
    [[nodiscard]] std::shared_ptr<lifecycle::CollectorCompanion> collector() const noexcept {
        return collector_companion_;
    }
    [[nodiscard]] coordinator::DockCapsuleCoordinator* coordinator() const noexcept {
        return coordinator_.get();
    }

  private:
    void apply_coordinator_state();
    void publish_surface_state();
    void ensure_settings_window();
    void close_settings_window();
    void apply_settings_window_chrome();
    void broadcast_to_all_bridges(const std::function<void(bridge::INativeBridge&)>& fn);
    [[nodiscard]] std::filesystem::path log_directory_path() const;
    void write_diagnostic_log(config::LogLevel level, std::string_view message) const noexcept;

    ProductHostOptions options_;
    std::unique_ptr<platform::Win32Window> window_;
    std::unique_ptr<coordinator::DockCapsuleCoordinator> coordinator_;
    std::shared_ptr<webview::IWebViewController> webview_;
    std::shared_ptr<bridge::NativeBridge> native_bridge_;

    std::unique_ptr<platform::Win32Window> settings_window_;
    std::unique_ptr<platform::IWindowEventListener> settings_listener_;
    std::shared_ptr<webview::IWebViewController> settings_webview_;
    std::shared_ptr<bridge::NativeBridge> settings_bridge_;

    std::shared_ptr<bridge::BridgeDispatcher> bridge_dispatcher_;
    std::shared_ptr<lifecycle::CollectorCompanion> collector_companion_;
    std::unique_ptr<platform::AppUpdater> app_updater_;
    config::AppConfig current_config_;
    std::optional<bridge::RankingSnapshotDto> latest_ranking_snapshot_;
    std::optional<bridge::CollectorStatusDto> latest_collector_status_;
    bool is_initialized_{false};
    bool is_shutting_down_{false};
    bool settings_surface_visible_{false};
    int settings_height_dip_{260};
    coordinator::PresentationState last_applied_presentation_state_{
        coordinator::PresentationState::CapsuleVisible};
    std::chrono::milliseconds collapse_bounds_remaining_{0};
    std::chrono::milliseconds panel_shrink_bounds_remaining_{0};
    std::chrono::milliseconds watchdog_elapsed_{0};
    mutable platform::RotatingFileLogger logger_;
    std::string last_logged_collector_error_;
    std::uint64_t last_logged_dropped_packets_{0};
};

} // namespace nfm::host::app

#endif // NETWORK_FLOW_MONITOR_HOST_APP_PRODUCT_HOST_H
