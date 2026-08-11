#ifndef NETWORK_FLOW_MONITOR_APP_COLLECTOR_RUNTIME_H
#define NETWORK_FLOW_MONITOR_APP_COLLECTOR_RUNTIME_H

#include "network_flow_monitor/collector/collector_host.h"
#include "network_flow_monitor/collector/etw_snapshot_provider.h"
#include "network_flow_monitor/ipc/security_policy.h"
#include "network_flow_monitor/service/service_entry.h"

#include <chrono>
#include <cstdint>
#include <stop_token>

namespace nfm::app {

struct CollectorPrincipalResult {
    ipc::AuthorizedPrincipal principal;
    std::uint32_t source_error{};

    [[nodiscard]] bool ok() const noexcept {
        return source_error == 0U;
    }
};

[[nodiscard]] CollectorPrincipalResult BuildCollectorPrincipal(
    ipc::CollectorEndpointMode mode) noexcept;
[[nodiscard]] ipc::CollectorInstanceId CreateCollectorInstanceId() noexcept;

class CollectorRuntime final : public service::ServiceHost {
  public:
    explicit CollectorRuntime(ipc::CollectorEndpointMode mode);
    ~CollectorRuntime() override = default;
    CollectorRuntime(const CollectorRuntime&) = delete;
    CollectorRuntime& operator=(const CollectorRuntime&) = delete;
    CollectorRuntime(CollectorRuntime&&) = delete;
    CollectorRuntime& operator=(CollectorRuntime&&) = delete;

    [[nodiscard]] collector::CollectorHostResult Start() noexcept override;
    [[nodiscard]] collector::CollectorHostResult Run(
        const std::stop_token& stop_token) noexcept override;
    [[nodiscard]] collector::CollectorHostResult Stop() noexcept override;

    // Runs until a console/session control requests the stop token or the host fails.
    [[nodiscard]] collector::CollectorHostResult RunPortable() noexcept;

  private:
    ipc::CollectorInstanceId instance_;
    ipc::AuthorizedPrincipal principal_;
    collector::WindowsEtwApi etw_api_;
    collector::WindowsEtwSession session_;
    collector::EtwSnapshotProvider provider_;
    ipc::WindowsPipeTransportFactory transport_;
    std::chrono::steady_clock::time_point started_at_;
    ipc::IpcServer server_;
    ipc::SnapshotPublisher publisher_;
    collector::ThreadHostWait wait_;
    collector::CollectorHost host_;
};

} // namespace nfm::app

#endif // NETWORK_FLOW_MONITOR_APP_COLLECTOR_RUNTIME_H
