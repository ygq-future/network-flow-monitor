#include "network_flow_monitor/ipc/frame_codec.h"
#include "network_flow_monitor/ipc/named_pipe_transport.h"
#include "network_flow_monitor/ipc/security_policy.h"
#include "network_flow_monitor/ipc/windows_pipe_security.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// === ipc_frame_codec_suite ===
namespace ipc_frame_codec_suite {
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

int Run() {
    TestContext context;
    CheckHeaderGoldenBytes(context);
    CheckAllMessageValuesAndBounds(context);
    CheckStreamingParse(context);
    CheckHeaderRejections(context);
    CheckJsonMessageEncoders(context);
    CheckFixturesLoadedAndValidated(context);
    return context.failures();
}
} // namespace ipc_frame_codec_suite

// === ipc_security_policy_suite ===
namespace ipc_security_policy_suite {
namespace {

using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::BuildPipeName;
using nfm::ipc::BuildPipeSecurity;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::PipeSecurityApi;
using nfm::ipc::PipeSecurityError;
using nfm::ipc::QueriedClientIdentity;
using nfm::ipc::VerifyPipeClient;

class TestContext {
  public:
    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures_;
        }
    }
    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

std::vector<std::byte> CopySid(PSID sid) {
    const auto size = GetLengthSid(sid);
    std::vector<std::byte> result(size);
    static_cast<void>(::CopySid(size, result.data(), sid));
    return result;
}

std::vector<std::byte> WellKnownSid(WELL_KNOWN_SID_TYPE type) {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    auto size = static_cast<DWORD>(storage.size());
    if (CreateWellKnownSid(type, nullptr, storage.data(), &size) == FALSE) {
        return {};
    }
    return {storage.begin(), storage.begin() + size};
}

std::vector<std::byte> LogonSid() {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    auto* sid = reinterpret_cast<PSID>(storage.data());
    if (InitializeSid(sid, &authority, 3U) == FALSE) {
        return {};
    }
    *GetSidSubAuthority(sid, 0U) = SECURITY_LOGON_IDS_RID;
    *GetSidSubAuthority(sid, 1U) = 0x1234U;
    *GetSidSubAuthority(sid, 2U) = 0x5678U;
    return CopySid(sid);
}

AuthorizedPrincipal PortablePrincipal() {
    const auto logon_sid = LogonSid();
    return {.mode = CollectorEndpointMode::Portable,
            .authorized_sid = logon_sid,
            .server_logon_sid = logon_sid};
}

std::optional<DWORD> AllowedMaskFor(PACL acl, const std::vector<std::byte>& sid) {
    ACL_SIZE_INFORMATION information{};
    if (GetAclInformation(acl, &information, sizeof(information), AclSizeInformation) == FALSE) {
        return std::nullopt;
    }
    DWORD combined_mask{};
    bool found{};
    for (DWORD index = 0U; index < information.AceCount; ++index) {
        void* raw_ace{};
        if (GetAce(acl, index, &raw_ace) == FALSE || raw_ace == nullptr) {
            return std::nullopt;
        }
        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            continue;
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        // Windows ACL inspection APIs require mutable legacy PSID values.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        auto* ace_sid = const_cast<DWORD*>(&ace->SidStart);
        // -- Win32 EqualSid signature.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        auto* expected_sid = const_cast<std::byte*>(sid.data());
        // -- Win32 EqualSid signature.
        if (EqualSid(ace_sid, expected_sid) != FALSE) {
            combined_mask |= ace->Mask;
            found = true;
        }
    }
    return found ? std::optional<DWORD>{combined_mask} : std::nullopt;
}

void CheckPrincipal(TestContext& context) {
    const auto principal = PortablePrincipal();
    const auto name = BuildPipeName(principal);
    context.Check(name.error == PipeSecurityError::None &&
                      name.name == LR"(\\.\pipe\NetworkFlowMonitor.portable.S-1-5-5-4660-22136)",
                  "pipe name contains the logon SID");

    auto missing = principal;
    missing.authorized_sid.clear();
    context.Check(BuildPipeName(missing).error == PipeSecurityError::MissingSid,
                  "missing authorized SID is rejected");

    auto missing_server = principal;
    missing_server.server_logon_sid.clear();
    context.Check(BuildPipeName(missing_server).error == PipeSecurityError::MissingSid,
                  "missing server SID is rejected");

    auto invalid = principal;
    invalid.mode = CollectorEndpointMode::Invalid;
    context.Check(BuildPipeName(invalid).error == PipeSecurityError::InvalidMode,
                  "unknown endpoint mode is rejected");
}

void CheckDescriptor(TestContext& context) {
    const auto principal = PortablePrincipal();
    auto result = BuildPipeSecurity(principal);
    context.Check(result.error == PipeSecurityError::None && result.descriptor != nullptr,
                  "security descriptor builds");
    if (result.descriptor == nullptr) {
        return;
    }
    BOOL present{};
    BOOL defaulted{};
    PACL acl{};
    auto* descriptor = result.descriptor->attributes()->lpSecurityDescriptor;
    context.Check(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) != FALSE &&
                      present != FALSE && acl != nullptr && defaulted == FALSE,
                  "descriptor has an explicit DACL");
    if (acl == nullptr) {
        return;
    }
    context.Check(AllowedMaskFor(acl, principal.authorized_sid) == FILE_ALL_ACCESS,
                  "shared logon SID receives server access");
    context.Check(!AllowedMaskFor(acl, WellKnownSid(WinWorldSid)).has_value() &&
                      !AllowedMaskFor(acl, WellKnownSid(WinAnonymousSid)).has_value(),
                  "broad principals receive no allow ACE");
}

