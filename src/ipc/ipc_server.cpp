#include "network_flow_monitor/ipc/ipc_server.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <ranges>
#include <thread>
#include <utility>

namespace nfm::ipc {
namespace {

class KernelHandle final {
  public:
    KernelHandle() noexcept = default;
    explicit KernelHandle(HANDLE handle) noexcept : handle_{handle} {}
    ~KernelHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    KernelHandle(const KernelHandle&) = delete;
    KernelHandle& operator=(const KernelHandle&) = delete;
    KernelHandle(KernelHandle&& other) noexcept : handle_{other.handle_} {
        other.handle_ = nullptr;
    }
    KernelHandle& operator=(KernelHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE handle_{};
};

KernelHandle MakeEvent(std::uint32_t& error) noexcept {
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        error = GetLastError();
    } else {
        error = ERROR_SUCCESS;
    }
    return KernelHandle{event};
}

bool IsWaitSuccess(DWORD result, DWORD index) noexcept {
    return result == WAIT_OBJECT_0 + index;
}

IpcServerResult ServerError(IpcServerError error, std::uint32_t win32_error = ERROR_SUCCESS) {
    return {.error = error, .win32_error = win32_error};
}

OwnedFrame MakeFrame(MessageType type, std::vector<std::byte> payload) {
    return OwnedFrame{.header = {.protocol_version = kCurrentProtocolVersion,
                                 .message_type = type,
                                 .request_id = 0U,
                                 .payload_length = static_cast<std::uint32_t>(payload.size())},
                      .payload = std::move(payload)};
}

bool IsValidCatalogSequence(const PublishedSnapshot& snapshot) noexcept {
    return snapshot.message.required_catalog_sequence != 0U &&
           snapshot.message.snapshot_sequence != 0U;
}

} // namespace

bool IpcServerResult::ok() const noexcept {
    return error == IpcServerError::None;
}

bool ClientSessionResult::ok() const noexcept {
    return error == ClientSessionError::None;
}

ClientSessionProtocol::ClientSessionProtocol(std::uint64_t client_id, CollectorInstanceId instance)
    : client_id_{client_id}, instance_{instance} {}

ClientSessionResult ClientSessionProtocol::ProtocolFailure(ProtocolErrorCode error,
                                                           std::uint64_t request_id) noexcept {
    if (!protocol_error_sent_) {
        ProtocolErrorMessage message{.code = error,
                                     .retry_disposition = 0U,
                                     .server_minimum_version = kCurrentProtocolVersion,
                                     .server_maximum_version = kCurrentProtocolVersion,
                                     .win32_error = 0U};
        auto encoded = EncodeProtocolError(message, request_id);
        if (!encoded.ok()) {
            state_ = ClientSessionState::Closed;
            return {.error = ClientSessionError::InternalFailure, .protocol_error = encoded.error};
        }
        const auto queued = QueueWireFrame(std::move(encoded.bytes));
        if (!queued.ok()) {
            state_ = ClientSessionState::Closed;
            return queued;
        }
        protocol_error_sent_ = true;
    }
    state_ = ClientSessionState::Closing;
    return {.error = ClientSessionError::Protocol, .protocol_error = error};
}

ClientSessionResult ClientSessionProtocol::QueueWireFrame(std::vector<std::byte> wire) noexcept {
    auto parsed = TryDecodeFrame(wire);
    if (parsed.status != FrameParseStatus::Decoded || !parsed.frame.has_value()) {
        return {.error = ClientSessionError::InternalFailure,
                .protocol_error = ProtocolErrorCode::MalformedPayload};
    }
    try {
        queued_frames_.push_back(QueuedFrame{.frame = std::move(*parsed.frame),
                                             .catalog_sequence = 0U,
                                             .final_catalog_chunk = false});
        return {};
    } catch (...) {
        return {.error = ClientSessionError::InternalFailure,
                .protocol_error = ProtocolErrorCode::InternalFailure};
    }
}

ClientSessionResult ClientSessionProtocol::QueueEncodedFrame(
    MessageType type, std::vector<std::byte> payload) noexcept {
    if (payload.size() > kMaximumFramePayload) {
        return {.error = ClientSessionError::InvalidSnapshot,
                .protocol_error = ProtocolErrorCode::PayloadTooLarge};
    }
    try {
        queued_frames_.push_back(QueuedFrame{.frame = MakeFrame(type, std::move(payload)),
                                             .catalog_sequence = 0U,
                                             .final_catalog_chunk = false});
        return {};
    } catch (...) {
        return {.error = ClientSessionError::InternalFailure,
                .protocol_error = ProtocolErrorCode::InternalFailure};
    }
}

ClientSessionResult ClientSessionProtocol::Receive(FrameView frame, bool authorized) noexcept {
    if (state_ == ClientSessionState::Closing || state_ == ClientSessionState::Closed) {
        return {.error = ClientSessionError::InvalidState,
                .protocol_error = ProtocolErrorCode::UnexpectedMessage};
    }
    const auto decoded = DecodeControlMessage(frame, MessageDirection::ClientToServer);
    if (!decoded.ok()) {
        return ProtocolFailure(decoded.error, frame.header.request_id);
    }

    if (const auto* hello = std::get_if<ClientHello>(&*decoded.message)) {
        if (state_ != ClientSessionState::AwaitingHello) {
            return ProtocolFailure(ProtocolErrorCode::UnexpectedMessage, frame.header.request_id);
        }
        if (!authorized) {
            return ProtocolFailure(ProtocolErrorCode::Unauthorized, frame.header.request_id);
        }
        if (hello->minimum_version > kCurrentProtocolVersion ||
            hello->maximum_version < kCurrentProtocolVersion) {
            return ProtocolFailure(ProtocolErrorCode::VersionMismatch, frame.header.request_id);
        }
        try {
            const ServerHello server_hello{.selected_version = kCurrentProtocolVersion,
                                           .maximum_frame_payload = kMaximumFramePayload,
                                           .instance = instance_};
            auto encoded_hello = EncodeServerHello(server_hello, frame.header.request_id);
            auto encoded_subscription = EncodeSubscriptionAccepted(
                SubscriptionAccepted{.effective_refresh = hello->requested_refresh},
                frame.header.request_id);
            if (!encoded_hello.ok() || !encoded_subscription.ok()) {
                state_ = ClientSessionState::Closed;
                return {.error = ClientSessionError::InternalFailure,
                        .protocol_error = ProtocolErrorCode::InternalFailure};
            }
            const auto hello_queued = QueueWireFrame(std::move(encoded_hello.bytes));
            const auto subscription_queued = QueueWireFrame(std::move(encoded_subscription.bytes));
            if (!hello_queued.ok() || !subscription_queued.ok()) {
                state_ = ClientSessionState::Closed;
                return {.error = ClientSessionError::InternalFailure,
                        .protocol_error = ProtocolErrorCode::InternalFailure};
            }
            refresh_interval_ = hello->requested_refresh;
            state_ = ClientSessionState::Streaming;
            return {};
        } catch (...) {
            state_ = ClientSessionState::Closed;
            return {.error = ClientSessionError::InternalFailure,
                    .protocol_error = ProtocolErrorCode::InternalFailure};
        }
    }

    if (const auto* subscribe = std::get_if<Subscribe>(&*decoded.message)) {
        if (state_ != ClientSessionState::Streaming) {
            return ProtocolFailure(ProtocolErrorCode::UnexpectedMessage, frame.header.request_id);
        }
        try {
            auto encoded = EncodeSubscriptionAccepted(
                SubscriptionAccepted{.effective_refresh = subscribe->requested_refresh},
                frame.header.request_id);
            if (!encoded.ok()) {
                state_ = ClientSessionState::Closed;
                return {.error = ClientSessionError::InternalFailure,
                        .protocol_error = ProtocolErrorCode::InternalFailure};
            }
            refresh_interval_ = subscribe->requested_refresh;
            return QueueWireFrame(std::move(encoded.bytes));
        } catch (...) {
            state_ = ClientSessionState::Closed;
            return {.error = ClientSessionError::InternalFailure,
                    .protocol_error = ProtocolErrorCode::InternalFailure};
        }
    }
    return ProtocolFailure(ProtocolErrorCode::UnexpectedMessage, frame.header.request_id);
}

ClientSessionResult ClientSessionProtocol::BuildSnapshotFrames(
    const PublishedSnapshot& snapshot) noexcept {
    if (!IsValidCatalogSequence(snapshot) ||
        snapshot.message.required_catalog_sequence < known_catalog_sequence_) {
        return {.error = ClientSessionError::InvalidSnapshot,
                .protocol_error = ProtocolErrorCode::CatalogOutOfOrder};
    }

    try {
        std::deque<QueuedFrame> candidate;
        const auto sequence = snapshot.message.required_catalog_sequence;
        if (sequence != known_catalog_sequence_) {
            const bool full_baseline =
                known_catalog_sequence_ == 0U || sequence != known_catalog_sequence_ + 1U;
            const auto& entries = full_baseline ? snapshot.full_catalog : snapshot.latest_changes;
            const auto catalog = EncodeCatalog(sequence, full_baseline, entries);
            if (!catalog.ok() || catalog.payloads.empty()) {
                return {.error = ClientSessionError::InvalidSnapshot,
                        .protocol_error = ProtocolErrorCode::CatalogOutOfOrder};
            }
            for (std::size_t index = 0U; index < catalog.payloads.size(); ++index) {
                candidate.push_back(QueuedFrame{
                    .frame = MakeFrame(MessageType::ApplicationCatalog, catalog.payloads.at(index)),
                    .catalog_sequence = sequence,
                    .final_catalog_chunk = index + 1U == catalog.payloads.size()});
            }
        }

        const auto encoded_snapshot = EncodeSnapshot(snapshot.message);
        if (!encoded_snapshot.ok()) {
            return {.error = ClientSessionError::InvalidSnapshot,
                    .protocol_error = ProtocolErrorCode::MalformedPayload};
        }
        candidate.push_back(
            QueuedFrame{.frame = MakeFrame(MessageType::Snapshot, encoded_snapshot.payload),
                        .catalog_sequence = 0U,
                        .final_catalog_chunk = false});
        for (auto& item : candidate) {
            queued_frames_.push_back(std::move(item));
        }
        return {};
    } catch (...) {
        return {.error = ClientSessionError::InternalFailure,
                .protocol_error = ProtocolErrorCode::InternalFailure};
    }
}

ClientSessionResult ClientSessionProtocol::BuildStatusFrame(
    const CollectorStatusMessage& status) noexcept {
    try {
        auto encoded = EncodeCollectorStatus(status);
        if (!encoded.ok()) {
            return {.error = ClientSessionError::InvalidSnapshot,
                    .protocol_error = ProtocolErrorCode::MalformedPayload};
        }
        return QueueEncodedFrame(MessageType::CollectorStatus, std::move(encoded.payload));
    } catch (...) {
        return {.error = ClientSessionError::InternalFailure,
                .protocol_error = ProtocolErrorCode::InternalFailure};
    }
}

ClientSessionResult ClientSessionProtocol::QueueSnapshot(
    const std::shared_ptr<const PublishedSnapshot>& snapshot) noexcept {
    if (state_ != ClientSessionState::Streaming || snapshot == nullptr) {
        return {.error = ClientSessionError::InvalidState,
                .protocol_error = ProtocolErrorCode::UnexpectedMessage};
    }
    if (active_frame_.has_value() || !queued_frames_.empty() || pending_snapshot_ != nullptr) {
        try {
            if (active_frame_.has_value()) {
                std::erase_if(queued_frames_, [](const QueuedFrame& item) {
                    return item.frame.header.message_type == MessageType::ApplicationCatalog ||
                           item.frame.header.message_type == MessageType::Snapshot;
                });
            }
            pending_snapshot_ = snapshot;
            return {};
        } catch (...) {
            return {.error = ClientSessionError::InternalFailure,
                    .protocol_error = ProtocolErrorCode::InternalFailure};
        }
    }
    return BuildSnapshotFrames(*snapshot);
}

ClientSessionResult ClientSessionProtocol::QueueStatus(
    const std::shared_ptr<const CollectorStatusMessage>& status) noexcept {
    if (state_ != ClientSessionState::Streaming || status == nullptr) {
        return {.error = ClientSessionError::InvalidState,
                .protocol_error = ProtocolErrorCode::UnexpectedMessage};
    }
    if (active_frame_.has_value() || !queued_frames_.empty() || pending_snapshot_ != nullptr ||
        pending_status_ != nullptr) {
        try {
            pending_status_ = status;
            return {};
        } catch (...) {
            return {.error = ClientSessionError::InternalFailure,
                    .protocol_error = ProtocolErrorCode::InternalFailure};
        }
    }
    return BuildStatusFrame(*status);
}

ClientSessionState ClientSessionProtocol::state() const noexcept {
    return state_;
}

std::uint64_t ClientSessionProtocol::client_id() const noexcept {
    return client_id_;
}

std::chrono::milliseconds ClientSessionProtocol::refresh_interval() const noexcept {
    return refresh_interval_;
}

const OwnedFrame* ClientSessionProtocol::StartNextFrame() noexcept {
    if (active_frame_.has_value()) {
        return &active_frame_->frame;
    }
    if (queued_frames_.empty()) {
        PromotePendingSnapshot();
        if (queued_frames_.empty()) {
            return nullptr;
        }
    }
    try {
        active_frame_.emplace(std::move(queued_frames_.front()));
        queued_frames_.pop_front();
        return &active_frame_->frame;
    } catch (...) {
        state_ = ClientSessionState::Closing;
        return nullptr;
    }
}

const OwnedFrame* ClientSessionProtocol::active_frame() const noexcept {
    return active_frame_.has_value() ? &active_frame_->frame : nullptr;
}

void ClientSessionProtocol::PromotePendingSnapshot() noexcept {
    if (pending_snapshot_ != nullptr) {
        auto snapshot = std::move(pending_snapshot_);
        const auto result = BuildSnapshotFrames(*snapshot);
        if (!result.ok()) {
            state_ = ClientSessionState::Closing;
        }
        return;
    }
    if (pending_status_ != nullptr) {
        auto status = std::move(pending_status_);
        const auto result = BuildStatusFrame(*status);
        if (!result.ok()) {
            state_ = ClientSessionState::Closing;
        }
    }
}

ClientSessionResult ClientSessionProtocol::CompleteActiveFrame() noexcept {
    if (!active_frame_.has_value()) {
        return {.error = ClientSessionError::InvalidState,
                .protocol_error = ProtocolErrorCode::UnexpectedMessage};
    }
    if (active_frame_->final_catalog_chunk) {
        known_catalog_sequence_ = active_frame_->catalog_sequence;
    }
    active_frame_.reset();
    return {};
}

void ClientSessionProtocol::Close() noexcept {
    active_frame_.reset();
    queued_frames_.clear();
    pending_snapshot_.reset();
    pending_status_.reset();
    state_ = ClientSessionState::Closed;
}

ClientIdentityResult WindowsClientAuthorizer::Verify(
    HANDLE pipe, const AuthorizedPrincipal& principal) noexcept {
    return VerifyPipeClient(pipe, principal, security_api_);
}

class IpcServer::Impl final {
  public:
    struct Session final {
        Session(Impl& owner_in, std::uint64_t id,
                std::unique_ptr<NamedPipeConnection> connection_in)
            : owner{&owner_in}, id{id}, connection{std::move(connection_in)},
              protocol{id, owner_in.instance} {}

