#include "network_flow_monitor/host/app/product_host.h"
#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"
#include "network_flow_monitor/host/bridge/bridge_types.h"
#include "network_flow_monitor/host/bridge/native_bridge.h"
#include "network_flow_monitor/host/coordinator/dock_capsule_coordinator.h"
#include "network_flow_monitor/host/history/traffic_history_store.h"
#include "network_flow_monitor/host/lifecycle/collector_companion.h"
#include "network_flow_monitor/host/platform/app_updater.h"
#include "network_flow_monitor/host/platform/rotating_file_logger.h"
#include "network_flow_monitor/host/platform/startup_registration.h"
#include "network_flow_monitor/host/platform/taskbar_speed_window.h"
#include "network_flow_monitor/host/platform/webview2_environment.h"
#include "network_flow_monitor/host/platform/win32_window.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// === host_bridge_suite ===
namespace host_bridge_suite {
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
    bool check_for_update_called{false};
    bool download_and_apply_update_called{false};
    bool request_traffic_history_called{false};
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
    void on_check_for_update() override {
        check_for_update_called = true;
    }
    void on_download_and_apply_update() override {
        download_and_apply_update_called = true;
    }
    void on_request_traffic_history() override {
        request_traffic_history_called = true;
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
    context.Check(parse_web_command_type("checkForUpdate") == WebCommandType::CheckForUpdate,
                  "Parse checkForUpdate");
    context.Check(parse_web_command_type("downloadAndApplyUpdate") ==
                      WebCommandType::DownloadAndApplyUpdate,
                  "Parse downloadAndApplyUpdate");
    context.Check(parse_web_command_type("requestTrafficHistory") ==
                      WebCommandType::RequestTrafficHistory,
                  "Parse requestTrafficHistory");
    context.Check(parse_web_command_type("invalidCommand") == WebCommandType::Unknown,
                  "Parse invalid command");

    context.Check(to_string(NativeEventType::TrafficTick) == "trafficTick",
                  "TrafficTick to string");
    context.Check(to_string(NativeEventType::UpdateStatus) == "updateStatus",
                  "UpdateStatus to string");
    context.Check(parse_native_event_type("trafficTick") == NativeEventType::TrafficTick,
                  "Parse trafficTick");
    context.Check(parse_native_event_type("updateStatus") == NativeEventType::UpdateStatus,
                  "Parse updateStatus");
    context.Check(to_string(NativeEventType::TrafficHistoryUpdated) == "trafficHistoryUpdated",
                  "TrafficHistoryUpdated to string");
    context.Check(parse_native_event_type("trafficHistoryUpdated") ==
                      NativeEventType::TrafficHistoryUpdated,
                  "Parse trafficHistoryUpdated");
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

    // 12. CheckForUpdate & DownloadAndApplyUpdate
    {
        BridgeErrorDto err;
        CommandResponseDto resp;
        bool ok1 = dispatcher.dispatch_web_message(
            R"({"type":"checkForUpdate","version":1,"commandId":"cmd_12a"})", &err, &resp);
        context.Check(ok1 && handler->check_for_update_called, "dispatch checkForUpdate succeeds");
        bool ok2 = dispatcher.dispatch_web_message(
            R"({"type":"downloadAndApplyUpdate","version":1,"commandId":"cmd_12b"})", &err, &resp);
        context.Check(ok2 && handler->download_and_apply_update_called,
                      "dispatch downloadAndApplyUpdate succeeds");
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

    const UpdateStatusDto update_status{
        .state = "UpdateAvailable",
        .current_version = "0.1.0",
        .latest_version = "0.2.0",
        .download_progress_percent = 65,
        .error_message = "",
    };
    const std::string update_json = BridgeDispatcher::serialize_update_status(update_status);
    context.Check(update_json.find("\"type\":\"updateStatus\"") != std::string::npos,
                  "contains updateStatus type");
    context.Check(update_json.find("\"state\":\"UpdateAvailable\"") != std::string::npos,
                  "contains update state");
    context.Check(update_json.find("\"latestVersion\":\"0.2.0\"") != std::string::npos,
                  "contains latestVersion");
    context.Check(update_json.find("\"downloadProgressPercent\":65") != std::string::npos,
                  "contains downloadProgressPercent");
}

} // namespace

int Run() {
    TestContext context;
    CheckEnumMappings(context);
    CheckDispatchCommands(context);
    CheckBridgeRejections(context);
    CheckNativeSerializers(context);
    return context.failures();
}
} // namespace host_bridge_suite

// === host_webview_policy_suite ===
namespace host_webview_policy_suite {
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

void CheckNavigationPolicies(TestContext& context) {
    using namespace nfm::host::platform;

    // Allowed origins
    context.Check(WebView2Environment::is_allowed_origin("https://nfm.local/"),
                  "allow https://nfm.local/");
    context.Check(WebView2Environment::is_allowed_origin("https://nfm.local"),
                  "allow https://nfm.local");
    context.Check(WebView2Environment::is_allowed_origin("http://localhost:5173/"),
                  "allow localhost");
    context.Check(WebView2Environment::is_allowed_origin("http://127.0.0.1:3000/"),
                  "allow 127.0.0.1");

    // Rejected origins
    context.Check(!WebView2Environment::is_allowed_origin("https://evil.com/"), "reject evil.com");
    context.Check(!WebView2Environment::is_allowed_origin("http://example.org/"),
                  "reject example.org");

    // Allowed navigation URLs
    context.Check(WebView2Environment::is_allowed_navigation(L"https://nfm.local/index.html"),
                  "allow local index.html");
    context.Check(WebView2Environment::is_allowed_navigation(L"about:blank"), "allow about:blank");
    context.Check(!WebView2Environment::is_allowed_navigation(L"https://google.com"),
                  "reject external google.com");
}

void CheckRecoveryGuidance(TestContext& context) {
    using namespace nfm::host::platform;

    auto guidance = WebView2Environment::get_recovery_guidance();
    context.Check(!guidance.empty(), "recovery guidance is non-empty");
    context.Check(guidance.find(L"WebView2") != std::wstring::npos,
                  "recovery guidance mentions WebView2");
}

void CheckFullscreenDesktopAndCloakedExclusion(TestContext& context) {
    using namespace nfm::host::platform;
    // Calling is_foreground_fullscreen_on_same_monitor with nullptr or desktop shell window must
    // never report true for shell/desktop surfaces.
    context.Check(!Win32Window::is_foreground_fullscreen_on_same_monitor(nullptr),
                  "null product window is never fullscreen-yielded");
}

void CheckWebView2ControllerEnsureAliveInterface(TestContext& context) {
    nfm::host::webview::WebView2Controller controller;
    controller.set_memory_target_low(true);
    controller.set_visible(false);
    controller.set_memory_target_low(false);
    controller.set_visible(true);
    context.Check(!controller.is_ready(), "uninitialized controller is not ready");
    context.Check(!controller.ensure_alive(),
                  "uninitialized controller ensure_alive returns false");
}

void CheckSettingsOpenAndCloseSurvival(TestContext& context) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "nfm_settings_repro_test";
    std::filesystem::create_directories(temp_dir);
    {
        std::ofstream index_out(temp_dir / "index.html");
        index_out << "<!doctype html><html><body>test</body></html>";
    }

    nfm::host::app::ProductHostOptions opts{
        .static_assets_dir = temp_dir,
        .config_file_path = temp_dir / "config.json",
    };
    nfm::host::app::ProductHost host(opts);
    // Exercise opening and closing settings without crashing or posting WM_QUIT
    host.on_open_settings("general");
    host.on_hide_monitor();
    MSG msg{};
    const bool quit_posted = (PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE);
    context.Check(!quit_posted, "closing settings window must never post WM_QUIT");
    context.Check(host.coordinator() == nullptr ||
                      host.coordinator()->state().presentation_state !=
                          nfm::host::coordinator::PresentationState::TrayOnly,
                  "closing settings keeps capsule/monitor presentation alive");
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}
void CheckPortableUserDataDirectory(TestContext& context) {
    using namespace nfm::host::platform;
    const std::wstring dir = WebView2Environment::get_user_data_directory();
    context.Check(!dir.empty(), "webview2 user data directory is non-empty");
    context.Check(dir.find(L"data\\webview2") != std::wstring::npos ||
                      dir.find(L"data/webview2") != std::wstring::npos,
                  "webview2 user data directory is inside portable data/webview2");
}

void CheckStartupRegistrationCrossPathCleanupAndEnable(TestContext& context) {
    using namespace nfm::host::platform;

    const std::filesystem::path current_exe =
        LR"(D:\PortableApps\NewFolder\NetworkFlowMonitor.exe)";

    // 1. Command line extraction and cross-path matching
    context.Check(StartupRegistration::extract_executable_path_from_command(
                      LR"("C:\OldDir\Sub Dir\NetworkFlowMonitor.exe" --minimized)") ==
                      LR"(C:\OldDir\Sub Dir\NetworkFlowMonitor.exe)",
                  "extract quoted exe path with arguments");
    context.Check(StartupRegistration::extract_executable_path_from_command(
                      LR"(E:\Tools\NetworkFlowMonitor.exe --silent)") ==
                      LR"(E:\Tools\NetworkFlowMonitor.exe)",
                  "extract unquoted exe path with arguments");
    context.Check(
        StartupRegistration::is_matching_application_entry(
            L"LegacyCustomKeyName", LR"("C:\AnyWhere\NetworkFlowMonitor.exe")", current_exe),
        "match entry by executable filename regardless of key name or path");
    context.Check(StartupRegistration::is_matching_application_entry(
                      L"NetworkFlowMonitor", LR"("Z:\MovedPath\RenamedBinary.exe")", current_exe),
                  "match entry by application identity key name");
    context.Check(!StartupRegistration::is_matching_application_entry(
                      L"OtherApp", LR"("C:\Program Files\OtherApp\OtherApp.exe")", current_exe),
                  "do not match unrelated startup entry");

    // 2. Isolated registry key + Startup folder test
    const std::wstring test_subkey = L"Software\\NetworkFlowMonitor_UnitTest_" +
                                     std::to_wstring(GetCurrentProcessId()) + L"\\Run";
    const auto temp_startup_dir =
        std::filesystem::temp_directory_path() /
        (L"nfm_startup_folder_test_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::create_directories(temp_startup_dir, ec);

    // Seed multiple stale entries pointing to different paths & key names, plus an unrelated app
    HKEY raw_key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, test_subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &raw_key,
                        nullptr) == ERROR_SUCCESS &&
        raw_key != nullptr) {
        const std::wstring stale_1 = LR"("C:\OldLocation1\NetworkFlowMonitor.exe")";
        const std::wstring stale_2 = LR"(E:\Downloads\Temp\networkflowmonitor.exe --tray)";
        const std::wstring unrelated = LR"("C:\Windows\System32\notepad.exe")";
        RegSetValueExW(raw_key, L"NetworkFlowMonitor", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(stale_1.c_str()),
                       static_cast<DWORD>((stale_1.size() + 1U) * sizeof(wchar_t)));
        RegSetValueExW(raw_key, L"NFM_OldCopy_In_Downloads", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(stale_2.c_str()),
                       static_cast<DWORD>((stale_2.size() + 1U) * sizeof(wchar_t)));
        RegSetValueExW(raw_key, L"KeepUnrelatedNotepad", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(unrelated.c_str()),
                       static_cast<DWORD>((unrelated.size() + 1U) * sizeof(wchar_t)));
        RegCloseKey(raw_key);
    }

    // Seed a stale .lnk in startup folder
    {
        std::ofstream lnk_out(temp_startup_dir / L"NetworkFlowMonitor.lnk", std::ios::binary);
        lnk_out << "dummy shortcut";
    }

    StartupRegistrationOptions opts{
        .registry_subkey = test_subkey,
        .app_identity_name = L"NetworkFlowMonitor",
        .startup_folder_override = temp_startup_dir,
    };

    // Disable startup: must delete both registry entries across different paths and the .lnk,
    // while preserving KeepUnrelatedNotepad
    const auto disable_res = StartupRegistration::sync_login_startup(false, current_exe, opts);
    context.Check(disable_res.success, "sync_login_startup(false) succeeds");
    context.Check(disable_res.removed_entries_count == 3U,
                  "sync_login_startup(false) removes all 3 matching entries across paths");
    context.Check(!StartupRegistration::is_registered_for_executable(current_exe, opts),
                  "not registered after disable");
    context.Check(!std::filesystem::exists(temp_startup_dir / L"NetworkFlowMonitor.lnk", ec),
                  "startup folder shortcut removed");

    // Verify unrelated entry is untouched
    if (RegOpenKeyExW(HKEY_CURRENT_USER, test_subkey.c_str(), 0, KEY_QUERY_VALUE, &raw_key) ==
            ERROR_SUCCESS &&
        raw_key != nullptr) {
        DWORD type = 0;
        DWORD size = 0;
        context.Check(RegQueryValueExW(raw_key, L"KeepUnrelatedNotepad", nullptr, &type, nullptr,
                                       &size) == ERROR_SUCCESS,
                      "unrelated startup entry preserved");
        context.Check(RegQueryValueExW(raw_key, L"NFM_OldCopy_In_Downloads", nullptr, &type,
                                       nullptr, &size) == ERROR_FILE_NOT_FOUND,
                      "stale cross-path entry deleted");
        RegCloseKey(raw_key);
    }

    // Enable startup: registers current_exe path; then move exe path and re-enable to verify
    // old path is replaced by new path
    const auto enable_res = StartupRegistration::sync_login_startup(true, current_exe, opts);
    context.Check(enable_res.success, "sync_login_startup(true) succeeds");
    context.Check(StartupRegistration::is_registered_for_executable(current_exe, opts),
                  "registered for current_exe after enable");

    const std::filesystem::path moved_exe = LR"(F:\AnotherDisk\Portable\NetworkFlowMonitor.exe)";
    context.Check(!StartupRegistration::is_registered_for_executable(moved_exe, opts),
                  "moved_exe not yet matching old registered path");
    const auto reenable_res = StartupRegistration::sync_login_startup(true, moved_exe, opts);
    context.Check(reenable_res.success, "re-enabling from moved path succeeds");
    context.Check(reenable_res.removed_entries_count == 1U,
                  "re-enabling cleans previous path entry first");
    context.Check(StartupRegistration::is_registered_for_executable(moved_exe, opts),
                  "registered for moved_exe after re-enable");

    // Clean up isolated registry tree and temp dir
    RegDeleteTreeW(HKEY_CURRENT_USER, (L"Software\\NetworkFlowMonitor_UnitTest_" +
                                       std::to_wstring(GetCurrentProcessId()))
                                          .c_str());
    std::filesystem::remove_all(temp_startup_dir, ec);
}

