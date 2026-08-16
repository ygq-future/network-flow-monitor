#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace nfm::ipc {
namespace {

constexpr auto kTrafficTickInterval = std::chrono::milliseconds{250};
constexpr auto kRankingSnapshotInterval = std::chrono::milliseconds{1000};

PublisherResult PublisherFailure(PublisherError error) noexcept {
    return {.error = error};
}

} // namespace

SnapshotPublisher::SnapshotPublisher(std::string collector_instance_id,
                                     std::chrono::steady_clock::time_point instance_started_at,
                                     SnapshotProvider& provider, IpcServer& server)
    : collector_instance_id_{std::move(collector_instance_id)},
      instance_started_at_{instance_started_at}, provider_{&provider}, server_{&server} {}

std::uint64_t SnapshotPublisher::MonotonicMs(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point started_at) noexcept {
    if (now <= started_at) {
        return 0U;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at);
    if (elapsed.count() <= 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(elapsed.count());
}

void SnapshotPublisher::ClientConnected(std::uint64_t client_id,
                                        const HelloMessage& hello) noexcept {
    try {
        const std::scoped_lock lock{mutex_};
        connected_clients_.insert_or_assign(client_id, hello);
    } catch (...) {
        pending_error_ = PublisherError::CaptureFailed;
    }
}

void SnapshotPublisher::ClientDisconnected(std::uint64_t client_id) noexcept {
    const std::scoped_lock lock{mutex_};
    connected_clients_.erase(client_id);
}

std::optional<std::chrono::steady_clock::time_point> SnapshotPublisher::next_deadline()
    const noexcept {
    const std::scoped_lock lock{mutex_};
    if (connected_clients_.empty()) {
        return std::nullopt;
    }
    const auto tick_deadline =
        last_tick_run_.has_value() ? *last_tick_run_ + kTrafficTickInterval : instance_started_at_;
    const auto snapshot_deadline = last_snapshot_run_.has_value()
                                       ? *last_snapshot_run_ + kRankingSnapshotInterval
                                       : instance_started_at_;
    return std::min(tick_deadline, snapshot_deadline);
}

PublisherResult SnapshotPublisher::RunDue(std::chrono::steady_clock::time_point now) noexcept {
    try {
        const std::scoped_lock lock{mutex_};
        if (pending_error_ != PublisherError::None) {
            const auto error = pending_error_;
            pending_error_ = PublisherError::None;
            return PublisherFailure(error);
        }
        if (now < instance_started_at_) {
            return PublisherFailure(PublisherError::TimeMovedBackward);
        }
        if (connected_clients_.empty()) {
            return {};
        }

        const bool tick_due =
            !last_tick_run_.has_value() || (now >= *last_tick_run_ + kTrafficTickInterval);
        const bool snapshot_due = !last_snapshot_run_.has_value() ||
                                  (now >= *last_snapshot_run_ + kRankingSnapshotInterval);

        if (!tick_due && !snapshot_due) {
            return {};
        }

        const auto capture_result = provider_->Capture();
        if (capture_result.error != CaptureError::None) {
            return PublisherFailure(PublisherError::CaptureFailed);
        }

        std::uint64_t total_download = 0U;
        std::uint64_t total_upload = 0U;
        for (const auto& app : capture_result.capture.counters.applications) {
            total_download += app.download_bytes;
            total_upload += app.upload_bytes;
        }

        const auto monotonic_ms = MonotonicMs(now, instance_started_at_);

        if (tick_due) {
            const TrafficTickMessage tick{
                .seq = ++tick_sequence_,
                .monotonic_ms = monotonic_ms,
                .download_bytes_per_sec = total_download,
                .upload_bytes_per_sec = total_upload,
            };
            const auto pub_result = server_->PublishTrafficTick(tick);
            if (!pub_result.ok()) {
                return PublisherFailure(PublisherError::ServerFailed);
            }
            last_tick_run_ = now;
        }

        if (snapshot_due) {
            RankingSnapshotMessage snapshot{
                .seq = ++snapshot_sequence_,
                .monotonic_ms = monotonic_ms,
                .status = "Ready",
                .total_rate =
                    {
                        .download_bytes_per_sec = total_download,
                        .upload_bytes_per_sec = total_upload,
                    },
            };

            for (const auto& app_counter : capture_result.capture.counters.applications) {
                RankingApplication app_item{
                    .id = app_counter.id,
                    .executable_path = app_counter.metadata.icon_key,
                    .display_name = app_counter.metadata.display_name.empty()
                                        ? app_counter.id
                                        : app_counter.metadata.display_name,
                    .icon_key = app_counter.metadata.icon_key,
                    .download_bytes_per_sec = app_counter.download_bytes,
                    .upload_bytes_per_sec = app_counter.upload_bytes,
                };
                snapshot.applications.push_back(std::move(app_item));
            }

            const auto pub_result = server_->PublishRankingSnapshot(snapshot);
            if (!pub_result.ok()) {
                return PublisherFailure(PublisherError::ServerFailed);
            }
            last_snapshot_run_ = now;
        }

        return {};
    } catch (...) {
        return PublisherFailure(PublisherError::CaptureFailed);
    }
}

} // namespace nfm::ipc
