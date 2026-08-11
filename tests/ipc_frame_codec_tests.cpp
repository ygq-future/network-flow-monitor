#include "network_flow_monitor/ipc/frame_codec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

using nfm::ipc::ClientHello;
using nfm::ipc::CollectorInstanceId;
using nfm::ipc::ControlMessage;
using nfm::ipc::FrameParseStatus;
using nfm::ipc::FrameView;
using nfm::ipc::MessageDirection;
using nfm::ipc::MessageType;
using nfm::ipc::ProtocolErrorCode;
using nfm::ipc::ProtocolErrorMessage;
using nfm::ipc::ServerHello;
using nfm::ipc::Subscribe;
using nfm::ipc::SubscriptionAccepted;

constexpr std::byte B(std::uint32_t value) noexcept {
    return static_cast<std::byte>(static_cast<std::uint8_t>(value));
}

void StoreU16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = B(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.at(offset + 1U) = B(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void StoreU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.at(offset + index) = B(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
}

void StoreU64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes.at(offset + index) = B(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
}

std::vector<std::byte> EncodeRaw(MessageType type, std::span<const std::byte> payload,
                                 std::uint64_t request_id = 1U,
                                 std::uint16_t protocol_version = 1U) {
    auto encoded = nfm::ipc::EncodeFrame(
        FrameView{.header = {.protocol_version = protocol_version,
                             .message_type = type,
                             .request_id = request_id,
                             .payload_length = static_cast<std::uint32_t>(payload.size())},
                  .payload = payload});
    return std::move(encoded.bytes);
}

template <typename Message>
std::optional<Message> DecodeAs(const nfm::ipc::FrameEncodeResult& encoded,
                                MessageDirection direction, ProtocolErrorCode& error) {
    const auto parsed = nfm::ipc::TryDecodeFrame(encoded.bytes);
    if (parsed.status != FrameParseStatus::Decoded || !parsed.frame.has_value()) {
        error = parsed.error;
        return std::nullopt;
    }
    const auto& owned = *parsed.frame;
    const auto decoded = nfm::ipc::DecodeControlMessage(
        FrameView{.header = owned.header, .payload = owned.payload}, direction);
    error = decoded.error;
    if (!decoded.message.has_value()) {
        return std::nullopt;
    }
    const auto* const message = std::get_if<Message>(&*decoded.message);
    return message == nullptr ? std::nullopt : std::optional<Message>{*message};
}

void CheckHeaderGoldenBytes(TestContext& context) {
    constexpr std::array<std::byte, 8> payload{B(1U),    B(0U), B(1U), B(0U),
                                               B(0xF4U), B(1U), B(0U), B(0U)};
    const auto encoded = nfm::ipc::EncodeFrame(
        FrameView{.header = {.protocol_version = 1U,
                             .message_type = MessageType::ClientHello,
                             .request_id = 1U,
                             .payload_length = static_cast<std::uint32_t>(payload.size())},
                  .payload = payload});
    constexpr std::array<std::byte, 24> expected_header{
        B('N'), B('F'), B('M'), B('1'), B(24U), B(0U), B(1U), B(0U), B(1U), B(0U), B(0U), B(0U),
        B(8U),  B(0U),  B(0U),  B(0U),  B(1U),  B(0U), B(0U), B(0U), B(0U), B(0U), B(0U), B(0U),
    };
    context.Check(encoded.error == ProtocolErrorCode::None, "golden frame encodes");
    context.Check(encoded.bytes.size() == expected_header.size() + payload.size(),
                  "golden frame has exact length");
    if (encoded.bytes.size() == expected_header.size() + payload.size()) {
        context.Check(std::ranges::equal(expected_header,
                                         std::span{encoded.bytes}.first(expected_header.size())),
                      "header offsets and endian match golden bytes");
        context.Check(
            std::ranges::equal(payload, std::span{encoded.bytes}.subspan(expected_header.size())),
            "payload follows exact 24-byte header");
    }
}

void CheckAllMessageValuesAndBounds(TestContext& context) {
    constexpr std::array<MessageType, 8> types{
        MessageType::ClientHello,          MessageType::ServerHello,        MessageType::Subscribe,
        MessageType::SubscriptionAccepted, MessageType::ApplicationCatalog, MessageType::Snapshot,
        MessageType::CollectorStatus,      MessageType::ProtocolError,
    };
    for (std::size_t index = 0U; index < types.size(); ++index) {
        const bool push = types.at(index) == MessageType::ApplicationCatalog ||
                          types.at(index) == MessageType::Snapshot ||
                          types.at(index) == MessageType::CollectorStatus;
        const auto bytes = EncodeRaw(types.at(index), {}, push ? 0U : 1U);
        context.Check(bytes.size() == nfm::ipc::kFrameHeaderSize,
                      "known message type encodes as zero-payload frame");
        if (bytes.size() == nfm::ipc::kFrameHeaderSize) {
            context.Check(std::to_integer<std::uint8_t>(bytes.at(8U)) == index + 1U &&
                              std::to_integer<std::uint8_t>(bytes.at(9U)) == 0U,
                          "message enum has exact little-endian wire value");
        }
    }

    const auto zero = EncodeRaw(MessageType::Snapshot, {}, 0U);
    const auto zero_decoded = nfm::ipc::TryDecodeFrame(zero);
    context.Check(zero_decoded.status == FrameParseStatus::Decoded &&
                      zero_decoded.consumed == nfm::ipc::kFrameHeaderSize &&
                      zero_decoded.required == nfm::ipc::kFrameHeaderSize,
                  "zero payload round trips with exact counts");

    std::vector<std::byte> maximum(nfm::ipc::kMaximumFramePayload, B(0xA5U));
    const auto maximum_bytes = EncodeRaw(MessageType::Snapshot, maximum, 0U);
    context.Check(maximum_bytes.size() ==
                      nfm::ipc::kFrameHeaderSize + nfm::ipc::kMaximumFramePayload,
                  "exact one MiB payload encodes");
    const auto maximum_decoded = nfm::ipc::TryDecodeFrame(maximum_bytes);
    context.Check(maximum_decoded.status == FrameParseStatus::Decoded &&
                      maximum_decoded.frame.has_value() &&
                      maximum_decoded.frame->payload == maximum,
                  "exact one MiB payload decodes without truncation");

    maximum.push_back(B(0U));
    const auto oversized = nfm::ipc::EncodeFrame(
        FrameView{.header = {.protocol_version = 1U,
                             .message_type = MessageType::Snapshot,
                             .request_id = 0U,
                             .payload_length = static_cast<std::uint32_t>(maximum.size())},
                  .payload = maximum});
    context.Check(oversized.error == ProtocolErrorCode::PayloadTooLarge && oversized.bytes.empty(),
                  "payload above one MiB is rejected before allocation");
}

void CheckStreamingParse(TestContext& context) {
    constexpr std::array<std::byte, 3> payload{B(7U), B(8U), B(9U)};
    const auto frame = EncodeRaw(MessageType::ProtocolError, payload, 0U);
    for (std::size_t length = 0U; length < nfm::ipc::kFrameHeaderSize; ++length) {
        const auto result = nfm::ipc::TryDecodeFrame(std::span{frame}.first(length));
        context.Check(result.status == FrameParseStatus::NeedMoreData && result.consumed == 0U &&
                          result.required == nfm::ipc::kFrameHeaderSize,
                      "partial header reports exact total requirement");
    }
    const auto partial_payload =
        nfm::ipc::TryDecodeFrame(std::span{frame}.first(nfm::ipc::kFrameHeaderSize + 2U));
    context.Check(partial_payload.status == FrameParseStatus::NeedMoreData &&
                      partial_payload.consumed == 0U &&
                      partial_payload.required == nfm::ipc::kFrameHeaderSize + payload.size(),
                  "partial payload reports exact frame requirement");

    const auto second = EncodeRaw(MessageType::Snapshot, {}, 0U);
    auto concatenated = frame;
    concatenated.insert(concatenated.end(), second.begin(), second.end());
    const auto first_result = nfm::ipc::TryDecodeFrame(concatenated);
    context.Check(first_result.status == FrameParseStatus::Decoded &&
                      first_result.consumed == frame.size() &&
                      first_result.required == frame.size(),
                  "decoder consumes one of two concatenated frames");
    const auto second_result =
        nfm::ipc::TryDecodeFrame(std::span{concatenated}.subspan(first_result.consumed));
    context.Check(second_result.status == FrameParseStatus::Decoded &&
                      second_result.consumed == second.size(),
                  "second concatenated frame remains independently decodable");

    std::vector<std::byte> unaligned{B(0xEEU)};
    unaligned.insert(unaligned.end(), frame.begin(), frame.end());
    const auto unaligned_result = nfm::ipc::TryDecodeFrame(std::span{unaligned}.subspan(1U));
    context.Check(unaligned_result.status == FrameParseStatus::Decoded &&
                      unaligned_result.frame.has_value() &&
                      unaligned_result.frame->payload ==
                          std::vector<std::byte>{payload.begin(), payload.end()},
                  "unaligned input is decoded without native struct casts");
}

void CheckHeaderRejections(TestContext& context) {
    auto valid = EncodeRaw(MessageType::ClientHello, {}, 1U);
    auto CheckRejected = [&context, &valid](std::size_t offset, std::uint64_t value,
                                            ProtocolErrorCode expected, std::string_view name) {
        auto candidate = valid;
        if (offset == 16U) {
            StoreU64(candidate, offset, value);
        } else if (offset == 12U || offset == 0U) {
            StoreU32(candidate, offset, static_cast<std::uint32_t>(value));
        } else {
            StoreU16(candidate, offset, static_cast<std::uint16_t>(value));
        }
        const auto decoded = nfm::ipc::TryDecodeFrame(candidate);
        context.Check(decoded.status == FrameParseStatus::Rejected && decoded.error == expected &&
                          decoded.consumed == 0U && !decoded.frame.has_value(),
                      name);
    };

    CheckRejected(0U, 0U, ProtocolErrorCode::MalformedHeader, "bad magic rejected");
    CheckRejected(4U, 23U, ProtocolErrorCode::UnsupportedHeader, "bad header size rejected");
    CheckRejected(6U, 0U, ProtocolErrorCode::VersionMismatch, "zero version rejected");
    CheckRejected(8U, 9U, ProtocolErrorCode::UnknownMessageType, "unknown type rejected");
    CheckRejected(8U, 0x0101U, ProtocolErrorCode::UnknownMessageType,
                  "wide message type is validated before enum conversion");
    CheckRejected(10U, 1U, ProtocolErrorCode::InvalidFlags, "nonzero flags rejected");
    CheckRejected(12U, nfm::ipc::kMaximumFramePayload + 1U, ProtocolErrorCode::PayloadTooLarge,
                  "oversized declared payload rejected");
    CheckRejected(16U, 0U, ProtocolErrorCode::UnexpectedMessage,
                  "client request with zero ID rejected");

    auto push = EncodeRaw(MessageType::Snapshot, {}, 0U);
    StoreU64(push, 16U, 1U);
    const auto invalid_push = nfm::ipc::TryDecodeFrame(push);
    context.Check(invalid_push.status == FrameParseStatus::Rejected &&
                      invalid_push.error == ProtocolErrorCode::UnexpectedMessage,
                  "server push with nonzero request ID rejected");

    const std::array<std::byte, 1> payload{B(1U)};
    const auto mismatch =
        nfm::ipc::EncodeFrame(FrameView{.header = {.protocol_version = 1U,
                                                   .message_type = MessageType::ProtocolError,
                                                   .request_id = 0U,
                                                   .payload_length = 0U},
                                        .payload = payload});
    context.Check(mismatch.error == ProtocolErrorCode::MalformedPayload,
                  "encoder rejects declared/actual payload mismatch");
}

void CheckControlRoundTrips(TestContext& context) {
    ProtocolErrorCode error{};
    const ClientHello hello{.minimum_version = 1U,
                            .maximum_version = 1U,
                            .requested_refresh = std::chrono::milliseconds{500}};
    const auto encoded_hello = nfm::ipc::EncodeClientHello(hello, 42U);
    const auto decoded_hello =
        DecodeAs<ClientHello>(encoded_hello, MessageDirection::ClientToServer, error);
    context.Check(error == ProtocolErrorCode::None && decoded_hello.has_value() &&
                      decoded_hello->minimum_version == 1U &&
                      decoded_hello->maximum_version == 1U &&
                      decoded_hello->requested_refresh == std::chrono::milliseconds{500},
                  "ClientHello round trips exact fields");
    context.Check(encoded_hello.bytes.size() == 32U &&
                      std::to_integer<std::uint8_t>(encoded_hello.bytes.at(24U)) == 1U &&
                      std::to_integer<std::uint8_t>(encoded_hello.bytes.at(28U)) == 0xF4U,
                  "ClientHello payload uses exact little-endian offsets");

    CollectorInstanceId instance{};
    for (std::size_t index = 0U; index < instance.bytes.size(); ++index) {
        instance.bytes.at(index) = B(static_cast<std::uint8_t>(index + 1U));
    }
    const ServerHello server{.selected_version = 1U,
                             .maximum_frame_payload = nfm::ipc::kMaximumFramePayload,
                             .instance = instance};
    const auto encoded_server = nfm::ipc::EncodeServerHello(server, 42U);
    const auto decoded_server =
        DecodeAs<ServerHello>(encoded_server, MessageDirection::ServerToClient, error);
    context.Check(error == ProtocolErrorCode::None && decoded_server.has_value() &&
                      decoded_server->selected_version == 1U &&
                      decoded_server->maximum_frame_payload == nfm::ipc::kMaximumFramePayload &&
                      decoded_server->instance == instance,
                  "ServerHello and canonical GUID bytes round trip");

    const Subscribe subscribe{.requested_refresh = std::chrono::milliseconds{100}};
    const auto encoded_subscribe = nfm::ipc::EncodeSubscribe(subscribe, 7U);
    const auto decoded_subscribe =
        DecodeAs<Subscribe>(encoded_subscribe, MessageDirection::ClientToServer, error);
    context.Check(error == ProtocolErrorCode::None && decoded_subscribe.has_value() &&
                      decoded_subscribe->requested_refresh == std::chrono::milliseconds{100},
                  "minimum Subscribe interval round trips");

    const SubscriptionAccepted accepted{.effective_refresh = std::chrono::milliseconds{5000}};
    const auto encoded_accepted = nfm::ipc::EncodeSubscriptionAccepted(accepted, 7U);
    const auto decoded_accepted =
        DecodeAs<SubscriptionAccepted>(encoded_accepted, MessageDirection::ServerToClient, error);
    context.Check(error == ProtocolErrorCode::None && decoded_accepted.has_value() &&
                      decoded_accepted->effective_refresh == std::chrono::milliseconds{5000},
                  "maximum accepted subscription interval round trips");

    const ProtocolErrorMessage protocol_error{
        .code = ProtocolErrorCode::VersionMismatch,
        .retry_disposition = 0U,
        .server_minimum_version = 1U,
        .server_maximum_version = 1U,
        .win32_error = 5U,
    };
    const auto encoded_error = nfm::ipc::EncodeProtocolError(protocol_error, 42U);
    const auto decoded_error =
        DecodeAs<ProtocolErrorMessage>(encoded_error, MessageDirection::ServerToClient, error);
    context.Check(
        error == ProtocolErrorCode::None && decoded_error.has_value() &&
            decoded_error->code == ProtocolErrorCode::VersionMismatch &&
            decoded_error->retry_disposition == 0U && decoded_error->server_minimum_version == 1U &&
            decoded_error->server_maximum_version == 1U && decoded_error->win32_error == 5U,
        "12-byte ProtocolError payload round trips");
    context.Check(encoded_error.bytes.size() == nfm::ipc::kFrameHeaderSize + 12U,
                  "ProtocolError has exact payload size");
}

void CheckControlValidation(TestContext& context) {
    const auto invalid_order =
        nfm::ipc::EncodeClientHello({.minimum_version = 2U,
                                     .maximum_version = 1U,
                                     .requested_refresh = std::chrono::milliseconds{500}},
                                    1U);
    context.Check(invalid_order.error == ProtocolErrorCode::VersionMismatch,
                  "descending Hello range rejected");
    const auto invalid_refresh =
        nfm::ipc::EncodeSubscribe({.requested_refresh = std::chrono::milliseconds{99}}, 1U);
    context.Check(invalid_refresh.error == ProtocolErrorCode::InvalidSubscription,
                  "subscription below config minimum rejected");
    const auto invalid_request = nfm::ipc::EncodeSubscriptionAccepted(
        {.effective_refresh = std::chrono::milliseconds{500}}, 0U);
    context.Check(invalid_request.error == ProtocolErrorCode::UnexpectedMessage,
                  "reply requires echoed nonzero request ID");

    constexpr std::array<std::byte, 8> hello_payload{B(1U),    B(0U), B(1U), B(0U),
                                                     B(0xF4U), B(1U), B(0U), B(0U)};
    const auto hello_frame = EncodeRaw(MessageType::ClientHello, hello_payload, 1U);
    const auto parsed = nfm::ipc::TryDecodeFrame(hello_frame);
    context.Check(parsed.frame.has_value(), "direction test frame parses");
    if (parsed.frame.has_value()) {
        const auto wrong_direction = nfm::ipc::DecodeControlMessage(
            {.header = parsed.frame->header, .payload = parsed.frame->payload},
            MessageDirection::ServerToClient);
        context.Check(wrong_direction.error == ProtocolErrorCode::UnexpectedMessage,
                      "client-only control message rejected in server direction");
    }

    auto bad_server_payload = std::vector<std::byte>(24U, B(0U));
    StoreU16(bad_server_payload, 0U, 1U);
    StoreU16(bad_server_payload, 2U, 1U);
    StoreU32(bad_server_payload, 4U, nfm::ipc::kMaximumFramePayload);
    const auto bad_server_frame = EncodeRaw(MessageType::ServerHello, bad_server_payload, 1U);
    const auto parsed_server = nfm::ipc::TryDecodeFrame(bad_server_frame);
    if (parsed_server.frame.has_value()) {
        const auto reserved = nfm::ipc::DecodeControlMessage(
            {.header = parsed_server.frame->header, .payload = parsed_server.frame->payload},
            MessageDirection::ServerToClient);
        context.Check(reserved.error == ProtocolErrorCode::MalformedPayload,
                      "nonzero ServerHello reserved field rejected");
    } else {
        context.Check(false, "reserved-field test frame parses");
    }

    auto malformed_error_payload = std::vector<std::byte>(12U, B(0U));
    StoreU16(malformed_error_payload, 0U,
             static_cast<std::uint16_t>(ProtocolErrorCode::InternalFailure));
    malformed_error_payload.at(2U) = B(3U);
    StoreU16(malformed_error_payload, 4U, 1U);
    StoreU16(malformed_error_payload, 6U, 1U);
    const auto malformed_error_frame =
        EncodeRaw(MessageType::ProtocolError, malformed_error_payload, 0U);
    const auto parsed_error = nfm::ipc::TryDecodeFrame(malformed_error_frame);
    if (parsed_error.frame.has_value()) {
        const auto invalid_retry = nfm::ipc::DecodeControlMessage(
            {.header = parsed_error.frame->header, .payload = parsed_error.frame->payload},
            MessageDirection::ClientToServer);
        context.Check(invalid_retry.error == ProtocolErrorCode::MalformedPayload,
                      "unknown retry disposition rejected");
    } else {
        context.Check(false, "retry-disposition test frame parses");
    }

    StoreU16(malformed_error_payload, 0U, 0x0101U);
    malformed_error_payload.at(2U) = B(0U);
    const auto wide_error_frame =
        EncodeRaw(MessageType::ProtocolError, malformed_error_payload, 0U);
    const auto parsed_wide_error = nfm::ipc::TryDecodeFrame(wide_error_frame);
    if (parsed_wide_error.frame.has_value()) {
        const auto invalid_code =
            nfm::ipc::DecodeControlMessage({.header = parsed_wide_error.frame->header,
                                            .payload = parsed_wide_error.frame->payload},
                                           MessageDirection::ServerToClient);
        context.Check(invalid_code.error == ProtocolErrorCode::MalformedPayload,
                      "wide protocol error code is validated before enum conversion");
    } else {
        context.Check(false, "wide protocol-error-code test frame parses");
    }

    const std::array<std::byte, 1> wrong_length{B(0U)};
    const auto wrong_length_frame = EncodeRaw(MessageType::Subscribe, wrong_length, 1U);
    const auto parsed_length = nfm::ipc::TryDecodeFrame(wrong_length_frame);
    if (parsed_length.frame.has_value()) {
        const auto wrong_length_result = nfm::ipc::DecodeControlMessage(
            {.header = parsed_length.frame->header, .payload = parsed_length.frame->payload},
            MessageDirection::ClientToServer);
        context.Check(wrong_length_result.error == ProtocolErrorCode::MalformedPayload,
                      "control payload requires exact length");
    } else {
        context.Check(false, "wrong-length test frame parses");
    }
}

} // namespace

int main() {
    TestContext context;
    CheckHeaderGoldenBytes(context);
    CheckAllMessageValuesAndBounds(context);
    CheckStreamingParse(context);
    CheckHeaderRejections(context);
    CheckControlRoundTrips(context);
    CheckControlValidation(context);
    return context.failures();
}