void CheckRotatingFileLoggerRollover(TestContext& context) {
    using namespace nfm::host::platform;

    const auto log_dir = std::filesystem::temp_directory_path() /
                         (L"nfm_rotating_log_test_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(log_dir, ec);

    RotatingFileLogger logger(RotatingFileLoggerOptions{
        .log_directory = log_dir,
        .file_name = L"nfm-host.log",
        .max_file_size_bytes = 240U, // Small threshold to deterministically trigger rotation
        .max_archive_count = 3U,
    });

    // Off level must not create log file
    logger.write(nfm::config::LogLevel::Off, nfm::config::LogLevel::Error, "Should not be written");
    context.Check(!std::filesystem::exists(logger.active_log_file_path(), ec),
                  "LogLevel::Off writes nothing");

    // Write enough entries to roll over across .1.log, .2.log, .3.log and evict older ones
    for (int i = 0; i < 15; ++i) {
        logger.write(nfm::config::LogLevel::Info, nfm::config::LogLevel::Error,
                     "Diagnostic failure entry #" + std::to_string(i) +
                         " with win32_error=5 and payload padding");
    }

    context.Check(std::filesystem::exists(logger.active_log_file_path(), ec),
                  "active log file exists");
    context.Check(std::filesystem::exists(logger.archive_file_path(1U), ec),
                  "archive .1.log exists");
    context.Check(std::filesystem::exists(logger.archive_file_path(2U), ec),
                  "archive .2.log exists");
    context.Check(std::filesystem::exists(logger.archive_file_path(3U), ec),
                  "archive .3.log exists");
    context.Check(!std::filesystem::exists(logger.archive_file_path(4U), ec),
                  "archive .4.log does not exist (capped at max_archive_count=3)");
    context.Check(std::filesystem::file_size(logger.active_log_file_path(), ec) <= 240U,
                  "active log file stays within max_file_size_bytes cap");

    std::filesystem::remove_all(log_dir, ec);
}

} // namespace

int Run() {
    TestContext context;
    CheckNavigationPolicies(context);
    CheckRecoveryGuidance(context);
    CheckFullscreenDesktopAndCloakedExclusion(context);
    CheckWebView2ControllerEnsureAliveInterface(context);
    CheckSettingsOpenAndCloseSurvival(context);
    CheckPortableUserDataDirectory(context);
    CheckStartupRegistrationCrossPathCleanupAndEnable(context);
    CheckRotatingFileLoggerRollover(context);
    return context.failures();
}
} // namespace host_webview_policy_suite

// === host_lifecycle_suite ===
namespace host_lifecycle_suite {
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

void CheckCollectorCompanionInjections(TestContext& context) {
    using namespace nfm::host::lifecycle;

    CollectorCompanion companion;

    // 1. Check HelloAck injection triggers session_reset_cb
    bool session_reset_called = false;
    std::string instance_id_received;
    companion.set_session_reset_callback([&](const nfm::host::bridge::SessionResetDto& reset) {
        session_reset_called = true;
        instance_id_received = reset.collector_instance_id;
    });

    nfm::ipc::HelloAckMessage ack{
        .protocol_version = 1,
        .collector_instance_id = "test_inst_999",
        .collector_pid = 1234,
        .capabilities = {"traffic_tick_4hz"},
    };
    companion.inject_hello_ack(ack);

    context.Check(session_reset_called, "hello_ack injection calls session_reset_cb");
    context.Check(instance_id_received == "test_inst_999", "instance id matches");
    context.Check(companion.is_running(), "companion state becomes Running after HelloAck");

    // 2. Check TrafficTick injection
    bool traffic_tick_called = false;
    std::uint64_t dl_received = 0;
    companion.set_traffic_tick_callback([&](const nfm::host::bridge::TrafficTickDto& tick) {
        traffic_tick_called = true;
        dl_received = tick.download_bytes_per_sec;
    });

    nfm::ipc::TrafficTickMessage tick{
        .seq = 42,
        .monotonic_ms = 1000,
        .download_bytes_per_sec = 204800,
        .upload_bytes_per_sec = 102400,
    };
    companion.inject_traffic_tick(tick);

    context.Check(traffic_tick_called, "traffic_tick injection calls traffic_tick_cb");
    context.Check(dl_received == 204800, "download rate matches");

    // 3. Check CollectorStatus injection
    bool status_called = false;
    companion.set_collector_status_callback(
        [&](const nfm::host::bridge::CollectorStatusDto& status) {
            status_called = true;
            context.Check(status.active_pids == 8, "active pids match");
        });

    nfm::ipc::CollectorStatusMessage status{
        .monotonic_ms = 1000,
        .etw_session_active = true,
        .dropped_packets = 0,
        .active_pids = 8,
    };
    companion.inject_collector_status(status);

    context.Check(status_called, "collector_status injection calls callback");

    // 3b. Check RankingSnapshot injection
    bool ranking_called = false;
    nfm::host::bridge::RankingSnapshotDto latest_ranking{};
    companion.set_ranking_snapshot_callback([&](const nfm::host::bridge::RankingSnapshotDto& snap) {
        ranking_called = true;
        latest_ranking = snap;
    });
    nfm::ipc::RankingSnapshotMessage snap_msg{
        .seq = 1,
        .monotonic_ms = 1000,
        .status = "Ready",
        .total_rate = {.download_bytes_per_sec = 1000, .upload_bytes_per_sec = 500},
        .applications = {nfm::ipc::RankingApplication{
            .id = "edge",
            .executable_path = "msedge.exe",
            .display_name = "Edge",
            .icon_key = "edge",
            .download_bytes_per_sec = 1000,
            .upload_bytes_per_sec = 500,
        }},
    };
    companion.inject_ranking_snapshot(snap_msg);
    context.Check(ranking_called, "ranking_snapshot injection calls callback");
    context.Check(latest_ranking.applications.size() == 1 &&
                      latest_ranking.applications[0].display_name == "Edge",
                  "ranking app name matches");
    // 3b2. Check excluded programs filtering on RankingSnapshot and TrafficTick
    companion.set_excluded_programs({"mihomo-multi.exe"});
    nfm::ipc::RankingSnapshotMessage proxy_snap{
        .seq = 2,
        .monotonic_ms = 2000,
        .status = "Ready",
        .total_rate = {.download_bytes_per_sec = 3000, .upload_bytes_per_sec = 1500},
        .applications =
            {
                nfm::ipc::RankingApplication{
                    .id = "executable:C:\\Proxy\\mihomo-multi.exe",
                    .executable_path = "C:\\Proxy\\mihomo-multi.exe",
                    .display_name = "mihomo-multi.exe",
                    .icon_key = "mihomo",
                    .download_bytes_per_sec = 1000,
                    .upload_bytes_per_sec = 1000,
                    .total_download_bytes = 50000,
                    .total_upload_bytes = 50000,
                },
                nfm::ipc::RankingApplication{
                    .id = "executable:C:\\Apps\\edge.exe",
                    .executable_path = "C:\\Apps\\edge.exe",
                    .display_name = "Edge",
                    .icon_key = "edge",
                    .download_bytes_per_sec = 1000,
                    .upload_bytes_per_sec = 500,
                    .total_download_bytes = 25000,
                    .total_upload_bytes = 12000,
                },
            },
    };
    companion.inject_ranking_snapshot(proxy_snap);
    context.Check(latest_ranking.applications.size() == 1U &&
                      latest_ranking.applications[0].display_name == "Edge",
                  "excluded program is filtered from ranking applications");
    context.Check(latest_ranking.total_download_bytes_per_sec == 1000U &&
                      latest_ranking.total_upload_bytes_per_sec == 500U,
                  "excluded program rate is subtracted from ranking total rate");
    companion.inject_traffic_tick(nfm::ipc::TrafficTickMessage{
        .seq = 43,
        .monotonic_ms = 2250,
        .download_bytes_per_sec = 2500,
        .upload_bytes_per_sec = 1200,
    });
    context.Check(dl_received == 1500U, "excluded program rate is subtracted from traffic tick");
    const auto observed = companion.connected_applications();
    context.Check(observed.applications.size() >= 2U,
                  "observed connected applications retain both apps for settings selection");
    companion.set_excluded_programs({});
    // 3c. Check inactive ETW session diagnostic logging and error message propagation
    std::vector<std::pair<bool, std::string>> diagnostics;
    companion.set_diagnostic_callback([&](bool is_error, std::string_view message) {
        diagnostics.emplace_back(is_error, std::string(message));
    });
    nfm::host::bridge::CollectorStatusDto latest_dto{};
    companion.set_collector_status_callback(
        [&](const nfm::host::bridge::CollectorStatusDto& dto) { latest_dto = dto; });
    companion.inject_collector_status(nfm::ipc::CollectorStatusMessage{
        .monotonic_ms = 2000,
        .etw_session_active = false,
        .dropped_packets = 0,
        .active_pids = 0,
    });
    context.Check(!latest_dto.etw_session_active && !latest_dto.last_error.empty(),
                  "inactive ETW status sets error detail");
    context.Check(!diagnostics.empty() && diagnostics.back().first,
                  "inactive ETW status emits error diagnostic");

    // 3d. Check Backoff state transition emits diagnostic and zeroes rates
    diagnostics.clear();
    dl_received = 99999;
    companion.StateChanged(nfm::ipc::IpcClientState::Backoff);
    context.Check(!diagnostics.empty() && diagnostics.back().first,
                  "IPC Backoff from Running emits error diagnostic");
    context.Check(dl_received == 0 && latest_ranking.applications.empty() &&
                      !latest_dto.etw_session_active,
                  "IPC Backoff zeroes traffic tick, clears ranking, and marks ETW inactive");

    // 4. Test stop zeroes rates and updates status
    dl_received = 88888;
    companion.stop();
    context.Check(!companion.is_running(), "companion not running after stop");
    context.Check(companion.get_info().state == CollectorState::Stopped, "state is Stopped");
    context.Check(dl_received == 0 && latest_dto.state == "Stopped" &&
                      !latest_dto.etw_session_active,
                  "stop zeroes traffic tick and broadcasts Stopped status");
}

struct PopupMenuProbe {
    const nfm::host::platform::Win32Window* window{nullptr};
    bool menu_window_found{false};
    bool host_topmost_during_menu{false};
};

constexpr wchar_t kPopupMenuProbeProp[] = L"nfm_popup_menu_probe";
constexpr UINT_PTR kPopupMenuProbeTimerId = 0x7001;
constexpr UINT kPopupMenuProbeDelayMs = 150;

VOID CALLBACK PopupMenuProbeTimer(HWND owner, UINT, UINT_PTR timer_id, DWORD) {
    KillTimer(owner, timer_id);
    auto* probe = static_cast<PopupMenuProbe*>(GetPropW(owner, kPopupMenuProbeProp));
    if (probe != nullptr && probe->window != nullptr) {
        probe->menu_window_found = FindWindowW(L"#32768", nullptr) != nullptr;
        probe->host_topmost_during_menu = probe->window->is_topmost();
    }
    EndMenu();
}

// Shows a real popup menu owned by `owner` and samples the host's topmost state while the menu
// window is on screen.
void ShowPopupMenuAndProbe(HWND owner, int x, int y, PopupMenuProbe& probe) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING, 1, L"Probe");
    SetPropW(owner, kPopupMenuProbeProp, &probe);
    SetTimer(owner, kPopupMenuProbeTimerId, kPopupMenuProbeDelayMs, PopupMenuProbeTimer);
    TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, owner, nullptr);
    RemovePropW(owner, kPopupMenuProbeProp);
    DestroyMenu(menu);
}

