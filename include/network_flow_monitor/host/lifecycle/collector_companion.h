#ifndef NETWORK_FLOW_MONITOR_HOST_LIFECYCLE_COLLECTOR_COMPANION_H
#define NETWORK_FLOW_MONITOR_HOST_LIFECYCLE_COLLECTOR_COMPANION_H

#include "network_flow_monitor/host/bridge/bridge_types.h"
#include "network_flow_monitor/ipc/ipc_client.h"
#include "network_flow_monitor/ipc/protocol_types.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace nfm::host::lifecycle {

enum class CollectorState : std::uint8_t {
    Stopped,
    Starting,
    Running,
    Failed,
};

struct CollectorInfo {
    CollectorState state{CollectorState::Stopped};
    std::string instance_id;
    std::uint32_t process_id{};
    std::string error_message;
};

using TrafficTickCallback = std::function<void(const bridge::TrafficTickDto&)>;
using CollectorStatusCallback = std::function<void(const bridge::CollectorStatusDto&)>;
using SessionResetCallback = std::function<void(const bridge::SessionResetDto&)>;

class CollectorCompanion : public ipc::IpcClientObserver {
  public:
    explicit CollectorCompanion(std::filesystem::path collector_executable_path = {});
    ~CollectorCompanion() override;

    CollectorCompanion(const CollectorCompanion&) = delete;
    CollectorCompanion& operator=(const CollectorCompanion&) = delete;
    CollectorCompanion(CollectorCompanion&&) = delete;
    CollectorCompanion& operator=(CollectorCompanion&&) = delete;

    void set_traffic_tick_callback(TrafficTickCallback callback);
    void set_collector_status_callback(CollectorStatusCallback callback);
    void set_session_reset_callback(SessionResetCallback callback);

    bool start();
    void stop();
    bool restart();

    [[nodiscard]] CollectorInfo get_info() const;
    [[nodiscard]] bool is_running() const noexcept;

    // ipc::IpcClientObserver overrides
    void StateChanged(ipc::IpcClientState state) noexcept override;
    void HelloAckReceived(ipc::HelloAckMessage ack) noexcept override;
    void TrafficTickReceived(ipc::TrafficTickMessage tick) noexcept override;
    void RankingSnapshotReceived(ipc::RankingSnapshotMessage snapshot) noexcept override;
    void CollectorStatusReceived(ipc::CollectorStatusMessage status) noexcept override;

    // Test seams / Mocking helpers
    void inject_traffic_tick(const ipc::TrafficTickMessage& tick);
    void inject_collector_status(const ipc::CollectorStatusMessage& status);
    void inject_hello_ack(const ipc::HelloAckMessage& ack);

  private:
    std::filesystem::path collector_path_;
    mutable std::mutex mutex_;
    CollectorInfo info_;

    TrafficTickCallback traffic_tick_cb_;
    CollectorStatusCallback collector_status_cb_;
    SessionResetCallback session_reset_cb_;

    std::unique_ptr<ipc::IpcClient> ipc_client_;
    void* collector_process_handle_{nullptr};
    void* stop_event_handle_{nullptr};
    std::string stop_event_name_;
};

} // namespace nfm::host::lifecycle

#endif // NETWORK_FLOW_MONITOR_HOST_LIFECYCLE_COLLECTOR_COMPANION_H
