#include "network_flow_monitor/host/bridge/native_bridge.h"

#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"

#include <utility>

namespace nfm::host::bridge {

NativeBridge::NativeBridge(WebMessageSender sender) : sender_(std::move(sender)) {}

void NativeBridge::set_sender(WebMessageSender sender) {
    sender_ = std::move(sender);
}

void NativeBridge::post_traffic_tick(const TrafficTickDto& tick) {
    if (!sender_) {
        return;
    }
    sender_(BridgeDispatcher::serialize_traffic_tick(tick));
}

void NativeBridge::post_collector_status(const CollectorStatusDto& status) {
    if (!sender_) {
        return;
    }
    sender_(BridgeDispatcher::serialize_collector_status(status));
}

void NativeBridge::post_session_reset(const SessionResetDto& reset) {
    if (!sender_) {
        return;
    }
    sender_(BridgeDispatcher::serialize_session_reset(reset));
}

void NativeBridge::post_theme_changed(const ThemeChangedDto& theme) {
    if (!sender_) {
        return;
    }
    sender_(BridgeDispatcher::serialize_theme_changed(theme));
}

void NativeBridge::post_locale_changed(const LocaleChangedDto& locale) {
    if (!sender_) {
        return;
    }
    sender_(BridgeDispatcher::serialize_locale_changed(locale));
}

void NativeBridge::post_command_response(const CommandResponseDto& response) {
    if (!sender_) {
        return;
    }
    sender_(BridgeDispatcher::serialize_command_response(response));
}

void NativeBridge::post_error(const BridgeErrorDto& error) {
    if (!sender_) {
        return;
    }
    sender_(BridgeDispatcher::serialize_error(error));
}

void NativeBridge::post_raw_event(std::string_view event_json) {
    if (!sender_) {
        return;
    }
    sender_(event_json);
}

} // namespace nfm::host::bridge
