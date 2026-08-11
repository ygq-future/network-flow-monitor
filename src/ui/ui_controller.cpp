#include "network_flow_monitor/ui/ui_controller.h"

#include "network_flow_monitor/app/collector_runtime.h"

#include <chrono>

namespace nfm::ui {

UiController::UiController(HINSTANCE instance, const config::AppConfig& config)
    : instance_{instance}, config_{config}, collector_process_{shell_api_},
      floating_window_{instance_, render_resources_, *this},
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

    static_cast<void>(tray_controller_.Initialize());
    static_cast<void>(floating_window_.Create());
    static_cast<void>(ranking_popup_.Create());
    started_ = true;
    return true;
}

void UiController::Shutdown() noexcept {
    if (ipc_client_ != nullptr) {
        ipc_client_->Stop();
        ipc_client_.reset();
    }
    ranking_popup_.Destroy();
    floating_window_.Destroy();
    tray_controller_.Shutdown();
    static_cast<void>(collector_process_.Stop());
    started_ = false;
}

void UiController::AttachMessageWindow(HWND window) noexcept {
    message_window_ = window;
    tray_controller_.SetOwner(window);
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
    interaction_.Handle(InteractionEvent::Toggle, InteractionStateMachine::Clock::now(),
                        InteractionOptionsFromConfig());
    floating_window_.SetInteractionState(interaction_.state());
}

void UiController::OnTrayRightClick() noexcept {
    static_cast<void>(tray_controller_.ShowContextMenu());
}

void UiController::OnTrayHover() noexcept {
    interaction_.Handle(InteractionEvent::PointerEnter, InteractionStateMachine::Clock::now(),
                        InteractionOptionsFromConfig(), MouseTarget::Anchor);
    floating_window_.SetInteractionState(interaction_.state());
}

void UiController::OnTraySettings() noexcept {}

void UiController::OnTrayExit() noexcept {
    ::PostQuitMessage(0);
}

void UiController::OnWindowInvalidated(WindowKind /*kind*/) noexcept {}

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

} // namespace nfm::ui
