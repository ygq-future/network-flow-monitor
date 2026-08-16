#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include "network_flow_monitor/config/app_config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
    : SnapshotPublisher(
          std::move(collector_instance_id), instance_started_at, provider, server,
          ranking::RankingOptions{.window = config::DefaultAppConfig().ranking.window,
                                  .top_n = config::DefaultAppConfig().ranking.top_n}) {}

SnapshotPublisher::SnapshotPublisher(std::string collector_instance_id,
                                     std::chrono::steady_clock::time_point instance_started_at,
                                     SnapshotProvider& provider, IpcServer& server,
                                     ranking::RankingOptions ranking_options)
    : collector_instance_id_{std::move(collector_instance_id)},
      instance_started_at_{instance_started_at}, provider_{&provider}, server_{&server} {
    auto create_result = ranking::CreateRankingEngine(ranking_options);
    if (create_result.error == ranking::RankingError::None) {
        ranking_engine_ = std::move(create_result.engine);
    }
}

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

        std::uint64_t download_rate = 0U;
        std::uint64_t upload_rate = 0U;

        if (previous_sample_time_.has_value() && now > *previous_sample_time_ &&
            previous_total_download_.has_value() && previous_total_upload_.has_value()) {
            const auto elapsed_us =
                std::chrono::duration_cast<std::chrono::microseconds>(now - *previous_sample_time_)
                    .count();
            if (elapsed_us > 0) {
                const auto delta_download = total_download >= *previous_total_download_
                                                ? total_download - *previous_total_download_
                                                : 0U;
                const auto delta_upload = total_upload >= *previous_total_upload_
                                              ? total_upload - *previous_total_upload_
                                              : 0U;
                const double elapsed_seconds = static_cast<double>(elapsed_us) / 1'000'000.0;
                download_rate = static_cast<std::uint64_t>(static_cast<double>(delta_download) /
                                                           elapsed_seconds);
                upload_rate =
                    static_cast<std::uint64_t>(static_cast<double>(delta_upload) / elapsed_seconds);
            }
        }

        previous_total_download_ = total_download;
        previous_total_upload_ = total_upload;
        previous_sample_time_ = now;

        const auto monotonic_ms = MonotonicMs(now, instance_started_at_);

        ranking::RankingResult ranking_result;
        if (ranking_engine_) {
            ranking_result = ranking_engine_->PushSnapshot(capture_result.capture.counters);
        }

        if (tick_due) {
            const TrafficTickMessage tick{
                .seq = ++tick_sequence_,
                .monotonic_ms = monotonic_ms,
                .download_bytes_per_sec = download_rate,
                .upload_bytes_per_sec = upload_rate,
            };
            const auto pub_result = server_->PublishTrafficTick(tick);
            if (!pub_result.ok()) {
                return PublisherFailure(PublisherError::ServerFailed);
            }
            last_tick_run_ = now;
        }

        if (snapshot_due) {
            std::string status = "WarmingUp";
            std::uint64_t snapshot_dl_rate = download_rate;
            std::uint64_t snapshot_ul_rate = upload_rate;

            RankingSnapshotMessage snapshot{
                .seq = ++snapshot_sequence_,
                .monotonic_ms = monotonic_ms,
                .status = status,
                .total_rate =
                    {
                        .download_bytes_per_sec = snapshot_dl_rate,
                        .upload_bytes_per_sec = snapshot_ul_rate,
                    },
            };

            if (ranking_result.accepted()) {
                if (ranking_result.snapshot.status == ranking::RankingStatus::Ready) {
                    snapshot.status = "Ready";
                    snapshot.total_rate.download_bytes_per_sec = static_cast<std::uint64_t>(
                        std::max(0.0, std::round(ranking_result.snapshot.total_rate
                                                     .download_bytes_per_second)));
                    snapshot.total_rate.upload_bytes_per_sec = static_cast<std::uint64_t>(std::max(
                        0.0,
                        std::round(ranking_result.snapshot.total_rate.upload_bytes_per_second)));
                }
                for (const auto& ranked_app : ranking_result.snapshot.applications) {
                    RankingApplication app_item{
                        .id = ranked_app.id,
                        .executable_path = ranked_app.metadata.icon_key,
                        .display_name = ranked_app.metadata.display_name.empty()
                                            ? ranked_app.id
                                            : ranked_app.metadata.display_name,
                        .icon_key = ranked_app.metadata.icon_key,
                        .download_bytes_per_sec = static_cast<std::uint64_t>(
                            std::max(0.0, std::round(ranked_app.rate.download_bytes_per_second))),
                        .upload_bytes_per_sec = static_cast<std::uint64_t>(
                            std::max(0.0, std::round(ranked_app.rate.upload_bytes_per_second))),
                    };
                    snapshot.applications.push_back(std::move(app_item));
                }
            }

            const auto pub_result = server_->PublishRankingSnapshot(snapshot);
            if (!pub_result.ok()) {
                return PublisherFailure(PublisherError::ServerFailed);
            }

            const CollectorStatusMessage status_msg{
                .monotonic_ms = monotonic_ms,
                .etw_session_active =
                    capture_result.capture.health.state == collector::CollectorState::Running ||
                    capture_result.capture.health.state == collector::CollectorState::Degraded,
                .dropped_packets = capture_result.capture.health.events_lost +
                                   capture_result.capture.health.realtime_buffers_lost,
                .active_pids = capture_result.capture.active_pids != 0U
                                   ? capture_result.capture.active_pids
                                   : static_cast<std::uint32_t>(
                                         capture_result.capture.counters.applications.size()),
            };
            const auto status_pub_result = server_->PublishCollectorStatus(status_msg);
            if (!status_pub_result.ok()) {
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
