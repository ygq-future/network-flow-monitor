#include "network_flow_monitor/config/config_validation.h"

#include <string_view>

namespace nfm::config {
namespace {

template <typename Value>
void AddRangeIssueIfNeeded(ConfigValidationResult& result, Value value, Value minimum,
                           Value maximum, std::string_view field_path) {
    if (value < minimum || value > maximum) {
        result.issues.push_back(ConfigIssue{.code = ConfigIssueCode::OutOfRange,
                                            .field_path = std::string{field_path}});
    }
}

void AddEnumIssue(ConfigValidationResult& result, std::string_view field_path) {
    result.issues.push_back(
        ConfigIssue{.code = ConfigIssueCode::InvalidEnum, .field_path = std::string{field_path}});
}

bool IsValid(SpeedUnit value) noexcept {
    switch (value) {
    case SpeedUnit::Auto:
    case SpeedUnit::Bytes:
    case SpeedUnit::Bits:
        return true;
    }
    return false;
}

bool IsValid(CollectorMode value) noexcept {
    switch (value) {
    case CollectorMode::Portable:
    case CollectorMode::Service:
        return true;
    }
    return false;
}

bool IsValid(ServiceStartType value) noexcept {
    switch (value) {
    case ServiceStartType::Manual:
    case ServiceStartType::Automatic:
    case ServiceStartType::AutomaticDelayed:
        return true;
    }
    return false;
}

bool IsValid(LogLevel value) noexcept {
    switch (value) {
    case LogLevel::Off:
    case LogLevel::Error:
    case LogLevel::Info:
    case LogLevel::Debug:
        return true;
    }
    return false;
}

} // namespace

bool ConfigValidationResult::ok() const noexcept {
    return issues.empty();
}

ConfigValidationResult ValidateAppConfig(const AppConfig& config) {
    ConfigValidationResult result;

    AddRangeIssueIfNeeded(result, config.schema_version, kCurrentSchemaVersion,
                          kCurrentSchemaVersion, "schema_version");
    AddRangeIssueIfNeeded(result, config.ranking.top_n, kMinimumTopN, kMaximumTopN,
                          "ranking.top_n");
    AddRangeIssueIfNeeded(result, config.ranking.window, kMinimumRankingWindow,
                          kMaximumRankingWindow, "ranking.window_ms");
    AddRangeIssueIfNeeded(result, config.display.refresh_interval, kMinimumRefreshInterval,
                          kMaximumRefreshInterval, "display.refresh_interval_ms");
    AddRangeIssueIfNeeded(result, config.interaction.hover_delay, kMinimumHoverDelay,
                          kMaximumHoverDelay, "interaction.hover_delay_ms");
    AddRangeIssueIfNeeded(result, config.interaction.dismiss_delay, kMinimumDismissDelay,
                          kMaximumDismissDelay, "interaction.dismiss_delay_ms");
    AddRangeIssueIfNeeded(result, config.floating.snap_distance_dip, kMinimumSnapDistanceDip,
                          kMaximumSnapDistanceDip, "floating.snap_distance_dip");
    AddRangeIssueIfNeeded(result, config.floating.auto_hide_delay, kMinimumAutoHideDelay,
                          kMaximumAutoHideDelay, "floating.auto_hide_delay_ms");
    AddRangeIssueIfNeeded(result, config.floating.reveal_strip_dip, kMinimumRevealStripDip,
                          kMaximumRevealStripDip, "floating.reveal_strip_dip");
    AddRangeIssueIfNeeded(result, config.floating.opacity_percent, kMinimumOpacityPercent,
                          kMaximumOpacityPercent, "floating.opacity_percent");

    if (!IsValid(config.display.speed_unit)) {
        AddEnumIssue(result, "display.speed_unit");
    }
    if (!IsValid(config.collector.preferred_mode)) {
        AddEnumIssue(result, "collector.preferred_mode");
    }
    if (!IsValid(config.service.start_type)) {
        AddEnumIssue(result, "service.start_type");
    }
    if (!IsValid(config.diagnostics.log_level)) {
        AddEnumIssue(result, "diagnostics.log_level");
    }

    return result;
}

} // namespace nfm::config
