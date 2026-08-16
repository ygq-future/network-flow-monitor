#include "network_flow_monitor/ipc/frame_codec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

using nfm::ipc::CollectorStatusMessage;
using nfm::ipc::FrameParseStatus;
using nfm::ipc::FrameView;
using nfm::ipc::HelloAckMessage;
using nfm::ipc::HelloMessage;
using nfm::ipc::MessageType;
using nfm::ipc::ProtocolErrorCode;
using nfm::ipc::RankingSnapshotMessage;
using nfm::ipc::ShutdownRequestMessage;
using nfm::ipc::TrafficTickMessage;

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

std::vector<std::byte> EncodeRaw(MessageType type, std::span<const std::byte> payload,
                                 std::uint16_t protocol_version = 1U) {
    auto encoded = nfm::ipc::EncodeFrame(
        FrameView{.header = {.protocol_version = protocol_version,
                             .message_type = type,
                             .payload_length = static_cast<std::uint32_t>(payload.size())},
                  .payload = payload});
    return std::move(encoded.bytes);
}

void CheckHeaderGoldenBytes(TestContext& context) {
    const std::string payload = "{\"seq\":4096}";
    const auto payload_bytes = std::as_bytes(std::span{payload});
    const auto encoded = nfm::ipc::EncodeFrame(
        FrameView{.header = {.protocol_version = 1U,
                             .message_type = MessageType::TrafficTick,
                             .payload_length = static_cast<std::uint32_t>(payload_bytes.size())},
                  .payload = payload_bytes});

    constexpr std::array<std::byte, 12> expected_header{
        B('N'), B('F'), B('M'), B('1'), // Magic: "NFM1"
        B(1U),  B(0U),                  // Version: 1
        B(3U),  B(0U),                  // MessageType: TrafficTick (3)
        B(12U), B(0U),  B(0U),  B(0U),  // PayloadLength: 12
    };

    context.Check(encoded.error == ProtocolErrorCode::None, "golden frame encodes");
    context.Check(encoded.bytes.size() == expected_header.size() + payload_bytes.size(),
                  "golden frame has exact 12-byte header + payload length");
    if (encoded.bytes.size() == expected_header.size() + payload_bytes.size()) {
        context.Check(std::ranges::equal(expected_header,
                                         std::span{encoded.bytes}.first(expected_header.size())),
                      "header offsets and endian match golden bytes");
        context.Check(std::ranges::equal(payload_bytes,
                                         std::span{encoded.bytes}.subspan(expected_header.size())),
                      "payload follows exact 12-byte header");
    }
}

void CheckAllMessageValuesAndBounds(TestContext& context) {
    constexpr std::array<MessageType, 6> types{
        MessageType::Hello,           MessageType::HelloAck,        MessageType::TrafficTick,
        MessageType::RankingSnapshot, MessageType::CollectorStatus, MessageType::ShutdownRequest,
    };
    for (std::size_t index = 0U; index < types.size(); ++index) {
        const auto bytes = EncodeRaw(types.at(index), {});
        context.Check(bytes.size() == nfm::ipc::kFrameHeaderSize,
                      "known message type encodes as zero-payload frame");
        if (bytes.size() == nfm::ipc::kFrameHeaderSize) {
            context.Check(std::to_integer<std::uint8_t>(bytes.at(6U)) == index + 1U &&
                              std::to_integer<std::uint8_t>(bytes.at(7U)) == 0U,
                          "message enum has exact little-endian wire value at offset 6");
        }
    }

    const auto zero = EncodeRaw(MessageType::ShutdownRequest, {});
    const auto zero_decoded = nfm::ipc::TryDecodeFrame(zero);
    context.Check(zero_decoded.status == FrameParseStatus::Decoded &&
                      zero_decoded.consumed == nfm::ipc::kFrameHeaderSize &&
                      zero_decoded.required == nfm::ipc::kFrameHeaderSize,
                  "zero payload round trips with exact counts");

    std::vector<std::byte> maximum(nfm::ipc::kMaximumFramePayload, B(0x20U)); // spaces
    const auto maximum_bytes = EncodeRaw(MessageType::ShutdownRequest, maximum);
    context.Check(maximum_bytes.size() ==
                      nfm::ipc::kFrameHeaderSize + nfm::ipc::kMaximumFramePayload,
                  "exact 256 KiB payload encodes");
    const auto maximum_decoded = nfm::ipc::TryDecodeFrame(maximum_bytes);
    context.Check(maximum_decoded.status == FrameParseStatus::Decoded &&
                      maximum_decoded.frame.has_value() &&
                      maximum_decoded.frame->payload == maximum,
                  "exact 256 KiB payload decodes without truncation");

    maximum.push_back(B(0U));
    const auto oversized = nfm::ipc::EncodeFrame(
        FrameView{.header = {.protocol_version = 1U,
                             .message_type = MessageType::ShutdownRequest,
                             .payload_length = static_cast<std::uint32_t>(maximum.size())},
                  .payload = maximum});
    context.Check(oversized.error == ProtocolErrorCode::PayloadTooLarge && oversized.bytes.empty(),
                  "payload above 256 KiB is rejected before allocation");
}

