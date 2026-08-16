#include "network_flow_monitor/ui/ui_controller.h"

#include "network_flow_monitor/app/collector_runtime.h"
#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/ui/appearance.h"
#include "network_flow_monitor/ui/visual_tokens.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <dwmapi.h>
#include <filesystem>
#include <limits>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <string>
#include <string_view>
#include <winreg.h>

namespace nfm::ui {
namespace {

constexpr auto kUiMessageWindowClass = std::to_array(L"NetworkFlowMonitor.UiMessageWindow");
constexpr UINT kTopmostPolicyPollMilliseconds = 250U;

[[nodiscard]] std::filesystem::path DefaultSettingsPath() {
    std::wstring executable(32'768U, L'\0');
    const auto length =
        ::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0U || length >= executable.size()) {
        return {};
    }
    executable.resize(length);
    return config::PortableConfigPath(executable);
}

[[nodiscard]] int Win32Result(std::uint32_t error) noexcept {
    return error == ERROR_SUCCESS ? 0 : static_cast<int>(error);
}

[[nodiscard]] bool ServiceStopSatisfied(const SettingsActionResult& result) noexcept {
    return result.ok() || result.win32_error == ERROR_SERVICE_DOES_NOT_EXIST ||
           result.win32_error == ERROR_SERVICE_NOT_ACTIVE;
}

[[nodiscard]] DockSide UiDockSide(config::DockSide side) noexcept {
    return side == config::DockSide::Left ? DockSide::Left : DockSide::Right;
}

[[nodiscard]] config::DockSide ConfigDockSide(DockSide side) noexcept {
    return side == DockSide::Left ? config::DockSide::Left : config::DockSide::Right;
}

struct MonitorEnumerationContext {
    MonitorPanelEnvironment* environment;
};

[[nodiscard]] std::string Utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto required =
        ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                              static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    static_cast<void>(::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                            static_cast<int>(text.size()), result.data(), required,
                                            nullptr, nullptr));
    return result;
}

[[nodiscard]] std::string StableMonitorIdentifier(const MONITORINFOEXW& information) {
    DISPLAY_DEVICEW device{.cb = sizeof(DISPLAY_DEVICEW)};
    if (::EnumDisplayDevicesW(information.szDevice, 0U, &device, EDD_GET_DEVICE_INTERFACE_NAME) !=
        FALSE) {
        auto identifier = Utf8(device.DeviceID);
        if (!identifier.empty()) {
            return identifier;
        }
    }
    return Utf8(information.szDevice);
}

[[nodiscard]] bool HasAutoHideBar(const RECT& monitor, UINT edge) noexcept {
    APPBARDATA data{.cbSize = sizeof(APPBARDATA), .uEdge = edge, .rc = monitor};
    return ::SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &data) != 0U;
}

BOOL CALLBACK AddMonitor(HMONITOR monitor, [[maybe_unused]] HDC device_context,
                         [[maybe_unused]] LPRECT clip, LPARAM data) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 callback transports caller context here.
    auto* const context = reinterpret_cast<MonitorEnumerationContext*>(data);
    MONITORINFOEXW information{};
    information.cbSize = sizeof(information);
    if (context == nullptr || ::GetMonitorInfoW(monitor, &information) == FALSE) {
        return TRUE;
    }
    auto identifier = StableMonitorIdentifier(information);
    UINT dpi_x = 96U;
    UINT dpi_y = 96U;
    if (::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y) != S_OK) {
        dpi_x = 96U;
    }
    const auto work = information.rcWork;
    context->environment->monitors.push_back(
        {.id = std::move(identifier),
         .rect = {.left = work.left, .top = work.top, .right = work.right, .bottom = work.bottom},
         .dpi = dpi_x,
         .primary = (information.dwFlags & MONITORINFOF_PRIMARY) != 0U,
         .auto_hide_reveal_on_left = HasAutoHideBar(information.rcMonitor, ABE_LEFT),
         .auto_hide_reveal_on_right = HasAutoHideBar(information.rcMonitor, ABE_RIGHT)});
    return TRUE;
}

[[nodiscard]] WindowDisplayOptions WindowOptionsFromConfig(
    const config::AppConfig& config) noexcept {
    return {.speed_unit = config.display.speed_unit,
            .show_upload = config.display.show_upload,
            .show_download = config.display.show_download,
            .opacity_percent = config.floating.opacity_percent,
            .blur_strength_percent = config.floating.blur_strength_percent,
            .top_n = config.ranking.top_n};
}

constexpr auto kPersonalizeThemeKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr int kLocaleNameLength = 85;

