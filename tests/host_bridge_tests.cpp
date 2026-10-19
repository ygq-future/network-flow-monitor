#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"
#include "network_flow_monitor/host/bridge/bridge_types.h"
#include "network_flow_monitor/host/bridge/native_bridge.h"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

class MockBridgeCommandHandler : public nfm::host::bridge::IBridgeCommandHandler {
  public:
    bool show_monitor_called{false};
    bool hide_monitor_called{false};
    bool set_pin_called{false};
    bool pinned_val{false};
    bool open_settings_called{false};
    std::string route_val;
    bool request_config_called{false};
    bool update_setting_called{false};
    std::string setting_key_val;
    std::string setting_value_val;
    bool retry_collector_called{false};
    bool restart_collector_called{false};
    bool minimize_window_called{false};
    bool toggle_maximize_called{false};
    bool start_window_drag_called{false};
    bool exit_app_called{false};
    bool set_theme_called{false};
    std::string theme_val;
    bool set_locale_called{false};
    std::string locale_val;
    bool open_context_menu_called{false};
    int context_menu_x{-1};
    int context_menu_y{-1};
    bool open_log_directory_called{false};
    void on_show_monitor() override {
        show_monitor_called = true;
    }
    void on_hide_monitor() override {
        hide_monitor_called = true;
    }
    void on_set_pin(bool pinned) override {
        set_pin_called = true;
        pinned_val = pinned;
    }
    void on_open_settings(std::string_view route) override {
        open_settings_called = true;
        route_val = route;
    }
    void on_request_config() override {
        request_config_called = true;
    }
    void on_update_setting(std::string_view key, std::string_view value_json) override {
        update_setting_called = true;
        setting_key_val = key;
        setting_value_val = value_json;
    }
    void on_retry_collector() override {
        retry_collector_called = true;
    }
    void on_restart_collector() override {
        restart_collector_called = true;
    }
    void on_minimize_window() override {
        minimize_window_called = true;
    }
    void on_toggle_maximize() override {
        toggle_maximize_called = true;
    }
    void on_start_window_drag() override {
        start_window_drag_called = true;
    }
    void on_exit_app() override {
        exit_app_called = true;
    }
    void on_set_theme(std::string_view theme) override {
        set_theme_called = true;
        theme_val = theme;
    }
    void on_set_locale(std::string_view locale) override {
        set_locale_called = true;
        locale_val = locale;
    }
    void on_open_context_menu(int screen_x, int screen_y) override {
        open_context_menu_called = true;
        context_menu_x = screen_x;
        context_menu_y = screen_y;
    }
    void on_open_log_directory() override {
        open_log_directory_called = true;
    }
};

void CheckEnumMappings(TestContext& context) {
    using namespace nfm::host::bridge;

    context.Check(to_string(WebCommandType::ShowMonitor) == "showMonitor", "ShowMonitor to string");
    context.Check(to_string(WebCommandType::SetPin) == "setPin", "SetPin to string");
    context.Check(to_string(WebCommandType::ToggleMaximizeWindow) == "toggleMaximizeWindow",
                  "ToggleMaximizeWindow to string");
    context.Check(parse_web_command_type("showMonitor") == WebCommandType::ShowMonitor,
                  "Parse showMonitor");
    context.Check(parse_web_command_type("toggleMaximizeWindow") ==
                      WebCommandType::ToggleMaximizeWindow,
                  "Parse toggleMaximizeWindow");
    context.Check(parse_web_command_type("toggleMaximize") == WebCommandType::ToggleMaximizeWindow,
                  "Parse toggleMaximize");
    context.Check(parse_web_command_type("resizePanel") == WebCommandType::ResizePanel,
                  "Parse resizePanel");
    context.Check(parse_web_command_type("openLogDirectory") == WebCommandType::OpenLogDirectory,
                  "Parse openLogDirectory");
    context.Check(parse_web_command_type("invalidCommand") == WebCommandType::Unknown,
                  "Parse invalid command");

    context.Check(to_string(NativeEventType::TrafficTick) == "trafficTick",
                  "TrafficTick to string");
    context.Check(parse_native_event_type("trafficTick") == NativeEventType::TrafficTick,
                  "Parse trafficTick");
    context.Check(parse_native_event_type("unknownEvent") == NativeEventType::Unknown,
                  "Parse unknown event");
}

