#ifndef NETWORK_FLOW_MONITOR_CONFIG_CONFIG_VALIDATION_H
#define NETWORK_FLOW_MONITOR_CONFIG_CONFIG_VALIDATION_H

#include "network_flow_monitor/config/app_config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nfm::config {

enum class ConfigIssueCode : std::uint8_t {
    OutOfRange,
    InvalidEnum,
};

struct ConfigIssue {
    ConfigIssueCode code;
    std::string field_path;

    bool operator==(const ConfigIssue&) const = default;
};

struct ConfigValidationResult {
    std::vector<ConfigIssue> issues;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] ConfigValidationResult ValidateAppConfig(const AppConfig& config);

} // namespace nfm::config

#endif // NETWORK_FLOW_MONITOR_CONFIG_CONFIG_VALIDATION_H