[[nodiscard]] bool SystemUsesLightTheme() noexcept {
    HKEY key = nullptr;
    DWORD value = 1U;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeThemeKey, 0U, KEY_QUERY_VALUE, &key) ==
        ERROR_SUCCESS) {
        DWORD size = sizeof(value);
        DWORD type = 0U;
        if (::RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                               reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS ||
            type != REG_DWORD) {
            value = 1U;
        }
        static_cast<void>(::RegCloseKey(key));
    }
    return value != 0U;
}

[[nodiscard]] bool SystemHighContrast() noexcept {
    HIGHCONTRASTW information{.cbSize = sizeof(HIGHCONTRASTW)};
    return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(information), &information, 0) !=
               FALSE &&
           (information.dwFlags & HCF_HIGHCONTRASTON) != 0U;
}

[[nodiscard]] bool SystemReducedMotion() noexcept {
    BOOL animations = TRUE;
    return ::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) == FALSE ||
           animations == FALSE;
}

[[nodiscard]] std::string SystemLanguageTag() {
    std::array<wchar_t, kLocaleNameLength> buffer{};
    if (::GetUserDefaultLocaleName(buffer.data(), static_cast<int>(buffer.size())) == 0) {
        return "en-US";
    }
    return Utf8(buffer.data());
}

} // namespace

UiController::UiController(HINSTANCE instance, const config::AppConfig& config)
    : instance_{instance}, config_{config}, settings_store_{}, service_action_api_{},
      startup_store_{},
      settings_controller_{DefaultSettingsPath(), config::ConfigDocumentKind::User, settings_store_,
                           service_action_api_},
      startup_registration_{startup_store_}, settings_window_{instance_, *this},
      collector_process_{shell_api_}, floating_window_{instance_, render_resources_, *this},
      tray_icon_{}, tray_controller_{nullptr,
                                     TrayIconOptions{.callback_message = kTrayCallbackMessage,
                                                     .icon_id = 1U,
                                                     .icon = nullptr,
                                                     .tooltip = L"Network Flow Monitor"},
                                     tray_api_, *this},
      snapshot_generation_{0U}, memory_error_{false} {
    tray_icon_ = CreateNetworkFlowMonitorIcon(instance_);
    if (tray_icon_ == nullptr) {
        tray_icon_ = ::LoadIconW(nullptr, IDI_APPLICATION);
        tray_icon_owned_ = false;
    } else {
        tray_icon_owned_ = true;
    }
    tray_controller_.SetIcon(tray_icon_);
    static_cast<void>(RecreateRankingEngine());
    floating_window_.SetDisplayOptions(WindowOptionsFromConfig(config_));
}

UiController::~UiController() {
    Shutdown();
    if (tray_icon_ != nullptr && tray_icon_owned_) {
        static_cast<void>(::DestroyIcon(tray_icon_));
    }
    tray_icon_ = nullptr;
    tray_icon_owned_ = false;
}

bool UiController::Start() noexcept {
    if (started_) {
        return true;
    }
    if (ranking_engine_ == nullptr || !render_resources_.Initialize()) {
        return false;
    }
    ApplyAppearance();

    const auto collection_started = StartCollection();
    static_cast<void>(tray_controller_.Initialize());
    if (!floating_window_.Create()) {
        tray_controller_.Shutdown();
        StopCollection();
        return false;
    }
    try {
        HandleMonitorPanelEvent(
            {.kind = MonitorPanelEventKind::Start,
             .anchor = PersistedMonitorAnchor{
                 .monitor_id = std::string{config_.floating.anchor_monitor_id.data()},
                 .side = UiDockSide(config_.floating.anchor_side),
                 .vertical_offset_dip = config_.floating.anchor_vertical_offset_dip}});
    } catch (...) {
        tray_controller_.Shutdown();
        StopCollection();
        return false;
    }
    if (!collection_started) {
        MarkCollectionDisconnected();
    }
    ApplyTopmostPolicy();
    if (message_window_ != nullptr) {
        static_cast<void>(::SetTimer(message_window_, kUiTopmostPolicyTimerId,
                                     kTopmostPolicyPollMilliseconds, nullptr));
    }
    started_ = true;
    return true;
}

