#ifndef NETWORK_FLOW_MONITOR_HOST_BRIDGE_BRIDGE_TYPES_H
#define NETWORK_FLOW_MONITOR_HOST_BRIDGE_BRIDGE_TYPES_H

#include "network_flow_monitor/config/app_config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nfm::host::bridge {

inline constexpr std::uint32_t kCurrentBridgeVersion = 1U;
inline constexpr std::size_t kMaxBridgePayloadBytes = 262'144U; // 256 KiB

enum class MessageDirection : std::uint8_t {
    NativeToWeb,
    WebToNative,
};

enum class BridgeErrorCode : std::uint8_t {
    None = 0,
    InvalidJson,
    PayloadTooLarge,
    UnknownCommand,
    InvalidDirection,
    InvalidVersion,
    MissingField,
    InvalidFieldType,
    ExecutionFailed,
    InvalidSettingKey,
    InvalidSettingValue,
};

// Web -> Native Commands
enum class WebCommandType : std::uint8_t {
    ShowMonitor,
    HideMonitor,
    SetPin,
    OpenSettings,
    RequestConfig,
    UpdateSetting,
    RetryCollector,
    RestartCollector,
    MinimizeWindow,
    ToggleMaximizeWindow,
    StartWindowDrag,
    ExitApp,
    SetTheme,
    SetLocale,
    OpenContextMenu,
    ResizePanel,
    ResizeSettingsWindow,
    OpenLogDirectory,
    CheckForUpdate,
    DownloadAndApplyUpdate,
    RequestConnectedApplications,
    BrowseExcludedProgram,
    Unknown,
};

// Native -> Web Events / Responses
enum class NativeEventType : std::uint8_t {
    TrafficTick,
    RankingSnapshot,
    CollectorStatus,
    SessionReset,
    ThemeChanged,
    LocaleChanged,
    ConfigChanged,
    SurfaceChanged,
    CommandResponse,
    UpdateStatus,
    ConnectedApplications,
    Error,
    Unknown,
};

enum class SurfaceKind : std::uint8_t {
    Tray,
    Capsule,
    Panel,
    Settings,
};

struct SurfaceChangedDto {
    SurfaceKind surface{SurfaceKind::Capsule};
    bool pinned{false};
    config::DockSide dock_side{config::DockSide::Right};
    bool auto_hidden{false};
    bool operator==(const SurfaceChangedDto&) const = default;
};

[[nodiscard]] std::string_view to_string(SurfaceKind surface) noexcept;

struct TrafficTickDto {
    std::uint64_t seq{};
    std::uint64_t monotonic_ms{};
    std::uint64_t download_bytes_per_sec{};
    std::uint64_t upload_bytes_per_sec{};

    bool operator==(const TrafficTickDto&) const = default;
};

struct RankingAppDto {
    std::string id;
    std::string executable_path;
    std::string display_name;
    std::string icon_key;
    std::uint64_t download_bytes_per_sec{};
    std::uint64_t upload_bytes_per_sec{};
    std::uint64_t total_download_bytes{};
    std::uint64_t total_upload_bytes{};

    bool operator==(const RankingAppDto&) const = default;
};

struct RankingSnapshotDto {
    std::uint64_t seq{};
    std::uint64_t monotonic_ms{};
    std::string status{"Ready"};
    std::uint64_t total_download_bytes_per_sec{};
    std::uint64_t total_upload_bytes_per_sec{};
    std::vector<RankingAppDto> applications;

    bool operator==(const RankingSnapshotDto&) const = default;
};

struct CollectorStatusDto {
    std::uint64_t monotonic_ms{};
    bool etw_session_active{true};
    std::uint64_t dropped_packets{};
    std::uint32_t active_pids{};
    std::string state{"Running"}; // "Stopped", "Starting", "Running", "Failed"
    std::uint32_t process_id{};
    std::string instance_id;
    bool is_elevated{false};
    std::string last_error;

    bool operator==(const CollectorStatusDto&) const = default;
};

struct SessionResetDto {
    std::string collector_instance_id;
    std::uint32_t collector_pid{};

    bool operator==(const SessionResetDto&) const = default;
};

struct ThemeChangedDto {
    std::string theme{"System"}; // "System", "Light", "Dark"

    bool operator==(const ThemeChangedDto&) const = default;
};

struct LocaleChangedDto {
    std::string locale{"System"}; // "System", "English", "SimplifiedChinese"

    bool operator==(const LocaleChangedDto&) const = default;
};

struct CommandResponseDto {
    std::string command_id;
    bool success{true};
    std::string message;

    bool operator==(const CommandResponseDto&) const = default;
};

struct UpdateStatusDto {
    std::string state{"Idle"}; // "Idle", "Checking", "UpToDate", "UpdateAvailable", "Downloading",
                               // "Applying", "Error"
    std::string current_version;
    std::string latest_version;
    std::uint8_t download_progress_percent{0};
    std::string error_message;

    bool operator==(const UpdateStatusDto&) const = default;
};
struct ConnectedAppDto {
    std::string id;
    std::string executable_path;
    std::string executable_name;
    std::string display_name;
    std::string icon_key;
    std::uint64_t total_download_bytes{};
    std::uint64_t total_upload_bytes{};

    bool operator==(const ConnectedAppDto&) const = default;
};

struct ConnectedApplicationsDto {
    std::vector<ConnectedAppDto> applications;

    bool operator==(const ConnectedApplicationsDto&) const = default;
};

struct BridgeErrorDto {
    std::string code;
    std::string message;

    bool operator==(const BridgeErrorDto&) const = default;
};

struct WebCommand {
    WebCommandType type{WebCommandType::Unknown};
    std::uint32_t version{kCurrentBridgeVersion};
    std::string command_id;
    std::string raw_payload_json;

    // Optional typed parameters extracted based on type
    std::optional<bool> pin_state;
    std::optional<std::string> route;
    std::optional<std::string> setting_key;
    std::optional<std::string> setting_value_json;
    std::optional<std::string> theme;
    std::optional<std::string> locale;
    std::optional<int> screen_x;
    std::optional<int> screen_y;
    std::optional<int> panel_height_dip;
};

[[nodiscard]] std::string_view to_string(WebCommandType type) noexcept;
[[nodiscard]] WebCommandType parse_web_command_type(std::string_view name) noexcept;

[[nodiscard]] std::string_view to_string(NativeEventType type) noexcept;
[[nodiscard]] NativeEventType parse_native_event_type(std::string_view name) noexcept;

[[nodiscard]] std::string_view to_error_code_string(BridgeErrorCode code) noexcept;

} // namespace nfm::host::bridge

#endif // NETWORK_FLOW_MONITOR_HOST_BRIDGE_BRIDGE_TYPES_H
