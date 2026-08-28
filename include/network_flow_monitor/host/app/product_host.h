#ifndef NETWORK_FLOW_MONITOR_HOST_APP_PRODUCT_HOST_H
#define NETWORK_FLOW_MONITOR_HOST_APP_PRODUCT_HOST_H

#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/config/config_store.h"
#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"
#include "network_flow_monitor/host/bridge/native_bridge.h"
#include "network_flow_monitor/host/lifecycle/collector_companion.h"
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
    bool enable_dev_tools{false};
    std::wstring dev_server_url; // Optional: e.g. L"http://localhost:5173"
};

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
    void on_window_dpi_changed(std::uint32_t new_dpi, const RECT* suggested_rect) override;
    void on_window_close_requested() override;
    void on_window_destroyed() override;

    // IBridgeCommandHandler implementation
    void on_show_monitor() override;
    void on_hide_monitor() override;
    void on_set_pin(bool pinned) override;
    void on_open_settings(std::string_view route) override;
    void on_update_setting(std::string_view key, std::string_view value_json) override;
    void on_retry_collector() override;
    void on_exit_app() override;
    void on_set_theme(std::string_view theme) override;
    void on_set_locale(std::string_view locale) override;

    [[nodiscard]] std::shared_ptr<bridge::INativeBridge> bridge() const noexcept {
        return native_bridge_;
    }
    [[nodiscard]] std::shared_ptr<lifecycle::CollectorCompanion> collector() const noexcept {
        return collector_companion_;
    }

  private:
    ProductHostOptions options_;
    std::unique_ptr<platform::Win32Window> window_;
    std::shared_ptr<webview::IWebViewController> webview_;
    std::shared_ptr<bridge::NativeBridge> native_bridge_;
    std::shared_ptr<bridge::BridgeDispatcher> bridge_dispatcher_;
    std::shared_ptr<lifecycle::CollectorCompanion> collector_companion_;
    config::AppConfig current_config_;

    bool is_initialized_{false};
    bool is_shutting_down_{false};
};

} // namespace nfm::host::app

#endif // NETWORK_FLOW_MONITOR_HOST_APP_PRODUCT_HOST_H