void CheckDispatchCommands(TestContext& context) {
    using namespace nfm::host::bridge;

    auto handler = std::make_shared<MockBridgeCommandHandler>();
    BridgeDispatcher dispatcher(handler);

    // 1. ShowMonitor
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"showMonitor","version":1,"commandId":"cmd_1"})", &err, &resp);
        context.Check(ok, "dispatch showMonitor succeeds");
        context.Check(handler->show_monitor_called, "handler->on_show_monitor called");
        context.Check(resp.command_id == "cmd_1" && resp.success, "resp matches commandId");
    }

    // 2. SetPin
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"setPin","version":1,"commandId":"cmd_2","payload":{"pinned":true}})", &err,
            &resp);
        context.Check(ok, "dispatch setPin succeeds");
        context.Check(handler->set_pin_called && handler->pinned_val, "handler->on_set_pin true");
    }

    // 3. SetTheme
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"setTheme","version":1,"commandId":"cmd_3","payload":{"theme":"Dark"}})",
            &err, &resp);
        context.Check(ok, "dispatch setTheme succeeds");
        context.Check(handler->set_theme_called && handler->theme_val == "Dark",
                      "handler->on_set_theme Dark");
    }

    // 4. SetLocale
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"setLocale","version":1,"commandId":"cmd_4","payload":{"locale":"SimplifiedChinese"}})",
            &err, &resp);
        context.Check(ok, "dispatch setLocale succeeds");
        context.Check(handler->set_locale_called && handler->locale_val == "SimplifiedChinese",
                      "handler->on_set_locale SimplifiedChinese");
    }

    // 5. RequestConfig
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"requestConfig","version":1,"commandId":"cmd_5"})", &err, &resp);
        context.Check(ok, "dispatch requestConfig succeeds");
        context.Check(handler->request_config_called, "handler->on_request_config called");
    }

    // 6. UpdateSetting
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"updateSetting","version":1,"commandId":"cmd_6","payload":{"key":"floating.capsule_width_dip","value":16}})",
            &err, &resp);
        context.Check(ok, "dispatch updateSetting succeeds");
        context.Check(handler->update_setting_called &&
                          handler->setting_key_val == "floating.capsule_width_dip" &&
                          handler->setting_value_val == "16",
                      "handler->on_update_setting with key and value");
    }

    // 7. RestartCollector
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"restartCollector","version":1,"commandId":"cmd_7"})", &err, &resp);
        context.Check(ok, "dispatch restartCollector succeeds");
        context.Check(handler->restart_collector_called, "handler->on_restart_collector called");
    }

    // 8. MinimizeWindow
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"minimizeWindow","version":1,"commandId":"cmd_8"})", &err, &resp);
        context.Check(ok, "dispatch minimizeWindow succeeds");
        context.Check(handler->minimize_window_called, "handler->on_minimize_window called");
    }

    // 8b. ToggleMaximizeWindow
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"toggleMaximizeWindow","version":1,"commandId":"cmd_8b"})", &err, &resp);
        context.Check(ok, "dispatch toggleMaximizeWindow succeeds");
        context.Check(handler->toggle_maximize_called, "handler->on_toggle_maximize called");
    }

    // 9. StartWindowDrag
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"startWindowDrag","version":1,"commandId":"cmd_9"})", &err, &resp);
        context.Check(ok, "dispatch startWindowDrag succeeds");
        context.Check(handler->start_window_drag_called, "handler->on_start_window_drag called");
    }

    // 10. OpenContextMenu
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"openContextMenu","version":1,"commandId":"cmd_10","payload":{"x":100,"y":200}})",
            &err, &resp);
        context.Check(ok, "dispatch openContextMenu succeeds");
        context.Check(handler->open_context_menu_called, "handler->on_open_context_menu called");
        context.Check(handler->context_menu_x == 100, "context_menu_x is 100");
        context.Check(handler->context_menu_y == 200, "context_menu_y is 200");
    }

    // 11. OpenLogDirectory
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"openLogDirectory","version":1,"commandId":"cmd_11"})", &err, &resp);
        context.Check(ok, "dispatch openLogDirectory succeeds");
        context.Check(handler->open_log_directory_called, "handler->on_open_log_directory called");
    }
}

void CheckBridgeRejections(TestContext& context) {
    using namespace nfm::host::bridge;

    auto handler = std::make_shared<MockBridgeCommandHandler>();
    BridgeDispatcher dispatcher(handler);

    // 1. Oversized payload > 256 KiB
    {
        std::string huge(kMaxBridgePayloadBytes + 10, 'x');
        BridgeErrorDto err;
        bool ok = dispatcher.dispatch_web_message(huge, &err);
        context.Check(!ok && err.code == "PAYLOAD_TOO_LARGE", "oversized payload rejected");
    }

    // 2. Malformed JSON
    {
        BridgeErrorDto err;
        bool ok = dispatcher.dispatch_web_message("{invalid_json", &err);
        context.Check(!ok && err.code == "INVALID_JSON", "malformed JSON rejected");
    }

    // 3. Native event sent from Web (Direction violation)
    {
        BridgeErrorDto err;
        bool ok = dispatcher.dispatch_web_message(
            R"({"type":"trafficTick","version":1,"payload":{"seq":1}})", &err);
        context.Check(!ok && err.code == "INVALID_DIRECTION",
                      "native event type from web rejected");
    }

    // 4. Unknown command
    {
        BridgeErrorDto err;
        bool ok =
            dispatcher.dispatch_web_message(R"({"type":"launchNuclearMissile","version":1})", &err);
        context.Check(!ok && err.code == "UNKNOWN_COMMAND", "unknown command rejected");
    }
}

