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
inline constexpr std::uint8_t kMinimumOpacityPercent = 30U;
inline constexpr std::uint8_t kMaximumOpacityPercent = 100U;
inline constexpr std::int32_t kMinimumCapsuleWidthDip = 8;
inline constexpr std::int32_t kMaximumCapsuleWidthDip = 24;
inline constexpr std::int32_t kMinimumCapsuleHeightDip = 40;
inline constexpr std::int32_t kMaximumCapsuleHeightDip = 160;
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

enum class CollectorMode : std::uint8_t {
    Portable,
    Service,
};

enum class ServiceStartType : std::uint8_t {
    Manual,
    Automatic,
    AutomaticDelayed,
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
    std::chrono::milliseconds refresh_interval;
    SpeedUnit speed_unit;
    bool show_upload;
    bool show_download;

    bool operator==(const DisplayConfig&) const = default;
};

struct InteractionConfig {
    std::chrono::milliseconds hover_delay;
    std::chrono::milliseconds dismiss_delay;

    bool operator==(const InteractionConfig&) const = default;
};

struct FloatingConfig {
    bool auto_hide_enabled;
    std::chrono::milliseconds auto_hide_delay;
    std::uint8_t opacity_percent;
    std::int32_t capsule_width_dip;
    std::int32_t capsule_height_dip;
    bool nfm_pinned;
    std::array<char, kMaximumMonitorIdentifierBytes + 1U> anchor_monitor_id{};
    DockSide anchor_side;
    std::int32_t anchor_vertical_offset_dip;

    bool operator==(const FloatingConfig&) const = default;
};

struct AppearanceConfig {
    LanguageMode language;
    ThemeMode theme;

    bool operator==(const AppearanceConfig&) const = default;
};

struct StartupConfig {
    bool ui_at_login;

    bool operator==(const StartupConfig&) const = default;
};

struct CollectorConfig {
    CollectorMode preferred_mode;

    bool operator==(const CollectorConfig&) const = default;
};

struct ServiceConfig {
    ServiceStartType start_type;

    bool operator==(const ServiceConfig&) const = default;
};

struct DiagnosticsConfig {
    LogLevel log_level;

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
    CollectorConfig collector{};
    ServiceConfig service{};
    DiagnosticsConfig diagnostics{};

    bool operator==(const AppConfig&) const = default;
};

[[nodiscard]] AppConfig DefaultAppConfig() noexcept;

} // namespace nfm::config

#endif // NETWORK_FLOW_MONITOR_CONFIG_APP_CONFIG_H
