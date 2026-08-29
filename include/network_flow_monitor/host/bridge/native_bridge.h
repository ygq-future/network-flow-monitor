#ifndef NETWORK_FLOW_MONITOR_HOST_BRIDGE_NATIVE_BRIDGE_H
#define NETWORK_FLOW_MONITOR_HOST_BRIDGE_NATIVE_BRIDGE_H

#include "network_flow_monitor/host/bridge/bridge_types.h"

#include <functional>
#include <memory>
#include <string_view>

namespace nfm::host::bridge {

class INativeBridge {
  public:
    INativeBridge() = default;
    virtual ~INativeBridge() = default;
    INativeBridge(const INativeBridge&) = delete;
    INativeBridge& operator=(const INativeBridge&) = delete;
    INativeBridge(INativeBridge&&) = delete;
    INativeBridge& operator=(INativeBridge&&) = delete;

    virtual void post_traffic_tick(const TrafficTickDto& tick) = 0;
    virtual void post_collector_status(const CollectorStatusDto& status) = 0;
    virtual void post_session_reset(const SessionResetDto& reset) = 0;
    virtual void post_theme_changed(const ThemeChangedDto& theme) = 0;
    virtual void post_locale_changed(const LocaleChangedDto& locale) = 0;
    virtual void post_config_changed(const config::AppConfig& config) = 0;
    virtual void post_command_response(const CommandResponseDto& response) = 0;
    virtual void post_error(const BridgeErrorDto& error) = 0;
    virtual void post_raw_event(std::string_view event_json) = 0;
};

class IBridgeCommandHandler {
  public:
    IBridgeCommandHandler() = default;
    virtual ~IBridgeCommandHandler() = default;
    IBridgeCommandHandler(const IBridgeCommandHandler&) = delete;
    IBridgeCommandHandler& operator=(const IBridgeCommandHandler&) = delete;
    IBridgeCommandHandler(IBridgeCommandHandler&&) = delete;
    IBridgeCommandHandler& operator=(IBridgeCommandHandler&&) = delete;

    virtual void on_show_monitor() = 0;
    virtual void on_hide_monitor() = 0;
    virtual void on_set_pin(bool pinned) = 0;
    virtual void on_open_settings(std::string_view route) = 0;
    virtual void on_request_config() = 0;
    virtual void on_update_setting(std::string_view key, std::string_view value_json) = 0;
    virtual void on_retry_collector() = 0;
    virtual void on_restart_collector() = 0;
    virtual void on_minimize_window() = 0;
    virtual void on_toggle_maximize() {}
    virtual void on_start_window_drag() = 0;
    virtual void on_exit_app() = 0;
    virtual void on_set_theme(std::string_view theme) = 0;
    virtual void on_set_locale(std::string_view locale) = 0;
};

using WebMessageSender = std::function<void(std::string_view json)>;

class NativeBridge : public INativeBridge {
  public:
    explicit NativeBridge(WebMessageSender sender);
    ~NativeBridge() override = default;
    NativeBridge(const NativeBridge&) = delete;
    NativeBridge& operator=(const NativeBridge&) = delete;
    NativeBridge(NativeBridge&&) = delete;
    NativeBridge& operator=(NativeBridge&&) = delete;

    void set_sender(WebMessageSender sender);

    void post_traffic_tick(const TrafficTickDto& tick) override;
    void post_collector_status(const CollectorStatusDto& status) override;
    void post_session_reset(const SessionResetDto& reset) override;
    void post_theme_changed(const ThemeChangedDto& theme) override;
    void post_locale_changed(const LocaleChangedDto& locale) override;
    void post_config_changed(const config::AppConfig& config) override;
    void post_command_response(const CommandResponseDto& response) override;
    void post_error(const BridgeErrorDto& error) override;
    void post_raw_event(std::string_view event_json) override;

  private:
    WebMessageSender sender_;
};

} // namespace nfm::host::bridge

#endif // NETWORK_FLOW_MONITOR_HOST_BRIDGE_NATIVE_BRIDGE_H