void CheckStreamingParse(TestContext& context) {
    constexpr std::array<std::byte, 5> payload{B('{'), B('}'), B('\n'), B(' '), B(' ')};
    const auto frame = EncodeRaw(MessageType::ShutdownRequest, payload);
    for (std::size_t length = 0U; length < nfm::ipc::kFrameHeaderSize; ++length) {
        const auto result = nfm::ipc::TryDecodeFrame(std::span{frame}.first(length));
        context.Check(result.status == FrameParseStatus::NeedMoreData && result.consumed == 0U &&
                          result.required == nfm::ipc::kFrameHeaderSize,
                      "partial header reports exact total requirement (12 bytes)");
    }
    const auto partial_payload =
        nfm::ipc::TryDecodeFrame(std::span{frame}.first(nfm::ipc::kFrameHeaderSize + 2U));
    context.Check(partial_payload.status == FrameParseStatus::NeedMoreData &&
                      partial_payload.consumed == 0U &&
                      partial_payload.required == nfm::ipc::kFrameHeaderSize + payload.size(),
                  "partial payload reports exact frame requirement");

    const auto second = EncodeRaw(MessageType::TrafficTick, {});
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
    auto valid = EncodeRaw(MessageType::Hello, {});
    auto CheckRejected = [&context, &valid](std::size_t offset, std::uint32_t value,
                                            ProtocolErrorCode expected, std::string_view name) {
        auto candidate = valid;
        if (offset == 8U || offset == 0U) {
            StoreU32(candidate, offset, value);
        } else {
            StoreU16(candidate, offset, static_cast<std::uint16_t>(value));
        }
        const auto decoded = nfm::ipc::TryDecodeFrame(candidate);
        context.Check(decoded.status == FrameParseStatus::Rejected && decoded.error == expected &&
                          decoded.consumed == 0U && !decoded.frame.has_value(),
                      name);
    };

    CheckRejected(0U, 0U, ProtocolErrorCode::MalformedHeader, "bad magic rejected");
    CheckRejected(4U, 0U, ProtocolErrorCode::VersionMismatch, "zero version rejected");
    CheckRejected(4U, 2U, ProtocolErrorCode::VersionMismatch, "unsupported version rejected");
    CheckRejected(6U, 0U, ProtocolErrorCode::UnknownMessageType, "zero message type rejected");
    CheckRejected(6U, 7U, ProtocolErrorCode::UnknownMessageType, "unknown message type rejected");
    CheckRejected(6U, 0x0100U, ProtocolErrorCode::UnknownMessageType, "wide message type rejected");
    CheckRejected(8U, nfm::ipc::kMaximumFramePayload + 1U, ProtocolErrorCode::PayloadTooLarge,
                  "oversized declared payload rejected");

    const std::array<std::byte, 1> payload{B(1U)};
    const auto mismatch =
        nfm::ipc::EncodeFrame(FrameView{.header = {.protocol_version = 1U,
                                                   .message_type = MessageType::Hello,
                                                   .payload_length = 0U},
                                        .payload = payload});
    context.Check(mismatch.error == ProtocolErrorCode::MalformedPayload,
                  "encoder rejects declared/actual payload mismatch");
}

