#ifndef NETWORK_FLOW_MONITOR_IPC_IPC_CLIENT_H
#define NETWORK_FLOW_MONITOR_IPC_IPC_CLIENT_H

#include "network_flow_monitor/ipc/frame_codec.h"
#include "network_flow_monitor/ipc/named_pipe_transport.h"
#include "network_flow_monitor/ipc/reconnect_policy.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace nfm::ipc {

enum class IpcClientState : std::uint8_t {
    Disconnected,
    Connecting,
    Handshaking,
    Streaming,
    Backoff,
    Unauthorized,
    Incompatible,
    Failed,
};

enum class IpcReconnectReason : std::uint8_t {
    EndpointMissing,
    Busy,
    BrokenPipe,
    CollectorRestart,
    TransportFailure,
};

enum class IpcClientError : std::uint8_t {
    None,
    InvalidState,
    MalformedFrame,
    Protocol,
    Unauthorized,
    Incompatible,
    TransportFailure,
};

using InboundMessage = std::variant<HelloAckMessage, TrafficTickMessage, RankingSnapshotMessage,
                                    CollectorStatusMessage>;

struct IpcClientResult {
    IpcClientError error{IpcClientError::None};
    ProtocolErrorCode protocol_error{ProtocolErrorCode::None};
    std::vector<std::byte> outbound;
    std::optional<InboundMessage> message;

    [[nodiscard]] bool ok() const noexcept {
        return error == IpcClientError::None;
    }
};

class IpcClientObserver {
  public:
    IpcClientObserver() = default;
    IpcClientObserver(const IpcClientObserver&) = delete;
    IpcClientObserver& operator=(const IpcClientObserver&) = delete;
    IpcClientObserver(IpcClientObserver&&) = delete;
    IpcClientObserver& operator=(IpcClientObserver&&) = delete;
    virtual ~IpcClientObserver() = default;

    virtual void StateChanged(IpcClientState state) noexcept = 0;
    virtual void HelloAckReceived(HelloAckMessage ack) noexcept = 0;
    virtual void TrafficTickReceived(TrafficTickMessage tick) noexcept = 0;
    virtual void RankingSnapshotReceived(RankingSnapshotMessage snapshot) noexcept = 0;
    virtual void CollectorStatusReceived(CollectorStatusMessage status) noexcept = 0;
};

class IpcClientProtocol final {
  public:
    IpcClientProtocol() noexcept = default;

    [[nodiscard]] IpcClientResult BeginHello(std::uint32_t client_pid,
                                             std::string client_name) noexcept;
    [[nodiscard]] IpcClientResult Receive(FrameView frame) noexcept;
    [[nodiscard]] IpcClientResult SendShutdown(std::string reason) noexcept;
    void ResetForReconnect() noexcept;

    [[nodiscard]] IpcClientState state() const noexcept {
        return state_;
    }
    [[nodiscard]] const std::string& collector_instance_id() const noexcept {
        return collector_instance_id_;
    }

  private:
    [[nodiscard]] IpcClientResult Failure(
        IpcClientError error, ProtocolErrorCode protocol_error = ProtocolErrorCode::None) noexcept;

    IpcClientState state_{IpcClientState::Disconnected};
    std::string collector_instance_id_;
};

class IpcClient final {
  public:
    IpcClient(IpcClientObserver& observer, ReconnectJitter* jitter = nullptr) noexcept;
    IpcClient(AuthorizedPrincipal principal, PipeTransportFactory& transport_factory,
              IpcClientObserver& observer, ReconnectJitter* jitter = nullptr);
    ~IpcClient();
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;
    IpcClient(IpcClient&&) = delete;
    IpcClient& operator=(IpcClient&&) = delete;

    [[nodiscard]] bool Start() noexcept;
    void BeginConnecting() noexcept;
    [[nodiscard]] IpcClientResult BeginHello(std::uint32_t client_pid, std::string client_name);
    [[nodiscard]] IpcClientResult Receive(FrameView frame);
    void TransportFailed(IpcReconnectReason reason) noexcept;
    void StreamingSucceeded() noexcept;
    void Stop() noexcept;

    [[nodiscard]] IpcClientState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::chrono::milliseconds next_reconnect_delay() noexcept;
    [[nodiscard]] IpcClientProtocol& protocol() noexcept {
        return protocol_;
    }

  private:
    void WorkerMain(const std::stop_token& stop_token) noexcept;
    void SetState(IpcClientState state) noexcept;

    IpcClientObserver* observer_{};
    IpcClientProtocol protocol_;
    ReconnectPolicy reconnect_policy_;
    AuthorizedPrincipal principal_;
    PipeTransportFactory* transport_factory_{};
    std::jthread worker_;
    std::atomic<IpcClientState> state_{IpcClientState::Disconnected};
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_IPC_CLIENT_H