void CheckNativeSerializers(TestContext& context) {
    using namespace nfm::host::bridge;

    // TrafficTick serialization
    TrafficTickDto tick{
        .seq = 100,
        .monotonic_ms = 5000,
        .download_bytes_per_sec = 1024000,
        .upload_bytes_per_sec = 512000,
    };
    std::string tick_json = BridgeDispatcher::serialize_traffic_tick(tick);
    context.Check(!tick_json.empty(), "trafficTick serialization non-empty");
    context.Check(tick_json.find("\"type\":\"trafficTick\"") != std::string::npos, "contains type");
    context.Check(tick_json.find("\"downloadBytesPerSec\":1024000") != std::string::npos,
                  "contains download rate");
    context.Check(tick_json.find("\"uploadBytesPerSec\":512000") != std::string::npos,
                  "contains upload rate");

    // RankingSnapshot serialization
    RankingSnapshotDto ranking{
        .seq = 5,
        .monotonic_ms = 5000,
        .status = "Ready",
        .total_download_bytes_per_sec = 2048000,
        .total_upload_bytes_per_sec = 512000,
        .applications = {RankingAppDto{
            .id = "app_1",
            .executable_path = "C:\\app.exe",
            .display_name = "Test App",
            .icon_key = "app",
            .download_bytes_per_sec = 1024000,
            .upload_bytes_per_sec = 256000,
        }},
    };
    std::string ranking_json = BridgeDispatcher::serialize_ranking_snapshot(ranking);
    context.Check(ranking_json.find("\"type\":\"rankingSnapshot\"") != std::string::npos,
                  "contains rankingSnapshot type");
    context.Check(ranking_json.find("\"displayName\":\"Test App\"") != std::string::npos,
                  "contains application displayName");
    // CollectorStatus serialization
    CollectorStatusDto status{
        .monotonic_ms = 5000,
        .etw_session_active = true,
        .dropped_packets = 0,
        .active_pids = 10,
    };
    std::string status_json = BridgeDispatcher::serialize_collector_status(status);
    context.Check(status_json.find("\"type\":\"collectorStatus\"") != std::string::npos,
                  "contains collectorStatus type");

    // SessionReset serialization
    SessionResetDto reset{
        .collector_instance_id = "inst_123",
        .collector_pid = 456,
    };
    std::string reset_json = BridgeDispatcher::serialize_session_reset(reset);
    context.Check(reset_json.find("\"collectorInstanceId\":\"inst_123\"") != std::string::npos,
                  "contains instance id");

    // ConfigChanged serialization
    const auto default_cfg = nfm::config::DefaultAppConfig();
    std::string cfg_json = BridgeDispatcher::serialize_config_changed(default_cfg);
    context.Check(cfg_json.find("\"type\":\"configChanged\"") != std::string::npos,
                  "contains configChanged type");
    context.Check(cfg_json.find("\"capsuleWidthDip\":10") != std::string::npos ||
                      cfg_json.find("\"widthDip\":10") != std::string::npos,
                  "contains capsule width config");
    context.Check(cfg_json.find("\"capsuleEndRounding\"") != std::string::npos,
                  "contains capsuleEndRounding schema");
    context.Check(cfg_json.find("\"schema\"") != std::string::npos, "contains schema metadata");

    const SurfaceChangedDto surface{
        .surface = SurfaceKind::Panel,
        .pinned = true,
        .dock_side = nfm::config::DockSide::Left,
    };
    const std::string surface_json = BridgeDispatcher::serialize_surface_changed(surface);
    context.Check(surface_json.find("\"type\":\"surfaceChanged\"") != std::string::npos,
                  "contains surfaceChanged type");
    context.Check(surface_json.find("\"surface\":\"panel\"") != std::string::npos,
                  "contains authoritative panel surface");
    context.Check(surface_json.find("\"pinned\":true") != std::string::npos,
                  "contains authoritative pin state");
    context.Check(surface_json.find("\"dockSide\":\"left\"") != std::string::npos,
                  "contains authoritative dock side");
}

} // namespace

int main() {
    TestContext context;
    CheckEnumMappings(context);
    CheckDispatchCommands(context);
    CheckBridgeRejections(context);
    CheckNativeSerializers(context);
    return context.failures();
}
