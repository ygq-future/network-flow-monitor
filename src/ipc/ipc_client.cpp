#include "network_flow_monitor/ipc/ipc_client.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <span>
#include <utility>

namespace nfm::ipc {
namespace {

constexpr auto kReadTimeout = std::chrono::seconds{5};
constexpr auto kWriteTimeout = std::chrono::seconds{5};

} // namespace

IpcClientResult IpcClientProtocol::Failure(IpcClientError error,
                                           ProtocolErrorCode protocol_error) noexcept {
    if (error == IpcClientError::Incompatible) {
        state_ = IpcClientState::Incompatible;
    } else if (error == IpcClientError::Unauthorized) {
        state_ = IpcClientState::Unauthorized;
    } else {
        state_ = IpcClientState::Failed;
    }
    return {.error = error, .protocol_error = protocol_error};
}

IpcClientResult IpcClientProtocol::BeginHello(std::uint32_t client_pid,
                                              std::string client_name) noexcept {
    try {
        const HelloMessage hello{
            .client_pid = client_pid,
            .protocol_version = kCurrentProtocolVersion,
            .client_name = std::move(client_name),
        };
        const auto encoded = EncodeHello(hello);
        if (!encoded.ok()) {
            return Failure(IpcClientError::Protocol, encoded.error);
        }
        state_ = IpcClientState::Handshaking;
        return {.outbound = encoded.bytes};
    } catch (...) {
        return Failure(IpcClientError::Protocol, ProtocolErrorCode::InternalFailure);
    }
}

IpcClientResult IpcClientProtocol::Receive(FrameView frame) noexcept {
    try {
        if (state_ == IpcClientState::Handshaking) {
            if (frame.header.message_type != MessageType::HelloAck) {
                return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnexpectedMessage);
            }
            const auto decoded = DecodeHelloAck(frame.payload);
            if (!decoded.ok() || !decoded.message.has_value()) {
                return Failure(IpcClientError::MalformedFrame, decoded.error);
            }
            if (decoded.message->protocol_version != kCurrentProtocolVersion) {
                return Failure(IpcClientError::Incompatible, ProtocolErrorCode::VersionMismatch);
            }
            collector_instance_id_ = decoded.message->collector_instance_id;
            state_ = IpcClientState::Streaming;
            return {.message = *decoded.message};
        }

        if (state_ == IpcClientState::Streaming) {
            switch (frame.header.message_type) {
            case MessageType::TrafficTick: {
                const auto decoded = DecodeTrafficTick(frame.payload);
                if (!decoded.ok() || !decoded.message.has_value()) {
                    return Failure(IpcClientError::MalformedFrame, decoded.error);
                }
                return {.message = *decoded.message};
            }
            case MessageType::RankingSnapshot: {
                const auto decoded = DecodeRankingSnapshot(frame.payload);
                if (!decoded.ok() || !decoded.message.has_value()) {
                    return Failure(IpcClientError::MalformedFrame, decoded.error);
                }
                return {.message = *decoded.message};
            }
            case MessageType::CollectorStatus: {
                const auto decoded = DecodeCollectorStatus(frame.payload);
                if (!decoded.ok() || !decoded.message.has_value()) {
                    return Failure(IpcClientError::MalformedFrame, decoded.error);
                }
                return {.message = *decoded.message};
            }
            case MessageType::Hello:
            case MessageType::HelloAck:
            case MessageType::ShutdownRequest:
                return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnexpectedMessage);
            }
            return Failure(IpcClientError::Protocol, ProtocolErrorCode::UnknownMessageType);
        }

        return Failure(IpcClientError::InvalidState);
    } catch (...) {
        return Failure(IpcClientError::Protocol, ProtocolErrorCode::InternalFailure);
    }
}