int UiController::Run(bool smoke_test) noexcept {
    if (!CreateMessageWindow()) {
        const auto error = ::GetLastError();
        return Win32Result(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error);
    }
    AttachMessageWindow(message_window_);
    if (!Start()) {
        Shutdown();
        DestroyMessageWindow();
        return ERROR_FUNCTION_FAILED;
    }
    if (smoke_test) {
        ::PostQuitMessage(0);
    }

    int result = 0;
    MSG message{};
    while (true) {
        const auto status = ::GetMessageW(&message, nullptr, 0U, 0U);
        if (status == -1) {
            result = Win32Result(::GetLastError());
            break;
        }
        if (status == 0) {
            break;
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    Shutdown();
    DestroyMessageWindow();
    return result;
}

void UiController::Shutdown() noexcept {
    if (message_window_ != nullptr) {
        static_cast<void>(::KillTimer(message_window_, kUiMonitorPanelTimerId));
        static_cast<void>(::KillTimer(message_window_, kUiTopmostPolicyTimerId));
    }
    floating_window_.Destroy();
    tray_controller_.Shutdown();
    StopCollection();
    started_ = false;
}

void UiController::AttachMessageWindow(HWND window) noexcept {
    message_window_ = window;
    tray_controller_.SetOwner(window);
}

void UiController::ApplyInteractionState() noexcept {
    floating_window_.SetInteractionState(overlay_state_.load(std::memory_order_acquire));
}

void UiController::ApplyAppearance() noexcept {
    try {
        const auto system_light = SystemUsesLightTheme();
        system_high_contrast_ = SystemHighContrast();
        reduced_motion_ = SystemReducedMotion();
        const auto language = ResolveLanguage(config_.appearance.language, SystemLanguageTag());
        const auto theme = ResolveTheme(config_.appearance.theme, system_light);
        const auto tokens = ResolveVisualTokens(theme, system_high_contrast_);
        floating_window_.SetAppearance(tokens, language);
        tray_controller_.SetLanguage(language);
    } catch (...) {
        memory_error_.store(true, std::memory_order_release);
    }
}

ForegroundWindowState UiController::QueryForegroundWindowState() const noexcept {
    ForegroundWindowState state;
    auto* const foreground = ::GetForegroundWindow();
    if (foreground == nullptr) {
        return state;
    }
    state.present = true;
    state.visible = ::IsWindowVisible(foreground) != FALSE;
    state.minimized = ::IsIconic(foreground) != FALSE;
    state.shell_window = foreground == ::GetShellWindow();
    DWORD process_id{};
    static_cast<void>(::GetWindowThreadProcessId(foreground, &process_id));
    state.belongs_to_current_process = process_id == ::GetCurrentProcessId();

    RECT bounds{};
    if (FAILED(::DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds,
                                       sizeof(bounds)))) {
        static_cast<void>(::GetWindowRect(foreground, &bounds));
    }
    state.bounds = {
        .left = bounds.left, .top = bounds.top, .right = bounds.right, .bottom = bounds.bottom};
    auto* const monitor = ::MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    MONITORINFO information{.cbSize = sizeof(information)};
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &information) != FALSE) {
        state.monitor_bounds = {.left = information.rcMonitor.left,
                                .top = information.rcMonitor.top,
                                .right = information.rcMonitor.right,
                                .bottom = information.rcMonitor.bottom};
    }
    return state;
}

