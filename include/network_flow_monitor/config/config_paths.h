#ifndef NETWORK_FLOW_MONITOR_CONFIG_CONFIG_PATHS_H
#define NETWORK_FLOW_MONITOR_CONFIG_CONFIG_PATHS_H

#include <filesystem>

namespace nfm::config {

[[nodiscard]] std::filesystem::path PortableDataDirectory(
    const std::filesystem::path& executable_path);
[[nodiscard]] std::filesystem::path PortableLogDirectory(
    const std::filesystem::path& executable_path);
[[nodiscard]] std::filesystem::path PortableWebView2UserDataDirectory(
    const std::filesystem::path& executable_path);
[[nodiscard]] std::filesystem::path PortableConfigPath(
    const std::filesystem::path& executable_path);
[[nodiscard]] std::filesystem::path PortableTrafficHistoryPath(
    const std::filesystem::path& executable_path);
[[nodiscard]] std::filesystem::path LegacyRootConfigPath(
    const std::filesystem::path& executable_path);
} // namespace nfm::config

#endif // NETWORK_FLOW_MONITOR_CONFIG_CONFIG_PATHS_H