        Impl* owner;
        std::uint64_t id;
        std::unique_ptr<NamedPipeConnection> connection;
        ClientSessionProtocol protocol;
        std::mutex protocol_mutex;
        KernelHandle wake_event;
        std::thread thread;
        std::atomic_bool finished{false};
        bool disconnected_callback_sent{};
    };

    Impl(CollectorInstanceId instance_in, AuthorizedPrincipal principal_in,
         PipeTransportFactory& factory_in, ClientAuthorizer* authorizer_in)
        : instance{instance_in}, principal{std::move(principal_in)}, factory{&factory_in},
          authorizer{authorizer_in != nullptr ? authorizer_in : &default_authorizer} {
        health.state = IpcServerState::Stopped;
    }

    ~Impl() {
        static_cast<void>(Stop());
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    CollectorInstanceId instance;
    AuthorizedPrincipal principal;
    PipeTransportFactory* factory;
    WindowsClientAuthorizer default_authorizer;
    ClientAuthorizer* authorizer;
    mutable std::mutex mutex;
    ClientSessionObserver* observer{};
    IpcServerHealth health{};
    KernelHandle stop_event;
    std::thread accept_thread;
    std::vector<std::shared_ptr<Session>> sessions;
    std::uint64_t next_client_id{1U};

    [[nodiscard]] bool IsStopping() const noexcept {
        return stop_event.valid() && WaitForSingleObject(stop_event.get(), 0U) == WAIT_OBJECT_0;
    }

    void SetFailure(IpcServerError error, std::uint32_t win32_error) noexcept {
        std::scoped_lock lock{mutex};
        health.last_win32_error = win32_error;
        health.state = error == IpcServerError::CancellationFailure ? IpcServerState::Degraded
                                                                    : IpcServerState::Failed;
        if (error == IpcServerError::CancellationFailure) {
            ++health.cancellation_failures;
        } else if (error == IpcServerError::InternalFailure) {
            ++health.internal_failures;
        }
    }

    void MarkSlowClient() noexcept {
        std::scoped_lock lock{mutex};
        ++health.slow_client_disconnects;
    }

    void RecordSessionIoFailure(const PipeIoResult& result) noexcept {
        std::scoped_lock lock{mutex};
        health.last_win32_error = result.win32_error;
        if (result.error == PipeIoError::Timeout) {
            ++health.slow_client_disconnects;
        } else if (result.error != PipeIoError::Disconnected) {
            ++health.internal_failures;
            health.state = IpcServerState::Degraded;
        }
    }

    void RecordSessionProtocolFailure() noexcept {
        std::scoped_lock lock{mutex};
        ++health.protocol_errors;
        health.last_win32_error = ERROR_INVALID_DATA;
    }

    void RecordSessionFrameReceived(std::size_t bytes) noexcept {
        std::scoped_lock lock{mutex};
        ++health.frames_received;
        health.bytes_received += bytes;
    }

    void NotifySubscription(std::uint64_t id, std::chrono::milliseconds refresh) noexcept {
        ClientSessionObserver* target{};
        {
            std::scoped_lock lock{mutex};
            target = observer;
        }
        if (target != nullptr) {
            target->SubscriptionChanged({.client_id = id, .refresh_interval = refresh});
        }
    }

    void NotifyDisconnected(Session& session) noexcept {
        ClientSessionObserver* target{};
        {
            std::scoped_lock lock{mutex};
            if (session.disconnected_callback_sent) {
                return;
            }
            session.disconnected_callback_sent = true;
            if (health.active_clients > 0U) {
                --health.active_clients;
            }
            target = observer;
        }
        if (target != nullptr) {
            target->ClientDisconnected(session.id);
        }
    }

    void SignalSession(Session& session) noexcept {
        if (session.wake_event.valid()) {
            static_cast<void>(SetEvent(session.wake_event.get()));
        }
    }

    [[nodiscard]] PipeIoResult WaitForRead(Session& session, std::span<std::byte> destination,
                                           DWORD timeout_ms) noexcept {
        auto result = session.connection->BeginReadExact(destination);
        if (!result.pending) {
            return result;
        }
        const auto started_at = GetTickCount64();
        for (;;) {
            DWORD wait_timeout = timeout_ms;
            if (timeout_ms != INFINITE) {
                const auto elapsed = GetTickCount64() - started_at;
                if (elapsed >= timeout_ms) {
                    const auto cancelled = session.connection->CancelAndDrain();
                    return cancelled.ok() ? PipeIoResult{.error = PipeIoError::Timeout} : cancelled;
                }
                wait_timeout = timeout_ms - static_cast<DWORD>(elapsed);
            }
            const std::array handles{stop_event.get(), session.connection->read_event()};
            const auto wait = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                     handles.data(), FALSE, wait_timeout);
            if (IsWaitSuccess(wait, 0U)) {
                return session.connection->CancelAndDrain();
            }
            if (wait == WAIT_TIMEOUT) {
                const auto cancelled = session.connection->CancelAndDrain();
                return cancelled.ok() ? PipeIoResult{.error = PipeIoError::Timeout} : cancelled;
            }
            if (!IsWaitSuccess(wait, 1U)) {
                const auto cancelled = session.connection->CancelAndDrain();
                return cancelled.ok() ? PipeIoResult{.error = PipeIoError::WindowsFailure,
                                                     .win32_error = GetLastError()}
                                      : cancelled;
            }
            result = session.connection->ContinueReadExact();
            if (!result.pending || !result.ok()) {
                return result;
            }
        }
    }