void CheckJsonMessageEncoders(TestContext& context) {
    // Hello
    const HelloMessage hello{
        .client_pid = 12344U, .protocol_version = 1U, .client_name = "NetworkFlowMonitor.Ui"};
    const auto encoded_hello = nfm::ipc::EncodeHello(hello);
    context.Check(encoded_hello.ok(), "Hello encodes to valid JSON frame");
    const auto decoded_hello = nfm::ipc::DecodeHello(encoded_hello.bytes);
    context.Check(decoded_hello.ok() && decoded_hello.message.has_value() &&
                      *decoded_hello.message == hello,
                  "Hello round trips with exact fields");

    // HelloAck
    const HelloAckMessage hello_ack{
        .protocol_version = 1U,
        .collector_instance_id = "cinst_9f8a3c2e1b",
        .collector_pid = 4588U,
        .capabilities = {"etw_live_counters", "traffic_tick_4hz", "ranking_snapshot_1hz"}};
    const auto encoded_hello_ack = nfm::ipc::EncodeHelloAck(hello_ack);
    context.Check(encoded_hello_ack.ok(), "HelloAck encodes to valid JSON frame");
    const auto decoded_hello_ack = nfm::ipc::DecodeHelloAck(encoded_hello_ack.bytes);
    context.Check(decoded_hello_ack.ok() && decoded_hello_ack.message.has_value() &&
                      *decoded_hello_ack.message == hello_ack,
                  "HelloAck round trips with exact fields");

    // TrafficTick
    const TrafficTickMessage tick{.seq = 4096U,
                                  .monotonic_ms = 83928471U,
                                  .download_bytes_per_sec = 5242880U,
                                  .upload_bytes_per_sec = 1048576U};
    const auto encoded_tick = nfm::ipc::EncodeTrafficTick(tick);
    context.Check(encoded_tick.ok(), "TrafficTick encodes to valid JSON frame");
    const auto decoded_tick = nfm::ipc::DecodeTrafficTick(encoded_tick.bytes);
    context.Check(decoded_tick.ok() && decoded_tick.message.has_value() &&
                      *decoded_tick.message == tick,
                  "TrafficTick round trips with exact fields");

    // RankingSnapshot
    const RankingSnapshotMessage snapshot{
        .seq = 1024U,
        .monotonic_ms = 83928471U,
        .status = "Ready",
        .total_rate = {.download_bytes_per_sec = 5242880U, .upload_bytes_per_sec = 1048576U},
        .applications = {
            {.id = "app_chrome",
             .executable_path = R"(C:\Program Files\Google\Chrome\Application\chrome.exe)",
             .display_name = "Google Chrome",
             .icon_key = R"(C:\Program Files\Google\Chrome\Application\chrome.exe)",
             .download_bytes_per_sec = 4194304U,
             .upload_bytes_per_sec = 838860U}}};
    const auto encoded_snapshot = nfm::ipc::EncodeRankingSnapshot(snapshot);
    context.Check(encoded_snapshot.ok(), "RankingSnapshot encodes to valid JSON frame");
    const auto decoded_snapshot = nfm::ipc::DecodeRankingSnapshot(encoded_snapshot.bytes);
    context.Check(decoded_snapshot.ok() && decoded_snapshot.message.has_value() &&
                      *decoded_snapshot.message == snapshot,
                  "RankingSnapshot round trips with exact fields");

    // CollectorStatus
    const CollectorStatusMessage status{
        .monotonic_ms = 83928471U,
        .etw_session_active = true,
        .dropped_packets = 0U,
        .active_pids = 42U,
    };
    const auto encoded_status = nfm::ipc::EncodeCollectorStatus(status);
    context.Check(encoded_status.ok(), "CollectorStatus encodes to valid JSON frame");
    const auto decoded_status = nfm::ipc::DecodeCollectorStatus(encoded_status.bytes);
    context.Check(decoded_status.ok() && decoded_status.message.has_value() &&
                      *decoded_status.message == status,
                  "CollectorStatus round trips with exact fields");

    // ShutdownRequest
    const ShutdownRequestMessage shutdown{.reason = "ui_exit"};
    const auto encoded_shutdown = nfm::ipc::EncodeShutdownRequest(shutdown);
    context.Check(encoded_shutdown.ok(), "ShutdownRequest encodes to valid JSON frame");
    const auto decoded_shutdown = nfm::ipc::DecodeShutdownRequest(encoded_shutdown.bytes);
    context.Check(decoded_shutdown.ok() && decoded_shutdown.message.has_value() &&
                      *decoded_shutdown.message == shutdown,
                  "ShutdownRequest round trips with exact fields");
}

std::vector<std::byte> HexToBytes(std::string_view hex) {
    std::vector<std::byte> bytes;
    bytes.reserve(hex.size() / 2U);
    for (std::size_t i = 0U; i + 1U < hex.size(); i += 2U) {
        const auto high = hex.at(i);
        const auto low = hex.at(i + 1U);
        auto HexVal = [](char c) -> std::uint8_t {
            if (c >= '0' && c <= '9') {
                return static_cast<std::uint8_t>(c - '0');
            }
            if (c >= 'a' && c <= 'f') {
                return static_cast<std::uint8_t>(c - 'a' + 10);
            }
            if (c >= 'A' && c <= 'F') {
                return static_cast<std::uint8_t>(c - 'A' + 10);
            }
            return 0U;
        };
        bytes.push_back(static_cast<std::byte>((HexVal(high) << 4U) | HexVal(low)));
    }
    return bytes;
}