class FakeSecurityApi final : public PipeSecurityApi {
  public:
    bool Impersonate(HANDLE, std::uint32_t& error) noexcept override {
        ++impersonate_calls;
        error = source_error;
        return impersonate_result;
    }
    bool QueryThreadIdentity(QueriedClientIdentity& output,
                             std::uint32_t& error) noexcept override {
        ++query_calls;
        error = source_error;
        if (query_result) {
            output = identity;
        }
        return query_result;
    }
    bool Revert(std::uint32_t& error) noexcept override {
        ++revert_calls;
        error = source_error;
        return revert_result;
    }

    QueriedClientIdentity identity;
    bool impersonate_result{true};
    bool query_result{true};
    bool revert_result{true};
    std::uint32_t source_error{5U};
    int impersonate_calls{};
    int query_calls{};
    int revert_calls{};
};

void CheckIdentity(TestContext& context) {
    const auto principal = PortablePrincipal();
    FakeSecurityApi valid;
    valid.identity.group_sids = {principal.authorized_sid};
    auto result = VerifyPipeClient(GetCurrentProcess(), principal, valid);
    context.Check(result.authorized() && valid.revert_calls == 1,
                  "matching logon group is authorized and reverted");

    FakeSecurityApi valid_user_sid;
    valid_user_sid.identity.user_sid = principal.authorized_sid;
    valid_user_sid.identity.group_sids = {WellKnownSid(WinBuiltinUsersSid)};
    result = VerifyPipeClient(GetCurrentProcess(), principal, valid_user_sid);
    context.Check(result.authorized() && valid_user_sid.revert_calls == 1,
                  "matching user SID across UAC split token is authorized and reverted");

    FakeSecurityApi wrong;
    wrong.identity.group_sids = {WellKnownSid(WinBuiltinGuestsSid)};
    result = VerifyPipeClient(GetCurrentProcess(), principal, wrong);
    context.Check(result.error == PipeSecurityError::Unauthorized && wrong.revert_calls == 1,
                  "unexpected logon is rejected and reverted");

    FakeSecurityApi impersonation_failure;
    impersonation_failure.impersonate_result = false;
    result = VerifyPipeClient(GetCurrentProcess(), principal, impersonation_failure);
    context.Check(result.error == PipeSecurityError::ImpersonationFailed &&
                      impersonation_failure.query_calls == 0 &&
                      impersonation_failure.revert_calls == 0,
                  "impersonation failure stops before token query");

    FakeSecurityApi query_failure;
    query_failure.query_result = false;
    result = VerifyPipeClient(GetCurrentProcess(), principal, query_failure);
    context.Check(result.error == PipeSecurityError::TokenQueryFailed &&
                      query_failure.revert_calls == 1,
                  "token query failure reverts once");

    FakeSecurityApi revert_failure;
    revert_failure.identity.group_sids = {principal.authorized_sid};
    revert_failure.revert_result = false;
    result = VerifyPipeClient(GetCurrentProcess(), principal, revert_failure);
    context.Check(result.error == PipeSecurityError::RevertFailed, "revert failure fails closed");
}

} // namespace