void UiController::ApplyTopmostPolicy() noexcept {
    auto* const window = floating_window_.hwnd();
    if (window == nullptr) {
        return;
    }
    const auto topmost = ShouldStayTopmost(config_.floating.yield_topmost_when_fullscreen,
                                           QueryForegroundWindowState());
    const auto currently_topmost = (::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (topmost == currently_topmost) {
        return;
    }
    static_cast<void>(::SetWindowPos(window, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
}

void UiController::OnSystemAppearanceChanged() noexcept {
    ApplyAppearance();
    HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::ThemeChanged});
}

void UiController::HandleStateOnUiThread(ipc::IpcClientState state) noexcept {
    const auto connected = state == ipc::IpcClientState::Streaming;
    overlay_state_.store(connected ? InteractionState::Visible
                                   : InteractionState::DisconnectedOverlay,
                         std::memory_order_release);
    if (!connected) {
        ranking_instance_.reset();
        ranking_snapshot_.reset();
        const auto generation = snapshot_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
        floating_window_.SetSnapshot(nullptr, generation);
    }
    HandleMonitorPanelEvent({.kind = state == ipc::IpcClientState::Streaming
                                         ? MonitorPanelEventKind::CollectorConnected
                                         : MonitorPanelEventKind::CollectorDisconnected});
    ApplyInteractionState();
}

PixelRect UiController::CurrentWorkArea() const noexcept {
    auto* const monitor = ::MonitorFromWindow(floating_window_.hwnd(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{.cbSize = sizeof(MONITORINFO)};
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        return PixelRect{.left = monitor_info.rcWork.left,
                         .top = monitor_info.rcWork.top,
                         .right = monitor_info.rcWork.right,
                         .bottom = monitor_info.rcWork.bottom};
    }
    return PixelRect{.left = 0,
                     .top = 0,
                     .right = ::GetSystemMetrics(SM_CXSCREEN),
                     .bottom = ::GetSystemMetrics(SM_CYSCREEN)};
}

bool UiController::CreateMessageWindow() noexcept {
    if (message_window_ != nullptr) {
        return true;
    }
    if (instance_ == nullptr) {
        instance_ = ::GetModuleHandleW(nullptr);
    }

    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = 0,
                                   .lpfnWndProc = &UiController::MessageWindowProc,
                                   .cbClsExtra = 0,
                                   .cbWndExtra = 0,
                                   .hInstance = instance_,
                                   .hIcon = nullptr,
                                   .hCursor = nullptr,
                                   .hbrBackground = nullptr,
                                   .lpszMenuName = nullptr,
                                   .lpszClassName = kUiMessageWindowClass.data(),
                                   .hIconSm = nullptr};
    if (::RegisterClassExW(&window_class) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    message_window_ = ::CreateWindowExW(0U, kUiMessageWindowClass.data(), L"Network Flow Monitor",
                                        0U, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, this);
    return message_window_ != nullptr;
}

void UiController::DestroyMessageWindow() noexcept {
    if (message_window_ != nullptr) {
        auto* const window = message_window_;
        message_window_ = nullptr;
        ::DestroyWindow(window);
    }
}

std::optional<ipc::ClientSnapshot> UiController::TakePendingSnapshot() noexcept {
    const std::scoped_lock lock{pending_mutex_};
    pending_message_posted_ = false;
    if (!pending_snapshot_.has_value()) {
        return std::nullopt;
    }
    auto snapshot = std::move(pending_snapshot_);
    pending_snapshot_.reset();
    return snapshot;
}

bool UiController::ProcessPendingSnapshot() noexcept {
    auto pending = TakePendingSnapshot();
    if (!pending.has_value() || ranking_engine_ == nullptr) {
        return false;
    }

    try {
        if (!ranking_instance_.has_value() || *ranking_instance_ != pending->instance) {
            ranking_instance_ = pending->instance;
            if (!RecreateRankingEngine()) {
                return false;
            }
            ranking_snapshot_.reset();
        }
        const auto result = ranking_engine_->PushSnapshot(pending->counters);
        if (!result.accepted()) {
            return false;
        }
        ranking_snapshot_ = std::make_shared<ranking::RankingSnapshot>(result.snapshot);
        const auto ranked_count = ranking_snapshot_->applications.size();
        auto presentation = MonitorPanelEventKind::RankingReadyRows;
        auto visible_rows = static_cast<std::uint8_t>(std::min<std::size_t>(ranked_count, 255U));
        if (ranking_snapshot_->status == ranking::RankingStatus::WarmingUp) {
            presentation = MonitorPanelEventKind::RankingWarming;
            visible_rows = 0;
        } else if (ranked_count == 0U) {
            presentation = MonitorPanelEventKind::RankingReadyIdle;
            visible_rows = 0;
        }
        HandleMonitorPanelEvent({.kind = presentation, .visible_rows = visible_rows});
        const auto generation = snapshot_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
        floating_window_.SetSnapshot(ranking_snapshot_, generation);
        return true;
    } catch (...) {
        memory_error_.store(true, std::memory_order_release);
        return false;
    }
}

bool UiController::UpdateRankingOptions(std::uint8_t top_n,
                                        std::chrono::milliseconds window) noexcept {
    if (ranking_engine_ == nullptr) {
        return false;
    }
    const auto previous = config_.ranking;
    config_.ranking.top_n = top_n;
    config_.ranking.window = window;
    const auto error = ranking_engine_->UpdateOptions({.window = window, .top_n = top_n});
    if (error != ranking::RankingError::None) {
        config_.ranking = previous;
        return false;
    }
    return true;
}

void UiController::StateChanged(ipc::IpcClientState state) noexcept {
    ipc_state_.store(state, std::memory_order_release);
    const auto disconnected = state != ipc::IpcClientState::Streaming;
    overlay_state_.store(disconnected ? InteractionState::DisconnectedOverlay
                                      : InteractionState::Visible,
                         std::memory_order_release);
    if (message_window_ != nullptr) {
        static_cast<void>(
            ::PostMessageW(message_window_, kUiStateMessage, static_cast<WPARAM>(state), 0));
    }
}

void UiController::SnapshotReceived(ipc::ClientSnapshot snapshot) noexcept {
    try {
        {
            const std::scoped_lock lock{pending_mutex_};
            pending_snapshot_ = std::move(snapshot);
        }
        PostSnapshotMessage();
    } catch (...) {
        memory_error_.store(true, std::memory_order_release);
    }
}

void UiController::OnTrayLeftClick() noexcept {
    HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::ToggleVisibility});
}

void UiController::OnTrayRightClick() noexcept {
    static_cast<void>(tray_controller_.ShowContextMenu());
}

void UiController::OnTrayHover() noexcept {
    // Tray hover is informational and never changes monitor-panel visibility.
}

