#include "network_flow_monitor/ipc/ipc_client.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

namespace nfm::ipc {
namespace {

IpcClientResult ErrorResult(IpcClientError error,
                            ProtocolErrorCode protocol_error = ProtocolErrorCode::None) noexcept {
    return {.error = error, .protocol_error = protocol_error};
}

enum class WorkerFrameResult : std::uint8_t {
    Received,
    Stopped,
    TransportFailure,
    Malformed,
};

struct WorkerFrame {
    WorkerFrameResult result{WorkerFrameResult::TransportFailure};
    std::vector<std::byte> bytes;
};

std::uint16_t ReadWireU16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U);
}

std::uint32_t ReadWireU32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    std::uint32_t result{};
    for (std::size_t index = 0U; index < sizeof(result); ++index) {
        result |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << (index * 8U);
    }
    return result;
}

std::uint64_t ReadWireU64(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    std::uint64_t result{};
    for (std::size_t index = 0U; index < sizeof(result); ++index) {
        result |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << (index * 8U);
    }
    return result;
}

bool DecodeWireHeader(std::span<const std::byte> bytes, FrameHeader& header) noexcept {
    if (bytes.size() != kFrameHeaderSize || ReadWireU32(bytes, 0U) != kFrameMagic ||
        ReadWireU16(bytes, 4U) != kFrameHeaderSize || ReadWireU16(bytes, 10U) != 0U) {
        return false;
    }
    const auto type = ReadWireU16(bytes, 8U);
    if (type < static_cast<std::uint16_t>(MessageType::ClientHello) ||
        type > static_cast<std::uint16_t>(MessageType::ProtocolError)) {
        return false;
    }
    header.protocol_version = ReadWireU16(bytes, 6U);
    header.message_type = static_cast<MessageType>(type);
    header.payload_length = ReadWireU32(bytes, 12U);
    header.request_id = ReadWireU64(bytes, 16U);
    return header.payload_length <= kMaximumFramePayload;
}

bool WaitForPipeRead(NamedPipeConnection& connection, HANDLE event, std::span<std::byte> buffer,
                     const std::stop_token& stop_token) noexcept {
    auto result = connection.BeginReadExact(buffer);
    while (result.ok() && result.pending) {
        if (stop_token.stop_requested()) {
            static_cast<void>(connection.CancelAndDrain());
            return false;
        }
        const auto wait = WaitForSingleObject(event, 50U);
        if (wait == WAIT_TIMEOUT) {
            continue;
        }
        if (wait != WAIT_OBJECT_0) {
            static_cast<void>(connection.CancelAndDrain());
            return false;
        }
        result = connection.ContinueReadExact();
    }
    return result.ok() && !result.pending;
}

bool WaitForPipeWrite(NamedPipeConnection& connection, HANDLE event,
                      std::span<const std::byte> buffer,
                      const std::stop_token& stop_token) noexcept {
    auto result = connection.BeginWriteAll(buffer);
    while (result.ok() && result.pending) {
        if (stop_token.stop_requested()) {
            static_cast<void>(connection.CancelAndDrain());
            return false;
        }
        const auto wait = WaitForSingleObject(event, 50U);
        if (wait == WAIT_TIMEOUT) {
            continue;
        }
        if (wait != WAIT_OBJECT_0) {
            static_cast<void>(connection.CancelAndDrain());
            return false;
        }
        result = connection.ContinueWriteAll();
    }
    return result.ok() && !result.pending;
}

bool WriteWireFrame(NamedPipeConnection& connection, std::span<const std::byte> bytes,
                    const std::stop_token& stop_token) noexcept {
    return WaitForPipeWrite(connection, connection.write_event(), bytes, stop_token);
}

