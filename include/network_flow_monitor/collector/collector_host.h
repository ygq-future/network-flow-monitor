#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_HOST_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_HOST_H

#include "network_flow_monitor/collector/windows_etw_session.h"
#include "network_flow_monitor/ipc/ipc_server.h"
#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <stop_token>

namespace nfm::collector {

enum class CollectorHostState : std::uint8_t {
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
};

enum class CollectorHostError : std::uint8_t {
    None,
    InvalidState,
    SubscriptionObserverFailed,
    EtwStartFailed,
    IpcStartFailed,
    PublisherFailed,
    EtwStopFailed,
    IpcStopFailed,
};

struct CollectorHostResult {
    CollectorHostError error{CollectorHostError::None};
    std::uint32_t source_error{};

    [[nodiscard]] bool ok() const noexcept {
        return error == CollectorHostError::None;
    }
};

class HostWait {
  public:
    HostWait() = default;
    HostWait(const HostWait&) = delete;
    HostWait& operator=(const HostWait&) = delete;
    HostWait(HostWait&&) = delete;
    HostWait& operator=(HostWait&&) = delete;
    virtual ~HostWait() = default;

    [[nodiscard]] virtual bool Until(
        const std::stop_token& stop_token,
        std::optional<std::chrono::steady_clock::time_point> deadline) noexcept = 0;
};

class ThreadHostWait final : public HostWait {
  public:
    [[nodiscard]] bool Until(
        const std::stop_token& stop_token,
        std::optional<std::chrono::steady_clock::time_point> deadline) noexcept override;
};

class CollectorHost final {
  public:
    CollectorHost(WindowsEtwSession& session, ipc::IpcServer& server,
                  ipc::SnapshotPublisher& publisher, HostWait& wait) noexcept;
    ~CollectorHost() = default;
    CollectorHost(const CollectorHost&) = delete;
    CollectorHost& operator=(const CollectorHost&) = delete;
    CollectorHost(CollectorHost&&) = delete;
    CollectorHost& operator=(CollectorHost&&) = delete;

    [[nodiscard]] CollectorHostResult Start() noexcept;
    [[nodiscard]] CollectorHostResult Run(const std::stop_token& stop_token) noexcept;
    [[nodiscard]] CollectorHostResult Stop() noexcept;
    [[nodiscard]] CollectorHostState state() const noexcept {
        return state_;
    }
    [[nodiscard]] CollectorHostError last_error() const noexcept {
        return last_error_;
    }
    [[nodiscard]] std::uint32_t last_source_error() const noexcept {
        return last_source_error_;
    }

  private:
    [[nodiscard]] CollectorHostResult Fail(CollectorHostError error,
                                           std::uint32_t source_error = 0U) noexcept;

    WindowsEtwSession* session_;
    ipc::IpcServer* server_;
    ipc::SnapshotPublisher* publisher_;
    HostWait* wait_;
    CollectorHostState state_{CollectorHostState::Stopped};
    CollectorHostError last_error_{CollectorHostError::None};
    std::uint32_t last_source_error_{};
};

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_HOST_H
