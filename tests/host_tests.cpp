#include "network_flow_monitor/host/app/product_host.h"
#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"
#include "network_flow_monitor/host/bridge/bridge_types.h"
#include "network_flow_monitor/host/bridge/native_bridge.h"
#include "network_flow_monitor/host/coordinator/dock_capsule_coordinator.h"
#include "network_flow_monitor/host/lifecycle/collector_companion.h"
#include "network_flow_monitor/host/platform/app_updater.h"
#include "network_flow_monitor/host/platform/rotating_file_logger.h"
#include "network_flow_monitor/host/platform/startup_registration.h"
#include "network_flow_monitor/host/platform/webview2_environment.h"
#include "network_flow_monitor/host/platform/win32_window.h"

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
    controller.set_visible(false);
    controller.set_visible(true);
    // Uninitialized controller is not ready and ensure_alive() safely returns false without crash
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
    companion.set_ranking_snapshot_callback([&](const nfm::host::bridge::RankingSnapshotDto& snap) {
        ranking_called = true;
        context.Check(snap.applications.size() == 1, "ranking apps size is 1");
        context.Check(snap.applications[0].display_name == "Edge", "ranking app name matches");
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

    // 3d. Check Backoff state transition emits diagnostic
    diagnostics.clear();
    companion.StateChanged(nfm::ipc::IpcClientState::Backoff);
    context.Check(!diagnostics.empty() && diagnostics.back().first,
                  "IPC Backoff from Running emits error diagnostic");

    // 4. Test stop and restart state
    companion.stop();
    context.Check(!companion.is_running(), "companion not running after stop");
    context.Check(companion.get_info().state == CollectorState::Stopped, "state is Stopped");
}

} // namespace

int Run() {
    TestContext context;
    CheckCollectorCompanionInjections(context);
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
    ctx.Check(script.find(L"$hostPid = 4242") != std::wstring::npos, "script embeds host PID");
    ctx.Check(script.find(L"Start-Process -FilePath $hostExe") != std::wstring::npos,
              "script restarts host executable");
}

} // namespace

int Run() {
    TestContext ctx;
    TestSemVerParsingAndComparison(ctx);
    TestGitHubReleaseJsonParsing(ctx);
    return ctx.failures();
}
} // namespace app_updater_suite

int main() {
    int failures = 0;
    failures += (host_bridge_suite::Run() != 0 ? 1 : 0);
    failures += (host_webview_policy_suite::Run() != 0 ? 1 : 0);
    failures += (host_lifecycle_suite::Run() != 0 ? 1 : 0);
    failures += (dock_capsule_coordinator_suite::Run() != 0 ? 1 : 0);
    failures += (app_updater_suite::Run() != 0 ? 1 : 0);
    return failures;
}
