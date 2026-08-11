#include "network_flow_monitor/ipc/frame_codec.h"

#include "network_flow_monitor/config/app_config.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace nfm::ipc {
namespace {

constexpr std::size_t kMagicOffset = 0U;
constexpr std::size_t kHeaderSizeOffset = 4U;
constexpr std::size_t kProtocolVersionOffset = 6U;
constexpr std::size_t kMessageTypeOffset = 8U;
constexpr std::size_t kFlagsOffset = 10U;
constexpr std::size_t kPayloadLengthOffset = 12U;
constexpr std::size_t kRequestIdOffset = 16U;

constexpr std::size_t kClientHelloPayloadSize = 8U;
constexpr std::size_t kServerHelloPayloadSize = 24U;
constexpr std::size_t kSubscriptionPayloadSize = 4U;
constexpr std::size_t kProtocolErrorPayloadSize = 12U;
static_assert(std::numeric_limits<std::size_t>::max() - kFrameHeaderSize >= kMaximumFramePayload);

void AppendU16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>(value & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::byte>& output, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        output.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void AppendU64(std::vector<std::byte>& output, std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        output.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

std::uint16_t ReadU16(std::span<const std::byte> input, std::size_t offset) noexcept {
    const auto bytes = input.subspan(offset, 2U);
    const auto first = std::to_integer<std::uint8_t>(bytes.front());
    const auto second = std::to_integer<std::uint8_t>(bytes.back());
    return static_cast<std::uint16_t>(first) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(second) << 8U);
}

std::uint32_t ReadU32(std::span<const std::byte> input, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    std::size_t shift = 0U;
    for (const auto byte : input.subspan(offset, 4U)) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(byte)) << shift;
        shift += 8U;
    }
    return value;
}

std::uint64_t ReadU64(std::span<const std::byte> input, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    std::size_t shift = 0U;
    for (const auto byte : input.subspan(offset, 8U)) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte)) << shift;
        shift += 8U;
    }
    return value;
}

bool IsKnownMessageType(MessageType type) noexcept {
    switch (type) {
    case MessageType::ClientHello:
    case MessageType::ServerHello:
    case MessageType::Subscribe:
    case MessageType::SubscriptionAccepted:
    case MessageType::ApplicationCatalog:
    case MessageType::Snapshot:
    case MessageType::CollectorStatus:
    case MessageType::ProtocolError:
        return true;
    }
    return false;
}

bool IsKnownMessageTypeValue(std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(MessageType::ClientHello) &&
           value <= static_cast<std::uint16_t>(MessageType::ProtocolError);
}

bool IsKnownProtocolError(ProtocolErrorCode code) noexcept {
    return code >= ProtocolErrorCode::MalformedHeader && code <= ProtocolErrorCode::InternalFailure;
}

bool IsKnownProtocolErrorValue(std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(ProtocolErrorCode::MalformedHeader) &&
           value <= static_cast<std::uint16_t>(ProtocolErrorCode::InternalFailure);
}

bool HasValidRequestId(MessageType type, std::uint64_t request_id) noexcept {
    switch (type) {
    case MessageType::ClientHello:
    case MessageType::ServerHello:
    case MessageType::Subscribe:
    case MessageType::SubscriptionAccepted:
        return request_id != 0U;
    case MessageType::ApplicationCatalog:
    case MessageType::Snapshot:
    case MessageType::CollectorStatus:
        return request_id == 0U;
    case MessageType::ProtocolError:
        return true;
    }
    return false;
}

bool IsAllowedDirection(MessageType type, MessageDirection direction) noexcept {
    if (type == MessageType::ProtocolError) {
        return true;
    }
    if (direction == MessageDirection::ClientToServer) {
        return type == MessageType::ClientHello || type == MessageType::Subscribe;
    }
    return type == MessageType::ServerHello || type == MessageType::SubscriptionAccepted;
}

bool IsValidRefresh(std::chrono::milliseconds value) noexcept {
    return value >= config::kMinimumRefreshInterval && value <= config::kMaximumRefreshInterval;
}

bool IsValidVersionRange(std::uint16_t minimum, std::uint16_t maximum) noexcept {
    return minimum != 0U && maximum != 0U && minimum <= maximum;
}

FrameEncodeResult MakeControlFrame(MessageType type, std::uint16_t protocol_version,
                                   std::uint64_t request_id, std::vector<std::byte> payload) {
    const auto payload_length = static_cast<std::uint32_t>(payload.size());
    return EncodeFrame(FrameView{.header = {.protocol_version = protocol_version,
                                            .message_type = type,
                                            .request_id = request_id,
                                            .payload_length = payload_length},
                                 .payload = payload});
}

