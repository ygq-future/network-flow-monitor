#ifndef NETWORK_FLOW_MONITOR_IPC_IPC_SERVER_H
#define NETWORK_FLOW_MONITOR_IPC_IPC_SERVER_H

#include "network_flow_monitor/ipc/frame_codec.h"
#include "network_flow_monitor/ipc/named_pipe_transport.h"
#include "network_flow_monitor/ipc/windows_pipe_security.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace nfm::ipc {

inline constexpr std::size_t kMaximumIpcClients = 4U;
inline constexpr auto kHandshakeTimeout = std::chrono::seconds{3};
inline constexpr auto kWriteTimeout = std::chrono::seconds{5};

enum class IpcServerError : std::uint8_t {
    None,
    AlreadyRunning,
    EndpointAlreadyOwned,
    ServerBusy,
    InvalidSnapshot,
    TransportFailure,
    CancellationFailure,
    InternalFailure,
};

struct IpcServerResult {
    IpcServerError error{IpcServerError::None};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept;
};

enum class IpcServerState : std::uint8_t {
    Stopped,
    Starting,
    Running,
    Degraded,
    Failed,
};

struct IpcServerHealth {
    IpcServerState state{IpcServerState::Stopped};
    std::uint8_t active_clients{};
    std::uint32_t last_win32_error{};
    std::uint64_t accepted_connections{};
    std::uint64_t unauthorized_connections{};
    std::uint64_t protocol_errors{};
    std::uint64_t frames_received{};
    std::uint64_t frames_sent{};
    std::uint64_t bytes_received{};
    std::uint64_t bytes_sent{};
    std::uint64_t ticks_published{};
    std::uint64_t snapshots_published{};
    std::uint64_t slow_client_disconnects{};
    std::uint64_t cancellation_failures{};
    std::uint64_t internal_failures{};

    bool operator==(const IpcServerHealth&) const = default;
};

class ClientSessionObserver {
  public:
    ClientSessionObserver() = default;
    ClientSessionObserver(const ClientSessionObserver&) = delete;
    ClientSessionObserver& operator=(const ClientSessionObserver&) = delete;
    ClientSessionObserver(ClientSessionObserver&&) = delete;
    ClientSessionObserver& operator=(ClientSessionObserver&&) = delete;
    virtual ~ClientSessionObserver() = default;

    virtual void ClientConnected(std::uint64_t client_id, const HelloMessage& hello) noexcept = 0;
    virtual void ClientDisconnected(std::uint64_t client_id) noexcept = 0;
};

enum class ClientSessionState : std::uint8_t {
    AwaitingHello,
    Streaming,
    Closing,
    Closed,
};

enum class ClientSessionError : std::uint8_t {
    None,
    InvalidState,
    Protocol,
    Unauthorized,
    InvalidPayload,
    InternalFailure,
};

struct ClientSessionResult {
    ClientSessionError error{ClientSessionError::None};
    ProtocolErrorCode protocol_error{ProtocolErrorCode::None};

    [[nodiscard]] bool ok() const noexcept;
};

class ClientSessionProtocol final {
  public:
    ClientSessionProtocol(std::uint64_t client_id, std::string collector_instance_id);

    [[nodiscard]] ClientSessionResult Receive(FrameView frame, bool authorized,
                                              std::uint32_t actual_client_pid) noexcept;
    [[nodiscard]] ClientSessionResult QueueTrafficTick(const TrafficTickMessage& tick) noexcept;
    [[nodiscard]] ClientSessionResult QueueRankingSnapshot(
        const RankingSnapshotMessage& snapshot) noexcept;
    [[nodiscard]] ClientSessionResult QueueCollectorStatus(
        const CollectorStatusMessage& status) noexcept;

    [[nodiscard]] ClientSessionState state() const noexcept;
    [[nodiscard]] std::uint64_t client_id() const noexcept;
    [[nodiscard]] const HelloMessage& hello_message() const noexcept;
    [[nodiscard]] const OwnedFrame* StartNextFrame() noexcept;
    [[nodiscard]] const OwnedFrame* active_frame() const noexcept;
    [[nodiscard]] ClientSessionResult CompleteActiveFrame() noexcept;
    void Close() noexcept;

  private:
    [[nodiscard]] ClientSessionResult QueueEncodedFrame(FrameEncodeResult encoded) noexcept;

    std::uint64_t client_id_{};
    std::string collector_instance_id_;
    ClientSessionState state_{ClientSessionState::AwaitingHello};
    HelloMessage hello_message_{};
    std::deque<OwnedFrame> queued_frames_;
    std::optional<OwnedFrame> active_frame_;
};

class ClientAuthorizer {
  public:
    ClientAuthorizer() = default;
    ClientAuthorizer(const ClientAuthorizer&) = delete;
    ClientAuthorizer& operator=(const ClientAuthorizer&) = delete;
    ClientAuthorizer(ClientAuthorizer&&) = delete;
    ClientAuthorizer& operator=(ClientAuthorizer&&) = delete;
    virtual ~ClientAuthorizer() = default;

    [[nodiscard]] virtual ClientIdentityResult Verify(
        HANDLE pipe, const AuthorizedPrincipal& principal) noexcept = 0;
};

class WindowsClientAuthorizer final : public ClientAuthorizer {
  public:
    [[nodiscard]] ClientIdentityResult Verify(
        HANDLE pipe, const AuthorizedPrincipal& principal) noexcept override;

  private:
    WindowsPipeSecurityApi security_api_;
};

class IpcServer final {
  public:
    IpcServer(std::string collector_instance_id, AuthorizedPrincipal principal,
              PipeTransportFactory& transport_factory);
    IpcServer(std::string collector_instance_id, AuthorizedPrincipal principal,
              PipeTransportFactory& transport_factory, ClientAuthorizer& authorizer);
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

    [[nodiscard]] IpcServerResult SetSessionObserver(ClientSessionObserver& observer) noexcept;
    [[nodiscard]] IpcServerResult SetSubscriptionObserver(
        ClientSessionObserver& observer) noexcept {
        return SetSessionObserver(observer);
    }
    [[nodiscard]] IpcServerResult Start();
    [[nodiscard]] IpcServerResult PublishTrafficTick(const TrafficTickMessage& tick);
    [[nodiscard]] IpcServerResult PublishRankingSnapshot(const RankingSnapshotMessage& snapshot);
    [[nodiscard]] IpcServerResult PublishCollectorStatus(const CollectorStatusMessage& status);
    [[nodiscard]] IpcServerResult Stop() noexcept;
    [[nodiscard]] IpcServerHealth health() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_IPC_SERVER_H