void UiController::OnTraySettings() noexcept {
    const auto loaded = settings_controller_.Load();
    if (loaded.status == config::ConfigLoadStatus::Loaded ||
        loaded.status == config::ConfigLoadStatus::LoadedWithDefaults ||
        loaded.status == config::ConfigLoadStatus::Missing) {
        if (!UpdateRankingOptions(loaded.config.ranking.top_n, loaded.config.ranking.window)) {
            static_cast<void>(::MessageBoxW(message_window_,
                                            L"The saved ranking settings could not be applied.",
                                            L"Network Flow Monitor", MB_OK | MB_ICONERROR));
            return;
        }
        config_ = loaded.config;
        floating_window_.SetDisplayOptions(WindowOptionsFromConfig(config_));
        HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::ConfigurationChanged});
    } else if (loaded.error.has_value()) {
        static_cast<void>(::MessageBoxW(
            message_window_,
            L"The configuration file is damaged or incompatible; safe defaults are in use.",
            L"Network Flow Monitor", MB_OK | MB_ICONWARNING));
    }
    if (!settings_window_.Show(message_window_, config_)) {
        static_cast<void>(::MessageBoxW(message_window_,
                                        L"The settings window could not be opened.",
                                        L"Network Flow Monitor", MB_OK | MB_ICONERROR));
    }
}

void UiController::OnTrayRetryCollector() noexcept {
    if (config_.collector.preferred_mode != config::CollectorMode::Portable) {
        const auto result =
            ExecuteServiceAction(SettingsServiceAction::Start, config_.service.start_type);
        if (!result.ok()) {
            static_cast<void>(::MessageBoxW(message_window_,
                                            L"The Windows service could not be started. Open "
                                            L"Settings to see its status and error code.",
                                            L"Network Flow Monitor", MB_OK | MB_ICONWARNING));
        }
        return;
    }
    StopCollection();
    if (!StartCollection()) {
        MarkCollectionDisconnected();
        static_cast<void>(::MessageBoxW(
            message_window_,
            L"The collector could not be started. Check UAC and the ETW session, then try again.",
            L"Network Flow Monitor", MB_OK | MB_ICONWARNING));
    }
}

void UiController::OnTrayPinToggle() noexcept {
    HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::PinTogglePressed});
}

void UiController::OnTrayExit() noexcept {
    ::PostQuitMessage(0);
}

void UiController::OnWindowContextMenu() noexcept {
    HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::ContextMenuOpened});
    // Suspend the topmost-policy timer while the context menu is shown. TrackPopupMenu
    // runs a modal message loop that dispatches WM_TIMER, so the 250 ms policy poll
    // would re-raise the panel to TOPMOST and cover the menu.
    if (message_window_ != nullptr) {
        static_cast<void>(::KillTimer(message_window_, kUiTopmostPolicyTimerId));
    }
    const auto lower = [](HWND window) noexcept {
        if (window != nullptr) {
            static_cast<void>(::SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        }
    };
    lower(floating_window_.hwnd());
    lower(floating_window_.hit_test_hwnd());
    static_cast<void>(tray_controller_.ShowContextMenu(message_window_));
    if (message_window_ != nullptr) {
        static_cast<void>(::SetTimer(message_window_, kUiTopmostPolicyTimerId,
                                     kTopmostPolicyPollMilliseconds, nullptr));
    }
    ApplyTopmostPolicy();
    POINT cursor{};
    static_cast<void>(::GetCursorPos(&cursor));
    HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::ContextMenuClosed,
                             .point = {.x = cursor.x, .y = cursor.y}});
}

void UiController::OnWindowMoved() noexcept {}

void UiController::OnMonitorPanelEvent(MonitorPanelEventKind event, PixelPoint point) noexcept {
    HandleMonitorPanelEvent({.kind = event, .point = point});
}

MonitorPanelEnvironment UiController::BuildMonitorEnvironment() const {
    MonitorPanelEnvironment environment;
    MonitorEnumerationContext context{.environment = &environment};
    static_cast<void>(::EnumDisplayMonitors(
        nullptr, nullptr, AddMonitor,
        reinterpret_cast<LPARAM>(
            &context))); // NOLINT(performance-no-int-to-ptr): Win32 callback context.
    if (environment.monitors.empty()) {
        const auto work = CurrentWorkArea();
        environment.monitors.push_back(
            {.id = "primary", .rect = work, .dpi = ::GetDpiForSystem(), .primary = true});
    }
    return environment;
}

