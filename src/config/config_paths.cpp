#include "network_flow_monitor/config/config_paths.h"

namespace nfm::config {

std::filesystem::path PortableDataDirectory(const std::filesystem::path& executable_path) {
    return executable_path.parent_path() / L"data";
}

std::filesystem::path PortableLogDirectory(const std::filesystem::path& executable_path) {
    return executable_path.parent_path() / L"logs";
}

std::filesystem::path PortableWebView2UserDataDirectory(
    const std::filesystem::path& executable_path) {
    return PortableDataDirectory(executable_path) / L"webview2";
}

std::filesystem::path PortableConfigPath(const std::filesystem::path& executable_path) {
    return PortableDataDirectory(executable_path) / L"config.json";
}

std::filesystem::path PortableTrafficHistoryPath(const std::filesystem::path& executable_path) {
    return PortableDataDirectory(executable_path) / L"traffic_history.json";
}

std::filesystem::path LegacyRootConfigPath(const std::filesystem::path& executable_path) {
    return executable_path.parent_path() / L"config.json";
}

} // namespace nfm::config