IpcClientResult IpcClientProtocol::SendShutdown(std::string reason) noexcept {
    try {
        const ShutdownRequestMessage shutdown{.reason = std::move(reason)};
        const auto encoded = EncodeShutdownRequest(shutdown);
        if (!encoded.ok()) {
            return Failure(IpcClientError::Protocol, encoded.error);
        }
        state_ = IpcClientState::Disconnected;
        return {.outbound = encoded.bytes};
    } catch (...) {
        return Failure(IpcClientError::Protocol, ProtocolErrorCode::InternalFailure);
    }
}

void IpcClientProtocol::ResetForReconnect() noexcept {
    state_ = IpcClientState::Disconnected;
}

IpcClient::IpcClient(IpcClientObserver& observer, ReconnectJitter* jitter) noexcept
    : observer_{&observer}, reconnect_policy_{jitter} {}

IpcClient::IpcClient(AuthorizedPrincipal principal, PipeTransportFactory& transport_factory,
                     IpcClientObserver& observer, ReconnectJitter* jitter)
    : observer_{&observer}, reconnect_policy_{jitter}, principal_{std::move(principal)},
      transport_factory_{&transport_factory} {}

IpcClient::~IpcClient() {
    Stop();
}

void IpcClient::SetState(IpcClientState next_state) noexcept {
    state_.store(next_state, std::memory_order_release);
    if (observer_ != nullptr) {
        observer_->StateChanged(next_state);
    }
}

bool IpcClient::Start() noexcept {
    if (transport_factory_ == nullptr || worker_.joinable()) {
        return false;
    }
    worker_ = std::jthread([this](const std::stop_token& token) { WorkerMain(token); });
    return true;
}

void IpcClient::Stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    SetState(IpcClientState::Disconnected);
}

void IpcClient::BeginConnecting() noexcept {
    SetState(IpcClientState::Connecting);
}

IpcClientResult IpcClient::BeginHello(std::uint32_t client_pid, std::string client_name) {
    auto result = protocol_.BeginHello(client_pid, std::move(client_name));
    if (result.ok()) {
        SetState(IpcClientState::Handshaking);
    } else {
        SetState(protocol_.state());
    }
    return result;
}

IpcClientResult IpcClient::Receive(FrameView frame) {
    auto result = protocol_.Receive(frame);
    if (!result.ok()) {
        SetState(protocol_.state());
    } else if (protocol_.state() == IpcClientState::Streaming &&
               state_.load(std::memory_order_acquire) != IpcClientState::Streaming) {
        SetState(IpcClientState::Streaming);
    }
    return result;
}

void IpcClient::TransportFailed(IpcReconnectReason /*reason*/) noexcept {
    protocol_.ResetForReconnect();
    SetState(IpcClientState::Backoff);
}

void IpcClient::StreamingSucceeded() noexcept {
    reconnect_policy_.Reset();
    SetState(IpcClientState::Streaming);
}

std::chrono::milliseconds IpcClient::next_reconnect_delay() noexcept {
    return reconnect_policy_.NextDelay();
}