MonitorPanelConfig UiController::MonitorPanelConfigFromAppConfig() const noexcept {
    return {.hover_delay = config_.interaction.hover_delay,
            .capsule_width_dip = config_.floating.capsule_width_dip,
            .capsule_height_dip = config_.floating.capsule_height_dip,
            .minimum_drag_distance_x_pixels = ::GetSystemMetrics(SM_CXDRAG),
            .minimum_drag_distance_y_pixels = ::GetSystemMetrics(SM_CYDRAG),
            .auto_hide_delay = config_.floating.auto_hide_delay,
            .auto_hide_enabled = config_.floating.auto_hide_enabled,
            .pinned = config_.floating.nfm_pinned,
            .top_n = config_.ranking.top_n,
            .reduced_motion = reduced_motion_};
}

void UiController::HandleMonitorPanelEvent(const MonitorPanelEvent& event) noexcept {
    try {
        ApplyMonitorPanelTransition(
            MonitorPanelBehavior::Transition(monitor_panel_state_, event, BuildMonitorEnvironment(),
                                             MonitorPanelConfigFromAppConfig()));
    } catch (...) {
        memory_error_.store(true, std::memory_order_release);
    }
}

void UiController::ApplyMonitorPanelTransition(MonitorPanelTransition transition) {
    const auto previous_deadline = monitor_panel_state_.deadline;
    monitor_panel_state_ = std::move(transition.state);
    monitor_panel_hit_bounds_ = transition.hit_bounds;
    monitor_panel_viewport_ = transition.viewport;
    if (monitor_panel_state_.surface == MonitorPanelSurface::Hidden) {
        floating_window_.Hide();
        ScheduleMonitorPanelTimer(std::nullopt);
        return;
    }
    CapsulePresentation capsule{};
    if (ranking_snapshot_ != nullptr) {
        const auto& rate = ranking_snapshot_->total_rate;
        const auto bounded = [](double value) noexcept {
            return value <= 0.0 ? 0ULL
                                : static_cast<std::uint64_t>(
                                      std::min(value, static_cast<double>(UINT64_MAX)));
        };
        capsule = MonitorPanelBehavior::MapCapsuleRates(bounded(rate.download_bytes_per_second),
                                                        bounded(rate.upload_bytes_per_second));
    }
    floating_window_.SetMonitorPanelPresentation(monitor_panel_state_.surface, capsule,
                                                 transition.target_bounds, transition.hit_bounds,
                                                 transition.viewport);
    tray_controller_.SetPinState(monitor_panel_state_.pin_preference,
                                 config_.floating.auto_hide_enabled);
    floating_window_.Reposition(transition.target_bounds);
    floating_window_.Show();
    if (monitor_panel_state_.deadline != previous_deadline) {
        ScheduleMonitorPanelTimer(transition.next_deadline);
    }
    for (const auto& effect : transition.effects) {
        if (effect.kind == MonitorPanelEffectKind::PersistAnchor) {
            const auto saved = PersistMonitorAnchor(effect.anchor);
            auto feedback = MonitorPanelBehavior::Transition(
                monitor_panel_state_,
                {.kind = saved ? MonitorPanelEventKind::PersistSucceeded
                               : MonitorPanelEventKind::PersistFailed},
                BuildMonitorEnvironment(), MonitorPanelConfigFromAppConfig());
            monitor_panel_state_ = std::move(feedback.state);
            if (!feedback.effects.empty()) {
                static_cast<void>(::MessageBoxW(
                    message_window_,
                    L"The monitor position could not be saved. It will remain here for this "
                    L"session.",
                    L"Network Flow Monitor", MB_OK | MB_ICONWARNING));
            }
        } else if (effect.kind == MonitorPanelEffectKind::PersistPin) {
            const auto saved = PersistPinPreference(effect.pinned);
            auto feedback = MonitorPanelBehavior::Transition(
                monitor_panel_state_,
                {.kind = saved ? MonitorPanelEventKind::PersistSucceeded
                               : MonitorPanelEventKind::PersistFailed},
                BuildMonitorEnvironment(), MonitorPanelConfigFromAppConfig());
            monitor_panel_state_ = std::move(feedback.state);
            if (!saved) {
                static_cast<void>(::MessageBoxW(
                    message_window_,
                    L"The pin preference could not be saved. It will remain active for this "
                    L"session.",
                    L"Network Flow Monitor", MB_OK | MB_ICONWARNING));
            }
        } else if (effect.kind == MonitorPanelEffectKind::ReportPersistenceFailure) {
            static_cast<void>(::MessageBoxW(
                message_window_,
                L"The monitor position could not be saved. It will remain here for this session.",
                L"Network Flow Monitor", MB_OK | MB_ICONWARNING));
        }
    }
}