WorkerFrame ReadWireFrame(NamedPipeConnection& connection,
                          const std::stop_token& stop_token) noexcept {
    try {
        std::array<std::byte, kFrameHeaderSize> header_bytes{};
        if (!WaitForPipeRead(connection, connection.read_event(), header_bytes, stop_token)) {
            return {.result = stop_token.stop_requested() ? WorkerFrameResult::Stopped
                                                          : WorkerFrameResult::TransportFailure};
        }
        FrameHeader header;
        if (!DecodeWireHeader(header_bytes, header)) {
            return {.result = WorkerFrameResult::Malformed};
        }
        std::vector<std::byte> bytes(kFrameHeaderSize + header.payload_length);
        std::ranges::copy(header_bytes, bytes.begin());
        auto payload = std::span<std::byte>{bytes}.subspan(kFrameHeaderSize);
        if (!payload.empty() &&
            !WaitForPipeRead(connection, connection.read_event(), payload, stop_token)) {
            return {.result = stop_token.stop_requested() ? WorkerFrameResult::Stopped
                                                          : WorkerFrameResult::TransportFailure};
        }
        const auto parsed = TryDecodeFrame(bytes);
        if (parsed.status != FrameParseStatus::Decoded || !parsed.frame.has_value()) {
            return {.result = WorkerFrameResult::Malformed};
        }
        return {.result = WorkerFrameResult::Received, .bytes = std::move(bytes)};
    } catch (...) {
        return {.result = WorkerFrameResult::Malformed};
    }
}

bool WaitReconnect(std::chrono::milliseconds delay, const std::stop_token& stop_token) noexcept {
    constexpr auto kPoll = std::chrono::milliseconds{25};
    auto remaining = delay;
    while (remaining > std::chrono::milliseconds::zero()) {
        if (stop_token.stop_requested()) {
            return false;
        }
        const auto sleep_for = std::min(remaining, kPoll);
        std::this_thread::sleep_for(sleep_for);
        remaining -= sleep_for;
    }
    return !stop_token.stop_requested();
}

IpcClientError MapProtocolError(ProtocolErrorCode error) noexcept {
    switch (error) {
    case ProtocolErrorCode::Unauthorized:
        return IpcClientError::Unauthorized;
    case ProtocolErrorCode::VersionMismatch:
        return IpcClientError::Incompatible;
    case ProtocolErrorCode::CatalogOutOfOrder:
        return IpcClientError::CatalogOutOfOrder;
    default:
        return IpcClientError::Protocol;
    }
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): value-only constructor has no throwing boundary.
IpcClientProtocol::IpcClientProtocol(
    std::chrono::milliseconds
        requested_refresh) noexcept // NOLINT(bugprone-exception-escape): value-only constructor has
                                    // no throwing boundary.
    : requested_refresh_{requested_refresh} {}

std::uint64_t IpcClientProtocol::NextRequestId(std::uint64_t request_id) noexcept {
    return request_id == std::numeric_limits<std::uint64_t>::max() ? 1U : request_id + 1U;
}

IpcClientResult IpcClientProtocol::Failure(IpcClientError error,
                                           ProtocolErrorCode protocol_error) noexcept {
    if (error == IpcClientError::Unauthorized) {
        state_ = IpcClientState::Unauthorized;
    } else if (error == IpcClientError::Incompatible) {
        state_ = IpcClientState::Incompatible;
    } else if (error != IpcClientError::None) {
        state_ = IpcClientState::Failed;
    }
    return ErrorResult(error, protocol_error);
}

void IpcClientProtocol::ResetForReconnect() noexcept {
    state_ = IpcClientState::Disconnected;
    hello_request_id_ = 0U;
    subscription_request_id_ = 0U;
    initial_subscription_acknowledged_ = false;
    catalog_sequence_ = 0U;
    catalog_.clear();
    catalog_assembly_.reset();
    previous_snapshot_.reset();
}

IpcClientResult IpcClientProtocol::BeginHello(std::uint64_t request_id) noexcept {
    if (request_id == 0U) {
        return Failure(IpcClientError::InvalidState);
    }
    try {
        ResetForReconnect();
        const auto encoded = EncodeClientHello({.minimum_version = kCurrentProtocolVersion,
                                                .maximum_version = kCurrentProtocolVersion,
                                                .requested_refresh = requested_refresh_},
                                               request_id);
        if (!encoded.ok()) {
            return Failure(IpcClientError::Protocol, encoded.error);
        }
        hello_request_id_ = request_id;
        state_ = IpcClientState::Handshaking;
        return {.outbound = encoded.bytes};
    } catch (...) {
        return Failure(IpcClientError::Protocol);
    }
}

