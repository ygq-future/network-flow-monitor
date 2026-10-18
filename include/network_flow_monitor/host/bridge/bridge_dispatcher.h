#ifndef NETWORK_FLOW_MONITOR_HOST_BRIDGE_BRIDGE_DISPATCHER_H
#define NETWORK_FLOW_MONITOR_HOST_BRIDGE_BRIDGE_DISPATCHER_H

#include "network_flow_monitor/host/bridge/bridge_types.h"
#include "network_flow_monitor/host/bridge/native_bridge.h"

#include <memory>
#include <string>
#include <string_view>

namespace nfm::host::bridge {

class BridgeDispatcher {
  public:
    explicit BridgeDispatcher(std::shared_ptr<IBridgeCommandHandler> handler);
    ~BridgeDispatcher() = default;
    BridgeDispatcher(const BridgeDispatcher&) = delete;
    BridgeDispatcher& operator=(const BridgeDispatcher&) = delete;
    BridgeDispatcher(BridgeDispatcher&&) = delete;
    BridgeDispatcher& operator=(BridgeDispatcher&&) = delete;

    // Process an incoming raw JSON message from Web content.
    // Returns true if dispatched successfully, false otherwise.
    // If error occurs, error_out is populated with structured error details.
    bool dispatch_web_message(std::string_view raw_json, BridgeErrorDto* error_out = nullptr,
                              CommandResponseDto* response_out = nullptr);

    // Static helper to serialize a Native -> Web envelope
    static std::string serialize_native_event(NativeEventType type, std::string_view payload_json);

    // Static helper to serialize TrafficTickDto
    static std::string serialize_traffic_tick(const TrafficTickDto& tick);

    // Static helper to serialize RankingSnapshotDto
    static std::string serialize_ranking_snapshot(const RankingSnapshotDto& snapshot);

    // Static helper to serialize CollectorStatusDto
    static std::string serialize_collector_status(const CollectorStatusDto& status);

    // Static helper to serialize SessionResetDto
    static std::string serialize_session_reset(const SessionResetDto& reset);

    // Static helper to serialize ThemeChangedDto
    static std::string serialize_theme_changed(const ThemeChangedDto& theme);

    // Static helper to serialize LocaleChangedDto
    static std::string serialize_locale_changed(const LocaleChangedDto& locale);

    // Static helper to serialize AppConfig + schema metadata to ConfigChangedDto
    static std::string serialize_config_changed(const config::AppConfig& config);

    static std::string serialize_surface_changed(const SurfaceChangedDto& surface);

    // Static helper to serialize CommandResponseDto
    static std::string serialize_command_response(const CommandResponseDto& response);

    // Static helper to serialize BridgeErrorDto
    static std::string serialize_error(const BridgeErrorDto& error);

    // Parse a Web -> Native command from JSON
    static bool parse_web_command(std::string_view raw_json, WebCommand* command_out,
                                  BridgeErrorDto* error_out = nullptr);

  private:
    std::shared_ptr<IBridgeCommandHandler> handler_;
};

} // namespace nfm::host::bridge

#endif // NETWORK_FLOW_MONITOR_HOST_BRIDGE_BRIDGE_DISPATCHER_H