void CheckWindowPresentationRecovery(TestContext& context) {
    using namespace nfm::host::platform;

    WindowConfig config{
        .title = L"Network Flow Monitor Test Window",
        .class_name = L"NetworkFlowMonitor_TestWindow",
        .initial_x = 100,
        .initial_y = 100,
        .initial_width = 340,
        .initial_height = 64,
        .frameless = true,
        .topmost = true,
        .tool_window = true,
        .transparent_backdrop = true,
    };
    Win32Window window(config);
    context.Check(window.create(nullptr), "test window creates headlessly");
    if (window.hwnd() == nullptr) {
        return;
    }
    context.Check(!window.is_presented(), "unshown test window is not presented");

    window.set_bounds(100, 120, 340, 64);
    RECT bounds{};
    context.Check(window.get_bounds(&bounds) && bounds.left == 100 && bounds.top == 120 &&
                      (bounds.right - bounds.left) == 340 && (bounds.bottom - bounds.top) == 64,
                  "set_bounds applies requested geometry");

    // An external move (shell snap, display topology change) must be reconciled even though the
    // requested geometry did not change.
    SetWindowPos(window.hwnd(), nullptr, 400, 300, 340, 64, SWP_NOZORDER | SWP_NOACTIVATE);
    window.set_bounds(100, 120, 340, 64);
    context.Check(window.get_bounds(&bounds) && bounds.left == 100 && bounds.top == 120,
                  "set_bounds repairs externally moved window");

    window.set_topmost(true);
    context.Check(window.is_topmost(), "set_topmost applies topmost extended style");
    SetWindowPos(window.hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    context.Check(!window.is_topmost(), "external topmost loss is observable");
    window.set_topmost(true);
    context.Check(window.is_topmost(), "set_topmost restores externally lost topmost style");

    // Verify force_z_order brings the topmost window above a competing topmost window even when
    // WS_EX_TOPMOST is already set on both windows, and that is_topmost() detects overlapping
    // topmost windows as well as owner-chain Z-order burial behind non-topmost windows while
    // WS_EX_TOPMOST remains set on GWL_EXSTYLE.
    WindowConfig rival_config = config;
    rival_config.title = L"Network Flow Monitor Rival Topmost";
    rival_config.class_name = L"NetworkFlowMonitor_RivalTopmostWindow";
    Win32Window rival(rival_config);
    if (rival.create(nullptr) && rival.hwnd() != nullptr) {
        rival.set_bounds(100, 120, 340, 64);
        rival.set_topmost(true);
        SetWindowPos(rival.hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        window.set_topmost(true, true);
        bool window_above_rival = false;
        for (HWND cur = GetWindow(rival.hwnd(), GW_HWNDPREV); cur != nullptr;
             cur = GetWindow(cur, GW_HWNDPREV)) {
            if (cur == window.hwnd()) {
                window_above_rival = true;
                break;
            }
        }
        context.Check(window_above_rival,
                      "set_topmost(true, true) raises Z-order above competing topmost window");

        // A real popup menu (class #32768) raised above the visible host over its rectangle must
        // not make is_topmost() report loss, otherwise the watchdog re-raises the host over the
        // menu.
        window.show(SW_SHOWNOACTIVATE);
        window.set_topmost(true, true);
        PopupMenuProbe menu_probe{.window = &window};
        ShowPopupMenuAndProbe(rival.hwnd(), 110, 130, menu_probe);
        window.show(SW_HIDE);
        context.Check(menu_probe.menu_window_found, "popup menu window is on screen during probe");
        context.Check(menu_probe.host_topmost_during_menu,
                      "is_topmost ignores an overlapping popup menu above the host");

        // Simulate an owned helper window (like Windows' per-thread Default IME / MSCTFIME UI)
        // whose reordering without SWP_NOOWNERZORDER drags the owner behind a non-topmost window
        // while leaving WS_EX_TOPMOST set on the owner's GWL_EXSTYLE.
        HWND non_topmost =
            CreateWindowExW(WS_EX_TOOLWINDOW, L"Static", L"NFM_NonTopmostAnchor", WS_POPUP, 100,
                            120, 340, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND owned_helper =
            CreateWindowExW(WS_EX_TOOLWINDOW, L"Static", L"NFM_OwnedHelper", WS_POPUP, 0, 0, 1, 1,
                            window.hwnd(), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (non_topmost != nullptr && owned_helper != nullptr) {
            SetWindowPos(non_topmost, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            SetWindowPos(owned_helper, HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            context.Check(!window.is_topmost(),
                          "reordering owned helper to HWND_BOTTOM without SWP_NOOWNERZORDER "
                          "demotes owner window");
            window.set_topmost(true);
            context.Check(window.is_topmost(),
                          "set_topmost with SWP_NOOWNERZORDER recovers owner window from bottom of "
                          "Z-order even when owned helper is at HWND_BOTTOM");
        }
        if (owned_helper != nullptr) {
            DestroyWindow(owned_helper);
        }
        if (non_topmost != nullptr) {
            DestroyWindow(non_topmost);
        }
        rival.close();
    }

    window.close();
}

void CheckCollapseFinishedDoesNotSuppressSurfacePublish(TestContext& context) {
    const auto temp_dir = std::filesystem::temp_directory_path() /
                          ("nfm_collapse_publish_test_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);

    nfm::host::app::ProductHostOptions opts{
        .static_assets_dir = temp_dir,
        .config_file_path = temp_dir / "config.json",
    };
    nfm::host::app::ProductHost host(opts);
    context.Check(host.coordinator() != nullptr, "headless ProductHost initializes coordinator");
    if (host.coordinator() != nullptr) {
        // 1. Open monitor panel -> last_published_surface_ becomes Panel
        host.on_show_monitor();
        context.Check(host.last_published_surface() == nfm::host::bridge::SurfaceKind::Panel,
                      "on_show_monitor publishes Panel surface");

        // 2. Start panel shrink bounds timer (240 ms) by shrinking panel height from 468 to 320
        host.on_resize_panel(320);

        // 3. Pointer leaves panel -> starts 500 ms default dismiss timer; advance 260 ms so dismiss
        //    has 240 ms remaining, then trigger 240 ms panel_shrink_bounds_remaining_ at 280 DIP so
        //    both timers have the exact same 240 ms remaining.
        host.on_surface_pointer_leave();
        host.on_surface_clock_tick(std::chrono::milliseconds{260});
        host.on_resize_panel(280);
        // 4. Advance 240 ms in a single tick: both dismiss_timer and panel_shrink_bounds_remaining_
        //    expire on this tick (collapse_finished == true AND presentation_state transitions from
        //    PanelVisible to CapsuleVisible). publish_surface_state() MUST still fire!
        host.on_surface_clock_tick(std::chrono::milliseconds{240});
        context.Check(host.coordinator()->state().presentation_state ==
                          nfm::host::coordinator::PresentationState::CapsuleVisible,
                      "coordinator transitioned to CapsuleVisible");
        context.Check(host.last_published_surface() == nfm::host::bridge::SurfaceKind::Capsule,
                      "surfaceChanged(Capsule) is published even when collapse_finished is true on "
                      "the same tick");
    }
    std::filesystem::remove_all(temp_dir, ec);
}

void CheckTickElapsedConversion(TestContext& context) {
    using namespace nfm::host::platform;
    constexpr std::int64_t kCounterFrequency = 10'000'000; // 10 MHz QPC

    context.Check(tick_elapsed_from_counter_delta(0, kCounterFrequency).count() == 0,
                  "zero counter delta yields zero elapsed");
    context.Check(tick_elapsed_from_counter_delta(-1000, kCounterFrequency).count() == 0,
                  "negative counter delta clamps to zero");
    context.Check(tick_elapsed_from_counter_delta(80'000, kCounterFrequency).count() == 8,
                  "8ms counter delta converts without drift");
    context.Check(tick_elapsed_from_counter_delta(157'000, kCounterFrequency).count() == 16,
                  "15.7ms counter delta rounds to the nearest millisecond");
    context.Check(tick_elapsed_from_counter_delta(5'000'000, kCounterFrequency).count() ==
                      kMaximumTickElapsed.count(),
                  "blocked interval clamps to the maximum frame delta");
    context.Check(
        tick_elapsed_from_counter_delta(5'000'000, kCounterFrequency, kMaximumObservedTickGap)
                .count() == 500,
        "diagnostic gap reports stall durations beyond the frame clamp");
    context.Check(tick_elapsed_from_counter_delta(80'000, 0).count() == 0,
                  "invalid counter frequency yields zero elapsed");
}

struct ContextMenuSnapshot {
    std::vector<std::wstring> labels;
    bool captured{false};
};

constexpr wchar_t kContextMenuProbeProp[] = L"nfm_context_menu_content_probe";
constexpr UINT_PTR kContextMenuProbeTimerId = 0x7002;
constexpr UINT kContextMenuProbeDelayMs = 150;

VOID CALLBACK ContextMenuProbeTimer(HWND owner, UINT, UINT_PTR timer_id, DWORD) {
    KillTimer(owner, timer_id);
    auto* probe = static_cast<ContextMenuSnapshot*>(GetPropW(owner, kContextMenuProbeProp));
    if (probe != nullptr) {
        HWND menu_window = FindWindowW(L"#32768", nullptr);
        if (menu_window != nullptr) {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            auto* menu = reinterpret_cast<HMENU>(SendMessageW(menu_window, MN_GETHMENU, 0, 0));
            if (menu != nullptr) {
                const int count = GetMenuItemCount(menu);
                for (int index = 0; index < count; ++index) {
                    wchar_t label[128]{};
                    GetMenuStringW(menu, static_cast<UINT>(index), label, 128, MF_BYPOSITION);
                    probe->labels.emplace_back(label);
                }
                probe->captured = true;
            }
        }
    }
    EndMenu();
}

class ContextMenuRecordingListener : public nfm::host::platform::IWindowEventListener {
  public:
    void on_window_created(HWND /*hwnd*/) override {}
    void on_window_size_changed(int /*width_px*/, int /*height_px*/) override {}
    void on_window_dpi_changed(std::uint32_t /*new_dpi*/, const RECT* /*suggested_rect*/) override {
    }
    void on_window_close_requested() override {}
    void on_window_destroyed() override {}
    void on_context_menu_opened() override {
        opened_ = true;
    }

    [[nodiscard]] bool opened() const noexcept {
        return opened_;
    }

  private:
    bool opened_{false};
};

ContextMenuSnapshot CaptureContextMenu(nfm::host::platform::Win32Window& window,
                                       const nfm::host::platform::ContextMenuOptions& options) {
    ContextMenuSnapshot snapshot;
    SetPropW(window.hwnd(), kContextMenuProbeProp, &snapshot);
    SetTimer(window.hwnd(), kContextMenuProbeTimerId, kContextMenuProbeDelayMs,
             ContextMenuProbeTimer);
    window.show_context_menu(options);
    RemovePropW(window.hwnd(), kContextMenuProbeProp);
    return snapshot;
}

[[nodiscard]] bool HasMenuLabel(const ContextMenuSnapshot& snapshot, std::wstring_view label) {
    return std::ranges::any_of(snapshot.labels,
                               [label](const std::wstring& item) { return item == label; });
}

void CheckContextMenuExposesTaskbarMonitorSwitch(TestContext& context) {
    using namespace nfm::host::platform;

    WindowConfig config{
        .title = L"Network Flow Monitor Context Menu Test",
        .class_name = L"NetworkFlowMonitor_ContextMenuTestWindow",
        .initial_x = 100,
        .initial_y = 100,
        .initial_width = 340,
        .initial_height = 64,
        .frameless = true,
        .topmost = true,
        .tool_window = true,
        .transparent_backdrop = true,
    };
    ContextMenuRecordingListener listener;
    Win32Window window(config);
    context.Check(window.create(&listener), "context menu test window creates headlessly");
    if (window.hwnd() == nullptr) {
        return;
    }

    const auto visible_menu =
        CaptureContextMenu(window, ContextMenuOptions{.screen_x = 120,
                                                      .screen_y = 120,
                                                      .is_taskbar_monitor_visible = true,
                                                      .is_chinese = true});
    context.Check(visible_menu.captured, "context menu captured while the overlay is shown");
    context.Check(HasMenuLabel(visible_menu, L"隐藏任务栏监控"),
                  "menu offers hiding the taskbar monitor while it is shown");

    const auto hidden_menu =
        CaptureContextMenu(window, ContextMenuOptions{.screen_x = 120,
                                                      .screen_y = 120,
                                                      .is_taskbar_monitor_visible = false,
                                                      .is_chinese = false});
    context.Check(hidden_menu.captured, "context menu captured while the overlay is hidden");
    context.Check(HasMenuLabel(hidden_menu, L"Show Taskbar Monitor"),
                  "menu offers showing the taskbar monitor while it is hidden");
    context.Check(listener.opened(), "context menu open notification reaches the listener");
}
} // namespace

int Run() {
    TestContext context;
    CheckCollectorCompanionInjections(context);
    CheckWindowPresentationRecovery(context);
    CheckTickElapsedConversion(context);
    CheckCollapseFinishedDoesNotSuppressSurfacePublish(context);
    CheckContextMenuExposesTaskbarMonitorSwitch(context);
    return context.failures();
}
} // namespace host_lifecycle_suite

// === dock_capsule_coordinator_suite ===
namespace dock_capsule_coordinator_suite {
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

using namespace nfm::host::coordinator;
using namespace std::chrono_literals;

void TestInitialStateAndGeometry(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.capsule_width_dip = 10;
    cfg.floating.capsule_height_dip = 64;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;
    cfg.floating.anchor_vertical_offset_dip = 100;
    cfg.floating.nfm_pinned = false;

    DockCapsuleCoordinator coord(cfg);
    const auto& s = coord.state();

    ctx.Check(s.presentation_state == PresentationState::CapsuleVisible,
              "Initial presentation state is CapsuleVisible");
    ctx.Check(!s.is_pinned, "Initial pinned is false");
    ctx.Check(s.is_topmost, "Initial topmost is true");
    ctx.Check(s.anchor_side == nfm::config::DockSide::Right, "Initial anchor side is Right");

    // Hit target width is at least 16 DIP (16px at 96 DPI)
    ctx.Check(s.geometry.width_px == 16,
              "Geometry width at 96 DPI is at least 16px inward hit target");
    ctx.Check(s.geometry.height_px == 64, "Geometry height at 96 DPI is 64px");
    ctx.Check(s.geometry.x_px == 1920 - 16, "Anchored to right edge at x = 1904");
    ctx.Check(s.geometry.y_px == 100, "Anchored at y = 100");
}

void TestHoverAndClickTransitions(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.interaction.hover_delay = 350ms;
    cfg.interaction.dismiss_delay = 500ms;

    DockCapsuleCoordinator coord(cfg);

    // 1. Hover Capsule
    coord.on_pointer_enter_capsule();
    ctx.Check(coord.state().is_hover_timer_active, "Hover timer active after enter");

    // Advance 200ms (not yet reached 350ms)
    coord.advance_time(200ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Still CapsuleVisible before 350ms");

    // Advance remaining 150ms -> expands to PanelVisible
    coord.advance_time(150ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Expands to PanelVisible after 350ms hover");
    ctx.Check(coord.state().geometry.width_px == 340, "Panel width is 340px");
    // 2. Pointer leaves capsule to outside
    coord.on_pointer_leave_capsule();
    ctx.Check(coord.state().is_dismiss_timer_active, "Dismiss timer active after leave");

    // Re-enter panel at 250ms -> cancels dismissal
    coord.advance_time(250ms);
    coord.on_pointer_enter_panel();
    ctx.Check(!coord.state().is_dismiss_timer_active, "Dismiss timer canceled on panel re-entry");

    // Leave panel again -> advance 500ms -> collapses to CapsuleVisible
    coord.on_pointer_leave_panel();
    ctx.Check(coord.state().is_dismiss_timer_active, "Dismiss timer active after panel leave");
    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Collapses to CapsuleVisible after 500ms dismiss");

    // 3. Click immediately expands without waiting for hover delay
    coord.on_pointer_click_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Click immediately expands to PanelVisible");
}

void TestPinBehavior(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.interaction.dismiss_delay = 500ms;

    DockCapsuleCoordinator coord(cfg);
    coord.show_monitor();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "PanelVisible after show_monitor");

    // Set pin
    coord.set_pin(true);
    ctx.Check(coord.state().is_pinned, "is_pinned is true");

    // Pointer leaves panel
    coord.on_pointer_leave_panel();
    ctx.Check(!coord.state().is_dismiss_timer_active, "Dismiss timer is NOT active when pinned");

    // Advance time 2000ms
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Panel remains visible when pinned");

    // Unpin while pointer is outside
    coord.set_pin(false);
    ctx.Check(coord.state().is_dismiss_timer_active,
              "Dismiss timer activated on unpin while outside");
    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Dismissed after unpin");
}

void TestDragSnappingAndTieRules(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.capsule_width_dip = 10;
    cfg.floating.capsule_height_dip = 64;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;

    std::vector<MonitorArea> monitors = {
        MonitorArea{
            .monitor_id = "mon1",
            .left_px = 0,
            .top_px = 0,
            .right_px = 1920,
            .bottom_px = 1080,
            .work_left_px = 0,
            .work_top_px = 0,
            .work_right_px = 1920,
            .work_bottom_px = 1040,
            .is_primary = true,
            .dpi = 96,
        },
        MonitorArea{
            .monitor_id = "mon2",
            .left_px = 1920,
            .top_px = 0,
            .right_px = 3840,
            .bottom_px = 1080,
            .work_left_px = 1920,
            .work_top_px = 0,
            .work_right_px = 3840,
            .work_bottom_px = 1040,
            .is_primary = false,
            .dpi = 96,
        },
    };

    DockCapsuleCoordinator coord(cfg, monitors);

    // 1. Small movement (< threshold) should be a click, not drag
    coord.on_pointer_down_capsule(ScreenPoint{1900, 100});
    coord.on_pointer_move(ScreenPoint{1902, 101});
    ctx.Check(!coord.state().is_dragging, "Small movement below threshold is not dragging");
    coord.on_pointer_up(ScreenPoint{1902, 101});
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Pointer up after sub-threshold movement expands as click");

    // Collapse back
    coord.hide_monitor();
    coord.show_monitor();
    coord.on_pointer_leave_panel();
    coord.advance_time(500ms);

    // 2. Drag to left side of primary monitor
    coord.on_pointer_down_capsule(ScreenPoint{1900, 100});
    coord.on_pointer_move(ScreenPoint{500, 200});
    ctx.Check(coord.state().is_dragging, "Dragging is active when moved past threshold");
    ctx.Check(coord.state().geometry.x_px == 500 - (coord.state().geometry.width_px / 2),
              "Window follows pointer while dragging");
    coord.on_pointer_up(ScreenPoint{500, 200});

    ctx.Check(!coord.state().is_dragging, "Dragging ended");
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Left,
              "Snaps to Left side when released left of midpoint");
    ctx.Check(coord.state().anchor_updated, "Anchor update was emitted");
    ctx.Check(coord.state().geometry.x_px == 0, "Geometry x is 0 on Left anchor");

    // 3. Drag to exact midpoint of primary monitor (960) -> tie rule preserves Left
    coord.clear_emitted_updates();
    coord.on_pointer_down_capsule(ScreenPoint{0, 200});
    coord.on_pointer_move(ScreenPoint{960, 300});
    coord.on_pointer_up(ScreenPoint{960, 300});
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Left,
              "Tie-break preserves existing Left side");

    // 4. Drag to second monitor
    coord.clear_emitted_updates();
    coord.on_pointer_down_capsule(ScreenPoint{0, 300});
    coord.on_pointer_move(ScreenPoint{3500, 400});
    coord.on_pointer_up(ScreenPoint{3500, 400});
    ctx.Check(coord.state().anchor_monitor_id == "mon2", "Anchor monitor updated to mon2");
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Right,
              "Snaps to Right side of mon2");
    ctx.Check(coord.state().geometry.x_px == 3840 - 16,
              "Geometry positioned at right edge of mon2");

    // 5. Cancel drag restores without updating anchor
    coord.clear_emitted_updates();
    coord.on_pointer_down_capsule(ScreenPoint{3840 - 16, 400});
    coord.on_pointer_move(ScreenPoint{100, 100});
    coord.on_cancel_drag();
    ctx.Check(!coord.state().is_dragging, "Drag canceled");
    ctx.Check(coord.state().anchor_monitor_id == "mon2", "Anchor monitor unchanged on cancel");
}

void TestConfiguredSystemDragThreshold(TestContext& ctx) {
    DockCapsuleCoordinator coord;
    coord.update_drag_threshold(DragThresholdPx{.horizontal_px = 10, .vertical_px = 12});
    coord.on_pointer_down_capsule(ScreenPoint{100, 100});
    coord.on_pointer_move(ScreenPoint{109, 111});
    ctx.Check(!coord.state().is_dragging, "Movement below system thresholds remains a click");
    coord.on_pointer_move(ScreenPoint{110, 111});
    ctx.Check(coord.state().is_dragging, "Horizontal system drag threshold starts dragging");
}

void TestContextMenuPausesDismiss(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.interaction.dismiss_delay = 500ms;

    DockCapsuleCoordinator coord(cfg);
    coord.on_pointer_click_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible, "PanelVisible");

    // Open context menu
    coord.on_context_menu_opened();
    ctx.Check(coord.state().is_context_menu_open, "Context menu open");

    // Pointer leaves panel while context menu is open
    coord.on_pointer_leave_panel();
    ctx.Check(!coord.state().is_dismiss_timer_active,
              "Dismiss timer paused while context menu is open");

    // Advance 2000ms
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Panel remains visible with context menu open");

    // Close context menu with cursor outside
    coord.on_context_menu_closed(false);
    ctx.Check(!coord.state().is_context_menu_open, "Context menu closed");
    ctx.Check(coord.state().is_dismiss_timer_active,
              "Dismiss timer started after menu closed outside");

    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Dismissed after dismiss delay");

    // Right-click context menu on capsule pauses hover expansion
    coord.on_pointer_enter_capsule();
    ctx.Check(coord.state().is_hover_timer_active, "Hover timer starts on capsule entry");
    coord.on_context_menu_opened();
    ctx.Check(!coord.state().is_hover_timer_active,
              "Hover timer canceled when capsule context menu opens");
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Capsule remains in capsule state while context menu is open");
    coord.on_context_menu_closed(true);
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Closing context menu keeps capsule state until a new hover interaction");
}

void TestAutoHide(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.auto_hide_enabled = true;
    cfg.floating.auto_hide_delay = 3000ms;

    DockCapsuleCoordinator coord(cfg);
    coord.on_pointer_enter_capsule();
    coord.on_pointer_leave_capsule();

    ctx.Check(coord.state().is_auto_hide_timer_active,
              "Auto-hide timer active after pointer leave");

    coord.advance_time(250ms);
    ctx.Check(!coord.state().is_auto_hidden, "Not yet auto-hidden at 250ms");

    coord.advance_time(250ms);
    ctx.Check(coord.state().is_auto_hidden, "Auto-hidden at 500ms");
    ctx.Check(
        coord.state().geometry.x_px == 1920 - 16,
        "Auto-hidden capsule retains 16px inward hit surface while UI retracts to edge strip");
    // Pointer enters capsule wakes it up
    coord.on_pointer_enter_capsule();
    ctx.Check(!coord.state().is_auto_hidden, "Auto-hide cleared on pointer enter");
    ctx.Check(coord.state().geometry.x_px == 1920 - 16,
              "Pointer entry restores the full inward hit surface");

    // Hover expands to panel, then leave panel -> dismisses to capsule -> auto-hides again
    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Hover expands capsule to panel");
    coord.on_pointer_leave_panel();
    coord.advance_time(600ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Panel dismisses back to capsule");
    ctx.Check(coord.state().is_auto_hide_timer_active,
              "Auto-hide timer restarts automatically when panel dismisses to capsule");
    coord.advance_time(500ms);
    ctx.Check(coord.state().is_auto_hidden, "Capsule auto-hides again after panel dismissal");

    // Tray show_monitor clears auto-hide and opens panel; show_capsule collapses back immediately
    coord.show_monitor();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "show_monitor transitions to PanelVisible");
    ctx.Check(!coord.state().is_auto_hidden, "show_monitor clears auto-hidden state");
    coord.show_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "show_capsule immediately collapses unpinned panel back to capsule");
}

void TestFullscreenTopmostYield(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.nfm_pinned = true;
    cfg.floating.ignore_hover_when_fullscreen = true;
    cfg.floating.hide_capsule_when_fullscreen = false;
    cfg.interaction.hover_delay = std::chrono::milliseconds{350};

    DockCapsuleCoordinator coord(cfg);
    ctx.Check(coord.state().is_topmost, "Topmost true by default");
    ctx.Check(coord.state().is_pinned, "Pinned initially");
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "PanelVisible initially when pinned");

    // Fullscreen window active on same monitor: breaks pin, collapses to capsule, enables
    // click-through
    coord.set_fullscreen_active_on_same_monitor(true);
    ctx.Check(!coord.state().is_pinned, "Fullscreen breaks pin state");
    ctx.Check(coord.state().pin_updated, "Fullscreen emits pin_updated");
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Fullscreen collapses panel back to capsule");
    ctx.Check(coord.state().is_click_through,
              "Capsule enables click-through when fullscreen active and ignore_hover is true");
    ctx.Check(coord.state().is_topmost, "Capsule stays topmost when not fullscreen-hidden");

    coord.on_pointer_enter_capsule();
    ctx.Check(!coord.state().is_hover_timer_active,
              "Hover timer ignored during fullscreen click-through");
    coord.on_pointer_click_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Click ignored during fullscreen click-through");

    // Explicit tray show_monitor must open panel and survive steady-state fullscreen clock ticks
    coord.show_monitor();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Tray show_monitor opens panel even if fullscreen was active");
    coord.set_fullscreen_active_on_same_monitor(true);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Steady-state fullscreen tick does not immediately collapse tray-opened panel");
    coord.show_capsule();

    // Enable hide_capsule_when_fullscreen
    cfg.floating.nfm_pinned = false;
    cfg.floating.hide_capsule_when_fullscreen = true;
    coord.update_config(cfg);
    ctx.Check(coord.state().is_fullscreen_hidden,
              "Capsule marked fullscreen-hidden when hide_capsule_when_fullscreen is enabled");
    ctx.Check(!coord.state().is_click_through,
              "Click-through false when capsule is completely hidden");

    // Fullscreen window inactive
    coord.set_fullscreen_active_on_same_monitor(false);
    ctx.Check(coord.state().is_topmost, "Topmost restored when fullscreen inactive");
    ctx.Check(!coord.state().is_fullscreen_hidden,
              "Fullscreen hidden cleared when fullscreen inactive");
    ctx.Check(!coord.state().is_click_through, "Click-through cleared when fullscreen inactive");

    // If both fullscreen policies are disabled in config
    cfg.floating.ignore_hover_when_fullscreen = false;
    cfg.floating.hide_capsule_when_fullscreen = false;
    coord.update_config(cfg);
    coord.set_fullscreen_active_on_same_monitor(true);
    ctx.Check(!coord.state().is_click_through,
              "Click-through NOT enabled when ignore_hover_when_fullscreen is disabled");
    coord.on_pointer_enter_capsule();
    ctx.Check(coord.state().is_hover_timer_active,
              "Hover timer starts when ignore_hover_when_fullscreen is disabled");
    // Simulate periodic clock tick calling set_fullscreen_active_on_same_monitor(true)
    coord.set_fullscreen_active_on_same_monitor(true);
    coord.advance_time(std::chrono::milliseconds{360});
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Hover expands to panel during fullscreen when lock is disabled");
}

void TestDpiScalingAndDisplayChangeFallback(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.capsule_width_dip = 10;
    cfg.floating.capsule_height_dip = 64;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;
    cfg.floating.anchor_vertical_offset_dip = 100;

    std::vector<MonitorArea> monitors = {
        MonitorArea{
            .monitor_id = "4k_mon",
            .left_px = 0,
            .top_px = 0,
            .right_px = 3840,
            .bottom_px = 2160,
            .work_left_px = 0,
            .work_top_px = 0,
            .work_right_px = 3840,
            .work_bottom_px = 2100,
            .is_primary = true,
            .dpi = 144, // 1.5x scaling
        },
    };

    DockCapsuleCoordinator coord(cfg, monitors);
    const auto& s = coord.state();

    // 16 DIP * 1.5 = 24 px
    ctx.Check(s.geometry.width_px == 24, "Scaled width is 24px at 144 DPI");
    // 64 DIP * 1.5 = 96 px
    ctx.Check(s.geometry.height_px == 96, "Scaled height is 96px at 144 DPI");
    ctx.Check(s.geometry.x_px == 3840 - 24, "Scaled x position is 3816");
    // 100 DIP * 1.5 = 150 px
    ctx.Check(s.geometry.y_px == 150, "Scaled y position is 150");

    // Display unplug fallback: replace monitors with new 1080p monitor
    std::vector<MonitorArea> new_monitors = {
        MonitorArea{
            .monitor_id = "new_mon",
            .left_px = 0,
            .top_px = 0,
            .right_px = 1920,
            .bottom_px = 1080,
            .work_left_px = 0,
            .work_top_px = 0,
            .work_right_px = 1920,
            .work_bottom_px = 1040,
            .is_primary = true,
            .dpi = 96,
        },
    };
    coord.update_monitors(new_monitors);
    ctx.Check(coord.state().geometry.width_px == 16, "Width updated to 16px on new 96 DPI monitor");
    ctx.Check(coord.state().geometry.x_px == 1920 - 16,
              "X position updated to 1904 on new monitor");
}

void TestAutoHideTaskbarEdgeReservation(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;
    std::vector<MonitorArea> monitors = {MonitorArea{
        .monitor_id = "taskbar-monitor",
        .left_px = 0,
        .top_px = 0,
        .right_px = 1920,
        .bottom_px = 1080,
        .work_left_px = 0,
        .work_top_px = 0,
        .work_right_px = 1920,
        .work_bottom_px = 1080,
        .auto_hide_right_reservation_px = 4,
        .is_primary = true,
        .dpi = 96,
    }};
    DockCapsuleCoordinator coord(cfg, monitors);
    ctx.Check(coord.state().geometry.x_px == 1900,
              "Right auto-hide taskbar edge keeps its four-pixel reveal reservation");
}

void TestPointerMoveAutoRecoversLostPointerUp(TestContext& ctx) {
    DockCapsuleCoordinator coord;
    coord.on_pointer_down_capsule(ScreenPoint{1900, 100});
    coord.on_pointer_move(ScreenPoint{500, 200});
    ctx.Check(coord.state().is_dragging, "Dragging active after pointer move past threshold");

    // If WM_LBUTTONUP was lost (e.g., focus stolen) and pointer later moves without button pressed,
    // calling on_pointer_up snaps cleanly back to edge and exits dragging state.
    coord.on_pointer_up(ScreenPoint{500, 200});
    ctx.Check(!coord.state().is_dragging, "Dragging cleared after recovery pointer up");
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Left,
              "Snapped to Left edge on recovery");
}

void TestPanelHeaderDragThresholdAndSnap(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;
    cfg.floating.anchor_vertical_offset_dip = 120;

    DockCapsuleCoordinator coord(cfg);
    coord.show_monitor();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "PanelVisible before header drag");

    // Start drag candidate from panel header at (1700, 135)
    coord.on_pointer_down_capsule(ScreenPoint{1700, 135});
    coord.on_pointer_move(ScreenPoint{1701, 136});
    ctx.Check(!coord.state().is_dragging,
              "Sub-threshold movement on panel header does not start drag");

    // Cross system drag threshold -> activates dragging and enters native move loop
    coord.on_pointer_move(ScreenPoint{1680, 145});
    ctx.Check(coord.state().is_dragging, "Panel header movement past threshold activates dragging");

    // Release after native SC_MOVE loop at (300, 255)
    coord.on_pointer_up(ScreenPoint{300, 255});
    ctx.Check(!coord.state().is_dragging, "Dragging ended after release");
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Left,
              "Panel snaps to Left side after native move release");
    ctx.Check(coord.state().anchor_vertical_offset_dip == 240,
              "Vertical offset preserves grab offset after native move release");
    ctx.Check(coord.state().geometry.x_px == 0, "Panel docked at left edge x = 0");
}

} // namespace