void UiController::ScheduleMonitorPanelTimer(
    std::optional<std::chrono::milliseconds> deadline) noexcept {
    if (message_window_ == nullptr) {
        return;
    }
    static_cast<void>(::KillTimer(message_window_, kUiMonitorPanelTimerId));
    if (deadline.has_value()) {
        const auto delay = static_cast<UINT>(std::clamp<std::int64_t>(deadline->count(), 1, 60000));
        static_cast<void>(::SetTimer(message_window_, kUiMonitorPanelTimerId, delay, nullptr));
    }
}

bool UiController::PersistMonitorAnchor(const PersistedMonitorAnchor& anchor) noexcept {
    config_.floating.anchor_monitor_id.fill('\0');
    const auto length = std::min(anchor.monitor_id.size(), config::kMaximumMonitorIdentifierBytes);
    std::copy_n(anchor.monitor_id.begin(), length, config_.floating.anchor_monitor_id.begin());
    config_.floating.anchor_side = ConfigDockSide(anchor.side);
    config_.floating.anchor_vertical_offset_dip = anchor.vertical_offset_dip;
    const auto saved = settings_controller_.Save(config_);
    return saved.saved;
}

bool UiController::PersistPinPreference(bool pinned) noexcept {
    config_.floating.nfm_pinned = pinned;
    return settings_controller_.Save(config_).saved;
}

void UiController::OnWindowInvalidated() noexcept {}

bool UiController::StartCollection() noexcept {
    const auto endpoint = config_.collector.preferred_mode == config::CollectorMode::Portable
                              ? ipc::CollectorEndpointMode::Portable
                              : ipc::CollectorEndpointMode::Service;
    const auto principal = app::BuildCollectorPrincipal(endpoint);
    if (!principal.ok()) {
        return false;
    }
    if (endpoint == ipc::CollectorEndpointMode::Portable) {
        const auto collector = collector_process_.StartPortable({});
        if (!collector.succeeded) {
            ipc_state_.store(ipc::IpcClientState::Failed, std::memory_order_release);
            return false;
        }
    }

    try {
        ipc_client_ = std::make_unique<ipc::IpcClient>(principal.principal, transport_, *this,
                                                       config_.display.refresh_interval);
    } catch (...) {
        static_cast<void>(collector_process_.Stop());
        memory_error_.store(true, std::memory_order_release);
        return false;
    }
    if (!ipc_client_->Start()) {
        ipc_client_.reset();
        static_cast<void>(collector_process_.Stop());
        return false;
    }
    return true;
}

void UiController::MarkCollectionDisconnected() noexcept {
    ipc_state_.store(ipc::IpcClientState::Failed, std::memory_order_release);
    overlay_state_.store(InteractionState::DisconnectedOverlay, std::memory_order_release);
    ranking_instance_.reset();
    ranking_snapshot_.reset();
    const auto generation = snapshot_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    floating_window_.SetSnapshot(nullptr, generation);
    HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::CollectorDisconnected});
    ApplyInteractionState();
}

void UiController::StopCollection() noexcept {
    if (ipc_client_ != nullptr) {
        ipc_client_->Stop();
        ipc_client_.reset();
    }
    static_cast<void>(collector_process_.Stop());
}

void UiController::RestoreCollectionState(const config::AppConfig& previous, bool reconnect,
                                          bool service_stopped) noexcept {
    if (reconnect) {
        StopCollection();
    }
    config_ = previous;
    static_cast<void>(UpdateRankingOptions(previous.ranking.top_n, previous.ranking.window));
    if (reconnect) {
        if (service_stopped) {
            static_cast<void>(
                ExecuteServiceAction(SettingsServiceAction::Start, previous.service.start_type));
        }
        static_cast<void>(StartCollection());
    }
}