IpcClientResult IpcClientProtocol::Receive(FrameView frame) noexcept {
    try {
        switch (frame.header.message_type) {
        case MessageType::ServerHello:
        case MessageType::SubscriptionAccepted:
        case MessageType::ProtocolError:
            return ReceiveControl(frame);
        case MessageType::ApplicationCatalog:
            return ReceiveCatalog(frame);
        case MessageType::Snapshot:
            return ReceiveSnapshot(frame);
        case MessageType::CollectorStatus: {
            if (state_ != IpcClientState::Streaming) {
                return Failure(IpcClientError::InvalidState);
            }
            const auto decoded = DecodeCollectorStatus(frame.payload);
            if (!decoded.ok()) {
                return Failure(IpcClientError::MalformedFrame);
            }
            return {};
        }
        default:
            return Failure(IpcClientError::MalformedFrame, ProtocolErrorCode::UnknownMessageType);
        }
    } catch (...) {
        return Failure(IpcClientError::Protocol);
    }
}

IpcClientResult IpcClientProtocol::ReceiveControl(FrameView frame) {
    auto decoded = DecodeControlMessage(frame, MessageDirection::ServerToClient);
    if (!decoded.ok() || !decoded.message.has_value()) {
        return Failure(IpcClientError::MalformedFrame, decoded.error);
    }

    if (const auto* protocol_error = std::get_if<ProtocolErrorMessage>(&*decoded.message)) {
        return Failure(MapProtocolError(protocol_error->code), protocol_error->code);
    }
    if (const auto* server_hello = std::get_if<ServerHello>(&*decoded.message)) {
        if (state_ != IpcClientState::Handshaking || frame.header.request_id != hello_request_id_ ||
            server_hello->selected_version != kCurrentProtocolVersion) {
            return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnexpectedMessage);
        }
        instance_ = server_hello->instance;
        instance_known_ = true;
        catalog_sequence_ = 0U;
        catalog_.clear();
        catalog_assembly_.reset();
        previous_snapshot_.reset();
        initial_subscription_acknowledged_ = false;
        subscription_request_id_ = NextRequestId(hello_request_id_);
        const auto encoded =
            EncodeSubscribe({.requested_refresh = requested_refresh_}, subscription_request_id_);
        if (!encoded.ok()) {
            return Failure(IpcClientError::Protocol, encoded.error);
        }
        return {.outbound = encoded.bytes};
    }
    if (const auto* accepted = std::get_if<SubscriptionAccepted>(&*decoded.message)) {
        if (state_ != IpcClientState::Handshaking || accepted->effective_refresh.count() <= 0) {
            return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnexpectedMessage);
        }
        if (frame.header.request_id == hello_request_id_) {
            if (initial_subscription_acknowledged_) {
                return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnexpectedMessage);
            }
            initial_subscription_acknowledged_ = true;
            return {};
        }
        if (frame.header.request_id != subscription_request_id_) {
            return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnexpectedMessage);
        }
        state_ = IpcClientState::Streaming;
        return {};
    }
    return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnexpectedMessage);
}

IpcClientResult IpcClientProtocol::ReceiveCatalog(FrameView frame) {
    if (state_ != IpcClientState::Streaming) {
        return Failure(IpcClientError::InvalidState);
    }
    auto decoded = DecodeCatalogChunk(frame.payload);
    if (!decoded.ok() || !decoded.chunk.has_value()) {
        return Failure(IpcClientError::MalformedFrame);
    }
    return ApplyCatalogChunk(std::move(*decoded.chunk));
}