    [[nodiscard]] PipeIoResult WaitForWrite(Session& session,
                                            std::span<const std::byte> source) noexcept {
        auto result = session.connection->BeginWriteAll(source);
        if (!result.pending) {
            return result;
        }
        const auto timeout_ms = static_cast<DWORD>(
            std::chrono::duration_cast<std::chrono::milliseconds>(kWriteTimeout).count());
        const auto started_at = GetTickCount64();
        for (;;) {
            const auto elapsed = GetTickCount64() - started_at;
            if (elapsed >= timeout_ms) {
                const auto cancelled = session.connection->CancelAndDrain();
                return cancelled.ok() ? PipeIoResult{.error = PipeIoError::Timeout} : cancelled;
            }
            const std::array handles{stop_event.get(), session.connection->write_event()};
            const auto wait =
                WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                       timeout_ms - static_cast<DWORD>(elapsed));
            if (IsWaitSuccess(wait, 0U)) {
                return session.connection->CancelAndDrain();
            }
            if (wait == WAIT_TIMEOUT) {
                const auto cancelled = session.connection->CancelAndDrain();
                return cancelled.ok() ? PipeIoResult{.error = PipeIoError::Timeout} : cancelled;
            }
            if (!IsWaitSuccess(wait, 1U)) {
                const auto cancelled = session.connection->CancelAndDrain();
                return cancelled.ok() ? PipeIoResult{.error = PipeIoError::WindowsFailure,
                                                     .win32_error = GetLastError()}
                                      : cancelled;
            }
            result = session.connection->ContinueWriteAll();
            if (!result.pending || !result.ok()) {
                return result;
            }
        }
    }

