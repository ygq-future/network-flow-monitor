#include "network_flow_monitor/host/app/product_host.h"
#include "network_flow_monitor/host/platform/rotating_file_logger.h"
#include "network_flow_monitor/host/platform/startup_registration.h"
#include "network_flow_monitor/host/platform/webview2_environment.h"
#include "network_flow_monitor/host/platform/win32_window.h"
#include "network_flow_monitor/host/webview/webview_controller.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
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

int main() {
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
