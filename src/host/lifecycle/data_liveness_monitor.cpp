#include "network_flow_monitor/host/lifecycle/data_liveness_monitor.h"

namespace nfm::host::lifecycle {
namespace {

using Clock = std::chrono::steady_clock;

} // namespace

DataLivenessMonitor::DataLivenessMonitor(DataLivenessPolicy policy) noexcept : policy_{policy} {}

std::chrono::milliseconds DataLivenessMonitor::last_stall_duration() const noexcept {
    return last_stall_duration_;
}

bool DataLivenessMonitor::CollectorRecoveryAllowed(Clock::time_point now) const noexcept {
    if (!collector_recovery_at_.has_value()) {
        return true;
    }
    return now - *collector_recovery_at_ >= policy_.recovery_cooldown;
}

bool DataLivenessMonitor::PageRecoveryAllowed(Clock::time_point now) const noexcept {
    if (!page_recovery_at_.has_value()) {
        return true;
    }
    return now - *page_recovery_at_ >= policy_.recovery_cooldown;
}

std::vector<DataLivenessAction> DataLivenessMonitor::Observe(
    const DataLivenessObservation& observation) {
    std::vector<DataLivenessAction> actions;
    const auto now = observation.now;

    if (observation.collector_streaming && !collector_streaming_) {
        // A new stream starts: judge stalls from this moment instead of the previous session.
        last_frame_at_ = now;
        collector_stall_logged_ = false;
        collector_restart_issued_ = false;
        etw_seen_active_ = false;
        etw_inactive_since_.reset();
        page_tracking_ = false;
        page_stall_logged_ = false;
        page_reload_issued_ = false;
        last_ack_at_.reset();
    }
    collector_streaming_ = observation.collector_streaming;
    if (observation.collector_streaming) {
        collector_down_since_.reset();
        collector_down_logged_ = false;
    }

    const bool frames_advanced = observation.frames_received > last_frames_received_;
    last_frames_received_ = observation.frames_received;
    if (frames_advanced) {
        last_frame_at_ = now;
        if (collector_stall_logged_) {
            actions.push_back(DataLivenessAction::CollectorStallCleared);
            collector_stall_logged_ = false;
            collector_restart_issued_ = false;
        }
    }

    if (observation.collector_streaming) {
        if (!last_frame_at_.has_value()) {
            last_frame_at_ = now;
        }
        const auto frame_age = now - *last_frame_at_;
        if (frame_age >= policy_.collector_stall_log) {
            // Only a stalled stream updates the measurement, so the cleared episode keeps the
            // duration that was actually observed.
            last_stall_duration_ = std::chrono::duration_cast<std::chrono::milliseconds>(frame_age);
        }
        if (frame_age >= policy_.collector_stall_log && !collector_stall_logged_) {
            actions.push_back(DataLivenessAction::CollectorStallDetected);
            collector_stall_logged_ = true;
        }
        if (frame_age >= policy_.collector_stall_restart && !collector_restart_issued_ &&
            CollectorRecoveryAllowed(now)) {
            actions.push_back(DataLivenessAction::CollectorRestartRequested);
            collector_restart_issued_ = true;
            collector_recovery_at_ = now;
        }

        // The collector names its own ETW failures through the status frame; a sustained
        // "inactive" report means the stream is alive but the pipeline behind it is not.
        if (observation.etw_active) {
            etw_seen_active_ = true;
            etw_inactive_since_.reset();
        } else if (etw_seen_active_) {
            if (!etw_inactive_since_.has_value()) {
                etw_inactive_since_ = now;
            }
            if (now - *etw_inactive_since_ >= policy_.etw_inactive_restart &&
                !collector_restart_issued_ && CollectorRecoveryAllowed(now)) {
                actions.push_back(DataLivenessAction::CollectorRestartRequested);
                collector_restart_issued_ = true;
                collector_recovery_at_ = now;
            }
        }
    } else {
        collector_stall_logged_ = false;
        collector_restart_issued_ = false;
        etw_inactive_since_.reset();
        // A missing collector is a fault only while the host still needs traffic data. The
        // process-exit path relies on this to recover without a manual app restart.
        if (observation.collector_expected) {
            if (!collector_down_since_.has_value()) {
                collector_down_since_ = now;
            }
            const auto down_for = now - *collector_down_since_;
            if (!collector_down_logged_ && down_for >= policy_.collector_stall_log) {
                actions.push_back(DataLivenessAction::CollectorUnavailable);
                collector_down_logged_ = true;
            }
            if (down_for >= policy_.collector_down_restart && !collector_restart_issued_ &&
                CollectorRecoveryAllowed(now)) {
                actions.push_back(DataLivenessAction::CollectorRestartRequested);
                collector_restart_issued_ = true;
                collector_recovery_at_ = now;
            }
        } else {
            collector_down_since_.reset();
            collector_down_logged_ = false;
        }
    }

    const bool page_observable =
        observation.webview_ready && observation.collector_streaming && !collector_stall_logged_;
    if (!page_observable) {
        page_tracking_ = false;
        page_stall_logged_ = false;
        page_reload_issued_ = false;
        last_ack_at_.reset();
        return actions;
    }

    if (!page_tracking_ && observation.frames_delivered) {
        page_tracking_ = true;
        last_ack_at_ = now;
    }

    if (observation.page_acks > last_page_acks_) {
        last_page_acks_ = observation.page_acks;
        last_ack_at_ = now;
        if (page_stall_logged_) {
            actions.push_back(DataLivenessAction::PageStallCleared);
            page_stall_logged_ = false;
            page_reload_issued_ = false;
        }
        page_tracking_ = false;
        return actions;
    }

    if (!page_tracking_ || !last_ack_at_.has_value()) {
        return actions;
    }

    const auto ack_age = now - *last_ack_at_;
    if (ack_age >= policy_.page_stall_log && !page_stall_logged_) {
        actions.push_back(DataLivenessAction::PageStallDetected);
        page_stall_logged_ = true;
    }
    if (ack_age >= policy_.page_stall_reload && !page_reload_issued_ && PageRecoveryAllowed(now)) {
        actions.push_back(DataLivenessAction::WebViewReloadRequested);
        page_reload_issued_ = true;
        page_recovery_at_ = now;
    }
    return actions;
}

} // namespace nfm::host::lifecycle
