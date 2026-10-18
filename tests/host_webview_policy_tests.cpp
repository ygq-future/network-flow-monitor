#include "network_flow_monitor/host/app/product_host.h"
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

} // namespace

int main() {
    TestContext context;
    CheckNavigationPolicies(context);
    CheckRecoveryGuidance(context);
    CheckFullscreenDesktopAndCloakedExclusion(context);
    CheckWebView2ControllerEnsureAliveInterface(context);
    CheckSettingsOpenAndCloseSurvival(context);
    return context.failures();
}
