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
#include <string_view>
#include <vector>

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
using RankingSnapshotCallback = std::function<void(const bridge::RankingSnapshotDto&)>;
using RawRankingSnapshotCallback = std::function<void(const ipc::RankingSnapshotMessage&)>;
using CollectorStatusCallback = std::function<void(const bridge::CollectorStatusDto&)>;
using SessionResetCallback = std::function<void(const bridge::SessionResetDto&)>;
using DiagnosticCallback = std::function<void(bool is_error, std::string_view message)>;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] bool is_application_excluded(std::string_view app_id,
                                           std::string_view executable_path,
                                           std::string_view display_name,
                                           const std::vector<std::string>& excluded_programs);
[[nodiscard]] std::string extract_executable_filename(std::string_view path_or_name);
[[nodiscard]] std::string extract_executable_icon_data_uri(std::string_view utf8_path);
class CollectorCompanion : public ipc::IpcClientObserver {
  public:
    explicit CollectorCompanion(std::filesystem::path collector_executable_path = {});
    ~CollectorCompanion() override;

    CollectorCompanion(const CollectorCompanion&) = delete;
    CollectorCompanion& operator=(const CollectorCompanion&) = delete;
    CollectorCompanion(CollectorCompanion&&) = delete;
    CollectorCompanion& operator=(CollectorCompanion&&) = delete;

    void set_traffic_tick_callback(TrafficTickCallback callback);
    void set_ranking_snapshot_callback(RankingSnapshotCallback callback);
    void set_raw_ranking_snapshot_callback(RawRankingSnapshotCallback callback);
    void set_collector_status_callback(CollectorStatusCallback callback);
    void set_session_reset_callback(SessionResetCallback callback);
    void set_diagnostic_callback(DiagnosticCallback callback);

    void set_excluded_programs(std::vector<std::string> excluded_programs);
    [[nodiscard]] std::vector<std::string> excluded_programs() const;
    [[nodiscard]] bridge::ConnectedApplicationsDto connected_applications() const;
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
    void inject_ranking_snapshot(const ipc::RankingSnapshotMessage& snapshot);
    void inject_collector_status(const ipc::CollectorStatusMessage& status);
    void inject_hello_ack(const ipc::HelloAckMessage& ack);

  private:
    std::filesystem::path collector_path_;
    mutable std::mutex mutex_;
    CollectorInfo info_;

    TrafficTickCallback traffic_tick_cb_;
    RankingSnapshotCallback ranking_snapshot_cb_;
    CollectorStatusCallback collector_status_cb_;
    RawRankingSnapshotCallback raw_ranking_snapshot_cb_;
    SessionResetCallback session_reset_cb_;
    DiagnosticCallback diagnostic_cb_;

    std::unique_ptr<ipc::PipeTransportFactory> transport_factory_;
    std::unique_ptr<ipc::IpcClient> ipc_client_;
    void* collector_process_handle_{nullptr};
    void* stop_event_handle_{nullptr};
    std::string stop_event_name_;
    std::vector<std::string> excluded_programs_;
    std::uint64_t excluded_download_bytes_per_sec_{0U};
    std::uint64_t excluded_upload_bytes_per_sec_{0U};
    std::vector<bridge::ConnectedAppDto> observed_connected_apps_;
    void emit_diagnostic(bool is_error, std::string_view message) noexcept;
};

} // namespace nfm::host::lifecycle

#endif // NETWORK_FLOW_MONITOR_HOST_LIFECYCLE_COLLECTOR_COMPANION_H