int Run() {
    TestContext context;
    CheckPrincipal(context);
    CheckDescriptor(context);
    CheckIdentity(context);
    return context.failures() == 0 ? 0 : 1;
}
} // namespace ipc_security_policy_suite

// === ipc_named_pipe_transport_suite ===
namespace ipc_named_pipe_transport_suite {
namespace {

using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::ClientPipeOpenRequest;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::NamedPipeConnection;
using nfm::ipc::OverlappedPipeOperation;
using nfm::ipc::PipeApiCallResult;
using nfm::ipc::PipeIoError;
using nfm::ipc::PipeOperationKind;
using nfm::ipc::PipeReadRequest;
using nfm::ipc::PipeTransportApi;
using nfm::ipc::PipeWriteRequest;
using nfm::ipc::ServerPipeOpenRequest;
using nfm::ipc::WindowsPipeTransportFactory;

class TestContext {
  public:
    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

struct IoAction {
    bool succeeds{true};
    DWORD error{ERROR_SUCCESS};
    DWORD transferred{};
};

IoAction SuccessfulIo(DWORD transferred = 0U) noexcept {
    return {.succeeds = true, .error = ERROR_SUCCESS, .transferred = transferred};
}

IoAction FailedIo(DWORD error) noexcept {
    return {.succeeds = false, .error = error};
}

class FakePipeTransportApi final : public PipeTransportApi {
  public:
    HANDLE CreateManualResetEvent(bool inherit_handle, std::uint32_t& error) noexcept override {
        event_inherit_values.push_back(inherit_handle);
        error = event_error;
        if (event_error != ERROR_SUCCESS) {
            return nullptr;
        }
        auto* const event = event_storage.at(event_count).data();
        ++event_count;
        return event;
    }

    bool ResetManualEvent(HANDLE event, std::uint32_t& error) noexcept override {
        reset_events.push_back(event);
        error = reset_error;
        return reset_error == ERROR_SUCCESS;
    }

    void CloseKernelHandle(HANDLE handle) noexcept override {
        closed_handles.push_back(handle);
    }

    HANDLE CreateServerPipe(const ServerPipeOpenRequest& request,
                            std::uint32_t& error) noexcept override {
        ++create_server_calls;
        server_name = request.name != nullptr ? request.name : L"";
        server_open_mode = request.open_mode;
        server_pipe_mode = request.pipe_mode;
        server_maximum_instances = request.maximum_instances;
        server_security_valid = request.security != nullptr &&
                                request.security->nLength == sizeof(SECURITY_ATTRIBUTES) &&
                                request.security->lpSecurityDescriptor != nullptr;
        error = create_server_error;
        return create_server_error == ERROR_SUCCESS ? server_pipe_storage.data()
                                                    : INVALID_HANDLE_VALUE;
    }

    HANDLE OpenClientPipe(const ClientPipeOpenRequest& request,
                          std::uint32_t& error) noexcept override {
        ++open_client_calls;
        client_name = request.name != nullptr ? request.name : L"";
        client_desired_access = request.desired_access;
        client_share_mode = request.share_mode;
        client_security = request.security;
        client_creation_disposition = request.creation_disposition;
        client_flags_and_attributes = request.flags_and_attributes;
        const auto action = Take(open_client_actions, {});
        error = action.error;
        return action.succeeds ? client_pipe_storage.data() : INVALID_HANDLE_VALUE;
    }

    bool WaitForPipe(LPCWSTR name, DWORD timeout_ms, std::uint32_t& error) noexcept override {
        ++wait_calls;
        waited_name = name != nullptr ? name : L"";
        wait_timeout_ms = timeout_ms;
        const auto action = Take(wait_actions, {});
        error = action.error;
        return action.succeeds;
    }

    bool Connect(HANDLE pipe, OVERLAPPED* operation, std::uint32_t& error) noexcept override {
        connect_pipe = pipe;
        connect_overlapped = operation;
        return Apply(Take(connect_actions, {}), nullptr, error);
    }

