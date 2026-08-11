#ifndef NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_PUBLISHER_H
#define NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_PUBLISHER_H

#include "network_flow_monitor/collector/collector_health.h"
#include "network_flow_monitor/ipc/application_catalog.h"
#include "network_flow_monitor/ipc/ipc_server.h"
#include "network_flow_monitor/ranking/counter_types.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>

namespace nfm::ipc {

struct CollectorCapture {
    collector::CollectorHealth health;
    ranking::CounterSnapshot counters;
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
    SnapshotPublisher(CollectorInstanceId instance,
                      std::chrono::steady_clock::time_point instance_started_at,
                      SnapshotProvider& provider, IpcServer& server);
    ~SnapshotPublisher() override = default;
    SnapshotPublisher(const SnapshotPublisher&) = delete;
    SnapshotPublisher& operator=(const SnapshotPublisher&) = delete;
    SnapshotPublisher(SnapshotPublisher&&) = delete;
    SnapshotPublisher& operator=(SnapshotPublisher&&) = delete;

    void SubscriptionChanged(ClientSubscription subscription) noexcept override;
    void ClientDisconnected(std::uint64_t client_id) noexcept override;

    [[nodiscard]] PublisherResult RunDue(std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline()
        const noexcept;

  private:
    struct SubscriptionState {
        std::chrono::milliseconds refresh_interval{};
        std::optional<std::chrono::steady_clock::time_point> next_deadline;
    };

    [[nodiscard]] static bool IsDue(const SubscriptionState& subscription,
                                    std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] static bool IsValidSubscription(const ClientSubscription& subscription) noexcept;
    [[nodiscard]] static std::uint64_t UptimeMicroseconds(
        std::chrono::steady_clock::time_point now,
        std::chrono::steady_clock::time_point started_at) noexcept;
    [[nodiscard]] PublisherResult PublishStatus(const CollectorCapture& capture,
                                                std::chrono::steady_clock::time_point now) noexcept;
    void ScheduleRetry(std::chrono::steady_clock::time_point now) noexcept;
    void ScheduleCompleted(std::chrono::steady_clock::time_point now,
                           std::span<const std::uint64_t> due_clients) noexcept;

    CollectorInstanceId instance_;
    std::chrono::steady_clock::time_point instance_started_at_;
    SnapshotProvider* provider_;
    IpcServer* server_;
    ApplicationCatalog catalog_;
    std::unordered_map<std::uint64_t, SubscriptionState> subscriptions_;
    std::optional<std::chrono::steady_clock::time_point> last_run_;
    std::optional<std::chrono::steady_clock::time_point> last_capture_;
    std::uint64_t snapshot_sequence_{};
    PublisherError pending_error_{PublisherError::None};
    mutable std::mutex mutex_;
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_PUBLISHER_H
