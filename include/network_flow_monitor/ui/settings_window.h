#ifndef NETWORK_FLOW_MONITOR_UI_SETTINGS_WINDOW_H
#define NETWORK_FLOW_MONITOR_UI_SETTINGS_WINDOW_H

#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/config/config_codec.h"
#include "network_flow_monitor/ui/settings_controller.h"

#include <windows.h>

#include <cstdint>

namespace nfm::ui {

class SettingsWindowHost {
  public:
    SettingsWindowHost() = default;
    virtual ~SettingsWindowHost() = default;
    SettingsWindowHost(const SettingsWindowHost&) = delete;
    SettingsWindowHost& operator=(const SettingsWindowHost&) = delete;
    SettingsWindowHost(SettingsWindowHost&&) = delete;
    SettingsWindowHost& operator=(SettingsWindowHost&&) = delete;

    [[nodiscard]] virtual SettingsSaveResult SaveSettings(
        const config::AppConfig& value) noexcept = 0;
    [[nodiscard]] virtual SettingsActionResult ExecuteServiceAction(
        SettingsServiceAction action, config::ServiceStartType start_type) noexcept = 0;
};

class SettingsWindow final {
  public:
    SettingsWindow(HINSTANCE instance, SettingsWindowHost& host) noexcept;
    ~SettingsWindow();
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;
    SettingsWindow(SettingsWindow&&) = delete;
    SettingsWindow& operator=(SettingsWindow&&) = delete;

    [[nodiscard]] bool Show(HWND owner, const config::AppConfig& value) noexcept;
    void Close() noexcept;
    [[nodiscard]] HWND hwnd() const noexcept {
        return window_;
    }

  private:
    // NOLINTNEXTLINE(performance-enum-size): Win32 passes child control IDs as int values.
    enum ControlId : int {
        kTopN = 100,
        kRankingWindow,
        kRefreshInterval,
        kHoverDelay,
        kDismissDelay,
        kCapsuleWidth,
        kAutoHideDelay,
        kCapsuleHeight,
        kOpacity,
        kSpeedUnit,
        kCollectorMode,
        kServiceStartType,
        kLogLevel,
        kShowUpload,
        kShowDownload,
        kAutoHide,
        kUiAtLogin,
        kSave,
        kCancel,
        kInstallService,
        kStartService,
        kStopService,
        kRestartService,
        kUninstallService,
        kQueryService,
        kAuthorizeCollector,
        kLanguage,
        kTheme,
        kBlurStrength,
        kYieldTopmostWhenFullscreen,
    };

    [[nodiscard]] bool CreateSettingsWindow(HWND owner) noexcept;
    [[nodiscard]] bool CreateControls() noexcept;
    void PopulateControls(const config::AppConfig& value) noexcept;
    void UpdateServiceStatus(const config::AppConfig& value) noexcept;
    [[nodiscard]] bool ReadControls(config::AppConfig& value) const noexcept;
    void HandleCommand(int control_id) noexcept;
    void HandleServiceCommand(int control_id) noexcept;
    void HandleAuthorizeCollector() noexcept;
    void ShowError(const wchar_t* title, const wchar_t* message) const noexcept;
    void ShowSaveError(const SettingsSaveResult& result) const noexcept;
    void ShowServiceResult(const SettingsActionResult& result) const noexcept;
    [[nodiscard]] static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                                     LPARAM lparam) noexcept;

    HINSTANCE instance_{};
    SettingsWindowHost* host_{};
    HWND window_{};
    HWND owner_{};
    HWND top_n_{};
    HWND ranking_window_{};
    HWND refresh_interval_{};
    HWND hover_delay_{};
    HWND dismiss_delay_{};
    HWND capsule_width_{};
    HWND auto_hide_delay_{};
    HWND capsule_height_{};
    HWND opacity_{};
    HWND blur_strength_{};
    HWND speed_unit_{};
    HWND collector_mode_{};
    HWND service_start_type_{};
    HWND log_level_{};
    HWND show_upload_{};
    HWND show_download_{};
    HWND auto_hide_{};
    HWND yield_topmost_when_fullscreen_{};
    HWND ui_at_login_{};
    HWND language_{};
    HWND theme_{};
    HWND service_status_{};
    config::AppConfig current_value_{};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_SETTINGS_WINDOW_H