    PipeApiCallResult Read(const PipeReadRequest& request) noexcept override {
        read_pipe = request.pipe;
        read_destinations.push_back(request.destination);
        read_lengths.push_back(request.length);
        read_overlapped = request.operation;
        const auto action = Take(read_actions, {});
        return {
            .succeeded = action.succeeds, .error = action.error, .transferred = action.transferred};
    }

    PipeApiCallResult Write(const PipeWriteRequest& request) noexcept override {
        write_pipe = request.pipe;
        write_sources.push_back(request.source);
        write_lengths.push_back(request.length);
        write_overlapped = request.operation;
        const auto action = Take(write_actions, {});
        return {
            .succeeded = action.succeeds, .error = action.error, .transferred = action.transferred};
    }

    bool GetOperationResult(HANDLE pipe, OVERLAPPED* operation, DWORD& transferred, bool wait,
                            std::uint32_t& error) noexcept override {
        result_pipes.push_back(pipe);
        result_operations.push_back(operation);
        result_wait_values.push_back(wait);
        return Apply(Take(completion_actions, {}), &transferred, error);
    }

    bool Cancel(HANDLE pipe, OVERLAPPED* operation, std::uint32_t& error) noexcept override {
        cancel_pipes.push_back(pipe);
        cancel_operations.push_back(operation);
        const auto action = Take(cancel_actions, {});
        error = action.error;
        return action.succeeds;
    }

    bool Disconnect(HANDLE pipe, std::uint32_t& error) noexcept override {
        disconnected_handles.push_back(pipe);
        error = disconnect_error;
        return disconnect_error == ERROR_SUCCESS;
    }

    static IoAction Take(std::deque<IoAction>& actions, IoAction fallback) noexcept {
        if (actions.empty()) {
            return fallback;
        }
        const auto result = actions.front();
        actions.pop_front();
        return result;
    }

    static bool Apply(const IoAction& action, DWORD* transferred, std::uint32_t& error) noexcept {
        if (transferred != nullptr) {
            *transferred = action.transferred;
        }
        error = action.error;
        return action.succeeds;
    }