    [[nodiscard]] PipeIoResult SendQueuedFrames(Session& session) noexcept {
        for (;;) {
            std::vector<std::byte> wire;
            try {
                std::scoped_lock protocol_lock{session.protocol_mutex};
                const auto* frame = session.protocol.StartNextFrame();
                if (frame == nullptr) {
                    return {};
                }
                const auto encoded =
                    EncodeFrame(FrameView{.header = frame->header, .payload = frame->payload});
                if (!encoded.ok()) {
                    return {.error = PipeIoError::WindowsFailure,
                            .win32_error = ERROR_INVALID_DATA};
                }
                wire = encoded.bytes;
            } catch (...) {
                return {.error = PipeIoError::WindowsFailure,
                        .win32_error = ERROR_NOT_ENOUGH_MEMORY};
            }
            const auto result = WaitForWrite(session, wire);
            if (!result.ok()) {
                if (result.error == PipeIoError::Timeout) {
                    MarkSlowClient();
                }
                return result;
            }
            ClientSessionResult completed;
            {
                std::scoped_lock protocol_lock{session.protocol_mutex};
                completed = session.protocol.CompleteActiveFrame();
            }
            if (!completed.ok()) {
                return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_INVALID_DATA};
            }
            {
                std::scoped_lock lock{mutex};
                ++health.frames_sent;
                health.bytes_sent += wire.size();
            }
        }
    }

    void SessionLoop(const std::shared_ptr<Session>& session) noexcept {
        try {
            std::vector<std::byte> bytes;
            bool authorized = false;
            bool got_hello = false;
            bool read_active = false;
            std::array<std::byte, kFrameHeaderSize> header{};
            for (;;) {
                if (!read_active) {
                    auto started = session->connection->BeginReadExact(header);
                    if (!started.ok()) {
                        RecordSessionIoFailure(started);
                        break;
                    }
                    read_active = started.pending;
                    if (!read_active) {
                        const auto parsed = TryDecodeFrame(header);
                        if (parsed.status != FrameParseStatus::NeedMoreData) {
                            RecordSessionProtocolFailure();
                            break;
                        }
                        bytes.assign(parsed.required, std::byte{});
                        std::ranges::copy(header, bytes.begin());
                        if (!bytes.empty()) {
                            auto payload = std::span{bytes}.subspan(kFrameHeaderSize);
                            if (!payload.empty()) {
                                const auto payload_result = WaitForRead(
                                    *session, payload,
                                    got_hello
                                        ? INFINITE
                                        : static_cast<DWORD>(
                                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  kHandshakeTimeout)
                                                  .count()));
                                if (!payload_result.ok()) {
                                    RecordSessionIoFailure(payload_result);
                                    break;
                                }
                            }
                        }
                        const auto decoded = TryDecodeFrame(bytes);
                        if (decoded.status != FrameParseStatus::Decoded || !decoded.frame) {
                            RecordSessionProtocolFailure();
                            break;
                        }
                        RecordSessionFrameReceived(bytes.size());
                        if (!got_hello) {
                            const auto identity =
                                authorizer->Verify(session->connection->native_handle(), principal);
                            authorized = identity.authorized();
                        }
                        ClientSessionState previous_state;
                        ClientSessionState current_state;
                        std::chrono::milliseconds previous_refresh{};
                        std::chrono::milliseconds current_refresh{};
                        ClientSessionResult received;
                        {
                            std::scoped_lock protocol_lock{session->protocol_mutex};
                            previous_state = session->protocol.state();
                            previous_refresh = session->protocol.refresh_interval();
                            received = session->protocol.Receive(
                                FrameView{.header = decoded.frame->header,
                                          .payload = decoded.frame->payload},
                                authorized);
                            current_state = session->protocol.state();
                            current_refresh = session->protocol.refresh_interval();
                        }
                        if (received.ok() && current_state == ClientSessionState::Streaming &&
                            (previous_state != current_state ||
                             previous_refresh != current_refresh)) {
                            NotifySubscription(session->id, current_refresh);
                        }
                        if (!received.ok() &&
                            received.error == ClientSessionError::InternalFailure) {
                            SetFailure(IpcServerError::InternalFailure, ERROR_NOT_ENOUGH_MEMORY);
                        } else if (!received.ok()) {
                            RecordSessionProtocolFailure();
                        }
                        got_hello =
                            got_hello || previous_state == ClientSessionState::AwaitingHello;
                        const auto sent = SendQueuedFrames(*session);
                        if (!sent.ok() || current_state == ClientSessionState::Closing ||
                            current_state == ClientSessionState::Closed) {
                            if (!sent.ok()) {
                                RecordSessionIoFailure(sent);
                            }
                            break;
                        }
                        header.fill(std::byte{});
                        continue;
                    }
                }

                const std::array handles{stop_event.get(), session->connection->read_event(),
                                         session->wake_event.get()};
                const auto wait_timeout =
                    got_hello
                        ? INFINITE
                        : static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 kHandshakeTimeout)
                                                 .count());
                const auto wait = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                         handles.data(), FALSE, wait_timeout);
                if (IsWaitSuccess(wait, 0U)) {
                    break;
                }
                if (wait == WAIT_TIMEOUT) {
                    RecordSessionIoFailure(PipeIoResult{.error = PipeIoError::Timeout});
                    break;
                }
                if (IsWaitSuccess(wait, 2U)) {
                    static_cast<void>(ResetEvent(session->wake_event.get()));
                    const auto sent = SendQueuedFrames(*session);
                    if (!sent.ok()) {
                        RecordSessionIoFailure(sent);
                        break;
                    }
                    continue;
                }
                if (!IsWaitSuccess(wait, 1U)) {
                    SetFailure(IpcServerError::TransportFailure, GetLastError());
                    break;
                }
                const auto completed = session->connection->ContinueReadExact();
                read_active = false;
                if (!completed.ok()) {
                    RecordSessionIoFailure(completed);
                    break;
                }
                const auto parsed = TryDecodeFrame(header);
                if (parsed.status != FrameParseStatus::NeedMoreData ||
                    parsed.required < kFrameHeaderSize ||
                    parsed.required > kFrameHeaderSize + kMaximumFramePayload) {
                    RecordSessionProtocolFailure();
                    break;
                }
                try {
                    bytes.assign(parsed.required, std::byte{});
                    std::ranges::copy(header, bytes.begin());
                } catch (...) {
                    break;
                }
                const auto payload_result = WaitForRead(
                    *session, std::span{bytes}.subspan(kFrameHeaderSize),
                    got_hello
                        ? INFINITE
                        : static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 kHandshakeTimeout)
                                                 .count()));
                if (!payload_result.ok()) {
                    RecordSessionIoFailure(payload_result);
                    break;
                }
                const auto decoded = TryDecodeFrame(bytes);
                if (decoded.status != FrameParseStatus::Decoded || !decoded.frame) {
                    RecordSessionProtocolFailure();
                    break;
                }
                RecordSessionFrameReceived(bytes.size());
                if (!got_hello) {
                    const auto identity =
                        authorizer->Verify(session->connection->native_handle(), principal);
                    authorized = identity.authorized();
                }
                ClientSessionState previous_state;
                ClientSessionState current_state;
                std::chrono::milliseconds previous_refresh{};
                std::chrono::milliseconds current_refresh{};
                ClientSessionResult received;
                {
                    std::scoped_lock protocol_lock{session->protocol_mutex};
                    previous_state = session->protocol.state();
                    previous_refresh = session->protocol.refresh_interval();
                    received =
                        session->protocol.Receive(FrameView{.header = decoded.frame->header,
                                                            .payload = decoded.frame->payload},
                                                  authorized);
                    current_state = session->protocol.state();
                    current_refresh = session->protocol.refresh_interval();
                }
                if (received.ok() && current_state == ClientSessionState::Streaming &&
                    (previous_state != current_state || previous_refresh != current_refresh)) {
                    NotifySubscription(session->id, current_refresh);
                }
                got_hello = got_hello || previous_state == ClientSessionState::AwaitingHello;
                const auto sent = SendQueuedFrames(*session);
                if (!sent.ok() || current_state == ClientSessionState::Closing ||
                    current_state == ClientSessionState::Closed) {
                    if (!sent.ok()) {
                        RecordSessionIoFailure(sent);
                    } else if (current_state == ClientSessionState::Closing ||
                               current_state == ClientSessionState::Closed) {
                        RecordSessionProtocolFailure();
                    }
                    break;
                }
                header.fill(std::byte{});
            }
            static_cast<void>(session->connection->CancelAndDrain());
            {
                std::scoped_lock protocol_lock{session->protocol_mutex};
                session->protocol.Close();
            }
            NotifyDisconnected(*session);
            session->finished.store(true);
        } catch (...) {
            SetFailure(IpcServerError::InternalFailure, ERROR_UNHANDLED_EXCEPTION);
            static_cast<void>(session->connection->CancelAndDrain());
            {
                std::scoped_lock protocol_lock{session->protocol_mutex};
                session->protocol.Close();
            }
            NotifyDisconnected(*session);
            session->finished.store(true);
        }
    }

    void RejectBusy(std::unique_ptr<NamedPipeConnection> connection) noexcept {
        if (connection == nullptr) {
            return;
        }
        const auto encoded = EncodeProtocolError(
            ProtocolErrorMessage{.code = ProtocolErrorCode::ServerBusy,
                                 .retry_disposition = 1U,
                                 .server_minimum_version = kCurrentProtocolVersion,
                                 .server_maximum_version = kCurrentProtocolVersion,
                                 .win32_error = ERROR_PIPE_BUSY},
            0U);
        if (encoded.ok()) {
            const auto wire = EncodeFrame(FrameView{
                .header = {.protocol_version = kCurrentProtocolVersion,
                           .message_type = MessageType::ProtocolError,
                           .request_id = 0U,
                           .payload_length = static_cast<std::uint32_t>(encoded.bytes.size())},
                .payload = encoded.bytes});
            if (wire.ok()) {
                auto result = connection->BeginWriteAll(wire.bytes);
                if (result.pending) {
                    const std::array handles{stop_event.get(), connection->write_event()};
                    if (IsWaitSuccess(WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                             handles.data(), FALSE, 1000U),
                                      1U)) {
                        result = connection->ContinueWriteAll();
                    }
                }
                static_cast<void>(result);
            }
        }
        static_cast<void>(connection->CancelAndDrain());
    }

    void ReapFinishedSessions() noexcept {
        std::array<std::shared_ptr<Session>, kMaximumIpcClients> finished{};
        std::size_t finished_count{};
        {
            std::scoped_lock lock{mutex};
            for (auto iterator = sessions.begin(); iterator != sessions.end();) {
                if (*iterator == nullptr || !(*iterator)->finished.load()) {
                    ++iterator;
                    continue;
                }
                if (finished_count < finished.size()) {
                    finished.at(finished_count++) = std::move(*iterator);
                    iterator = sessions.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        }
        for (std::size_t index = 0U; index < finished_count; ++index) {
            if (finished.at(index)->thread.joinable()) {
                finished.at(index)->thread.join();
            }
        }
    }

    void AcceptLoop() noexcept {
        try {
            std::unique_ptr<NamedPipeConnection> pending = std::move(initial_connection);
            for (;;) {
                ReapFinishedSessions();
                if (IsStopping()) {
                    if (pending != nullptr) {
                        static_cast<void>(pending->CancelAndDrain());
                    }
                    break;
                }
                if (pending == nullptr) {
                    auto created = factory->CreateServer(principal, false);
                    if (!created.ok()) {
                        if (created.error == PipeIoError::Busy) {
                            const auto wait = WaitForSingleObject(
                                stop_event.get(), static_cast<DWORD>(kPipeBusyWait.count()));
                            if (wait == WAIT_OBJECT_0) {
                                break;
                            }
                            continue;
                        }
                        SetFailure(IpcServerError::TransportFailure, created.win32_error);
                        break;
                    }
                    pending = std::move(created.connection);
                }
                const auto started = pending->BeginConnect();
                if (!started.ok()) {
                    SetFailure(IpcServerError::TransportFailure, started.win32_error);
                    pending.reset();
                    continue;
                }
                if (started.pending) {
                    const std::array handles{stop_event.get(), pending->connect_event()};
                    const auto wait = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                             handles.data(), FALSE, INFINITE);
                    if (IsWaitSuccess(wait, 0U)) {
                        static_cast<void>(pending->CancelAndDrain());
                        break;
                    }
                    if (!IsWaitSuccess(wait, 1U)) {
                        SetFailure(IpcServerError::TransportFailure, GetLastError());
                        static_cast<void>(pending->CancelAndDrain());
                        pending.reset();
                        continue;
                    }
                    const auto completed = pending->ContinueConnect();
                    if (!completed.ok()) {
                        SetFailure(IpcServerError::TransportFailure, completed.win32_error);
                        pending.reset();
                        continue;
                    }
                }

                std::shared_ptr<Session> session;
                bool busy{};
                std::uint32_t session_event_error{};
                {
                    std::scoped_lock lock{mutex};
                    const auto active = std::ranges::count_if(sessions, [](const auto& item) {
                        return item != nullptr && !item->finished.load();
                    });
                    if (active >= kMaximumIpcClients) {
                        busy = true;
                    } else {
                        std::uint32_t event_error{};
                        auto wake = MakeEvent(event_error);
                        if (!wake.valid()) {
                            session_event_error = event_error;
                        } else {
                            auto connection = std::exchange(pending, nullptr);
                            session = std::make_shared<Session>(*this, next_client_id++,
                                                                std::move(connection));
                            session->wake_event = std::move(wake);
                            sessions.push_back(session);
                            ++health.accepted_connections;
                            health.active_clients = static_cast<std::uint8_t>(active + 1U);
                        }
                    }
                }
                if (session_event_error != 0U) {
                    SetFailure(IpcServerError::InternalFailure, session_event_error);
                    static_cast<void>(pending->CancelAndDrain());
                    pending.reset();
                    continue;
                }
                if (busy) {
                    auto connection = std::exchange(pending, nullptr);
                    RejectBusy(std::move(connection));
                } else if (session != nullptr) {
                    session->thread = std::thread([this, session] { SessionLoop(session); });
                }
            }
        } catch (...) {
            SetFailure(IpcServerError::InternalFailure, ERROR_UNHANDLED_EXCEPTION);
        }
    }

    IpcServerResult SetObserver(ClientSessionObserver& target) noexcept {
        std::scoped_lock lock{mutex};
        if (health.state != IpcServerState::Stopped) {
            return ServerError(IpcServerError::AlreadyRunning);
        }
        observer = &target;
        return {};
    }

    IpcServerResult Start() {
        std::scoped_lock lock{mutex};
        if (health.state != IpcServerState::Stopped) {
            return ServerError(IpcServerError::AlreadyRunning);
        }
        std::uint32_t event_error{};
        auto stop = MakeEvent(event_error);
        if (!stop.valid()) {
            return ServerError(IpcServerError::InternalFailure, event_error);
        }
        auto created = factory->CreateServer(principal, true);
        if (!created.ok()) {
            if (created.error == PipeIoError::Busy) {
                return ServerError(IpcServerError::EndpointAlreadyOwned, created.win32_error);
            }
            return ServerError(IpcServerError::TransportFailure, created.win32_error);
        }
        stop_event = std::move(stop);
        initial_connection = std::move(created.connection);
        health = {};
        health.state = IpcServerState::Running;
        try {
            accept_thread = std::thread([this] { AcceptLoop(); });
        } catch (...) {
            health.state = IpcServerState::Failed;
            static_cast<void>(SetEvent(stop_event.get()));
            return ServerError(IpcServerError::InternalFailure, ERROR_NOT_ENOUGH_MEMORY);
        }
        return {};
    }

    std::unique_ptr<NamedPipeConnection> initial_connection;

    IpcServerResult PublishSnapshot(const std::shared_ptr<const PublishedSnapshot>& snapshot) {
        if (snapshot == nullptr || !IsValidCatalogSequence(*snapshot)) {
            return ServerError(IpcServerError::InvalidSnapshot);
        }
        std::vector<std::shared_ptr<Session>> current;
        {
            std::scoped_lock lock{mutex};
            if (health.state != IpcServerState::Running &&
                health.state != IpcServerState::Degraded) {
                return ServerError(IpcServerError::AlreadyRunning);
            }
            for (const auto& session : sessions) {
                if (session == nullptr || session->finished.load()) {
                    continue;
                }
                const bool selected =
                    snapshot->recipient_client_ids.empty() ||
                    std::ranges::find(snapshot->recipient_client_ids, session->id) !=
                        snapshot->recipient_client_ids.end();
                if (selected) {
                    current.push_back(session);
                }
            }
            ++health.snapshots_captured;
        }
        for (const auto& session : current) {
            ClientSessionResult queued;
            {
                std::scoped_lock protocol_lock{session->protocol_mutex};
                queued = session->protocol.QueueSnapshot(snapshot);
            }
            if (!queued.ok()) {
                return ServerError(IpcServerError::InvalidSnapshot);
            }
            SignalSession(*session);
        }
        {
            std::scoped_lock lock{mutex};
            ++health.snapshots_published;
        }
        return {};
    }

    IpcServerResult PublishCollectorStatus(
        const std::shared_ptr<const CollectorStatusMessage>& status) {
        if (status == nullptr) {
            return ServerError(IpcServerError::InvalidSnapshot);
        }
        std::vector<std::shared_ptr<Session>> current;
        {
            std::scoped_lock lock{mutex};
            if (health.state != IpcServerState::Running &&
                health.state != IpcServerState::Degraded) {
                return ServerError(IpcServerError::AlreadyRunning);
            }
            for (const auto& session : sessions) {
                if (session != nullptr && !session->finished.load()) {
                    current.push_back(session);
                }
            }
        }
        for (const auto& session : current) {
            ClientSessionResult queued;
            {
                std::scoped_lock protocol_lock{session->protocol_mutex};
                queued = session->protocol.QueueStatus(status);
            }
            if (!queued.ok()) {
                return ServerError(IpcServerError::InvalidSnapshot);
            }
            SignalSession(*session);
        }
        return {};
    }

    IpcServerResult Stop() noexcept {
        std::thread accept;
        std::vector<std::shared_ptr<Session>> current;
        {
            std::scoped_lock lock{mutex};
            if (health.state == IpcServerState::Stopped) {
                return {};
            }
            if (stop_event.valid()) {
                static_cast<void>(SetEvent(stop_event.get()));
            }
            accept = std::move(accept_thread);
            current.swap(sessions);
        }
        if (accept.joinable()) {
            accept.join();
        }
        for (const auto& session : current) {
            SignalSession(*session);
        }
        for (const auto& session : current) {
            if (session->thread.joinable()) {
                session->thread.join();
            }
        }
        {
            std::scoped_lock lock{mutex};
            sessions.clear();
            stop_event = KernelHandle{};
            health.active_clients = 0U;
            health.state = IpcServerState::Stopped;
        }
        return {};
    }

    [[nodiscard]] IpcServerHealth Health() const noexcept {
        std::scoped_lock lock{mutex};
        return health;
    }
};

