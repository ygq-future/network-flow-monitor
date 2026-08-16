#ifndef NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_PUBLISHER_H
#define NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_PUBLISHER_H

#include "network_flow_monitor/collector/collector_health.h"
#include "network_flow_monitor/ipc/ipc_server.h"
#include "network_flow_monitor/ranking/counter_types.h"
#include "network_flow_monitor/ranking/ranking_engine.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace nfm::ipc {

struct CollectorCapture {
    collector::CollectorHealth health;
    ranking::CounterSnapshot counters;
    std::uint32_t active_pids{};
};

enum class CaptureError : std::uint8_t {
    None,
    FlushFailed,
    SnapshotFailed,
    InvalidTimestamp,
    CollectorFailed,
};

struct CaptureResult {
    CaptureError error{CaptureError::None};
    CollectorCapture capture;
    std::uint32_t source_error{};
};

enum class PublisherError : std::uint8_t {
    None,
    InvalidSubscription,
    TimeMovedBackward,
    CaptureFailed,
    CatalogFailed,
    ServerFailed,
    SequenceExhausted,
    UptimeOverflow,
};

struct PublisherResult {
    PublisherError error{PublisherError::None};

    [[nodiscard]] bool ok() const noexcept {
        return error == PublisherError::None;
    }
};

class SnapshotProvider {
  public:
    SnapshotProvider() = default;
    SnapshotProvider(const SnapshotProvider&) = delete;
    SnapshotProvider& operator=(const SnapshotProvider&) = delete;
    SnapshotProvider(SnapshotProvider&&) = delete;
    SnapshotProvider& operator=(SnapshotProvider&&) = delete;
    virtual ~SnapshotProvider() = default;

    [[nodiscard]] virtual CaptureResult Capture() noexcept = 0;
};

class SnapshotPublisher final : public ClientSessionObserver {
  public:
    SnapshotPublisher(std::string collector_instance_id,
                      std::chrono::steady_clock::time_point instance_started_at,
                      SnapshotProvider& provider, IpcServer& server);
    SnapshotPublisher(std::string collector_instance_id,
                      std::chrono::steady_clock::time_point instance_started_at,
                      SnapshotProvider& provider, IpcServer& server,
                      ranking::RankingOptions ranking_options);
    ~SnapshotPublisher() override = default;
    SnapshotPublisher(const SnapshotPublisher&) = delete;
    SnapshotPublisher& operator=(const SnapshotPublisher&) = delete;
    SnapshotPublisher(SnapshotPublisher&&) = delete;
    SnapshotPublisher& operator=(SnapshotPublisher&&) = delete;

    void ClientConnected(std::uint64_t client_id, const HelloMessage& hello) noexcept override;
    void ClientDisconnected(std::uint64_t client_id) noexcept override;

    [[nodiscard]] PublisherResult RunDue(std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline()
        const noexcept;

  private:
    [[nodiscard]] static std::uint64_t MonotonicMs(
        std::chrono::steady_clock::time_point now,
        std::chrono::steady_clock::time_point started_at) noexcept;

    std::string collector_instance_id_;
    std::chrono::steady_clock::time_point instance_started_at_;
    SnapshotProvider* provider_;
    IpcServer* server_;
    std::unique_ptr<ranking::RankingEngine> ranking_engine_;
    std::unordered_map<std::uint64_t, HelloMessage> connected_clients_;
    std::optional<std::chrono::steady_clock::time_point> last_tick_run_;
    std::optional<std::chrono::steady_clock::time_point> last_snapshot_run_;
    std::optional<std::uint64_t> previous_total_download_;
    std::optional<std::uint64_t> previous_total_upload_;
    std::optional<std::chrono::steady_clock::time_point> previous_sample_time_;
    std::uint64_t tick_sequence_{};
    std::uint64_t snapshot_sequence_{};
    PublisherError pending_error_{PublisherError::None};
    mutable std::mutex mutex_;
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_PUBLISHER_H