    std::deque<IoAction> open_client_actions;
    std::deque<IoAction> wait_actions;
    std::deque<IoAction> connect_actions;
    std::deque<IoAction> read_actions;
    std::deque<IoAction> write_actions;
    std::deque<IoAction> completion_actions;
    std::deque<IoAction> cancel_actions;
    std::array<std::byte, 1U> server_pipe_storage{};
    std::array<std::byte, 1U> client_pipe_storage{};
    std::array<std::array<std::byte, 1U>, 12U> event_storage{};
    std::vector<bool> event_inherit_values;
    std::vector<HANDLE> reset_events;
    std::vector<HANDLE> closed_handles;
    std::vector<void*> read_destinations;
    std::vector<DWORD> read_lengths;
    std::vector<const void*> write_sources;
    std::vector<DWORD> write_lengths;
    std::vector<HANDLE> result_pipes;
    std::vector<OVERLAPPED*> result_operations;
    std::vector<bool> result_wait_values;
    std::vector<HANDLE> cancel_pipes;
    std::vector<OVERLAPPED*> cancel_operations;
    std::vector<HANDLE> disconnected_handles;
    std::wstring server_name;
    std::wstring client_name;
    std::wstring waited_name;
    bool server_security_valid{};
    SECURITY_ATTRIBUTES* client_security{};
    OVERLAPPED* connect_overlapped{};
    OVERLAPPED* read_overlapped{};
    OVERLAPPED* write_overlapped{};
    HANDLE connect_pipe{};
    HANDLE read_pipe{};
    HANDLE write_pipe{};
    DWORD server_open_mode{};
    DWORD server_pipe_mode{};
    DWORD server_maximum_instances{};
    DWORD client_desired_access{};
    DWORD client_share_mode{};
    DWORD client_creation_disposition{};
    DWORD client_flags_and_attributes{};
    DWORD wait_timeout_ms{};
    std::size_t event_count{};
    std::uint32_t create_server_calls{};
    std::uint32_t open_client_calls{};
    std::uint32_t wait_calls{};
    std::uint32_t event_error{ERROR_SUCCESS};
    std::uint32_t reset_error{ERROR_SUCCESS};
    std::uint32_t create_server_error{ERROR_SUCCESS};
    std::uint32_t disconnect_error{ERROR_SUCCESS};
};

std::vector<std::byte> CopySid(PSID sid) {
    const auto size = GetLengthSid(sid);
    std::vector<std::byte> result(size);
    static_cast<void>(::CopySid(size, result.data(), sid));
    return result;
}

std::vector<std::byte> LogonSid() {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    auto* sid = reinterpret_cast<PSID>(storage.data());
    if (InitializeSid(sid, &authority, 3U) == FALSE) {
        return {};
    }
    *GetSidSubAuthority(sid, 0U) = SECURITY_LOGON_IDS_RID;
    *GetSidSubAuthority(sid, 1U) = 0x1234U;
    *GetSidSubAuthority(sid, 2U) = 0x5678U;
    return CopySid(sid);
}

AuthorizedPrincipal PortablePrincipal() {
    const auto sid = LogonSid();
    return {
        .mode = CollectorEndpointMode::Portable, .authorized_sid = sid, .server_logon_sid = sid};
}

void CheckFactoryConfiguration(TestContext& context) {
    FakePipeTransportApi api;
    WindowsPipeTransportFactory factory{api};
    {
        auto result = factory.CreateServer(PortablePrincipal(), true);
        context.Check(result.ok(), "server factory succeeds");
        context.Check(api.create_server_calls == 1U && api.server_security_valid,
                      "server receives explicit descriptor");
        context.Check((api.server_open_mode & PIPE_ACCESS_DUPLEX) != 0U &&
                          (api.server_open_mode & FILE_FLAG_OVERLAPPED) != 0U &&
                          (api.server_open_mode & FILE_FLAG_FIRST_PIPE_INSTANCE) != 0U,
                      "server open flags are exact capabilities");
        context.Check(api.server_pipe_mode == (PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                               PIPE_REJECT_REMOTE_CLIENTS) &&
                          api.server_maximum_instances == nfm::ipc::kMaximumPipeInstances,
                      "server byte mode, local-only mode, and instance cap configured");
        context.Check(api.event_inherit_values.size() == 3U && !api.event_inherit_values.at(0U) &&
                          !api.event_inherit_values.at(1U) && !api.event_inherit_values.at(2U),
                      "operation events are not inheritable");
        context.Check(result.connection->connect_event() != result.connection->read_event() &&
                          result.connection->read_event() != result.connection->write_event(),
                      "connect read and write events are separate");
    }
    context.Check(api.disconnected_handles.size() == 1U && api.closed_handles.size() == 4U,
                  "server connection disconnects and closes pipe plus events");

    FakePipeTransportApi subsequent_api;
    WindowsPipeTransportFactory subsequent_factory{subsequent_api};
    auto subsequent = subsequent_factory.CreateServer(PortablePrincipal(), false);
    context.Check(subsequent.ok() &&
                      (subsequent_api.server_open_mode & FILE_FLAG_FIRST_PIPE_INSTANCE) == 0U,
                  "subsequent server instance omits ownership flag");
}

void CheckConnectLifecycle(TestContext& context) {
    FakePipeTransportApi api;
    api.connect_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.cancel_actions.push_back(SuccessfulIo());
    api.completion_actions.push_back(FailedIo(ERROR_OPERATION_ABORTED));
    OverlappedPipeOperation operation{api, api.server_pipe_storage.data(),
                                      PipeOperationKind::Connect};
    context.Check(operation.initialization_result().ok(), "connect operation initializes");
    const auto pending = operation.BeginConnect();
    context.Check(pending.ok() && pending.pending, "connect reports pending");
    const auto cancel = operation.RequestCancel();
    context.Check(cancel.ok() && cancel.pending, "cancel request retains pending lifetime");
    const auto completion = operation.Complete();
    context.Check(completion.error == PipeIoError::WindowsFailure &&
                      completion.win32_error == ERROR_OPERATION_ABORTED &&
                      !api.result_wait_values.empty() && api.result_wait_values.front(),
                  "cancel drains final completion before reuse");

    api.connect_actions.push_back(FailedIo(ERROR_PIPE_CONNECTED));
    const auto raced = operation.BeginConnect();
    context.Check(raced.ok() && !raced.pending && operation.Complete().ok(),
                  "already-connected race is successful completion");

    api.connect_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.cancel_actions.push_back(FailedIo(ERROR_NOT_FOUND));
    api.completion_actions.push_back(SuccessfulIo());
    context.Check(operation.BeginConnect().pending && operation.RequestCancel().ok() &&
                      operation.Complete().ok(),
                  "cancel not-found race accepts normal completion");

    api.connect_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.completion_actions.push_back(FailedIo(ERROR_INVALID_DATA));
    context.Check(operation.BeginConnect().pending &&
                      operation.Complete().win32_error == ERROR_INVALID_DATA,
                  "pending completion failure is retained");
}

void CheckExactTransfers(TestContext& context) {
    FakePipeTransportApi api;
    NamedPipeConnection connection{api, api.server_pipe_storage.data(), false};
    context.Check(connection.initialization_result().ok(), "connection initializes");

    std::array<std::byte, 4U> destination{};
    api.read_actions.push_back(SuccessfulIo(2U));
    api.read_actions.push_back(SuccessfulIo(2U));
    const auto read = connection.BeginReadExact(destination);
    context.Check(read.ok() && !read.pending && read.transferred == destination.size() &&
                      api.read_lengths.size() == 2U && api.read_lengths.at(0U) == 4U &&
                      api.read_lengths.at(1U) == 2U,
                  "partial immediate reads continue to exact length");

    const std::array source{std::byte{1U}, std::byte{2U}, std::byte{3U}, std::byte{4U}};
    api.write_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.completion_actions.push_back(SuccessfulIo(1U));
    api.write_actions.push_back(SuccessfulIo(3U));
    const auto begin_write = connection.BeginWriteAll(source);
    context.Check(begin_write.ok() && begin_write.pending, "write starts pending");
    const auto finish_write = connection.ContinueWriteAll();
    context.Check(finish_write.ok() && finish_write.transferred == source.size() &&
                      api.write_lengths.size() == 2U && api.write_lengths.at(0U) == 4U &&
                      api.write_lengths.at(1U) == 3U && api.write_sources.at(0U) != source.data(),
                  "pending partial write resumes from connection-owned copy");

    api.read_actions.push_back(SuccessfulIo());
    context.Check(connection.BeginReadExact(destination).error == PipeIoError::ZeroProgress,
                  "zero-progress success is rejected");

    api.read_actions.push_back(FailedIo(ERROR_MORE_DATA));
    context.Check(connection.BeginReadExact(destination).error ==
                      PipeIoError::UnexpectedMessageMode,
                  "message-mode result is rejected");

    api.write_actions.push_back(FailedIo(ERROR_BROKEN_PIPE));
    context.Check(connection.BeginWriteAll(source).error == PipeIoError::Disconnected,
                  "broken pipe is classified");
}

void CheckShutdownAndDestructor(TestContext& context) {
    FakePipeTransportApi api;
    {
        NamedPipeConnection connection{api, api.server_pipe_storage.data(), true};
        std::array<std::byte, 2U> destination{};
        api.read_actions.push_back(FailedIo(ERROR_IO_PENDING));
        api.cancel_actions.push_back(SuccessfulIo());
        api.completion_actions.push_back(FailedIo(ERROR_OPERATION_ABORTED));
        context.Check(connection.BeginReadExact(destination).pending,
                      "destructor case has pending read");
        const auto first = connection.CancelAndDrain();
        const auto second = connection.CancelAndDrain();
        context.Check(first.ok() && second.ok() && api.cancel_operations.size() == 1U &&
                          api.result_operations.size() == 1U,
                      "shutdown accepts cancellation and is idempotent");
    }
    context.Check(api.disconnected_handles.size() == 1U && api.closed_handles.size() == 4U,
                  "destructor cleans all owned handles");

    FakePipeTransportApi cancel_failure_api;
    NamedPipeConnection cancel_failure_connection{
        cancel_failure_api, cancel_failure_api.server_pipe_storage.data(), true};
    std::array<std::byte, 1U> destination{};
    cancel_failure_api.read_actions.push_back(FailedIo(ERROR_IO_PENDING));
    cancel_failure_api.cancel_actions.push_back(FailedIo(ERROR_ACCESS_DENIED));
    cancel_failure_api.completion_actions.push_back(FailedIo(ERROR_OPERATION_ABORTED));
    context.Check(cancel_failure_connection.BeginReadExact(destination).pending &&
                      cancel_failure_connection.CancelAndDrain().win32_error ==
                          ERROR_ACCESS_DENIED &&
                      cancel_failure_api.result_operations.size() == 1U,
                  "cancel failure still drains operation before releasing owned buffer");
}

void CheckClientOpenAndFailures(TestContext& context) {
    FakePipeTransportApi api;
    api.open_client_actions.push_back(FailedIo(ERROR_PIPE_BUSY));
    api.wait_actions.push_back(SuccessfulIo());
    api.open_client_actions.push_back(SuccessfulIo());
    WindowsPipeTransportFactory factory{api};
    auto result = factory.CreateClient(PortablePrincipal());
    context.Check(result.ok() && api.open_client_calls == 2U && api.wait_calls == 1U,
                  "busy client waits then retries once");
    context.Check(api.client_desired_access == nfm::ipc::kPipeClientAccessMask &&
                      api.client_share_mode == 0U && api.client_security == nullptr &&
                      api.client_creation_disposition == OPEN_EXISTING &&
                      api.client_flags_and_attributes ==
                          (FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION),
                  "client requests exact rights and identification SQOS");
    context.Check(api.wait_timeout_ms == nfm::ipc::kPipeBusyWait.count(),
                  "busy wait uses bounded transport timeout");

    FakePipeTransportApi absent_api;
    absent_api.open_client_actions.push_back(FailedIo(ERROR_FILE_NOT_FOUND));
    WindowsPipeTransportFactory absent_factory{absent_api};
    context.Check(absent_factory.CreateClient(PortablePrincipal()).error ==
                          PipeIoError::Disconnected &&
                      absent_api.wait_calls == 0U,
                  "non-busy open failure does not wait");

    FakePipeTransportApi timeout_api;
    timeout_api.open_client_actions.push_back(FailedIo(ERROR_PIPE_BUSY));
    timeout_api.wait_actions.push_back(FailedIo(ERROR_SEM_TIMEOUT));
    WindowsPipeTransportFactory timeout_factory{timeout_api};
    context.Check(timeout_factory.CreateClient(PortablePrincipal()).error == PipeIoError::Timeout,
                  "busy wait timeout is classified");

    FakePipeTransportApi conflict_api;
    conflict_api.create_server_error = ERROR_ACCESS_DENIED;
    WindowsPipeTransportFactory conflict_factory{conflict_api};
    context.Check(conflict_factory.CreateServer(PortablePrincipal(), true).error ==
                      PipeIoError::Busy,
                  "first-instance ownership conflict is classified");

    FakePipeTransportApi event_failure_api;
    event_failure_api.event_error = ERROR_NOT_ENOUGH_MEMORY;
    WindowsPipeTransportFactory event_failure_factory{event_failure_api};
    auto event_failure = event_failure_factory.CreateServer(PortablePrincipal(), true);
    context.Check(event_failure.error == PipeIoError::WindowsFailure &&
                      event_failure.win32_error == ERROR_NOT_ENOUGH_MEMORY &&
                      event_failure_api.closed_handles.size() == 1U,
                  "event construction failure closes pipe");
}

} // namespace

int Run() {
    static_assert(nfm::ipc::kMaximumPipeInstances == 4U);
    static_assert(nfm::ipc::kPipeBusyWait.count() <=
                  static_cast<std::int64_t>(std::numeric_limits<DWORD>::max()));
    TestContext context;
    CheckFactoryConfiguration(context);
    CheckConnectLifecycle(context);
    CheckExactTransfers(context);
    CheckShutdownAndDestructor(context);
    CheckClientOpenAndFailures(context);
    return context.failures() == 0 ? 0 : 1;
}
} // namespace ipc_named_pipe_transport_suite

int main() {
    int failures = 0;
    failures += (ipc_frame_codec_suite::Run() != 0 ? 1 : 0);
    failures += (ipc_security_policy_suite::Run() != 0 ? 1 : 0);
    failures += (ipc_named_pipe_transport_suite::Run() != 0 ? 1 : 0);
    return failures;
}