int Run() {
    TestContext ctx;
    TestInitialStateAndGeometry(ctx);
    TestHoverAndClickTransitions(ctx);
    TestPinBehavior(ctx);
    TestDragSnappingAndTieRules(ctx);
    TestConfiguredSystemDragThreshold(ctx);
    TestContextMenuPausesDismiss(ctx);
    TestAutoHide(ctx);
    TestFullscreenTopmostYield(ctx);
    TestDpiScalingAndDisplayChangeFallback(ctx);
    TestAutoHideTaskbarEdgeReservation(ctx);
    TestPointerMoveAutoRecoversLostPointerUp(ctx);
    TestPanelHeaderDragThresholdAndSnap(ctx);

    if (ctx.failures() > 0) {
        std::cerr << ctx.failures() << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All DockCapsuleCoordinator tests passed.\n";
    return 0;
}
} // namespace dock_capsule_coordinator_suite

namespace app_updater_suite {
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

void TestSemVerParsingAndComparison(TestContext& ctx) {
    using nfm::host::platform::AppUpdater;

    const auto v1 = AppUpdater::parse_semver("v0.1.0");
    ctx.Check(v1.has_value() && v1->major == 0 && v1->minor == 1 && v1->patch == 0, "parse v0.1.0");
    const auto v2 = AppUpdater::parse_semver("1.2.3-beta.1");
    ctx.Check(v2.has_value() && v2->major == 1 && v2->minor == 2 && v2->patch == 3,
              "parse 1.2.3-beta.1");
    ctx.Check(!AppUpdater::parse_semver("invalid").has_value(), "reject invalid semver");
    ctx.Check(!AppUpdater::parse_semver("1.2").has_value(), "reject 2-part version");

    ctx.Check(AppUpdater::is_newer_version("0.1.0", "v0.1.1"), "0.1.1 > 0.1.0");
    ctx.Check(AppUpdater::is_newer_version("0.1.9", "0.2.0"), "0.2.0 > 0.1.9");
    ctx.Check(!AppUpdater::is_newer_version("0.2.0", "0.2.0"), "0.2.0 == 0.2.0 not newer");
    ctx.Check(!AppUpdater::is_newer_version("0.2.0", "0.1.9"), "0.1.9 < 0.2.0 not newer");
}

void TestGitHubReleaseJsonParsing(TestContext& ctx) {
    using nfm::host::platform::AppUpdater;

    const std::string sample_json = R"({
        "tag_name": "v0.2.0",
        "html_url": "https://github.com/ygq-future/network-flow-monitor/releases/tag/v0.2.0",
        "body": "Bug fixes and online updater",
        "assets": [
            {
                "name": "NetworkFlowMonitor-0.2.0-win-x64.zip.sha256",
                "size": 98,
                "browser_download_url": "https://github.com/ygq-future/network-flow-monitor/releases/download/v0.2.0/NetworkFlowMonitor-0.2.0-win-x64.zip.sha256"
            },
            {
                "name": "NetworkFlowMonitor-0.2.0-win-x64.zip",
                "size": 1048576,
                "browser_download_url": "https://github.com/ygq-future/network-flow-monitor/releases/download/v0.2.0/NetworkFlowMonitor-0.2.0-win-x64.zip"
            }
        ]
    })";

    std::string err;
    const auto parsed = AppUpdater::parse_github_release_json(sample_json, &err);
    ctx.Check(parsed.has_value(), "parse valid GitHub release JSON");
    if (parsed.has_value()) {
        ctx.Check(parsed->version == "0.2.0", "extracted version 0.2.0");
        ctx.Check(parsed->asset_size_bytes == 1048576, "extracted asset size");
        ctx.Check(parsed->download_url.ends_with("NetworkFlowMonitor-0.2.0-win-x64.zip"),
                  "selected .zip asset instead of .sha256");
    }

    const auto script = AppUpdater::build_apply_update_script(
        4242, LR"(C:\App\data\update\staged)", LR"(C:\App)", LR"(C:\App\NetworkFlowMonitor.exe)");
    ctx.Check(script.find(L"HOST_PID=4242") != std::wstring::npos, "script embeds host PID");
    ctx.Check(script.find(L"start \"\" /D \"%APP_ROOT%\" \"%HOST_EXE%\"") != std::wstring::npos,
              "script restarts host executable");
}

