#ifndef NETWORK_FLOW_MONITOR_IPC_IPC_CLIENT_H
#define NETWORK_FLOW_MONITOR_IPC_IPC_CLIENT_H

#include "network_flow_monitor/collector/collector_health.h"
#include "network_flow_monitor/ipc/frame_codec.h"
#include "network_flow_monitor/ipc/named_pipe_transport.h"
#include "network_flow_monitor/ipc/reconnect_policy.h"
#include "network_flow_monitor/ipc/snapshot_codec.h"
#include "network_flow_monitor/ranking/counter_types.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
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
    CatalogOutOfOrder,
    UnknownApplication,
    SnapshotOutOfOrder,
    CounterDecreased,
    TransportFailure,
};

struct ClientSnapshot {
    CollectorInstanceId instance;
    collector::CollectorHealth collector_health;
    IpcServerHealth ipc_health;
    ranking::CounterSnapshot counters;
};

struct IpcClientResult {
    IpcClientError error{IpcClientError::None};
    ProtocolErrorCode protocol_error{ProtocolErrorCode::None};
    std::vector<std::byte> outbound;
    std::optional<ClientSnapshot> snapshot;

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
    virtual void SnapshotReceived(ClientSnapshot snapshot) noexcept = 0;
};

class IpcClientProtocol final {
  public:
    explicit IpcClientProtocol(std::chrono::milliseconds requested_refresh) noexcept;

    [[nodiscard]] IpcClientResult BeginHello(std::uint64_t request_id) noexcept;
    [[nodiscard]] IpcClientResult Receive(FrameView frame) noexcept;
    void ResetForReconnect() noexcept;

    [[nodiscard]] IpcClientState state() const noexcept {
        return state_;
    }
    [[nodiscard]] const CollectorInstanceId& instance() const noexcept {
        return instance_;
    }
    [[nodiscard]] std::uint64_t catalog_sequence() const noexcept {
        return catalog_sequence_;
    }
    [[nodiscard]] bool has_catalog() const noexcept {
        return catalog_sequence_ != 0U;
    }

  private:
    struct CatalogAssembly {
        std::uint64_t sequence{};
        std::uint32_t chunk_count{};
        std::uint32_t next_chunk{};
        bool full_baseline{};
        std::vector<CatalogEntry> entries;
    };

    [[nodiscard]] IpcClientResult ReceiveControl(FrameView frame);
    [[nodiscard]] IpcClientResult ReceiveCatalog(FrameView frame);
    [[nodiscard]] IpcClientResult ReceiveSnapshot(FrameView frame);
    [[nodiscard]] IpcClientResult ApplyCatalogChunk(CatalogChunk chunk);
    [[nodiscard]] IpcClientResult BuildSnapshot(const SnapshotMessage& message);
    [[nodiscard]] IpcClientResult Failure(
        IpcClientError error, ProtocolErrorCode protocol_error = ProtocolErrorCode::None) noexcept;
    [[nodiscard]] static std::string ApplicationId(const CollectorInstanceId& instance,
                                                   ApplicationHandle handle);
    [[nodiscard]] static std::uint64_t NextRequestId(std::uint64_t request_id) noexcept;

    std::chrono::milliseconds requested_refresh_;
    IpcClientState state_{IpcClientState::Disconnected};
    CollectorInstanceId instance_{};
    bool instance_known_{};
    std::uint64_t hello_request_id_{};
    std::uint64_t subscription_request_id_{};
    bool initial_subscription_acknowledged_{};
    std::uint64_t catalog_sequence_{};
    std::unordered_map<std::uint64_t, CatalogEntry> catalog_;
    std::optional<CatalogAssembly> catalog_assembly_;
    std::optional<SnapshotMessage> previous_snapshot_;
};

class IpcClient final {
  public:
    IpcClient(IpcClientObserver& observer, std::chrono::milliseconds requested_refresh,
              ReconnectJitter* jitter = nullptr) noexcept;
    IpcClient(AuthorizedPrincipal principal, PipeTransportFactory& transport_factory,
              IpcClientObserver& observer, std::chrono::milliseconds requested_refresh,
              ReconnectJitter* jitter = nullptr);
    ~IpcClient() = default;
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;
    IpcClient(IpcClient&&) = delete;
    IpcClient& operator=(IpcClient&&) = delete;

    [[nodiscard]] bool Start() noexcept;
    void BeginConnecting() noexcept;
    [[nodiscard]] IpcClientResult BeginHello(std::uint64_t request_id);
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

    IpcClientObserver* observer_;
    IpcClientProtocol protocol_;
    ReconnectPolicy reconnect_policy_;
    AuthorizedPrincipal principal_;
    PipeTransportFactory* transport_factory_{};
    std::jthread worker_;
    std::atomic<IpcClientState> state_{IpcClientState::Disconnected};
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_IPC_CLIENT_H
