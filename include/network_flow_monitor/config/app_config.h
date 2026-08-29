#ifndef NETWORK_FLOW_MONITOR_CONFIG_APP_CONFIG_H
#define NETWORK_FLOW_MONITOR_CONFIG_APP_CONFIG_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace nfm::config {

inline constexpr std::uint32_t kCurrentSchemaVersion = 2U;

inline constexpr std::uint8_t kMinimumTopN = 1U;
inline constexpr std::uint8_t kMaximumTopN = 20U;
inline constexpr auto kMinimumRankingWindow = std::chrono::milliseconds{500};
inline constexpr auto kMaximumRankingWindow = std::chrono::milliseconds{10000};
inline constexpr auto kMinimumRefreshInterval = std::chrono::milliseconds{100};
inline constexpr auto kMaximumRefreshInterval = std::chrono::milliseconds{5000};
inline constexpr auto kMinimumHoverDelay = std::chrono::milliseconds{0};
inline constexpr auto kMaximumHoverDelay = std::chrono::milliseconds{2000};
inline constexpr auto kMinimumDismissDelay = std::chrono::milliseconds{0};
inline constexpr auto kMaximumDismissDelay = std::chrono::milliseconds{2000};
inline constexpr auto kMinimumAutoHideDelay = std::chrono::milliseconds{0};
inline constexpr auto kMaximumAutoHideDelay = std::chrono::milliseconds{5000};
inline constexpr std::uint8_t kMinimumOpacityPercent = 0U;
inline constexpr std::uint8_t kMaximumOpacityPercent = 100U;
inline constexpr std::uint8_t kMinimumBlurStrengthPercent = 0U;
inline constexpr std::uint8_t kMaximumBlurStrengthPercent = 100U;
inline constexpr std::int32_t kMinimumCapsuleWidthDip = 8;
inline constexpr std::int32_t kMaximumCapsuleWidthDip = 24;
inline constexpr std::int32_t kMinimumCapsuleHeightDip = 40;
inline constexpr std::int32_t kMaximumCapsuleHeightDip = 160;
inline constexpr std::int32_t kMinimumCapsuleEndRoundingDip = 0;
inline constexpr std::int32_t kMaximumCapsuleEndRoundingDip = 12;
inline constexpr std::int32_t kDefaultCapsuleEndRoundingDip = 5;
inline constexpr std::size_t kMaximumMonitorIdentifierBytes = 255U;

enum class SpeedUnit : std::uint8_t {
    Auto,
    Bytes,
    Bits,
};

enum class LanguageMode : std::uint8_t {
    System,
    English,
    SimplifiedChinese
};
enum class ThemeMode : std::uint8_t {
    System,
    Light,
    Dark
};
enum class DockSide : std::uint8_t {
    Left,
    Right
};

enum class LogLevel : std::uint8_t {
    Off,
    Error,
    Info,
    Debug,
};

struct RankingConfig {
    std::uint8_t top_n;
    std::chrono::milliseconds window;

    bool operator==(const RankingConfig&) const = default;
};

struct DisplayConfig {
    std::chrono::milliseconds refresh_interval{250};
    SpeedUnit speed_unit{SpeedUnit::Auto};
    bool show_upload{true};
    bool show_download{true};

    bool operator==(const DisplayConfig&) const = default;
};

struct InteractionConfig {
    std::chrono::milliseconds hover_delay{350};
    std::chrono::milliseconds dismiss_delay{500};

    bool operator==(const InteractionConfig&) const = default;
};

struct FloatingConfig {
    bool auto_hide_enabled{false};
    std::chrono::milliseconds auto_hide_delay{5000};
    std::uint8_t opacity_percent{100};
    std::uint8_t blur_strength_percent{0};
    std::int32_t capsule_width_dip{10};
    std::int32_t capsule_height_dip{64};
    std::int32_t capsule_end_rounding_dip{5};
    bool nfm_pinned{false};
    bool yield_topmost_when_fullscreen{true};
    std::array<char, kMaximumMonitorIdentifierBytes + 1U> anchor_monitor_id{};
    DockSide anchor_side{DockSide::Right};
    std::int32_t anchor_vertical_offset_dip{0};

    bool operator==(const FloatingConfig&) const = default;
};

[[nodiscard]] inline std::int32_t EffectiveCapsuleEndRoundingDip(
    const FloatingConfig& floating) noexcept {
    const auto max_possible = floating.capsule_width_dip / 2;
    return std::min(floating.capsule_end_rounding_dip, max_possible);
}

struct AppearanceConfig {
    LanguageMode language{LanguageMode::System};
    ThemeMode theme{ThemeMode::System};

    bool operator==(const AppearanceConfig&) const = default;
};

struct StartupConfig {
    bool ui_at_login{false};

    bool operator==(const StartupConfig&) const = default;
};

struct DiagnosticsConfig {
    LogLevel log_level{LogLevel::Info};

    bool operator==(const DiagnosticsConfig&) const = default;
};

struct AppConfig {
    std::uint32_t schema_version{};
    RankingConfig ranking{};
    DisplayConfig display{};
    InteractionConfig interaction{};
    FloatingConfig floating{};
    AppearanceConfig appearance{};
    StartupConfig startup{};
    DiagnosticsConfig diagnostics{};

    bool operator==(const AppConfig&) const = default;
};

[[nodiscard]] AppConfig DefaultAppConfig() noexcept;

} // namespace nfm::config

#endif // NETWORK_FLOW_MONITOR_CONFIG_APP_CONFIG_H