void TestDetachedUpdateScriptExecution(TestContext& ctx) {
    using nfm::host::platform::AppUpdater;

    const auto temp_dir = std::filesystem::temp_directory_path() /
                          ("nfm_updater_test_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);

    const auto script_path = temp_dir / L"test-apply.cmd";
    const auto marker_path = temp_dir / L"applied.marker";
    const std::wstring script_body =
        L"@echo off\r\necho applied> \"" + marker_path.wstring() + L"\"\r\n";

    std::string err;
    ctx.Check(AppUpdater::write_utf8_script(script_path, script_body, &err),
              "write UTF-8 batch script");
    ctx.Check(AppUpdater::launch_detached_update_script(script_path, &err),
              "launch detached batch script without console loss");

    bool marker_created = false;
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (std::filesystem::exists(marker_path, ec)) {
            marker_created = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ctx.Check(marker_created, "detached batch script actually executed and wrote marker file");
    std::filesystem::remove_all(temp_dir, ec);
}

void TestProxyNormalizationAndFallbackCandidates(TestContext& ctx) {
    using nfm::host::platform::AppUpdater;
    using nfm::host::platform::HttpAccessMode;
    using nfm::host::platform::HttpProxyConfigSources;
    ctx.Check(AppUpdater::normalize_winhttp_proxy_server(L"http://127.0.0.1:7890/") ==
                  L"127.0.0.1:7890",
              "normalize http:// proxy URL with trailing slash");
    ctx.Check(AppUpdater::normalize_winhttp_proxy_server(
                  L"http=127.0.0.1:7890;https=127.0.0.1:7891") == L"127.0.0.1:7891",
              "select https proxy from multi-scheme WinINET string");
    ctx.Check(AppUpdater::normalize_winhttp_proxy_server(L"socks5://127.0.0.1:1080").empty(),
              "reject socks5 scheme not supported by WinHTTP HTTP CONNECT");

    // Case 1: ProxyEnable=0, but ProxyServer="127.0.0.1:7890" is present in registry
    const auto candidates_with_reg =
        AppUpdater::build_http_access_candidates(HttpProxyConfigSources{
            .env_proxy = L"",
            .registry_proxy = L"127.0.0.1:7890",
            .registry_proxy_enabled = false,
        });
    ctx.Check(candidates_with_reg.size() == 3U,
              "AutoProxy -> NamedProxy(127.0.0.1:7890) -> Direct(NoProxy)");
    if (candidates_with_reg.size() == 3U) {
        ctx.Check(candidates_with_reg[0].mode == HttpAccessMode::AutomaticProxy,
                  "first candidate is AutomaticProxy");
        ctx.Check(candidates_with_reg[1].mode == HttpAccessMode::NamedProxy &&
                      candidates_with_reg[1].proxy_server == L"127.0.0.1:7890",
                  "second candidate is NamedProxy from registry ProxyServer");
        ctx.Check(candidates_with_reg[2].mode == HttpAccessMode::NoProxy,
                  "final candidate is Direct NoProxy fallback");
    }

    const auto candidates_default =
        AppUpdater::build_http_access_candidates(HttpProxyConfigSources{});
    ctx.Check(candidates_default.size() >= 3U &&
                  candidates_default.front().mode == HttpAccessMode::AutomaticProxy &&
                  candidates_default.back().mode == HttpAccessMode::NoProxy,
              "empty proxy config still includes local port fallbacks and ends with "
              "Direct NoProxy");
}
} // namespace

int Run() {
    TestContext ctx;
    TestSemVerParsingAndComparison(ctx);
    TestGitHubReleaseJsonParsing(ctx);
    TestDetachedUpdateScriptExecution(ctx);
    TestProxyNormalizationAndFallbackCandidates(ctx);
    return ctx.failures();
}
} // namespace app_updater_suite