IpcClientResult IpcClientProtocol::ApplyCatalogChunk(CatalogChunk chunk) {
    if (chunk.sequence == 0U || chunk.chunk_count == 0U || chunk.chunk_index >= chunk.chunk_count) {
        return Failure(IpcClientError::CatalogOutOfOrder);
    }

    if (!catalog_assembly_.has_value()) {
        if (chunk.chunk_index != 0U) {
            return Failure(IpcClientError::CatalogOutOfOrder);
        }
        if (chunk.full_baseline) {
            if (chunk.entries.empty() && chunk.chunk_count != 1U) {
                return Failure(IpcClientError::CatalogOutOfOrder);
            }
        } else if (catalog_sequence_ == 0U || chunk.sequence != catalog_sequence_ + 1U) {
            return Failure(IpcClientError::CatalogOutOfOrder);
        }
        catalog_assembly_.emplace(CatalogAssembly{.sequence = chunk.sequence,
                                                  .chunk_count = chunk.chunk_count,
                                                  .next_chunk = 0U,
                                                  .full_baseline = chunk.full_baseline});
        catalog_assembly_->entries.reserve(chunk.entries.size());
    }

    auto& assembly = *catalog_assembly_;
    if (assembly.sequence != chunk.sequence || assembly.chunk_count != chunk.chunk_count ||
        assembly.full_baseline != chunk.full_baseline || assembly.next_chunk != chunk.chunk_index) {
        catalog_assembly_.reset();
        return Failure(IpcClientError::CatalogOutOfOrder);
    }
    try {
        assembly.entries.insert(assembly.entries.end(), chunk.entries.begin(), chunk.entries.end());
    } catch (...) {
        catalog_assembly_.reset();
        return Failure(IpcClientError::Protocol);
    }
    ++assembly.next_chunk;
    if (assembly.next_chunk != assembly.chunk_count) {
        return {};
    }

    try {
        auto candidate =
            assembly.full_baseline ? std::unordered_map<std::uint64_t, CatalogEntry>{} : catalog_;
        std::unordered_set<std::uint64_t> changed_handles;
        for (const auto& entry : assembly.entries) {
            if (entry.handle.value == 0U || !changed_handles.insert(entry.handle.value).second) {
                catalog_assembly_.reset();
                return Failure(IpcClientError::CatalogOutOfOrder);
            }
            if (assembly.full_baseline) {
                if (entry.removed || entry.metadata_revision == 0U) {
                    catalog_assembly_.reset();
                    return Failure(IpcClientError::CatalogOutOfOrder);
                }
                candidate.emplace(entry.handle.value, entry);
                continue;
            }
            if (entry.removed) {
                if (candidate.erase(entry.handle.value) == 0U) {
                    catalog_assembly_.reset();
                    return Failure(IpcClientError::UnknownApplication);
                }
                continue;
            }
            if (entry.metadata_revision == 0U) {
                catalog_assembly_.reset();
                return Failure(IpcClientError::CatalogOutOfOrder);
            }
            const auto existing = candidate.find(entry.handle.value);
            if (existing != candidate.end() &&
                entry.metadata_revision <= existing->second.metadata_revision) {
                catalog_assembly_.reset();
                return Failure(IpcClientError::CatalogOutOfOrder);
            }
            candidate.insert_or_assign(entry.handle.value, entry);
        }
        catalog_ = std::move(candidate);
        catalog_sequence_ = assembly.sequence;
        catalog_assembly_.reset();
        return {};
    } catch (...) {
        catalog_assembly_.reset();
        return Failure(IpcClientError::Protocol);
    }
}

IpcClientResult IpcClientProtocol::ReceiveSnapshot(FrameView frame) {
    if (state_ != IpcClientState::Streaming || catalog_sequence_ == 0U) {
        return Failure(IpcClientError::CatalogOutOfOrder, ProtocolErrorCode::CatalogOutOfOrder);
    }
    auto decoded = previous_snapshot_.has_value()
                       ? DecodeSnapshot(frame.payload, *previous_snapshot_)
                       : DecodeSnapshot(frame.payload);
    if (!decoded.ok() || !decoded.message.has_value()) {
        if (decoded.error == SnapshotCodecError::CounterDecreased) {
            return Failure(IpcClientError::CounterDecreased);
        }
        if (decoded.error == SnapshotCodecError::InvalidSnapshotOrder) {
            return Failure(IpcClientError::SnapshotOutOfOrder);
        }
        return Failure(IpcClientError::MalformedFrame);
    }
    return BuildSnapshot(*decoded.message);
}

