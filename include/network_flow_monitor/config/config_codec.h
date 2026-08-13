#ifndef NETWORK_FLOW_MONITOR_CONFIG_CONFIG_CODEC_H
#define NETWORK_FLOW_MONITOR_CONFIG_CONFIG_CODEC_H

#include "network_flow_monitor/config/app_config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nfm::config {

enum class ConfigLoadStatus : std::uint8_t {
    Loaded,
    LoadedWithDefaults,
    Missing,
    Damaged,
    FutureVersion,
    UnsupportedLegacyVersion,
    IoError,
};

enum class ConfigDocumentKind : std::uint8_t {
    User,
    Service,
};

enum class ConfigErrorCode : std::uint8_t {
    None,
    ParseFailed,
    InvalidRoot,
    MissingSchemaVersion,
    InvalidSchemaVersion,
    DuplicateKey,
    UnknownKey,
    TypeMismatch,
    InvalidEnum,
    OutOfRange,
    FileTooLarge,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    ReplaceFailed,
};

struct ConfigError {
    ConfigErrorCode code;
    std::string field_path;
    std::string detail;
    std::uint32_t system_error;

    bool operator==(const ConfigError&) const = default;
};

struct ConfigLoadResult {
    AppConfig config{};
    ConfigLoadStatus status{ConfigLoadStatus::Damaged};
    std::optional<ConfigError> error;
};

struct ConfigJsonResult {
    std::string json;
    std::optional<ConfigError> error;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] ConfigLoadResult ParseConfigJson(std::string_view json, ConfigDocumentKind kind);
[[nodiscard]] ConfigJsonResult SerializeConfigJson(const AppConfig& config,
                                                   ConfigDocumentKind kind);

} // namespace nfm::config

#endif // NETWORK_FLOW_MONITOR_CONFIG_CONFIG_CODEC_H