namespace traffic_history_suite {
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

void Test2DPersistenceDeltaAndExclusionFiltering(TestContext& ctx) {
    using nfm::host::history::TrafficHistoryStore;
    using nfm::ipc::RankingApplication;
    using nfm::ipc::RankingSnapshotMessage;

    const auto temp_dir = std::filesystem::temp_directory_path() /
                          ("nfm_history_test_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto history_file = temp_dir / L"data" / L"traffic_history.json";

    std::string current_date = "2026-10-19";
    TrafficHistoryStore store(history_file, [&]() { return current_date; });
    store.on_session_reset("session_1");

    std::vector<std::string> excluded{"mihomo-multi.exe"};

    // Snapshot 1 on 2026-10-19: Edge (1000 DL, 500 UL) and excluded Mihomo (99999 DL, 99999 UL)
    RankingSnapshotMessage snap1{
        .seq = 1U,
        .monotonic_ms = 1000U,
        .status = "Ready",
        .applications =
            {
                RankingApplication{
                    .id = "executable:C:\\Program Files\\Edge\\msedge.exe",
                    .executable_path = "C:\\Program Files\\Edge\\msedge.exe",
                    .display_name = "Microsoft Edge",
                    .total_download_bytes = 1000U,
                    .total_upload_bytes = 500U,
                },
                RankingApplication{
                    .id = "executable:C:\\Proxy\\mihomo-multi.exe",
                    .executable_path = "C:\\Proxy\\mihomo-multi.exe",
                    .display_name = "mihomo-multi.exe",
                    .total_download_bytes = 99999U,
                    .total_upload_bytes = 99999U,
                },
            },
    };
    ctx.Check(store.record_snapshot(snap1, excluded), "record_snapshot #1 records Edge");

    // Snapshot 2 on 2026-10-19: Edge cumulative increases to (2500 DL, 800 UL) -> delta (+1500 DL,
    // +300 UL)
    RankingSnapshotMessage snap2{
        .seq = 2U,
        .monotonic_ms = 2000U,
        .status = "Ready",
        .applications =
            {
                RankingApplication{
                    .id = "executable:C:\\Program Files\\Edge\\msedge.exe",
                    .executable_path = "C:\\Program Files\\Edge\\msedge.exe",
                    .display_name = "Microsoft Edge",
                    .total_download_bytes = 2500U,
                    .total_upload_bytes = 800U,
                },
            },
    };
    ctx.Check(store.record_snapshot(snap2, excluded), "record_snapshot #2 records delta");

    // Next calendar day 2026-10-20 in the same Collector session: Edge cumulative is (4000 DL, 1200
    // UL) Delta (+1500 DL, +400 UL) goes to 2026-10-20 bucket
    current_date = "2026-10-20";
    RankingSnapshotMessage snap3{
        .seq = 3U,
        .monotonic_ms = 3000U,
        .status = "Ready",
        .applications =
            {
                RankingApplication{
                    .id = "executable:C:\\Program Files\\Edge\\msedge.exe",
                    .executable_path = "C:\\Program Files\\Edge\\msedge.exe",
                    .display_name = "Microsoft Edge",
                    .total_download_bytes = 4000U,
                    .total_upload_bytes = 1200U,
                },
                RankingApplication{
                    .id = "executable:C:\\Code\\Code.exe",
                    .executable_path = "C:\\Code\\Code.exe",
                    .display_name = "Visual Studio Code",
                    .total_download_bytes = 600U,
                    .total_upload_bytes = 200U,
                },
            },
    };
    ctx.Check(store.record_snapshot(snap3, excluded), "record_snapshot #3 records new day");

    // Simulate Collector restart (new session_2) where cumulative counters start from 0 again
    store.on_session_reset("session_2");
    RankingSnapshotMessage snap4{
        .seq = 1U,
        .monotonic_ms = 1000U,
        .status = "Ready",
        .applications =
            {
                RankingApplication{
                    .id = "executable:C:\\Program Files\\Edge\\msedge.exe",
                    .executable_path = "C:\\Program Files\\Edge\\msedge.exe",
                    .display_name = "Microsoft Edge",
                    .total_download_bytes = 500U,
                    .total_upload_bytes = 100U,
                },
            },
    };
    ctx.Check(store.record_snapshot(snap4, excluded),
              "record_snapshot #4 after session reset accumulates without losing prior bytes");

    // Save to disk and reload into a fresh TrafficHistoryStore
    ctx.Check(store.save_if_dirty(),
              "atomic save to <app_root>/data/traffic_history.json succeeds");
    ctx.Check(std::filesystem::exists(history_file, ec),
              "traffic_history.json file exists on disk");

    TrafficHistoryStore reloaded(history_file);
    ctx.Check(reloaded.load(), "reload traffic_history.json succeeds");
    const auto dto = reloaded.export_dto(excluded, false);
    ctx.Check(dto.days.size() == 2U, "2 calendar days persisted");
    if (dto.days.size() == 2U) {
        ctx.Check(dto.days[0].date == "2026-10-20", "newest day first (2026-10-20)");
        ctx.Check(dto.days[0].total_download_bytes == 2600U &&
                      dto.days[0].total_upload_bytes == 700U,
                  "2026-10-20 totals match Edge (1500+500 DL, 400+100 UL) + Code (600 DL, 200 UL)");
        ctx.Check(dto.days[0].applications.size() == 2U, "2 non-excluded apps on 2026-10-20");

        ctx.Check(dto.days[1].date == "2026-10-19", "older day second (2026-10-19)");
        ctx.Check(dto.days[1].total_download_bytes == 2500U &&
                      dto.days[1].total_upload_bytes == 800U,
                  "2026-10-19 totals match Edge (2500 DL, 800 UL) and exclude mihomo");
    }

    // Dynamic exclusion on export: if Code.exe is added to excluded_programs later, export filters
    // it out
    const auto filtered_dto = reloaded.export_dto({"mihomo-multi.exe", "Code.exe"}, false);
    if (!filtered_dto.days.empty()) {
        ctx.Check(filtered_dto.days[0].applications.size() == 1U &&
                      filtered_dto.days[0].total_download_bytes == 2000U,
                  "export_dto dynamically excludes newly added excluded_programs");
    }

    std::filesystem::remove_all(temp_dir, ec);
}

void TestSameNameDifferentPathsMerge(TestContext& ctx) {
    using nfm::host::history::TrafficHistoryStore;
    using nfm::ipc::RankingApplication;
    using nfm::ipc::RankingSnapshotMessage;

    const auto temp_dir = std::filesystem::temp_directory_path() /
                          ("nfm_history_merge_test_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto history_file = temp_dir / L"data" / L"traffic_history.json";

    std::string current_date = "2026-10-21";
    TrafficHistoryStore store(history_file, [&]() { return current_date; });
    store.on_session_reset("session_1");
    const std::vector<std::string> excluded{};
    RankingSnapshotMessage snap{
        .seq = 1U,
        .monotonic_ms = 1000U,
        .status = "Ready",
        .applications =
            {
                RankingApplication{
                    .id = "executable:C:\\Apps\\ChromeStable\\chrome.exe",
                    .executable_path = "C:\\Apps\\ChromeStable\\chrome.exe",
                    .display_name = "Google Chrome",
                    .total_download_bytes = 1000U,
                    .total_upload_bytes = 100U,
                },
                RankingApplication{
                    .id = "executable:D:\\Portable\\Chrome\\chrome.exe",
                    .executable_path = "D:\\Portable\\Chrome\\chrome.exe",
                    .display_name = "chrome.exe",
                    .total_download_bytes = 500U,
                    .total_upload_bytes = 50U,
                },
                RankingApplication{
                    .id = "executable:C:\\Tools\\curl.exe",
                    .executable_path = "C:\\Tools\\curl.exe",
                    .display_name = "curl.exe",
                    .total_download_bytes = 10U,
                    .total_upload_bytes = 20U,
                },
            },
    };
    ctx.Check(store.record_snapshot(snap, excluded), "record_snapshot records merge candidates");

    const auto dto = store.export_dto(excluded, false);
    ctx.Check(dto.days.size() == 1U, "single day exported");
    if (!dto.days.empty()) {
        ctx.Check(dto.days[0].applications.size() == 2U,
                  "same executable name from different paths collapses into one history row");
        bool found_chrome = false;
        for (const auto& app : dto.days[0].applications) {
            if (app.app_id == "exe:chrome.exe") {
                found_chrome = true;
                ctx.Check(app.download_bytes == 1500U && app.upload_bytes == 150U,
                          "merged chrome.exe row sums both install paths");
            }
        }
        ctx.Check(found_chrome, "merged row is keyed by executable name");
    }
    std::filesystem::remove_all(temp_dir, ec);
}

} // namespace

int Run() {
    TestContext ctx;
    Test2DPersistenceDeltaAndExclusionFiltering(ctx);
    TestSameNameDifferentPathsMerge(ctx);
    return ctx.failures();
}
} // namespace traffic_history_suite

namespace taskbar_speed_window_suite {
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

void TestRateFormatting(TestContext& ctx) {
    using nfm::config::SpeedUnit;
    using nfm::host::platform::TaskbarSpeedWindow;

    ctx.Check(TaskbarSpeedWindow::format_rate_line(L"\u2191", 0U, SpeedUnit::Auto) ==
                  L"\u2191: 0.0 KB/s",
              "0 bytes in Auto formats as 0.0 KB/s");
    ctx.Check(TaskbarSpeedWindow::format_rate_line(L"\u2193", 7372U, SpeedUnit::Bytes) ==
                  L"\u2193: 7.2 KB/s",
              "7372 bytes formats as 7.2 KB/s");
    ctx.Check(TaskbarSpeedWindow::format_rate_line(L"\u2193", 434176U, SpeedUnit::Auto) ==
                  L"\u2193: 424 KB/s",
              ">= 100 KB/s rounds to integer");
    ctx.Check(TaskbarSpeedWindow::format_rate_line(L"\u2191", 125000U, SpeedUnit::Bits) ==
                  L"\u2191: 1.0 Mbps",
              "125000 bytes in Bits formats as 1.0 Mbps");
    const auto parts = TaskbarSpeedWindow::format_rate_parts(434176U, SpeedUnit::Auto);
    ctx.Check(parts.value == L"424" && parts.unit == L"KB/s",
              "format_rate_parts separates value and unit for fixed-column rendering");
}

void TestHardwareColumnSubstitutionAndPairedNetworkRates(TestContext& ctx) {
    using nfm::config::DisplayConfig;
    using nfm::config::SpeedUnit;
    using nfm::host::platform::SystemTelemetrySnapshot;
    using nfm::host::platform::TaskbarRatePair;
    using nfm::host::platform::TaskbarSpeedWindow;

    const SystemTelemetrySnapshot sample{
        .cpu_usage_percent = 18.4,
        .memory_usage_percent = 63.7,
        .memory_used_gb = 30.8,
        .memory_total_gb = 48.0,
        .cpu_temperature_celsius = 79.2,
        .cpu_package_power_watts = 45.2,
        .gpu_usage_percent = 24.1,
        .gpu_vram_usage_percent = 26.8,
        .gpu_vram_used_gb = 2.14,
        .gpu_vram_total_gb = 7.99,
    };
    const TaskbarRatePair rates{
        .download_bytes_per_sec = 1048576U,
        .upload_bytes_per_sec = 524288U,
    };

    // 1. All 6 hardware metrics + network speed -> 4 columns in canonical 2-row order
    DisplayConfig all_on{};
    all_on.speed_unit = SpeedUnit::Auto;
    all_on.show_taskbar_cpu_usage = true;
    all_on.show_taskbar_cpu_temp = true;
    all_on.show_taskbar_cpu_power = true;
    all_on.show_taskbar_memory_usage = true;
    all_on.show_taskbar_gpu_usage = true;
    all_on.show_taskbar_gpu_memory = true;
    all_on.show_taskbar_speed = true;

    const auto cols_all = TaskbarSpeedWindow::build_display_columns(all_on, rates, sample);
    ctx.Check(cols_all.size() == 4U, "all 6 hardware metrics + speed produces 4 columns");
    if (cols_all.size() == 4U) {
        ctx.Check(cols_all[0].top.label == L"CPU:" && cols_all[0].top.value == L"18" &&
                      cols_all[0].top.unit == L"%" && cols_all[0].bottom.label == L"CPU:" &&
                      cols_all[0].bottom.value == L"79" && cols_all[0].bottom.unit == L"\u2103",
                  "Column 0 pairs CPU usage% on top and CPU temperature on bottom");
        ctx.Check(cols_all[1].top.label == L"CPU:" && cols_all[1].top.value == L"45" &&
                      cols_all[1].top.unit == L"W" && cols_all[1].bottom.label == L"RAM:" &&
                      cols_all[1].bottom.value == L"64" && cols_all[1].bottom.unit == L"%",
                  "Column 1 pairs CPU power W on top and RAM usage% on bottom");
        ctx.Check(cols_all[2].top.label == L"GPU:" && cols_all[2].top.value == L"24" &&
                      cols_all[2].top.unit == L"%" && cols_all[2].bottom.label == L"VRM:" &&
                      cols_all[2].bottom.value == L"27" && cols_all[2].bottom.unit == L"%",
                  "Column 2 pairs GPU usage% on top and VRM usage% on bottom");
        ctx.Check(cols_all[3].top.label == L"\u2191:" && cols_all[3].bottom.label == L"\u2193:",
                  "Column 3 keeps upload and download speeds paired vertically");
    }

    const auto tip_rows = TaskbarSpeedWindow::build_tooltip_rows(all_on, rates, sample, true);
    ctx.Check(tip_rows.size() == 8U, "tooltip rows include all 6 hardware items + 2 speed rows");
    if (tip_rows.size() == 8U) {
        ctx.Check(tip_rows[3].value == L"63.7% (30.8 / 48.0 GB)",
                  "Memory tooltip row formats percentage and used/total GB");
        ctx.Check(tip_rows[5].value == L"26.8% (2.1 / 8.0 GB)",
                  "VRAM tooltip row formats percentage and used/total GB");
    }

    // 2. Substitution rule: disabling CPU temp (#2) shifts CPU power (#3) into Col 0 bottom,
    // while an odd hardware count (3 hardware items) leaves Col 1 bottom empty instead of pulling
    // upload speed into the hardware column.
    DisplayConfig odd_hw{};
    odd_hw.show_taskbar_cpu_usage = true;
    odd_hw.show_taskbar_cpu_power = true;
    odd_hw.show_taskbar_gpu_memory = true;
    odd_hw.show_taskbar_speed = true;

    const auto cols_odd = TaskbarSpeedWindow::build_display_columns(odd_hw, rates, sample);
    ctx.Check(cols_odd.size() == 3U, "3 hardware items + speed produces 3 columns");
    if (cols_odd.size() == 3U) {
        ctx.Check(cols_odd[0].top.unit == L"%" && cols_odd[0].bottom.unit == L"W",
                  "CPU power substitutes into Col 0 bottom when CPU temp is disabled");
        ctx.Check(cols_odd[1].top.label == L"VRM:" && cols_odd[1].top.unit == L"%" &&
                      cols_odd[1].bottom.empty,
                  "Odd hardware item occupies Col 1 top while Col 1 bottom stays empty");
        ctx.Check(cols_odd[2].top.label == L"\u2191:" && cols_odd[2].bottom.label == L"\u2193:",
                  "Upload and download speeds never substitute into hardware slots");
    }

    // 3. Live SystemTelemetrySampler smoke check (verifies PDH/Win32/DXGI zero-driver path)
    nfm::host::platform::SystemTelemetrySampler live_sampler;
    const auto live_snap = live_sampler.sample();
    ctx.Check(live_snap.cpu_usage_percent >= 0.0 && live_snap.cpu_usage_percent <= 100.0 &&
                  live_snap.memory_usage_percent > 0.0 && live_snap.memory_usage_percent <= 100.0 &&
                  live_snap.gpu_usage_percent >= 0.0 && live_snap.gpu_usage_percent <= 100.0,
              "live SystemTelemetrySampler returns bounded CPU, memory, and GPU percentages");
}

void TestAdaptivePlacementWin11AndWin10(TestContext& ctx) {
    using nfm::host::platform::TaskbarLayoutInput;
    using nfm::host::platform::TaskbarSpeedWindow;

    // Case 1: Win11 centered taskbar without widgets -> placed on left edge
    const TaskbarLayoutInput win11_centered{
        .taskbar_rect = RECT{0, 1032, 1920, 1080},
        .notify_rect = RECT{1720, 1032, 1920, 1080},
        .has_notify_rect = true,
        .taskbar_centered = true,
        .widgets_enabled = false,
        .dpi = 96U,
    };
    const auto p1 = TaskbarSpeedWindow::compute_placement(win11_centered);
    ctx.Check(p1.valid && p1.placed_on_left, "Win11 centered without widgets places on left");
    ctx.Check(p1.x_px == 2 && p1.y_px == 8 && p1.width_px == 74 && p1.height_px == 32,
              "Win11 left placement uses relative Shell_TrayWnd coordinates (2, 8, 74x32)");
    // Case 2: Win10 / Win11 left-aligned taskbar -> placed on right side immediately left of
    // TrayNotifyWnd in relative taskbar coordinates
    const TaskbarLayoutInput win10_left_aligned{
        .taskbar_rect = RECT{0, 1040, 1920, 1080},
        .notify_rect = RECT{1700, 1040, 1920, 1080},
        .has_notify_rect = true,
        .taskbar_centered = false,
        .widgets_enabled = false,
        .dpi = 96U,
    };
    const auto p2 = TaskbarSpeedWindow::compute_placement(win10_left_aligned);
    ctx.Check(p2.valid && !p2.placed_on_left,
              "Win10/left-aligned taskbar places on right before TrayNotifyWnd");
    ctx.Check(p2.x_px == 1700 - 74 - 2 && p2.y_px == 4 && p2.height_px == 32,
              "Win10 right placement docks 2px left of TrayNotifyWnd in relative coords");
}

void EnsureTaskbarTestClassesRegistered(HINSTANCE hinst) noexcept {
    constexpr const wchar_t* kClasses[] = {
        L"NFM_TestShellTrayWnd",
        L"TrayNotifyWnd",
        L"NFM_TestTaskbarBandWnd",
        L"MSTaskSwWClass",
    };
    for (const wchar_t* class_name : kClasses) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = &DefWindowProcW;
        wc.hInstance = hinst;
        wc.lpszClassName = class_name;
        RegisterClassExW(&wc);
    }
}

void TestClassicalTaskbarNonIntrusiveEmbeddingAndZOrder(TestContext& ctx) {
    using nfm::host::platform::TaskbarSpeedWindow;

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    EnsureTaskbarTestClassesRegistered(hinst);

    HWND fake_taskbar =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"NFM_TestShellTrayWnd", L"",
                        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 400, 1920, 40, nullptr,
                        nullptr, hinst, nullptr);
    ctx.Check(fake_taskbar != nullptr, "synthetic classical taskbar container created");
    if (fake_taskbar == nullptr) {
        return;
    }