void IpcClient::WorkerMain(const std::stop_token& stop_token) noexcept {
    try {
        while (!stop_token.stop_requested()) {
            SetState(IpcClientState::Connecting);
            auto connection_res = transport_factory_->CreateClient(principal_);
            if (!connection_res.ok() || connection_res.connection == nullptr) {
                SetState(IpcClientState::Backoff);
                const auto delay = reconnect_policy_.NextDelay();
                std::this_thread::sleep_for(delay);
                continue;
            }

            auto connection = std::move(connection_res.connection);
            protocol_.ResetForReconnect();
            const auto hello_res = BeginHello(static_cast<std::uint32_t>(GetCurrentProcessId()),
                                              "NetworkFlowMonitor.Ui");
            if (!hello_res.ok()) {
                SetState(IpcClientState::Failed);
                break;
            }

            auto write_res = connection->BeginWriteAll(hello_res.outbound);
            if (write_res.pending) {
                const auto wait = WaitForSingleObject(
                    connection->write_event(),
                    static_cast<DWORD>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(kWriteTimeout)
                            .count()));
                if (wait != WAIT_OBJECT_0) {
                    TransportFailed(IpcReconnectReason::TransportFailure);
                    continue;
                }
                write_res = connection->ContinueWriteAll();
            }
            if (!write_res.ok()) {
                TransportFailed(IpcReconnectReason::TransportFailure);
                continue;
            }

            std::array<std::byte, kFrameHeaderSize> header_buf{};
            std::vector<std::byte> frame_buf;

            while (!stop_token.stop_requested()) {
                auto read_res = connection->BeginReadExact(header_buf);
                if (read_res.pending) {
                    const auto wait = WaitForSingleObject(
                        connection->read_event(),
                        static_cast<DWORD>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(kReadTimeout)
                                .count()));
                    if (wait != WAIT_OBJECT_0) {
                        break;
                    }
                    read_res = connection->ContinueReadExact();
                }
                if (!read_res.ok()) {
                    break;
                }

                const auto header_parse = TryDecodeFrame(header_buf);
                if (header_parse.status != FrameParseStatus::NeedMoreData ||
                    header_parse.required < kFrameHeaderSize ||
                    header_parse.required > kFrameHeaderSize + kMaximumFramePayload) {
                    break;
                }

                const auto total_req = header_parse.required;
                frame_buf.assign(total_req, std::byte{});
                std::ranges::copy(header_buf, frame_buf.begin());

                const auto payload_len = total_req - kFrameHeaderSize;
                if (payload_len > 0U) {
                    auto payload_span = std::span{frame_buf}.subspan(kFrameHeaderSize, payload_len);
                    auto payload_read = connection->BeginReadExact(payload_span);
                    if (payload_read.pending) {
                        const auto wait = WaitForSingleObject(
                            connection->read_event(),
                            static_cast<DWORD>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(kReadTimeout)
                                    .count()));
                        if (wait != WAIT_OBJECT_0) {
                            break;
                        }
                        payload_read = connection->ContinueReadExact();
                    }
                    if (!payload_read.ok()) {
                        break;
                    }
                }

                const auto full_frame = TryDecodeFrame(frame_buf);
                if (full_frame.status != FrameParseStatus::Decoded ||
                    !full_frame.frame.has_value()) {
                    break;
                }

                const auto recv_res = Receive(FrameView{.header = full_frame.frame->header,
                                                        .payload = full_frame.frame->payload});
                if (!recv_res.ok()) {
                    break;
                }

                if (recv_res.message.has_value()) {
                    if (const auto* ack = std::get_if<HelloAckMessage>(&*recv_res.message)) {
                        StreamingSucceeded();
                        if (observer_ != nullptr) {
                            observer_->HelloAckReceived(*ack);
                        }
                    } else if (const auto* tick =
                                   std::get_if<TrafficTickMessage>(&*recv_res.message)) {
                        if (observer_ != nullptr) {
                            observer_->TrafficTickReceived(*tick);
                        }
                    } else if (const auto* snap =
                                   std::get_if<RankingSnapshotMessage>(&*recv_res.message)) {
                        if (observer_ != nullptr) {
                            observer_->RankingSnapshotReceived(*snap);
                        }
                    } else if (const auto* status =
                                   std::get_if<CollectorStatusMessage>(&*recv_res.message)) {
                        if (observer_ != nullptr) {
                            observer_->CollectorStatusReceived(*status);
                        }
                    }
                }
            }

            static_cast<void>(connection->CancelAndDrain());
            if (stop_token.stop_requested()) {
                break;
            }
            TransportFailed(IpcReconnectReason::BrokenPipe);
            const auto delay = reconnect_policy_.NextDelay();
            std::this_thread::sleep_for(delay);
        }
    } catch (...) {
        SetState(IpcClientState::Failed);
    }
}

} // namespace nfm::ipc