ControlDecodeResult RejectControl(ProtocolErrorCode error) {
    return {.error = error};
}

ControlDecodeResult DecodeClientHello(FrameView frame) {
    if (frame.payload.size() != kClientHelloPayloadSize) {
        return RejectControl(ProtocolErrorCode::MalformedPayload);
    }
    const auto minimum = ReadU16(frame.payload, 0U);
    const auto maximum = ReadU16(frame.payload, 2U);
    const auto refresh =
        std::chrono::milliseconds{static_cast<std::int64_t>(ReadU32(frame.payload, 4U))};
    if (!IsValidVersionRange(minimum, maximum) || frame.header.protocol_version != maximum) {
        return RejectControl(ProtocolErrorCode::VersionMismatch);
    }
    if (!IsValidRefresh(refresh)) {
        return RejectControl(ProtocolErrorCode::InvalidSubscription);
    }
    return {.message = ClientHello{.minimum_version = minimum,
                                   .maximum_version = maximum,
                                   .requested_refresh = refresh}};
}

ControlDecodeResult DecodeServerHello(FrameView frame) {
    if (frame.payload.size() != kServerHelloPayloadSize) {
        return RejectControl(ProtocolErrorCode::MalformedPayload);
    }
    const auto selected = ReadU16(frame.payload, 0U);
    if (selected != kCurrentProtocolVersion || frame.header.protocol_version != selected) {
        return RejectControl(ProtocolErrorCode::VersionMismatch);
    }
    if (ReadU16(frame.payload, 2U) != 0U || ReadU32(frame.payload, 4U) != kMaximumFramePayload) {
        return RejectControl(ProtocolErrorCode::MalformedPayload);
    }
    CollectorInstanceId instance;
    std::ranges::copy(frame.payload.subspan(8U, instance.bytes.size()), instance.bytes.begin());
    return {.message = ServerHello{.selected_version = selected,
                                   .maximum_frame_payload = kMaximumFramePayload,
                                   .instance = instance}};
}

template <typename Message> ControlDecodeResult DecodeRefreshMessage(FrameView frame) {
    if (frame.payload.size() != kSubscriptionPayloadSize ||
        frame.header.protocol_version != kCurrentProtocolVersion) {
        return RejectControl(frame.header.protocol_version != kCurrentProtocolVersion
                                 ? ProtocolErrorCode::VersionMismatch
                                 : ProtocolErrorCode::MalformedPayload);
    }
    const auto refresh =
        std::chrono::milliseconds{static_cast<std::int64_t>(ReadU32(frame.payload, 0U))};
    if (!IsValidRefresh(refresh)) {
        return RejectControl(ProtocolErrorCode::InvalidSubscription);
    }
    if constexpr (std::is_same_v<Message, Subscribe>) {
        return {.message = Subscribe{.requested_refresh = refresh}};
    } else {
        return {.message = SubscriptionAccepted{.effective_refresh = refresh}};
    }
}

ControlDecodeResult DecodeProtocolError(FrameView frame) {
    if (frame.payload.size() != kProtocolErrorPayloadSize) {
        return RejectControl(ProtocolErrorCode::MalformedPayload);
    }
    const auto code_value = ReadU16(frame.payload, 0U);
    const auto retry = std::to_integer<std::uint8_t>(frame.payload.subspan(2U, 1U).front());
    const auto minimum = ReadU16(frame.payload, 4U);
    const auto maximum = ReadU16(frame.payload, 6U);
    if (!IsKnownProtocolErrorValue(code_value) || retry > 2U ||
        frame.payload.subspan(3U, 1U).front() != std::byte{0} ||
        !IsValidVersionRange(minimum, maximum)) {
        return RejectControl(ProtocolErrorCode::MalformedPayload);
    }
    const auto code = static_cast<ProtocolErrorCode>(static_cast<std::uint8_t>(code_value));
    return {.message = ProtocolErrorMessage{.code = code,
                                            .retry_disposition = retry,
                                            .server_minimum_version = minimum,
                                            .server_maximum_version = maximum,
                                            .win32_error = ReadU32(frame.payload, 8U)}};
}

} // namespace

bool FrameEncodeResult::ok() const noexcept {
    return error == ProtocolErrorCode::None;
}

