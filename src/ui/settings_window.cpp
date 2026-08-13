#include "network_flow_monitor/ui/settings_window.h"

#include "network_flow_monitor/ui/tray_controller.h"

#include <windows.h>

#include <array>
#include <cwchar>
#include <string>

namespace nfm::ui {
namespace {

constexpr wchar_t kSettingsClassName[] = L"NetworkFlowMonitor.SettingsWindow";
constexpr int kWindowWidth = 600;
constexpr int kWindowHeight = 690;
constexpr int kLabelX = 20;
constexpr int kControlX = 270;
constexpr int kControlWidth = 250;
constexpr int kRowHeight = 29;

[[nodiscard]] HMENU ControlMenu(int id) noexcept {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): CreateWindowExW uses HMENU for child IDs.
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

HWND CreateLabel(HWND parent, HINSTANCE instance, const wchar_t* text, int y) {
    return ::CreateWindowExW(0U, L"STATIC", text, WS_CHILD | WS_VISIBLE, kLabelX, y, 230, 22,
                             parent, nullptr, instance, nullptr);
}

HWND CreateEdit(HWND parent, HINSTANCE instance, int id, int y) {
    return ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER,
                             kControlX, y, 120, 22, parent, ControlMenu(id), instance, nullptr);
}

HWND CreateCombo(HWND parent, HINSTANCE instance, int id, int y) {
    return ::CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                             WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, kControlX, y, 190, 180,
                             parent, ControlMenu(id), instance, nullptr);
}

HWND CreateCheck(HWND parent, HINSTANCE instance, int id, const wchar_t* text, int y) {
    return ::CreateWindowExW(0U, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             kControlX, y, 220, 22, parent, ControlMenu(id), instance, nullptr);
}

HWND CreateButton(HWND parent, HINSTANCE instance, int id, const wchar_t* text, int x, int y,
                  int width = 105) {
    return ::CreateWindowExW(0U, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y,
                             width, 27, parent, ControlMenu(id), instance, nullptr);
}

void AddComboItem(HWND combo, const wchar_t* text) noexcept {
    static_cast<void>(::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)));
}

[[nodiscard]] bool ReadUnsigned(HWND control, std::uint32_t minimum, std::uint32_t maximum,
                                std::uint32_t& value) noexcept {
    std::array<wchar_t, 64> buffer{};
    const auto length = ::GetWindowTextW(control, buffer.data(), static_cast<int>(buffer.size()));
    if (length <= 0 || length >= static_cast<int>(buffer.size())) {
        return false;
    }
    wchar_t* end{};
    const auto parsed = std::wcstoul(buffer.data(), &end, 10);
    if (end == buffer.data() || *end != L'\0' || parsed > maximum || parsed < minimum) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

[[nodiscard]] bool IsChecked(HWND control) noexcept {
    return ::SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SetChecked(HWND control, bool checked) noexcept {
    static_cast<void>(
        ::SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0));
}

[[nodiscard]] const wchar_t* ServiceStateText(service::ServiceState state) noexcept {
    switch (state) {
    case service::ServiceState::Stopped:
        return L"stopped";
    case service::ServiceState::Running:
        return L"running";
    case service::ServiceState::StartPending:
        return L"start pending";
    case service::ServiceState::StopPending:
        return L"stop pending";
    case service::ServiceState::ContinuePending:
        return L"continue pending";
    case service::ServiceState::PausePending:
        return L"pause pending";
    case service::ServiceState::Paused:
        return L"paused";
    case service::ServiceState::Unknown:
        return L"unknown";
    }
    return L"unknown";
}

[[nodiscard]] std::wstring ServiceStatusText(const SettingsActionResult& result) {
    if (result.succeeded) {
        return L"Service status: " + std::wstring{ServiceStateText(result.state)};
    }
    if (result.win32_error == ERROR_SERVICE_DOES_NOT_EXIST) {
        return L"Service status: not installed";
    }
    return L"Service status: query failed (Win32 " + std::to_wstring(result.win32_error) + L")";
}

} // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, SettingsWindowHost& host) noexcept
    : instance_{instance == nullptr ? ::GetModuleHandleW(nullptr) : instance}, host_{&host} {}

SettingsWindow::~SettingsWindow() {
    Close();
}

bool SettingsWindow::Show(HWND owner, const config::AppConfig& value) noexcept {
    try {
        owner_ = owner;
        if (window_ == nullptr && !CreateSettingsWindow(owner)) {
            return false;
        }
        PopulateControls(value);
        UpdateServiceStatus(value);
        const auto window = window_;
        if (window == nullptr) {
            return false;
        }
        ::ShowWindow(window, SW_SHOWNORMAL);
        ::SetForegroundWindow(window);
        return true;
    } catch (...) {
        return false;
    }
}

