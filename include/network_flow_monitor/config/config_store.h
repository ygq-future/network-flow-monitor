#ifndef NETWORK_FLOW_MONITOR_CONFIG_CONFIG_STORE_H
#define NETWORK_FLOW_MONITOR_CONFIG_CONFIG_STORE_H

#include "network_flow_monitor/config/config_codec.h"

#include <filesystem>
#include <optional>

namespace nfm::config {

struct ConfigSaveResult {
    bool saved{};
    std::optional<ConfigError> error;
};

[[nodiscard]] ConfigLoadResult LoadConfigFile(const std::filesystem::path& path,
                                              ConfigDocumentKind kind);
[[nodiscard]] ConfigSaveResult SaveConfigFile(const std::filesystem::path& path,
                                              const AppConfig& config, ConfigDocumentKind kind);

} // namespace nfm::config

#endif // NETWORK_FLOW_MONITOR_CONFIG_CONFIG_STORE_H
