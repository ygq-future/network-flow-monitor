#include "network_flow_monitor/collector/collector_host.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace nfm::collector {
namespace {

CollectorHostResult HostFailure(CollectorHostError error, std::uint32_t source_error) noexcept {
    return {.error = error, .source_error = source_error};
}

} // namespace

bool ThreadHostWait::Until(const std::stop_token& stop_token,
                           std::optional<std::chrono::steady_clock::time_point> deadline) noexcept {
    constexpr auto poll = std::chrono::milliseconds{25};
    if (!deadline.has_value()) {
        // No subscription yet. Sleep briefly, then return so the host loop re-evaluates
        // next_deadline(); a subscription that arrives in the meantime is then picked up.
        std::this_thread::sleep_for(poll);
        return !stop_token.stop_requested();
    }
    while (!stop_token.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= *deadline) {
            return true;
        }
        const auto remaining = *deadline - now;
        const auto poll_duration =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(poll);
        std::this_thread::sleep_for(std::min(poll_duration, remaining));
    }
    return false;
}

CollectorHost::CollectorHost(WindowsEtwSession& session, ipc::IpcServer& server,
                             ipc::SnapshotPublisher& publisher, HostWait& wait) noexcept
    : session_{&session}, server_{&server}, publisher_{&publisher}, wait_{&wait} {}

CollectorHostResult CollectorHost::Fail(CollectorHostError error,
                                        std::uint32_t source_error) noexcept {
    state_ = CollectorHostState::Failed;
    last_error_ = error;
    last_source_error_ = source_error;
    return HostFailure(error, source_error);
}

CollectorHostResult CollectorHost::Start() noexcept {
    if (state_ != CollectorHostState::Stopped) {
        return HostFailure(CollectorHostError::InvalidState, 0U);
    }
    state_ = CollectorHostState::Starting;
    last_error_ = CollectorHostError::None;
    last_source_error_ = 0U;
    const auto observer = server_->SetSubscriptionObserver(*publisher_);
    if (!observer.ok()) {
        return Fail(CollectorHostError::SubscriptionObserverFailed, observer.win32_error);
    }
    const auto started = session_->Start();
    if (!started.ok()) {
        return Fail(CollectorHostError::EtwStartFailed, started.win32_error);
    }
    const auto server_started = server_->Start();
    if (!server_started.ok()) {
        static_cast<void>(session_->Stop());
        return Fail(CollectorHostError::IpcStartFailed, server_started.win32_error);
    }
    state_ = CollectorHostState::Running;
    return {};
}

CollectorHostResult CollectorHost::Run(const std::stop_token& stop_token) noexcept {
    if (state_ != CollectorHostState::Running) {
        return HostFailure(CollectorHostError::InvalidState, 0U);
    }
    try {
        while (!stop_token.stop_requested()) {
            if (!wait_->Until(stop_token, publisher_->next_deadline())) {
                break;
            }
            if (stop_token.stop_requested()) {
                break;
            }
            const auto published = publisher_->RunDue(std::chrono::steady_clock::now());
            if (!published.ok()) {
                last_error_ = CollectorHostError::PublisherFailed;
            }
        }
        return {};
    } catch (...) {
        return Fail(CollectorHostError::PublisherFailed, 0U);
    }
}

CollectorHostResult CollectorHost::Stop() noexcept {
    if (state_ == CollectorHostState::Stopped) {
        return {};
    }
    if (state_ == CollectorHostState::Stopping) {
        return HostFailure(CollectorHostError::InvalidState, 0U);
    }
    state_ = CollectorHostState::Stopping;
    const auto server_stopped = server_->Stop();
    const auto session_stopped = session_->Stop();
    if (!server_stopped.ok()) {
        return Fail(CollectorHostError::IpcStopFailed, server_stopped.win32_error);
    }
    if (!session_stopped.ok() && session_stopped.error != WindowsEtwSessionError::NotRunning) {
        return Fail(CollectorHostError::EtwStopFailed, session_stopped.win32_error);
    }
    state_ = CollectorHostState::Stopped;
    return {};
}

} // namespace nfm::collector
