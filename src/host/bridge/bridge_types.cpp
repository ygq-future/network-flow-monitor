#include "network_flow_monitor/host/bridge/bridge_types.h"

namespace nfm::host::bridge {

std::string_view to_string(WebCommandType type) noexcept {
    switch (type) {
    case WebCommandType::ShowMonitor:
        return "showMonitor";
    case WebCommandType::HideMonitor:
        return "hideMonitor";
    case WebCommandType::SetPin:
        return "setPin";
    case WebCommandType::OpenSettings:
        return "openSettings";
    case WebCommandType::RequestConfig:
        return "requestConfig";
    case WebCommandType::UpdateSetting:
        return "updateSetting";
    case WebCommandType::RetryCollector:
        return "retryCollector";
    case WebCommandType::RestartCollector:
        return "restartCollector";
    case WebCommandType::MinimizeWindow:
        return "minimizeWindow";
    case WebCommandType::ToggleMaximizeWindow:
        return "toggleMaximizeWindow";
    case WebCommandType::StartWindowDrag:
        return "startWindowDrag";
    case WebCommandType::ExitApp:
        return "exitApp";
    case WebCommandType::SetTheme:
        return "setTheme";
    case WebCommandType::SetLocale:
        return "setLocale";
    case WebCommandType::OpenContextMenu:
        return "openContextMenu";
    case WebCommandType::ResizePanel:
        return "resizePanel";
    case WebCommandType::ResizeSettingsWindow:
        return "resizeSettingsWindow";
    case WebCommandType::OpenLogDirectory:
        return "openLogDirectory";
    case WebCommandType::CheckForUpdate:
        return "checkForUpdate";
    case WebCommandType::DownloadAndApplyUpdate:
        return "downloadAndApplyUpdate";
    case WebCommandType::RequestConnectedApplications:
        return "requestConnectedApplications";
    case WebCommandType::BrowseExcludedProgram:
        return "browseExcludedProgram";
    case WebCommandType::Unknown:
    default:
        return "unknown";
    }
}

WebCommandType parse_web_command_type(std::string_view name) noexcept {
    if (name == "showMonitor") {
        return WebCommandType::ShowMonitor;
    }
    if (name == "hideMonitor") {
        return WebCommandType::HideMonitor;
    }
    if (name == "setPin") {
        return WebCommandType::SetPin;
    }
    if (name == "openSettings") {
        return WebCommandType::OpenSettings;
    }
    if (name == "requestConfig") {
        return WebCommandType::RequestConfig;
    }
    if (name == "updateSetting") {
        return WebCommandType::UpdateSetting;
    }
    if (name == "retryCollector") {
        return WebCommandType::RetryCollector;
    }
    if (name == "restartCollector") {
        return WebCommandType::RestartCollector;
    }
    if (name == "minimizeWindow") {
        return WebCommandType::MinimizeWindow;
    }
    if (name == "toggleMaximizeWindow" || name == "toggleMaximize") {
        return WebCommandType::ToggleMaximizeWindow;
    }
    if (name == "startWindowDrag") {
        return WebCommandType::StartWindowDrag;
    }
    if (name == "exitApp") {
        return WebCommandType::ExitApp;
    }
    if (name == "setTheme") {
        return WebCommandType::SetTheme;
    }
    if (name == "setLocale") {
        return WebCommandType::SetLocale;
    }
    if (name == "openContextMenu") {
        return WebCommandType::OpenContextMenu;
    }
    if (name == "resizePanel") {
        return WebCommandType::ResizePanel;
    }
    if (name == "resizeSettingsWindow") {
        return WebCommandType::ResizeSettingsWindow;
    }
    if (name == "openLogDirectory") {
        return WebCommandType::OpenLogDirectory;
    }
    if (name == "checkForUpdate") {
        return WebCommandType::CheckForUpdate;
    }
    if (name == "downloadAndApplyUpdate") {
        return WebCommandType::DownloadAndApplyUpdate;
    }
    if (name == "requestConnectedApplications") {
        return WebCommandType::RequestConnectedApplications;
    }
    if (name == "browseExcludedProgram") {
        return WebCommandType::BrowseExcludedProgram;
    }
    return WebCommandType::Unknown;
}

std::string_view to_string(NativeEventType type) noexcept {
    switch (type) {
    case NativeEventType::TrafficTick:
        return "trafficTick";
    case NativeEventType::RankingSnapshot:
        return "rankingSnapshot";
    case NativeEventType::CollectorStatus:
        return "collectorStatus";
    case NativeEventType::SessionReset:
        return "sessionReset";
    case NativeEventType::ThemeChanged:
        return "themeChanged";
    case NativeEventType::LocaleChanged:
        return "localeChanged";
    case NativeEventType::ConfigChanged:
        return "configChanged";
    case NativeEventType::SurfaceChanged:
        return "surfaceChanged";
    case NativeEventType::CommandResponse:
        return "commandResponse";
    case NativeEventType::UpdateStatus:
        return "updateStatus";
    case NativeEventType::ConnectedApplications:
        return "connectedApplications";
    case NativeEventType::Error:
        return "error";
    case NativeEventType::Unknown:
    default:
        return "unknown";
    }
}

NativeEventType parse_native_event_type(std::string_view name) noexcept {
    if (name == "trafficTick") {
        return NativeEventType::TrafficTick;
    }
    if (name == "rankingSnapshot") {
        return NativeEventType::RankingSnapshot;
    }
    if (name == "collectorStatus") {
        return NativeEventType::CollectorStatus;
    }
    if (name == "sessionReset") {
        return NativeEventType::SessionReset;
    }
    if (name == "themeChanged") {
        return NativeEventType::ThemeChanged;
    }
    if (name == "localeChanged") {
        return NativeEventType::LocaleChanged;
    }
    if (name == "configChanged") {
        return NativeEventType::ConfigChanged;
    }
    if (name == "surfaceChanged") {
        return NativeEventType::SurfaceChanged;
    }
    if (name == "commandResponse") {
        return NativeEventType::CommandResponse;
    }
    if (name == "updateStatus") {
        return NativeEventType::UpdateStatus;
    }
    if (name == "connectedApplications") {
        return NativeEventType::ConnectedApplications;
    }
    if (name == "error") {
        return NativeEventType::Error;
    }
    return NativeEventType::Unknown;
}

std::string_view to_string(SurfaceKind surface) noexcept {
    switch (surface) {
    case SurfaceKind::Tray:
        return "tray";
    case SurfaceKind::Capsule:
        return "capsule";
    case SurfaceKind::Panel:
        return "panel";
    case SurfaceKind::Settings:
        return "settings";
    default:
        return "capsule";
    }
}

std::string_view to_error_code_string(BridgeErrorCode code) noexcept {
    switch (code) {
    case BridgeErrorCode::InvalidJson:
        return "INVALID_JSON";
    case BridgeErrorCode::PayloadTooLarge:
        return "PAYLOAD_TOO_LARGE";
    case BridgeErrorCode::UnknownCommand:
        return "UNKNOWN_COMMAND";
    case BridgeErrorCode::InvalidDirection:
        return "INVALID_DIRECTION";
    case BridgeErrorCode::InvalidVersion:
        return "INVALID_VERSION";
    case BridgeErrorCode::MissingField:
        return "MISSING_FIELD";
    case BridgeErrorCode::InvalidFieldType:
        return "INVALID_FIELD_TYPE";
    case BridgeErrorCode::ExecutionFailed:
        return "EXECUTION_FAILED";
    case BridgeErrorCode::InvalidSettingKey:
        return "INVALID_SETTING_KEY";
    case BridgeErrorCode::InvalidSettingValue:
        return "INVALID_SETTING_VALUE";
    case BridgeErrorCode::None:
    default:
        return "NONE";
    }
}

} // namespace nfm::host::bridge
