#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_WEBVIEW2_ENVIRONMENT_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_WEBVIEW2_ENVIRONMENT_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace nfm::host::platform {

inline constexpr std::string_view kVirtualHostName = "nfm.local";
inline constexpr std::string_view kVirtualHostUrl = "https://nfm.local/";
inline constexpr std::string_view kVirtualHostIndexUrl = "https://nfm.local/index.html";

struct WebView2RuntimeInfo {
    bool is_available{false};
    std::wstring version;
    std::wstring install_path;
    std::wstring error_message;
};

class WebView2Environment {
  public:
    [[nodiscard]] static WebView2RuntimeInfo detect_runtime();

    [[nodiscard]] static std::wstring get_user_data_directory();

    [[nodiscard]] static bool is_allowed_origin(std::string_view url) noexcept;
    [[nodiscard]] static bool is_allowed_navigation(std::wstring_view url) noexcept;

    [[nodiscard]] static std::wstring get_recovery_guidance();
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_WEBVIEW2_ENVIRONMENT_H