IpcClientResult IpcClientProtocol::BuildSnapshot(const SnapshotMessage& message) {
    if (message.required_catalog_sequence != catalog_sequence_) {
        return Failure(IpcClientError::CatalogOutOfOrder, ProtocolErrorCode::CatalogOutOfOrder);
    }
    try {
        const auto maximum_microseconds =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (message.uptime_microseconds > maximum_microseconds) {
            return Failure(IpcClientError::SnapshotOutOfOrder);
        }

        ranking::CounterSnapshot counters;
        const auto uptime = std::chrono::microseconds{message.uptime_microseconds};
        counters.captured_at = std::chrono::steady_clock::time_point{
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(uptime)};
        counters.applications.reserve(message.applications.size());
        for (const auto& counter : message.applications) {
            const auto found = catalog_.find(counter.handle.value);
            if (found == catalog_.end()) {
                return Failure(IpcClientError::UnknownApplication);
            }
            counters.applications.push_back({.id = ApplicationId(instance_, counter.handle),
                                             .metadata = found->second.metadata,
                                             .upload_bytes = counter.upload_bytes,
                                             .download_bytes = counter.download_bytes});
        }
        previous_snapshot_ = message;
        return {.snapshot = ClientSnapshot{.instance = instance_,
                                           .collector_health = message.collector_health,
                                           .ipc_health = message.ipc_health,
                                           .counters = std::move(counters)}};
    } catch (...) {
        return Failure(IpcClientError::Protocol);
    }
}

std::string IpcClientProtocol::ApplicationId(const CollectorInstanceId& instance,
                                             ApplicationHandle handle) {
    static constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                                 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result{"ipc:"};
    result.reserve(4U + 32U + 1U + 20U);
    for (std::size_t index = 0U; index < instance.bytes.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            result.push_back('-');
        }
        const auto byte = std::to_integer<std::uint8_t>(instance.bytes.at(index));
        result.push_back(digits.at(byte >> 4U));
        result.push_back(digits.at(byte & 0x0FU));
    }
    result.push_back(':');
    result += std::to_string(handle.value);
    return result;
}

IpcClient::IpcClient(
    IpcClientObserver& observer, std::chrono::milliseconds requested_refresh,
    ReconnectJitter* jitter) noexcept // NOLINT(bugprone-exception-escape): observer and protocol
                                      // boundaries are noexcept by contract.
    : observer_{&observer}, protocol_{requested_refresh}, reconnect_policy_{jitter} {}

IpcClient::IpcClient(AuthorizedPrincipal principal, PipeTransportFactory& transport_factory,
                     IpcClientObserver& observer, std::chrono::milliseconds requested_refresh,
                     ReconnectJitter* jitter)
    : observer_{&observer}, protocol_{requested_refresh}, reconnect_policy_{jitter},
      principal_{std::move(principal)}, transport_factory_{&transport_factory} {}

void IpcClient::SetState(IpcClientState state) noexcept {
    const auto previous = state_.exchange(state, std::memory_order_acq_rel);
    if (previous == state) {
        return;
    }
    observer_->StateChanged(state);
}

void IpcClient::BeginConnecting() noexcept {
    protocol_.ResetForReconnect();
    SetState(IpcClientState::Connecting);
}

bool IpcClient::Start() noexcept {
    if (transport_factory_ == nullptr || worker_.joinable()) {
        return false;
    }
    BeginConnecting();
    try {
        worker_ =
            std::jthread([this](const std::stop_token& stop_token) { WorkerMain(stop_token); });
        return true;
    } catch (...) {
        SetState(IpcClientState::Failed);
        return false;
    }
}

IpcClientResult IpcClient::BeginHello(std::uint64_t request_id) {
    const auto current_state = state();
    if (current_state != IpcClientState::Connecting && current_state != IpcClientState::Backoff &&
        current_state != IpcClientState::Disconnected) {
        return ErrorResult(IpcClientError::InvalidState);
    }
    const auto result = protocol_.BeginHello(request_id);
    if (result.ok()) {
        SetState(IpcClientState::Handshaking);
    } else {
        SetState(IpcClientState::Failed);
    }
    return result;
}