bool ControlDecodeResult::ok() const noexcept {
    return error == ProtocolErrorCode::None && message.has_value();
}

FrameEncodeResult EncodeFrame(FrameView frame) {
    if (frame.header.protocol_version == 0U) {
        return {.error = ProtocolErrorCode::VersionMismatch};
    }
    if (!IsKnownMessageType(frame.header.message_type)) {
        return {.error = ProtocolErrorCode::UnknownMessageType};
    }
    if (!HasValidRequestId(frame.header.message_type, frame.header.request_id)) {
        return {.error = ProtocolErrorCode::UnexpectedMessage};
    }
    if (frame.payload.size() != frame.header.payload_length) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    if (frame.header.payload_length > kMaximumFramePayload) {
        return {.error = ProtocolErrorCode::PayloadTooLarge};
    }

    FrameEncodeResult result;
    result.bytes.reserve(kFrameHeaderSize + frame.payload.size());
    AppendU32(result.bytes, kFrameMagic);
    AppendU16(result.bytes, kFrameHeaderSize);
    AppendU16(result.bytes, frame.header.protocol_version);
    AppendU16(result.bytes, static_cast<std::uint16_t>(frame.header.message_type));
    AppendU16(result.bytes, 0U);
    AppendU32(result.bytes, frame.header.payload_length);
    AppendU64(result.bytes, frame.header.request_id);
    result.bytes.insert(result.bytes.end(), frame.payload.begin(), frame.payload.end());
    return result;
}

FrameParseResult TryDecodeFrame(std::span<const std::byte> bytes) {
    if (bytes.size() < kFrameHeaderSize) {
        return {.required = kFrameHeaderSize};
    }
    if (ReadU32(bytes, kMagicOffset) != kFrameMagic) {
        return {.status = FrameParseStatus::Rejected, .error = ProtocolErrorCode::MalformedHeader};
    }
    if (ReadU16(bytes, kHeaderSizeOffset) != kFrameHeaderSize) {
        return {.status = FrameParseStatus::Rejected,
                .error = ProtocolErrorCode::UnsupportedHeader};
    }
    const auto protocol_version = ReadU16(bytes, kProtocolVersionOffset);
    if (protocol_version == 0U) {
        return {.status = FrameParseStatus::Rejected, .error = ProtocolErrorCode::VersionMismatch};
    }
    const auto message_type_value = ReadU16(bytes, kMessageTypeOffset);
    if (!IsKnownMessageTypeValue(message_type_value)) {
        return {.status = FrameParseStatus::Rejected,
                .error = ProtocolErrorCode::UnknownMessageType};
    }
    const auto message_type =
        static_cast<MessageType>(static_cast<std::uint8_t>(message_type_value));
    if (ReadU16(bytes, kFlagsOffset) != 0U) {
        return {.status = FrameParseStatus::Rejected, .error = ProtocolErrorCode::InvalidFlags};
    }
    const auto payload_length = ReadU32(bytes, kPayloadLengthOffset);
    if (payload_length > kMaximumFramePayload) {
        return {.status = FrameParseStatus::Rejected, .error = ProtocolErrorCode::PayloadTooLarge};
    }
    const auto request_id = ReadU64(bytes, kRequestIdOffset);
    if (!HasValidRequestId(message_type, request_id)) {
        return {.status = FrameParseStatus::Rejected,
                .error = ProtocolErrorCode::UnexpectedMessage};
    }
    const auto required = kFrameHeaderSize + static_cast<std::size_t>(payload_length);
    if (bytes.size() < required) {
        return {.required = required};
    }

    const auto payload = bytes.subspan(kFrameHeaderSize, payload_length);
    OwnedFrame frame{
        .header = {.protocol_version = protocol_version,
                   .message_type = message_type,
                   .request_id = request_id,
                   .payload_length = payload_length},
        .payload = std::vector<std::byte>{payload.begin(), payload.end()},
    };
    return {.status = FrameParseStatus::Decoded,
            .consumed = required,
            .required = required,
            .frame = std::move(frame)};
}

FrameEncodeResult EncodeClientHello(const ClientHello& message, std::uint64_t request_id) {
    if (!IsValidVersionRange(message.minimum_version, message.maximum_version)) {
        return {.error = ProtocolErrorCode::VersionMismatch};
    }
    if (!IsValidRefresh(message.requested_refresh)) {
        return {.error = ProtocolErrorCode::InvalidSubscription};
    }
    std::vector<std::byte> payload;
    payload.reserve(kClientHelloPayloadSize);
    AppendU16(payload, message.minimum_version);
    AppendU16(payload, message.maximum_version);
    AppendU32(payload, static_cast<std::uint32_t>(message.requested_refresh.count()));
    return MakeControlFrame(MessageType::ClientHello, message.maximum_version, request_id,
                            std::move(payload));
}

