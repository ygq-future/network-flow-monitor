#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_ETW_SNAPSHOT_PROVIDER_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_ETW_SNAPSHOT_PROVIDER_H

#include "network_flow_monitor/collector/windows_etw_session.h"
#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include <chrono>
#include <optional>

namespace nfm::collector {

class EtwSnapshotProvider final : public ipc::SnapshotProvider {
  public:
    explicit EtwSnapshotProvider(WindowsEtwSession& session) noexcept : session_{&session} {}
    ~EtwSnapshotProvider() override = default;
    EtwSnapshotProvider(const EtwSnapshotProvider&) = delete;
    EtwSnapshotProvider& operator=(const EtwSnapshotProvider&) = delete;
    EtwSnapshotProvider(EtwSnapshotProvider&&) = delete;
    EtwSnapshotProvider& operator=(EtwSnapshotProvider&&) = delete;

    [[nodiscard]] ipc::CaptureResult Capture() noexcept override;

  private:
    WindowsEtwSession* session_;
    // Capture runs every 250 ms; only the healthy<->failed edges are worth a log line.
    bool capture_failing_{false};
    // Cross-check that the realtime session still matches the OS network stack.
    std::optional<EtwDeliverySample> delivery_sample_;
    std::optional<std::chrono::steady_clock::time_point> delivery_check_at_;
};

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_ETW_SNAPSHOT_PROVIDER_H