IpcClientResult IpcClient::Receive(FrameView frame) {
    const auto result = protocol_.Receive(frame);
    if (!result.ok()) {
        if (result.error == IpcClientError::Unauthorized) {
            SetState(IpcClientState::Unauthorized);
        } else if (result.error == IpcClientError::Incompatible) {
            SetState(IpcClientState::Incompatible);
        } else {
            SetState(IpcClientState::Failed);
        }
        return result;
    }
    if (protocol_.state() == IpcClientState::Streaming) {
        reconnect_policy_.Reset();
        SetState(IpcClientState::Streaming);
    }
    if (result.snapshot.has_value()) {
        observer_->SnapshotReceived(*result.snapshot);
    }
    return result;
}

void IpcClient::TransportFailed(IpcReconnectReason reason) noexcept {
    static_cast<void>(reason);
    const auto current_state = state();
    if (current_state == IpcClientState::Unauthorized ||
        current_state == IpcClientState::Incompatible) {
        return;
    }
    protocol_.ResetForReconnect();
    SetState(IpcClientState::Backoff);
}

void IpcClient::StreamingSucceeded() noexcept {
    reconnect_policy_.Reset();
    SetState(IpcClientState::Streaming);
}

void IpcClient::Stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    protocol_.ResetForReconnect();
    SetState(IpcClientState::Disconnected);
}

std::chrono::milliseconds IpcClient::next_reconnect_delay() noexcept {
    return reconnect_policy_.NextDelay();
}

void IpcClient::WorkerMain(const std::stop_token& stop_token) noexcept {
    try {
        std::uint64_t request_id = 1U;
        while (!stop_token.stop_requested()) {
            SetState(IpcClientState::Connecting);
            auto created = transport_factory_->CreateClient(principal_);
            if (!created.ok() || created.connection == nullptr) {
                TransportFailed(IpcReconnectReason::EndpointMissing);
                if (!WaitReconnect(next_reconnect_delay(), stop_token)) {
                    break;
                }
                continue;
            }
            auto connection = std::move(created.connection);
            const auto hello = protocol_.BeginHello(request_id);
            request_id =
                request_id == std::numeric_limits<std::uint64_t>::max() ? 1U : request_id + 1U;
            if (!hello.ok() || !WriteWireFrame(*connection, hello.outbound, stop_token)) {
                TransportFailed(IpcReconnectReason::BrokenPipe);
                if (!WaitReconnect(next_reconnect_delay(), stop_token)) {
                    break;
                }
                continue;
            }

            bool terminal = false;
            while (!stop_token.stop_requested()) {
                const auto received = ReadWireFrame(*connection, stop_token);
                if (received.result == WorkerFrameResult::Stopped) {
                    break;
                }
                if (received.result == WorkerFrameResult::Malformed) {
                    SetState(IpcClientState::Failed);
                    terminal = true;
                    break;
                }
                if (received.result != WorkerFrameResult::Received) {
                    break;
                }
                const auto parsed = TryDecodeFrame(received.bytes);
                if (parsed.status != FrameParseStatus::Decoded || !parsed.frame.has_value()) {
                    SetState(IpcClientState::Failed);
                    terminal = true;
                    break;
                }
                const auto result =
                    Receive({.header = parsed.frame->header, .payload = parsed.frame->payload});
                if (!result.ok()) {
                    terminal = state() == IpcClientState::Unauthorized ||
                               state() == IpcClientState::Incompatible ||
                               state() == IpcClientState::Failed;
                    break;
                }
                if (!result.outbound.empty() &&
                    !WriteWireFrame(*connection, result.outbound, stop_token)) {
                    break;
                }
            }
            static_cast<void>(connection->CancelAndDrain());
            if (stop_token.stop_requested() || terminal) {
                break;
            }
            TransportFailed(IpcReconnectReason::BrokenPipe);
            if (!WaitReconnect(next_reconnect_delay(), stop_token)) {
                break;
            }
        }
    } catch (...) {
        SetState(IpcClientState::Failed);
    }
}

} // namespace nfm::ipc