    HWND rebar_hwnd = CreateWindowExW(0, L"NFM_TestTaskbarBandWnd", L"",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 96,
                                      0, 1604, 40, fake_taskbar, nullptr, hinst, nullptr);
    // Simulate a Windows 10 taskbar where a third-party monitor (such as TrafficMonitor) already
    // shrunk MSTaskSwWClass from 1604px to 1460px on the right.
    HWND mstask_hwnd = CreateWindowExW(0, L"MSTaskSwWClass", L"",
                                       WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 4,
                                       0, 1460, 40, rebar_hwnd, nullptr, hinst, nullptr);
    HWND notify_hwnd = CreateWindowExW(0, L"TrayNotifyWnd", L"", WS_CHILD | WS_VISIBLE, 1700, 0,
                                       220, 40, fake_taskbar, nullptr, hinst, nullptr);

    RECT initial_mstask_rect{};
    GetWindowRect(mstask_hwnd, &initial_mstask_rect);

    TaskbarSpeedWindow speed_wnd;
    ctx.Check(speed_wnd.create(nullptr, nullptr), "TaskbarSpeedWindow creates successfully");
    speed_wnd.set_target_taskbar_for_testing(fake_taskbar);

    // 1. First enable: attaches directly to the taskbar container, configures LWA_COLORKEY after
    // SetParent, docks left of TrayNotifyWnd, and leaves MSTaskSwWClass completely untouched.
    speed_wnd.set_enabled(true);
    ctx.Check(GetAncestor(speed_wnd.hwnd(), GA_PARENT) == fake_taskbar,
              "TaskbarSpeedWindow attaches directly to Shell_TrayWnd container");
    COLORREF color_key = 0;
    DWORD layered_flags = 0;
    ctx.Check((GetWindowLongPtrW(speed_wnd.hwnd(), GWL_EXSTYLE) &
               static_cast<LONG_PTR>(WS_EX_LAYERED)) != 0 &&
                  GetLayeredWindowAttributes(speed_wnd.hwnd(), &color_key, nullptr,
                                             &layered_flags) != FALSE &&
                  (layered_flags & LWA_COLORKEY) != 0U,
              "TaskbarSpeedWindow re-establishes WS_EX_LAYERED + LWA_COLORKEY after SetParent");