FrameEncodeResult EncodeServerHello(const ServerHello& message, std::uint64_t request_id) {
    if (message.selected_version != kCurrentProtocolVersion) {
        return {.error = ProtocolErrorCode::VersionMismatch};
    }
    if (message.maximum_frame_payload != kMaximumFramePayload) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    std::vector<std::byte> payload;
    payload.reserve(kServerHelloPayloadSize);
    AppendU16(payload, message.selected_version);
    AppendU16(payload, 0U);
    AppendU32(payload, message.maximum_frame_payload);
    payload.insert(payload.end(), message.instance.bytes.begin(), message.instance.bytes.end());
    return MakeControlFrame(MessageType::ServerHello, message.selected_version, request_id,
                            std::move(payload));
}

FrameEncodeResult EncodeSubscribe(const Subscribe& message, std::uint64_t request_id) {
    if (!IsValidRefresh(message.requested_refresh)) {
        return {.error = ProtocolErrorCode::InvalidSubscription};
    }
    std::vector<std::byte> payload;
    payload.reserve(kSubscriptionPayloadSize);
    AppendU32(payload, static_cast<std::uint32_t>(message.requested_refresh.count()));
    return MakeControlFrame(MessageType::Subscribe, kCurrentProtocolVersion, request_id,
                            std::move(payload));
}

FrameEncodeResult EncodeSubscriptionAccepted(const SubscriptionAccepted& message,
                                             std::uint64_t request_id) {
    if (!IsValidRefresh(message.effective_refresh)) {
        return {.error = ProtocolErrorCode::InvalidSubscription};
    }
    std::vector<std::byte> payload;
    payload.reserve(kSubscriptionPayloadSize);
    AppendU32(payload, static_cast<std::uint32_t>(message.effective_refresh.count()));
    return MakeControlFrame(MessageType::SubscriptionAccepted, kCurrentProtocolVersion, request_id,
                            std::move(payload));
}

FrameEncodeResult EncodeProtocolError(const ProtocolErrorMessage& message,
                                      std::uint64_t request_id) {
    if (!IsKnownProtocolError(message.code) || message.retry_disposition > 2U ||
        !IsValidVersionRange(message.server_minimum_version, message.server_maximum_version)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    std::vector<std::byte> payload;
    payload.reserve(kProtocolErrorPayloadSize);
    AppendU16(payload, static_cast<std::uint16_t>(message.code));
    payload.push_back(static_cast<std::byte>(message.retry_disposition));
    payload.push_back(std::byte{0});
    AppendU16(payload, message.server_minimum_version);
    AppendU16(payload, message.server_maximum_version);
    AppendU32(payload, message.win32_error);
    return MakeControlFrame(MessageType::ProtocolError, kCurrentProtocolVersion, request_id,
                            std::move(payload));
}

ControlDecodeResult DecodeControlMessage(FrameView frame, MessageDirection expected_direction) {
    if (frame.payload.size() != frame.header.payload_length) {
        return RejectControl(ProtocolErrorCode::MalformedPayload);
    }
    if (!HasValidRequestId(frame.header.message_type, frame.header.request_id) ||
        !IsAllowedDirection(frame.header.message_type, expected_direction)) {
        return RejectControl(ProtocolErrorCode::UnexpectedMessage);
    }
    switch (frame.header.message_type) {
    case MessageType::ClientHello:
        return DecodeClientHello(frame);
    case MessageType::ServerHello:
        return DecodeServerHello(frame);
    case MessageType::Subscribe:
        return DecodeRefreshMessage<Subscribe>(frame);
    case MessageType::SubscriptionAccepted:
        return DecodeRefreshMessage<SubscriptionAccepted>(frame);
    case MessageType::ProtocolError:
        return DecodeProtocolError(frame);
    case MessageType::ApplicationCatalog:
    case MessageType::Snapshot:
    case MessageType::CollectorStatus:
        return RejectControl(ProtocolErrorCode::UnexpectedMessage);
    }
    return RejectControl(ProtocolErrorCode::UnknownMessageType);
}

} // namespace nfm::ipc
