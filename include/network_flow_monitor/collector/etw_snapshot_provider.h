#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_ETW_SNAPSHOT_PROVIDER_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_ETW_SNAPSHOT_PROVIDER_H

#include "network_flow_monitor/collector/windows_etw_session.h"
#include "network_flow_monitor/ipc/snapshot_publisher.h"

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
};

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_ETW_SNAPSHOT_PROVIDER_H