    const UINT dpi_val = GetDpiForWindow(fake_taskbar);
    const int dpi_int = static_cast<int>(dpi_val > 0U ? dpi_val : 96U);
    const int expected_width = (TaskbarSpeedWindow::kDefaultOverlayWidthDip * dpi_int) / 96;
    const int expected_margin = (TaskbarSpeedWindow::kHorizontalMarginDip * dpi_int) / 96;
    const int expected_rel_x = 1700 - expected_width - expected_margin;

    RECT taskbar_rc{};
    RECT speed_rc_first{};
    GetWindowRect(fake_taskbar, &taskbar_rc);
    GetWindowRect(speed_wnd.hwnd(), &speed_rc_first);
    ctx.Check((speed_rc_first.left - taskbar_rc.left) == expected_rel_x &&
                  (speed_rc_first.right - speed_rc_first.left) == expected_width,
              "TaskbarSpeedWindow docks immediately left of TrayNotifyWnd on classical taskbar");

    RECT mstask_after_first{};
    GetWindowRect(mstask_hwnd, &mstask_after_first);
    ctx.Check(mstask_after_first.left == initial_mstask_rect.left &&
                  mstask_after_first.right == initial_mstask_rect.right,
              "First enable does not shrink or shift MSTaskSwWClass / third-party taskbar windows");

    // 2. Disable and second enable: neither MSTaskSwWClass nor TaskbarSpeedWindow drifts.
    speed_wnd.set_enabled(false);
    speed_wnd.set_enabled(true);
    RECT mstask_after_second{};
    RECT speed_rc_second{};
    GetWindowRect(mstask_hwnd, &mstask_after_second);
    GetWindowRect(speed_wnd.hwnd(), &speed_rc_second);
    ctx.Check(mstask_after_second.left == initial_mstask_rect.left &&
                  mstask_after_second.right == initial_mstask_rect.right,
              "Repeated disable/enable cycles never mutate MSTaskSwWClass bounds");
    ctx.Check(speed_rc_second.left == speed_rc_first.left &&
                  speed_rc_second.right == speed_rc_first.right,
              "Repeated disable/enable cycles preserve exact overlay coordinates");

    // 3. Sibling Z-order self-healing: if a sibling band window is raised to HWND_TOP,
    // sync_with_taskbar() restores TaskbarSpeedWindow to the top of the taskbar's child list.
    SetWindowPos(rebar_hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    ctx.Check(GetWindow(speed_wnd.hwnd(), GW_HWNDPREV) != nullptr,
              "raising sibling taskbar band places it ahead of TaskbarSpeedWindow before sync");
    speed_wnd.sync_with_taskbar();
    ctx.Check(GetWindow(speed_wnd.hwnd(), GW_HWNDPREV) == nullptr,
              "sync_with_taskbar restores TaskbarSpeedWindow to HWND_TOP among taskbar children");
    // 4. Verify client rect dimensions match the target DPI-scaled overlay geometry (74x32 at 96
    // DPI, resized from initial 1x1 so child layered surface allocation is triggered).
    RECT client_rc{};
    ctx.Check(GetClientRect(speed_wnd.hwnd(), &client_rc) != FALSE &&
                  (client_rc.right - client_rc.left) == expected_width &&
                  (client_rc.bottom - client_rc.top) ==
                      (TaskbarSpeedWindow::kDefaultOverlayHeightDip * dpi_int) / 96,
              "reparented child overlay resizes from 1x1 to target DPI dimensions");

    speed_wnd.close();
    if (notify_hwnd != nullptr) {
        DestroyWindow(notify_hwnd);
    }
    if (mstask_hwnd != nullptr) {
        DestroyWindow(mstask_hwnd);
    }
    if (rebar_hwnd != nullptr) {
        DestroyWindow(rebar_hwnd);
    }
    DestroyWindow(fake_taskbar);
}

void TestTooltipRequiresPointerDwell(TestContext& ctx) {
    using namespace std::chrono_literals;
    using nfm::host::platform::TaskbarSpeedWindow;

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    EnsureTaskbarTestClassesRegistered(hinst);

    HWND fake_taskbar =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"NFM_TestShellTrayWnd", L"",
                        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 400, 1920, 40, nullptr,
                        nullptr, hinst, nullptr);
    ctx.Check(fake_taskbar != nullptr, "synthetic taskbar for dwell test created");
    if (fake_taskbar == nullptr) {
        return;
    }
    HWND notify_hwnd = CreateWindowExW(0, L"TrayNotifyWnd", L"", WS_CHILD | WS_VISIBLE, 1700, 0,
                                       220, 40, fake_taskbar, nullptr, hinst, nullptr);

    TaskbarSpeedWindow speed_wnd;
    ctx.Check(speed_wnd.create(nullptr, nullptr), "TaskbarSpeedWindow creates for dwell test");
    speed_wnd.set_target_taskbar_for_testing(fake_taskbar);
    speed_wnd.set_enabled(true);

    ctx.Check(TaskbarSpeedWindow::kTooltipHoverDelay >= 500ms,
              "tooltip dwell requirement is at least 500ms");

    SendMessageW(speed_wnd.hwnd(), WM_MOUSEMOVE, 0, 0);
    ctx.Check(!speed_wnd.is_tooltip_visible(),
              "pointer movement alone does not reveal the tooltip immediately");
    ctx.Check(speed_wnd.is_tooltip_hover_timer_active(),
              "pointer movement arms the tooltip dwell timer");

    SendMessageW(speed_wnd.hwnd(), WM_MOUSEMOVE, 0, 0);
    ctx.Check(speed_wnd.is_tooltip_hover_timer_active(),
              "continued pointer movement keeps the pending dwell timer armed");

    speed_wnd.set_enabled(false);
    ctx.Check(!speed_wnd.is_tooltip_hover_timer_active(),
              "disabling the overlay cancels the pending dwell timer");

    speed_wnd.close();
    if (notify_hwnd != nullptr) {
        DestroyWindow(notify_hwnd);
    }
    DestroyWindow(fake_taskbar);
}

void TestMasterSwitchGatesOverlay(TestContext& ctx) {
    using nfm::config::DisplayConfig;
    using nfm::config::LanguageMode;
    using nfm::config::ThemeMode;
    using nfm::host::platform::TaskbarSpeedWindow;

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    EnsureTaskbarTestClassesRegistered(hinst);

    HWND fake_taskbar =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"NFM_TestShellTrayWnd", L"",
                        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 400, 1920, 40, nullptr,
                        nullptr, hinst, nullptr);
    ctx.Check(fake_taskbar != nullptr, "synthetic taskbar for master switch test created");
    if (fake_taskbar == nullptr) {
        return;
    }
    HWND notify_hwnd = CreateWindowExW(0, L"TrayNotifyWnd", L"", WS_CHILD | WS_VISIBLE, 1700, 0,
                                       220, 40, fake_taskbar, nullptr, hinst, nullptr);

    TaskbarSpeedWindow speed_wnd;
    ctx.Check(speed_wnd.create(nullptr, nullptr),
              "TaskbarSpeedWindow creates for master switch test");
    speed_wnd.set_target_taskbar_for_testing(fake_taskbar);

    DisplayConfig display{};
    display.show_taskbar_speed = true;
    display.show_taskbar_monitor = false;
    speed_wnd.update_config(display, ThemeMode::System, LanguageMode::English);
    ctx.Check(!speed_wnd.is_enabled(),
              "master switch off hides the overlay even with monitored items enabled");

    display.show_taskbar_monitor = true;
    speed_wnd.update_config(display, ThemeMode::System, LanguageMode::English);
    ctx.Check(speed_wnd.is_enabled(), "master switch on renders the overlay again");

    display.show_taskbar_speed = false;
    speed_wnd.update_config(display, ThemeMode::System, LanguageMode::English);
    ctx.Check(!speed_wnd.is_enabled(), "overlay stays hidden when no monitored item is selected");

    speed_wnd.close();
    if (notify_hwnd != nullptr) {
        DestroyWindow(notify_hwnd);
    }
    DestroyWindow(fake_taskbar);
}

} // namespace

int Run() {
    TestContext ctx;
    TestRateFormatting(ctx);
    TestHardwareColumnSubstitutionAndPairedNetworkRates(ctx);
    TestAdaptivePlacementWin11AndWin10(ctx);
    TestClassicalTaskbarNonIntrusiveEmbeddingAndZOrder(ctx);
    TestTooltipRequiresPointerDwell(ctx);
    TestMasterSwitchGatesOverlay(ctx);
    return ctx.failures();
}
} // namespace taskbar_speed_window_suite

int main() {
    int failures = 0;
    failures += (host_bridge_suite::Run() != 0 ? 1 : 0);
    failures += (host_webview_policy_suite::Run() != 0 ? 1 : 0);
    failures += (host_lifecycle_suite::Run() != 0 ? 1 : 0);
    failures += (dock_capsule_coordinator_suite::Run() != 0 ? 1 : 0);
    failures += (app_updater_suite::Run() != 0 ? 1 : 0);
    failures += (traffic_history_suite::Run() != 0 ? 1 : 0);
    failures += (taskbar_speed_window_suite::Run() != 0 ? 1 : 0);
    return failures;
}