// NOLINTNEXTLINE(bugprone-exception-escape): settings persistence and rollback are caught below.
SettingsSaveResult UiController::SaveSettings(const config::AppConfig& value) noexcept {
    const auto previous = config_;
    if (!UpdateRankingOptions(value.ranking.top_n, value.ranking.window)) {
        return {.saved = false,
                .error = config::ConfigError{.code = config::ConfigErrorCode::OutOfRange,
                                             .field_path = "ranking",
                                             .detail =
                                                 "the ranking engine rejected the selected options",
                                             .system_error = ERROR_INVALID_DATA}};
    }
    const auto reconnect =
        started_ && (value.collector.preferred_mode != previous.collector.preferred_mode ||
                     value.display.refresh_interval != previous.display.refresh_interval);
    const auto leaving_service =
        started_ && previous.collector.preferred_mode == config::CollectorMode::Service &&
        value.collector.preferred_mode == config::CollectorMode::Portable;
    bool service_stopped{};
    config_ = value;
    floating_window_.SetDisplayOptions(WindowOptionsFromConfig(config_));
    ApplyAppearance();
    ApplyTopmostPolicy();
    if (reconnect) {
        StopCollection();
        if (!StartCollection()) {
            RestoreCollectionState(previous, true, false);
            return {.saved = false,
                    .error = config::ConfigError{
                        .code = config::ConfigErrorCode::WriteFailed,
                        .field_path = "collector.preferred_mode",
                        .detail = "the collector could not be restarted with the new settings",
                        .system_error = ERROR_FUNCTION_FAILED}};
        }
    }
    if (leaving_service) {
        const auto stopped =
            ExecuteServiceAction(SettingsServiceAction::Stop, value.service.start_type);
        if (!ServiceStopSatisfied(stopped)) {
            RestoreCollectionState(previous, reconnect, false);
            return {
                .saved = false,
                .error = config::ConfigError{
                    .code = config::ConfigErrorCode::WriteFailed,
                    .field_path = "collector.preferred_mode",
                    .detail = "the service could not be stopped while switching to portable mode",
                    .system_error = stopped.win32_error}};
        }
        service_stopped = stopped.ok();
    }
    const auto startup_changed = value.startup.ui_at_login != previous.startup.ui_at_login;
    if (startup_changed) {
        const auto startup_result = startup_registration_.Apply(value.startup.ui_at_login);
        if (!startup_result.ok()) {
            RestoreCollectionState(previous, reconnect, service_stopped);
            return {.saved = false,
                    .error = config::ConfigError{
                        .code = config::ConfigErrorCode::WriteFailed,
                        .field_path = "startup.ui_at_login",
                        .detail = "the Windows login startup entry could not be updated",
                        .system_error = startup_result.win32_error}};
        }
    }
    const auto result = settings_controller_.Save(value);
    if (!result.saved) {
        if (startup_changed) {
            static_cast<void>(startup_registration_.Apply(previous.startup.ui_at_login));
        }
        RestoreCollectionState(previous, reconnect, service_stopped);
        return result;
    }
    HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::ConfigurationChanged});
    ApplyInteractionState();
    return result;
}

SettingsActionResult UiController::ExecuteServiceAction(
    SettingsServiceAction action, config::ServiceStartType start_type) noexcept {
    return settings_controller_.ExecuteService(action, start_type);
}

bool UiController::RecreateRankingEngine() noexcept {
    try {
        auto created = ranking::CreateRankingEngine(
            {.window = config_.ranking.window, .top_n = config_.ranking.top_n});
        if (created.error != ranking::RankingError::None) {
            return false;
        }
        ranking_engine_ = std::move(created.engine);
        return ranking_engine_ != nullptr;
    } catch (...) {
        memory_error_.store(true, std::memory_order_release);
        return false;
    }
}

void UiController::PostSnapshotMessage() noexcept {
    if (message_window_ == nullptr) {
        return;
    }
    bool should_post = false;
    {
        const std::scoped_lock lock{pending_mutex_};
        if (!pending_message_posted_) {
            pending_message_posted_ = true;
            should_post = true;
        }
    }
    if (should_post && ::PostMessageW(message_window_, kUiSnapshotMessage, 0, 0) == FALSE) {
        const std::scoped_lock lock{pending_mutex_};
        pending_message_posted_ = false;
    }
}

LRESULT CALLBACK UiController::MessageWindowProc(HWND window, UINT message, WPARAM wparam,
                                                 LPARAM lparam) noexcept {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 stores the controller pointer in LONG_PTR.
    auto* controller = reinterpret_cast<UiController*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 LPARAM carries CREATESTRUCTW here.
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        controller = static_cast<UiController*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controller));
        return TRUE;
    }
    if (message == WM_NCDESTROY) {
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    if (controller == nullptr) {
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    if (message == kUiSnapshotMessage) {
        static_cast<void>(controller->ProcessPendingSnapshot());
        return 0;
    }
    if (message == kUiStateMessage) {
        controller->HandleStateOnUiThread(static_cast<ipc::IpcClientState>(wparam));
        return 0;
    }
    if (message == WM_TIMER && wparam == kUiMonitorPanelTimerId) {
        controller->HandleMonitorPanelEvent({.kind = MonitorPanelEventKind::HoverDeadline});
        return 0;
    }
    if (message == WM_TIMER && wparam == kUiTopmostPolicyTimerId) {
        controller->ApplyTopmostPolicy();
        return 0;
    }
    if (message == kTrayCallbackMessage) {
        // NOTIFYICON_VERSION_4: the icon id is in HIWORD(lParam) and the event in LOWORD(lParam).
        static_cast<void>(controller->tray_controller_.HandleCallback(
            TrayNotification{.icon_id = HIWORD(static_cast<UINT>(lparam)),
                             .message = LOWORD(static_cast<UINT>(lparam))}));
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace nfm::ui