void CheckFixturesLoadedAndValidated(TestContext& context) {
    std::filesystem::path fixture_path;
    auto current = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        auto candidate = current / "tests" / "fixtures" / "framing_vectors.json";
        if (std::filesystem::exists(candidate)) {
            fixture_path = candidate;
            break;
        }
        if (current.has_parent_path() && current != current.parent_path()) {
            current = current.parent_path();
        } else {
            break;
        }
    }
    if (fixture_path.empty()) {
        context.Check(false, "tests/fixtures/framing_vectors.json exists and is reachable");
        return;
    }

    std::ifstream file(fixture_path, std::ios::binary);
    context.Check(file.is_open(), "framing_vectors.json opened");
    if (!file.is_open()) {
        return;
    }

    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    context.Check(!content.empty(), "framing_vectors.json is non-empty");

    // Test message fixture files
    const auto fixture_dir = fixture_path.parent_path() / "messages";
    auto ReadMessageFile = [&](const std::string& filename) -> std::string {
        std::ifstream msg_file(fixture_dir / filename, std::ios::binary);
        if (!msg_file.is_open()) {
            return {};
        }
        return {(std::istreambuf_iterator<char>(msg_file)), std::istreambuf_iterator<char>()};
    };

    const auto hello_json = ReadMessageFile("hello.json");
    context.Check(!hello_json.empty(), "hello.json fixture loaded");
    const auto hello_decoded = nfm::ipc::DecodeHello(std::as_bytes(std::span{hello_json}));
    context.Check(hello_decoded.ok() && hello_decoded.message.has_value() &&
                      hello_decoded.message->client_pid == 12344U &&
                      hello_decoded.message->client_name == "NetworkFlowMonitor.Ui",
                  "hello.json fixture decodes accurately");

    const auto hello_ack_json = ReadMessageFile("hello_ack.json");
    context.Check(!hello_ack_json.empty(), "hello_ack.json fixture loaded");
    const auto hello_ack_decoded =
        nfm::ipc::DecodeHelloAck(std::as_bytes(std::span{hello_ack_json}));
    context.Check(hello_ack_decoded.ok() && hello_ack_decoded.message.has_value() &&
                      hello_ack_decoded.message->collector_instance_id == "cinst_9f8a3c2e1b" &&
                      hello_ack_decoded.message->capabilities.size() == 3U,
                  "hello_ack.json fixture decodes accurately");

    const auto tick_json = ReadMessageFile("traffic_tick.json");
    context.Check(!tick_json.empty(), "traffic_tick.json fixture loaded");
    const auto tick_decoded = nfm::ipc::DecodeTrafficTick(std::as_bytes(std::span{tick_json}));
    context.Check(tick_decoded.ok() && tick_decoded.message.has_value() &&
                      tick_decoded.message->seq == 4096U &&
                      tick_decoded.message->download_bytes_per_sec == 5242880U,
                  "traffic_tick.json fixture decodes accurately");

    const auto snapshot_json = ReadMessageFile("ranking_snapshot.json");
    context.Check(!snapshot_json.empty(), "ranking_snapshot.json fixture loaded");
    const auto snapshot_decoded =
        nfm::ipc::DecodeRankingSnapshot(std::as_bytes(std::span{snapshot_json}));
    context.Check(snapshot_decoded.ok() && snapshot_decoded.message.has_value() &&
                      snapshot_decoded.message->seq == 1024U &&
                      snapshot_decoded.message->applications.size() == 1U &&
                      snapshot_decoded.message->applications.front().id == "app_chrome",
                  "ranking_snapshot.json fixture decodes accurately");

    const auto status_json = ReadMessageFile("collector_status.json");
    context.Check(!status_json.empty(), "collector_status.json fixture loaded");
    const auto status_decoded =
        nfm::ipc::DecodeCollectorStatus(std::as_bytes(std::span{status_json}));
    context.Check(status_decoded.ok() && status_decoded.message.has_value() &&
                      status_decoded.message->etw_session_active &&
                      status_decoded.message->active_pids == 42U,
                  "collector_status.json fixture decodes accurately");

    const auto shutdown_json = ReadMessageFile("shutdown_request.json");
    context.Check(!shutdown_json.empty(), "shutdown_request.json fixture loaded");
    const auto shutdown_decoded =
        nfm::ipc::DecodeShutdownRequest(std::as_bytes(std::span{shutdown_json}));
    context.Check(shutdown_decoded.ok() && shutdown_decoded.message.has_value() &&
                      shutdown_decoded.message->reason == "ui_exit",
                  "shutdown_request.json fixture decodes accurately");
}

} // namespace

int main() {
    TestContext context;
    CheckHeaderGoldenBytes(context);
    CheckAllMessageValuesAndBounds(context);
    CheckStreamingParse(context);
    CheckHeaderRejections(context);
    CheckJsonMessageEncoders(context);
    CheckFixturesLoadedAndValidated(context);
    return context.failures();
}
