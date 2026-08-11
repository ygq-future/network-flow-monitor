#include "network_flow_monitor/ui/ui_controller.h"

#include "network_flow_monitor/app/collector_runtime.h"
#include "network_flow_monitor/config/config_paths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>

namespace nfm::ui {
namespace {

constexpr auto kUiMessageWindowClass = std::to_array(L"NetworkFlowMonitor.UiMessageWindow");

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

} // namespace

UiController::UiController(HINSTANCE instance, const config::AppConfig& config)
    : instance_{instance}, config_{config}, settings_store_{}, service_action_api_{},
      startup_store_{},
      settings_controller_{DefaultSettingsPath(), config::ConfigDocumentKind::User, settings_store_,
                           service_action_api_},
      startup_registration_{startup_store_}, settings_window_{instance_, *this},
      collector_process_{shell_api_}, floating_window_{instance_, render_resources_, *this},
      ranking_popup_{instance_, render_resources_, *this},
      tray_controller_{nullptr, TrayIconOptions{}, tray_api_, *this}, snapshot_generation_{0U},
      memory_error_{false} {
    static_cast<void>(RecreateRankingEngine());
}

UiController::~UiController() {
    Shutdown();
}

bool UiController::Start() noexcept {
    if (started_) {
        return true;
    }
    if (ranking_engine_ == nullptr || !render_resources_.Initialize()) {
        return false;
    }

    if (!StartCollection()) {
        return false;
    }

    static_cast<void>(tray_controller_.Initialize());
    static_cast<void>(floating_window_.Create());
    static_cast<void>(ranking_popup_.Create());
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
        static_cast<void>(::KillTimer(message_window_, kUiInteractionTimerId));
    }
    ranking_popup_.Destroy();
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
    const auto state = interaction_.state();
    floating_window_.SetInteractionState(state);
    switch (state) {
    case InteractionState::Hidden:
    case InteractionState::EdgeHidden:
        ranking_popup_.Hide();
        floating_window_.Hide();
        break;
    case InteractionState::Visible:
    case InteractionState::DisconnectedOverlay:
        ranking_popup_.Hide();
        floating_window_.Show();
        break;
    case InteractionState::RankingVisible:
        floating_window_.Show();
        ranking_popup_.ShowAt(PopupPlacementContext{.anchor = floating_window_.rect(),
                                                    .work_area = CurrentWorkArea()});
        break;
    }
}

void UiController::HandleInteraction(InteractionEvent event, MouseTarget target) noexcept {
    interaction_.Handle(event, InteractionStateMachine::Clock::now(),
                        InteractionOptionsFromConfig(), target);
    ApplyInteractionState();
    ScheduleInteractionTimer();
}

void UiController::ScheduleInteractionTimer() noexcept {
    if (message_window_ == nullptr) {
        return;
    }
    const auto deadline = interaction_.next_deadline();
    if (!deadline.has_value()) {
        static_cast<void>(::KillTimer(message_window_, kUiInteractionTimerId));
        return;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               *deadline - InteractionStateMachine::Clock::now())
                               .count();
    constexpr auto kMaximumTimerMilliseconds =
        static_cast<std::int64_t>(std::numeric_limits<UINT>::max());
    const auto delay = static_cast<UINT>(
        std::clamp<std::int64_t>(remaining <= 0 ? 1 : remaining, 1, kMaximumTimerMilliseconds));
    static_cast<void>(::SetTimer(message_window_, kUiInteractionTimerId, delay, nullptr));
}

void UiController::HandleStateOnUiThread(ipc::IpcClientState state) noexcept {
    HandleInteraction(state == ipc::IpcClientState::Streaming ? InteractionEvent::Reconnected
                                                              : InteractionEvent::Disconnected);
}

PixelRect UiController::CurrentWorkArea() const noexcept {
    const auto monitor = ::MonitorFromWindow(floating_window_.hwnd(), MONITOR_DEFAULTTONEAREST);
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
        const auto generation = snapshot_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
        floating_window_.SetSnapshot(ranking_snapshot_, generation);
        ranking_popup_.SetSnapshot(ranking_snapshot_, generation);
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
    HandleInteraction(InteractionEvent::Toggle);
}

void UiController::OnTrayRightClick() noexcept {
    static_cast<void>(tray_controller_.ShowContextMenu());
}

void UiController::OnTrayHover() noexcept {
    HandleInteraction(InteractionEvent::PointerEnter, MouseTarget::Anchor);
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

void UiController::OnTrayExit() noexcept {
    ::PostQuitMessage(0);
}

void UiController::OnWindowInvalidated(WindowKind /*kind*/) noexcept {}

void UiController::OnWindowPointer(WindowKind, InteractionEvent event,
                                   MouseTarget target) noexcept {
    HandleInteraction(event, target);
}

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
    ApplyInteractionState();
    ScheduleInteractionTimer();
    return result;
}

SettingsActionResult UiController::ExecuteServiceAction(
    SettingsServiceAction action, config::ServiceStartType start_type) noexcept {
    return settings_controller_.ExecuteService(action, start_type);
}

InteractionOptions UiController::InteractionOptionsFromConfig() const noexcept {
    return {.hover_delay = config_.interaction.hover_delay,
            .dismiss_delay = config_.interaction.dismiss_delay,
            .auto_hide_delay = config_.floating.auto_hide_delay,
            .auto_hide_enabled = config_.floating.auto_hide_enabled};
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
    if (message == WM_TIMER && wparam == kUiInteractionTimerId) {
        controller->interaction_.Tick(InteractionStateMachine::Clock::now(),
                                      controller->InteractionOptionsFromConfig());
        controller->ApplyInteractionState();
        controller->ScheduleInteractionTimer();
        return 0;
    }
    if (message == kTrayCallbackMessage) {
        static_cast<void>(controller->tray_controller_.HandleCallback(TrayNotification{
            .icon_id = static_cast<UINT>(wparam), .message = static_cast<UINT>(lparam)}));
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace nfm::ui
