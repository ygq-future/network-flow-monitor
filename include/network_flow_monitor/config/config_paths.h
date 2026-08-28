#ifndef NETWORK_FLOW_MONITOR_CONFIG_CONFIG_PATHS_H
#define NETWORK_FLOW_MONITOR_CONFIG_CONFIG_PATHS_H

#include <filesystem>

namespace nfm::config {

[[nodiscard]] std::filesystem::path PortableConfigPath(
    const std::filesystem::path& executable_path);
[[nodiscard]] std::filesystem::path InstalledUserConfigPath(
    const std::filesystem::path& local_app_data);
} // namespace nfm::config

#endif // NETWORK_FLOW_MONITOR_CONFIG_CONFIG_PATHS_H
