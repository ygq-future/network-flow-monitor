#include "network_flow_monitor/host/platform/webview2_environment.h"

#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/host/platform/startup_registration.h"
#include "network_flow_monitor/host/platform/webview2_includes.h"

#include <string>

namespace nfm::host::platform {

WebView2RuntimeInfo WebView2Environment::detect_runtime() {
    WebView2RuntimeInfo info;
    LPWSTR version_info = nullptr;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version_info);
    if (SUCCEEDED(hr) && version_info != nullptr) {
        info.is_available = true;
        info.version = version_info;
        CoTaskMemFree(version_info);
    } else {
        info.is_available = false;
        info.error_message = L"WebView2 Evergreen Runtime was not detected on this system.";
    }
    return info;
}

std::wstring WebView2Environment::get_user_data_directory() {
    const auto exe_path = StartupRegistration::current_executable_path();
    if (!exe_path.empty()) {
        const auto dir = config::PortableWebView2UserDataDirectory(exe_path);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir.wstring();
    }
    return L".\\data\\webview2";
}

bool WebView2Environment::is_allowed_origin(std::string_view url) noexcept {
    return url.starts_with("https://nfm.local/") || url == "https://nfm.local" ||
           url.starts_with("http://localhost:") || url.starts_with("http://127.0.0.1:");
}

bool WebView2Environment::is_allowed_navigation(std::wstring_view url) noexcept {
    return url.starts_with(L"https://nfm.local/") || url == L"https://nfm.local" ||
           url.starts_with(L"http://localhost:") || url.starts_with(L"http://127.0.0.1:") ||
           url == L"about:blank";
}

std::wstring WebView2Environment::get_recovery_guidance() {
    return L"Microsoft Edge WebView2 Runtime is required to run Network Flow Monitor.\n"
           L"Please download and install the Evergreen Bootstrapper or Standalone Installer from:\n"
           L"https://developer.microsoft.com/en-us/microsoft-edge/webview2/\n";
}

} // namespace nfm::host::platform
