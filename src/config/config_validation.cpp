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

bool IsValid(LanguageMode value) noexcept {
    switch (value) {
    case LanguageMode::System:
    case LanguageMode::English:
    case LanguageMode::SimplifiedChinese:
        return true;
    }
    return false;
}

bool IsValid(ThemeMode value) noexcept {
    switch (value) {
    case ThemeMode::System:
    case ThemeMode::Light:
    case ThemeMode::Dark:
        return true;
    }
    return false;
}

bool IsValid(DockSide value) noexcept {
    switch (value) {
    case DockSide::Left:
    case DockSide::Right:
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
    AddRangeIssueIfNeeded(result, config.floating.auto_hide_delay, kMinimumAutoHideDelay,
                          kMaximumAutoHideDelay, "floating.auto_hide_delay_ms");
    AddRangeIssueIfNeeded(result, config.floating.opacity_percent, kMinimumOpacityPercent,
                          kMaximumOpacityPercent, "floating.opacity_percent");
    AddRangeIssueIfNeeded(result, config.floating.blur_strength_percent,
                          kMinimumBlurStrengthPercent, kMaximumBlurStrengthPercent,
                          "floating.blur_strength_percent");
    AddRangeIssueIfNeeded(result, config.floating.capsule_width_dip, kMinimumCapsuleWidthDip,
                          kMaximumCapsuleWidthDip, "floating.capsule_width_dip");
    AddRangeIssueIfNeeded(result, config.floating.capsule_height_dip, kMinimumCapsuleHeightDip,
                          kMaximumCapsuleHeightDip, "floating.capsule_height_dip");

    if (!IsValid(config.display.speed_unit)) {
        AddEnumIssue(result, "display.speed_unit");
    }
    if (!IsValid(config.appearance.language)) {
        AddEnumIssue(result, "appearance.language");
    }
    if (!IsValid(config.appearance.theme)) {
        AddEnumIssue(result, "appearance.theme");
    }
    if (!IsValid(config.floating.anchor_side)) {
        AddEnumIssue(result, "floating.anchor_side");
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