IpcServer::IpcServer(CollectorInstanceId instance, AuthorizedPrincipal principal,
                     PipeTransportFactory& transport_factory)
    : impl_{std::make_unique<Impl>(instance, std::move(principal), transport_factory, nullptr)} {}

IpcServer::IpcServer(CollectorInstanceId instance, AuthorizedPrincipal principal,
                     PipeTransportFactory& transport_factory, ClientAuthorizer& authorizer)
    : impl_{
          std::make_unique<Impl>(instance, std::move(principal), transport_factory, &authorizer)} {}

IpcServer::~IpcServer() = default;

IpcServerResult IpcServer::SetSubscriptionObserver(ClientSessionObserver& observer) noexcept {
    return impl_->SetObserver(observer);
}

IpcServerResult IpcServer::Start() {
    return impl_->Start();
}

IpcServerResult IpcServer::Publish(
    std::shared_ptr<const PublishedSnapshot>
        snapshot) { // NOLINT(performance-unnecessary-value-param): public sink API owns the shared
                    // snapshot reference.
    return impl_->PublishSnapshot(snapshot);
}

IpcServerResult IpcServer::PublishStatus(
    std::shared_ptr<const CollectorStatusMessage>
        status) { // NOLINT(performance-unnecessary-value-param): public sink API owns the shared
                  // status reference.
    return impl_->PublishCollectorStatus(status);
}

IpcServerResult IpcServer::Stop() noexcept {
    return impl_->Stop();
}

IpcServerHealth IpcServer::health() const noexcept {
    return impl_->Health();
}

} // namespace nfm::ipc
