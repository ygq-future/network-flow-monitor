#ifndef NETWORK_FLOW_MONITOR_IPC_IPC_SERVER_H
#define NETWORK_FLOW_MONITOR_IPC_IPC_SERVER_H

#include "network_flow_monitor/ipc/frame_codec.h"
#include "network_flow_monitor/ipc/named_pipe_transport.h"
#include "network_flow_monitor/ipc/snapshot_codec.h"
#include "network_flow_monitor/ipc/windows_pipe_security.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace nfm::ipc {

inline constexpr std::size_t kMaximumIpcClients = 4U;
inline constexpr auto kHandshakeTimeout = std::chrono::seconds{3};
inline constexpr auto kWriteTimeout = std::chrono::seconds{5};

struct ClientSubscription {
    std::uint64_t client_id{};
    std::chrono::milliseconds refresh_interval{};
};

struct PublishedSnapshot {
    SnapshotMessage message;
    std::vector<CatalogEntry> latest_changes;
    std::vector<CatalogEntry> full_catalog;
    // Empty means broadcast. A publisher can restrict delivery to clients whose
    // refresh deadlines have elapsed without changing the wire payload.
    std::vector<std::uint64_t> recipient_client_ids;
};

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

class ClientSessionObserver {
  public:
    ClientSessionObserver() = default;
    ClientSessionObserver(const ClientSessionObserver&) = delete;
    ClientSessionObserver& operator=(const ClientSessionObserver&) = delete;
    ClientSessionObserver(ClientSessionObserver&&) = delete;
    ClientSessionObserver& operator=(ClientSessionObserver&&) = delete;
    virtual ~ClientSessionObserver() = default;

    virtual void SubscriptionChanged(ClientSubscription subscription) noexcept = 0;
    virtual void ClientDisconnected(std::uint64_t client_id) noexcept = 0;
};

enum class ClientSessionState : std::uint8_t {
    AwaitingHello,
    AwaitingAuthorization,
    Streaming,
    Closing,
    Closed,
};

enum class ClientSessionError : std::uint8_t {
    None,
    InvalidState,
    Protocol,
    Unauthorized,
    InvalidSnapshot,
    InternalFailure,
};

struct ClientSessionResult {
    ClientSessionError error{ClientSessionError::None};
    ProtocolErrorCode protocol_error{ProtocolErrorCode::None};

    [[nodiscard]] bool ok() const noexcept;
};

// Pure protocol and mailbox state. It owns no Windows handles and is used by the
// worker and by deterministic tests alike.
class ClientSessionProtocol final {
  public:
    ClientSessionProtocol(std::uint64_t client_id, CollectorInstanceId instance);

    [[nodiscard]] ClientSessionResult Receive(FrameView frame, bool authorized) noexcept;
    [[nodiscard]] ClientSessionResult QueueSnapshot(
        const std::shared_ptr<const PublishedSnapshot>& snapshot) noexcept;
    [[nodiscard]] ClientSessionResult QueueStatus(
        const std::shared_ptr<const CollectorStatusMessage>& status) noexcept;

    [[nodiscard]] ClientSessionState state() const noexcept;
    [[nodiscard]] std::uint64_t client_id() const noexcept;
    [[nodiscard]] std::chrono::milliseconds refresh_interval() const noexcept;
    [[nodiscard]] const OwnedFrame* StartNextFrame() noexcept;
    [[nodiscard]] const OwnedFrame* active_frame() const noexcept;
    [[nodiscard]] ClientSessionResult CompleteActiveFrame() noexcept;
    void Close() noexcept;

  private:
    struct QueuedFrame {
        OwnedFrame frame;
        std::uint64_t catalog_sequence{};
        bool final_catalog_chunk{};
    };

    [[nodiscard]] ClientSessionResult ProtocolFailure(ProtocolErrorCode error,
                                                      std::uint64_t request_id) noexcept;
    [[nodiscard]] ClientSessionResult QueueWireFrame(std::vector<std::byte> wire) noexcept;
    [[nodiscard]] ClientSessionResult QueueEncodedFrame(MessageType type,
                                                        std::vector<std::byte> payload) noexcept;
    [[nodiscard]] ClientSessionResult BuildSnapshotFrames(
        const PublishedSnapshot& snapshot) noexcept;
    [[nodiscard]] ClientSessionResult BuildStatusFrame(
        const CollectorStatusMessage& status) noexcept;
    void PromotePendingSnapshot() noexcept;

    std::uint64_t client_id_{};
    CollectorInstanceId instance_{};
    ClientSessionState state_{ClientSessionState::AwaitingHello};
    std::chrono::milliseconds refresh_interval_{};
    std::uint64_t known_catalog_sequence_{};
    std::deque<QueuedFrame> queued_frames_;
    std::optional<QueuedFrame> active_frame_;
    std::shared_ptr<const PublishedSnapshot> pending_snapshot_;
    std::shared_ptr<const CollectorStatusMessage> pending_status_;
    bool protocol_error_sent_{};
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
    IpcServer(CollectorInstanceId instance, AuthorizedPrincipal principal,
              PipeTransportFactory& transport_factory);
    IpcServer(CollectorInstanceId instance, AuthorizedPrincipal principal,
              PipeTransportFactory& transport_factory, ClientAuthorizer& authorizer);
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

    [[nodiscard]] IpcServerResult SetSubscriptionObserver(ClientSessionObserver& observer) noexcept;
    [[nodiscard]] IpcServerResult Start();
    [[nodiscard]] IpcServerResult Publish(std::shared_ptr<const PublishedSnapshot> snapshot);
    [[nodiscard]] IpcServerResult PublishStatus(
        std::shared_ptr<const CollectorStatusMessage> status);
    [[nodiscard]] IpcServerResult Stop() noexcept;
    [[nodiscard]] IpcServerHealth health() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_IPC_SERVER_H