void SettingsWindow::Close() noexcept {
    if (window_ != nullptr) {
        const auto window = window_;
        window_ = nullptr;
        ::DestroyWindow(window);
    }
}

bool SettingsWindow::CreateSettingsWindow(HWND owner) noexcept {
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (::GetClassInfoExW(instance_, kSettingsClassName, &existing) == FALSE) {
        const auto icon = CreateNetworkFlowMonitorIcon(instance_);
        const WNDCLASSEXW window_class{.cbSize = sizeof(window_class),
                                       .style = CS_HREDRAW | CS_VREDRAW,
                                       .lpfnWndProc = &SettingsWindow::WindowProc,
                                       .cbClsExtra = 0,
                                       .cbWndExtra = 0,
                                       .hInstance = instance_,
                                       .hIcon = icon,
                                       .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
                                       .hbrBackground = ::GetSysColorBrush(COLOR_WINDOW),
                                       .lpszMenuName = nullptr,
                                       .lpszClassName = kSettingsClassName,
                                       .hIconSm = icon};
        if (::RegisterClassExW(&window_class) == 0 &&
            ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    window_ = ::CreateWindowExW(
        WS_EX_DLGMODALFRAME, kSettingsClassName, L"Network Flow Monitor - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT,
        kWindowWidth, kWindowHeight, owner, nullptr, instance_, this);
    if (window_ == nullptr) {
        return false;
    }
    return CreateControls();
}

bool SettingsWindow::CreateControls() noexcept {
    const auto font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    int y = 18;
    top_n_ = CreateEdit(window_, instance_, kTopN, y);
    CreateLabel(window_, instance_, L"Top N applications", y);
    y += kRowHeight;
    ranking_window_ = CreateEdit(window_, instance_, kRankingWindow, y);
    CreateLabel(window_, instance_, L"Ranking window (ms)", y);
    y += kRowHeight;
    refresh_interval_ = CreateEdit(window_, instance_, kRefreshInterval, y);
    CreateLabel(window_, instance_, L"Display refresh interval (ms)", y);
    y += kRowHeight;
    hover_delay_ = CreateEdit(window_, instance_, kHoverDelay, y);
    CreateLabel(window_, instance_, L"Hover delay (ms)", y);
    y += kRowHeight;
    dismiss_delay_ = CreateEdit(window_, instance_, kDismissDelay, y);
    CreateLabel(window_, instance_, L"Dismiss delay (ms)", y);
    y += kRowHeight;
    snap_distance_ = CreateEdit(window_, instance_, kSnapDistance, y);
    CreateLabel(window_, instance_, L"Snap distance (DIP)", y);
    y += kRowHeight;
    auto_hide_delay_ = CreateEdit(window_, instance_, kAutoHideDelay, y);
    CreateLabel(window_, instance_, L"Auto-hide delay (ms)", y);
    y += kRowHeight;
    reveal_strip_ = CreateEdit(window_, instance_, kRevealStrip, y);
    CreateLabel(window_, instance_, L"Reveal strip (DIP)", y);
    y += kRowHeight;
    opacity_ = CreateEdit(window_, instance_, kOpacity, y);
    CreateLabel(window_, instance_, L"Opacity (%)", y);
    y += kRowHeight;

    speed_unit_ = CreateCombo(window_, instance_, kSpeedUnit, y);
    CreateLabel(window_, instance_, L"Speed unit", y);
    AddComboItem(speed_unit_, L"Automatic");
    AddComboItem(speed_unit_, L"Bytes/s");
    AddComboItem(speed_unit_, L"Bits/s");
    y += kRowHeight;
    collector_mode_ = CreateCombo(window_, instance_, kCollectorMode, y);
    CreateLabel(window_, instance_, L"Collector mode", y);
    AddComboItem(collector_mode_, L"Portable (UAC)");
    AddComboItem(collector_mode_, L"Windows service");
    y += kRowHeight;
    service_start_type_ = CreateCombo(window_, instance_, kServiceStartType, y);
    CreateLabel(window_, instance_, L"Service start type", y);
    AddComboItem(service_start_type_, L"Manual");
    AddComboItem(service_start_type_, L"Automatic");
    AddComboItem(service_start_type_, L"Automatic (delayed)");
    y += kRowHeight;
    log_level_ = CreateCombo(window_, instance_, kLogLevel, y);
    CreateLabel(window_, instance_, L"Log level", y);
    AddComboItem(log_level_, L"Off");
    AddComboItem(log_level_, L"Error");
    AddComboItem(log_level_, L"Info");
    AddComboItem(log_level_, L"Debug");
    y += kRowHeight;

    show_upload_ = CreateCheck(window_, instance_, kShowUpload, L"Show upload", y);
    y += kRowHeight;
    show_download_ = CreateCheck(window_, instance_, kShowDownload, L"Show download", y);
    y += kRowHeight;
    auto_hide_ = CreateCheck(window_, instance_, kAutoHide, L"Enable edge auto-hide", y);
    y += kRowHeight;
    ui_at_login_ = CreateCheck(window_, instance_, kUiAtLogin, L"Start UI at login", y);
    y += kRowHeight + 8;

    CreateButton(window_, instance_, kSave, L"Save", 20, y, 100);
    CreateButton(window_, instance_, kCancel, L"Cancel", 130, y, 100);
    y += 38;
    CreateButton(window_, instance_, kInstallService, L"Install", 20, y, 100);
    CreateButton(window_, instance_, kStartService, L"Start", 130, y, 100);
    CreateButton(window_, instance_, kStopService, L"Stop", 240, y, 100);
    CreateButton(window_, instance_, kRestartService, L"Restart", 350, y, 100);
    CreateButton(window_, instance_, kUninstallService, L"Uninstall", 460, y, 100);
    y += 35;
    CreateButton(window_, instance_, kAuthorizeCollector, L"Authorize no-UAC", 20, y, 180);
    y += 35;
    CreateButton(window_, instance_, kQueryService, L"Query service status", 20, y, 180);
    y += 35;
    service_status_ = CreateLabel(window_, instance_, L"Service status: unknown", y);
    if (service_status_ != nullptr) {
        ::SetWindowPos(service_status_, nullptr, kControlX, y, kControlWidth, 22,
                       SWP_NOACTIVATE | SWP_NOZORDER);
    }

    const std::array<HWND, 18> controls{
        top_n_,          ranking_window_,     refresh_interval_, hover_delay_, dismiss_delay_,
        snap_distance_,  auto_hide_delay_,    reveal_strip_,     opacity_,     speed_unit_,
        collector_mode_, service_start_type_, log_level_,        show_upload_, show_download_,
        auto_hide_,      ui_at_login_,        service_status_};
    for (const auto control : controls) {
        static_cast<void>(
            ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE));
    }
    return true;
}

void SettingsWindow::UpdateServiceStatus(const config::AppConfig& value) noexcept {
    if (service_status_ == nullptr) {
        return;
    }
    try {
        const auto result =
            host_->ExecuteServiceAction(SettingsServiceAction::Query, value.service.start_type);
        const auto text = ServiceStatusText(result);
        ::SetWindowTextW(service_status_, text.c_str());
    } catch (...) {
        ::SetWindowTextW(service_status_, L"Service status: unavailable");
    }
}

void SettingsWindow::PopulateControls(const config::AppConfig& value) noexcept {
    try {
        const auto set_number = [](HWND control, auto number) {
            const auto text = std::to_wstring(number);
            ::SetWindowTextW(control, text.c_str());
        };
        set_number(top_n_, value.ranking.top_n);
        set_number(ranking_window_, value.ranking.window.count());
        set_number(refresh_interval_, value.display.refresh_interval.count());
        set_number(hover_delay_, value.interaction.hover_delay.count());
        set_number(dismiss_delay_, value.interaction.dismiss_delay.count());
        set_number(snap_distance_, value.floating.snap_distance_dip);
        set_number(auto_hide_delay_, value.floating.auto_hide_delay.count());
        set_number(reveal_strip_, value.floating.reveal_strip_dip);
        set_number(opacity_, value.floating.opacity_percent);
        static_cast<void>(::SendMessageW(speed_unit_, CB_SETCURSEL,
                                         static_cast<WPARAM>(value.display.speed_unit), 0));
        static_cast<void>(::SendMessageW(collector_mode_, CB_SETCURSEL,
                                         static_cast<WPARAM>(value.collector.preferred_mode), 0));
        static_cast<void>(::SendMessageW(service_start_type_, CB_SETCURSEL,
                                         static_cast<WPARAM>(value.service.start_type), 0));
        static_cast<void>(::SendMessageW(log_level_, CB_SETCURSEL,
                                         static_cast<WPARAM>(value.diagnostics.log_level), 0));
        SetChecked(show_upload_, value.display.show_upload);
        SetChecked(show_download_, value.display.show_download);
        SetChecked(auto_hide_, value.floating.auto_hide_enabled);
        SetChecked(ui_at_login_, value.startup.ui_at_login);
    } catch (...) {
        ShowError(L"Settings", L"The settings controls could not be populated.");
    }
}

bool SettingsWindow::ReadControls(config::AppConfig& value) const noexcept {
    std::uint32_t top_n{};
    std::uint32_t ranking_window{};
    std::uint32_t refresh_interval{};
    std::uint32_t hover_delay{};
    std::uint32_t dismiss_delay{};
    std::uint32_t snap_distance{};
    std::uint32_t auto_hide_delay{};
    std::uint32_t reveal_strip{};
    std::uint32_t opacity{};
    if (!ReadUnsigned(top_n_, config::kMinimumTopN, config::kMaximumTopN, top_n) ||
        !ReadUnsigned(
            ranking_window_, static_cast<std::uint32_t>(config::kMinimumRankingWindow.count()),
            static_cast<std::uint32_t>(config::kMaximumRankingWindow.count()), ranking_window) ||
        !ReadUnsigned(refresh_interval_,
                      static_cast<std::uint32_t>(config::kMinimumRefreshInterval.count()),
                      static_cast<std::uint32_t>(config::kMaximumRefreshInterval.count()),
                      refresh_interval) ||
        !ReadUnsigned(hover_delay_, static_cast<std::uint32_t>(config::kMinimumHoverDelay.count()),
                      static_cast<std::uint32_t>(config::kMaximumHoverDelay.count()),
                      hover_delay) ||
        !ReadUnsigned(
            dismiss_delay_, static_cast<std::uint32_t>(config::kMinimumDismissDelay.count()),
            static_cast<std::uint32_t>(config::kMaximumDismissDelay.count()), dismiss_delay) ||
        !ReadUnsigned(snap_distance_, static_cast<std::uint32_t>(config::kMinimumSnapDistanceDip),
                      static_cast<std::uint32_t>(config::kMaximumSnapDistanceDip), snap_distance) ||
        !ReadUnsigned(
            auto_hide_delay_, static_cast<std::uint32_t>(config::kMinimumAutoHideDelay.count()),
            static_cast<std::uint32_t>(config::kMaximumAutoHideDelay.count()), auto_hide_delay) ||
        !ReadUnsigned(reveal_strip_, static_cast<std::uint32_t>(config::kMinimumRevealStripDip),
                      static_cast<std::uint32_t>(config::kMaximumRevealStripDip), reveal_strip) ||
        !ReadUnsigned(opacity_, config::kMinimumOpacityPercent, config::kMaximumOpacityPercent,
                      opacity)) {
        return false;
    }
    const auto speed_unit = ::SendMessageW(speed_unit_, CB_GETCURSEL, 0, 0);
    const auto collector_mode = ::SendMessageW(collector_mode_, CB_GETCURSEL, 0, 0);
    const auto start_type = ::SendMessageW(service_start_type_, CB_GETCURSEL, 0, 0);
    const auto log_level = ::SendMessageW(log_level_, CB_GETCURSEL, 0, 0);
    if (speed_unit < 0 || speed_unit > 2 || collector_mode < 0 || collector_mode > 1 ||
        start_type < 0 || start_type > 2 || log_level < 0 || log_level > 3) {
        return false;
    }
    value.schema_version = config::kCurrentSchemaVersion;
    value.ranking.top_n = static_cast<std::uint8_t>(top_n);
    value.ranking.window = std::chrono::milliseconds{ranking_window};
    value.display.refresh_interval = std::chrono::milliseconds{refresh_interval};
    value.display.speed_unit = static_cast<config::SpeedUnit>(speed_unit);
    value.display.show_upload = IsChecked(show_upload_);
    value.display.show_download = IsChecked(show_download_);
    value.interaction.hover_delay = std::chrono::milliseconds{hover_delay};
    value.interaction.dismiss_delay = std::chrono::milliseconds{dismiss_delay};
    value.floating.snap_distance_dip = static_cast<std::int32_t>(snap_distance);
    value.floating.auto_hide_enabled = IsChecked(auto_hide_);
    value.floating.auto_hide_delay = std::chrono::milliseconds{auto_hide_delay};
    value.floating.reveal_strip_dip = static_cast<std::int32_t>(reveal_strip);
    value.floating.opacity_percent = static_cast<std::uint8_t>(opacity);
    value.startup.ui_at_login = IsChecked(ui_at_login_);
    value.collector.preferred_mode = static_cast<config::CollectorMode>(collector_mode);
    value.service.start_type = static_cast<config::ServiceStartType>(start_type);
    value.diagnostics.log_level = static_cast<config::LogLevel>(log_level);
    return true;
}

void SettingsWindow::HandleCommand(int control_id) noexcept {
    if (control_id == kCancel) {
        Close();
        return;
    }
    if (control_id == kSave) {
        config::AppConfig value = config::DefaultAppConfig();
        if (!ReadControls(value)) {
            ShowError(L"Invalid settings",
                      L"One or more values are missing or outside the allowed range.");
            return;
        }
        const auto result = host_->SaveSettings(value);
        if (result.saved) {
            Close();
        } else {
            ShowSaveError(result);
        }
        return;
    }
    if (control_id == kAuthorizeCollector) {
        HandleAuthorizeCollector();
        return;
    }
    HandleServiceCommand(control_id);
}

void SettingsWindow::HandleServiceCommand(int control_id) noexcept {
    SettingsServiceAction action{};
    switch (control_id) {
    case kInstallService:
        action = SettingsServiceAction::Install;
        break;
    case kStartService:
        action = SettingsServiceAction::Start;
        break;
    case kStopService:
        action = SettingsServiceAction::Stop;
        break;
    case kRestartService:
        action = SettingsServiceAction::Restart;
        break;
    case kUninstallService:
        action = SettingsServiceAction::Uninstall;
        break;
    case kQueryService:
        action = SettingsServiceAction::Query;
        break;
    default:
        return;
    }
    config::AppConfig value = config::DefaultAppConfig();
    if (!ReadControls(value)) {
        ShowError(L"Invalid settings", L"Correct the settings values before managing the service.");
        return;
    }
    const auto result = host_->ExecuteServiceAction(action, value.service.start_type);
    ShowServiceResult(result);
    UpdateServiceStatus(value);
}

void SettingsWindow::HandleAuthorizeCollector() noexcept {
    const auto result = host_->ExecuteServiceAction(SettingsServiceAction::AuthorizeCollector,
                                                    config::ServiceStartType::AutomaticDelayed);
    if (result.ok()) {
        static_cast<void>(::MessageBoxW(
            window_, L"Authorization succeeded. The collector will now start without a UAC prompt.",
            L"Network Flow Monitor", MB_OK | MB_ICONINFORMATION));
        return;
    }
    const auto message =
        L"Authorization failed. Win32 error: " + std::to_wstring(result.win32_error);
    static_cast<void>(
        ::MessageBoxW(window_, message.c_str(), L"Network Flow Monitor", MB_OK | MB_ICONERROR));
}

void SettingsWindow::ShowError(const wchar_t* title, const wchar_t* message) const noexcept {
    static_cast<void>(::MessageBoxW(window_, message, title, MB_OK | MB_ICONERROR));
}

void SettingsWindow::ShowSaveError(const SettingsSaveResult& result) const noexcept {
    if (!result.error.has_value()) {
        ShowError(L"Settings", L"The configuration could not be saved.");
        return;
    }
    try {
        const auto field =
            std::wstring{result.error->field_path.begin(), result.error->field_path.end()};
        const auto message = L"Could not save settings. Field: " + field + L"\nWin32 error: " +
                             std::to_wstring(result.error->system_error);
        static_cast<void>(
            ::MessageBoxW(window_, message.c_str(), L"Settings", MB_OK | MB_ICONERROR));
    } catch (...) {
        ShowError(L"Settings", L"The configuration could not be saved.");
    }
}

void SettingsWindow::ShowServiceResult(const SettingsActionResult& result) const noexcept {
    try {
        if (result.ok()) {
            const auto message = L"Service operation succeeded. Current state: " +
                                 std::wstring{ServiceStateText(result.state)};
            static_cast<void>(
                ::MessageBoxW(window_, message.c_str(), L"Service", MB_OK | MB_ICONINFORMATION));
            return;
        }
        const auto message =
            L"Service operation failed. Win32 error: " + std::to_wstring(result.win32_error);
        static_cast<void>(
            ::MessageBoxW(window_, message.c_str(), L"Service", MB_OK | MB_ICONERROR));
    } catch (...) {
        ShowError(L"Service", L"The service operation failed.");
    }
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam) noexcept {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 stores the window object in LONG_PTR.
    auto* settings = reinterpret_cast<SettingsWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 LPARAM carries CREATESTRUCTW here.
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        settings = static_cast<SettingsWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(settings));
        return TRUE;
    }
    if (settings == nullptr) {
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
        settings->HandleCommand(LOWORD(wparam));
        return 0;
    }
    if (message == WM_CLOSE) {
        settings->Close();
        return 0;
    }
    if (message == WM_NCDESTROY) {
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace nfm::ui
