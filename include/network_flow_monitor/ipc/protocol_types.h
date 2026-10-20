#ifndef NETWORK_FLOW_MONITOR_IPC_PROTOCOL_TYPES_H
#define NETWORK_FLOW_MONITOR_IPC_PROTOCOL_TYPES_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nfm::ipc {

inline constexpr std::uint32_t kFrameMagic = 0x314D464EU; // "NFM1" in little-endian
inline constexpr std::uint16_t kFrameHeaderSize = 12U;
inline constexpr std::uint16_t kCurrentProtocolVersion = 1U;
inline constexpr std::uint32_t kMaximumFramePayload = 262'144U; // 256 KiB

// NOLINTNEXTLINE(performance-enum-size): Wire header specifies a fixed 16-bit message type.
enum class MessageType : std::uint16_t {
    Hello = 1,
    HelloAck = 2,
    TrafficTick = 3,
    RankingSnapshot = 4,
    CollectorStatus = 5,
    ShutdownRequest = 6,
};

enum class ProtocolErrorCode : std::uint8_t {
    None = 0,
    MalformedHeader = 1,
    UnsupportedHeader = 2,
    VersionMismatch = 3,
    UnknownMessageType = 4,
    PayloadTooLarge = 5,
    MalformedPayload = 6,
    InvalidUtf8 = 7,
    Unauthorized = 8,
    UnexpectedMessage = 9,
    ServerBusy = 10,
    InternalFailure = 11,
};

enum class FrameParseStatus : std::uint8_t {
    NeedMoreData,
    Decoded,
    Rejected,
};

struct FrameHeader {
    std::uint16_t protocol_version{kCurrentProtocolVersion};
    MessageType message_type{MessageType::Hello};
    std::uint32_t payload_length{};

    bool operator==(const FrameHeader&) const = default;
};

struct FrameView {
    FrameHeader header;
    std::span<const std::byte> payload;
};

struct OwnedFrame {
    FrameHeader header;
    std::vector<std::byte> payload;
};

struct HelloMessage {
    std::uint32_t client_pid{};
    std::uint16_t protocol_version{kCurrentProtocolVersion};
    std::string client_name;

    bool operator==(const HelloMessage&) const = default;
};

struct HelloAckMessage {
    std::uint16_t protocol_version{kCurrentProtocolVersion};
    std::string collector_instance_id;
    std::uint32_t collector_pid{};
    std::vector<std::string> capabilities;

    bool operator==(const HelloAckMessage&) const = default;
};

struct TrafficTickMessage {
    std::uint64_t seq{};
    std::uint64_t monotonic_ms{};
    std::uint64_t download_bytes_per_sec{};
    std::uint64_t upload_bytes_per_sec{};

    bool operator==(const TrafficTickMessage&) const = default;
};

struct RankingApplication {
    std::string id;
    std::string executable_path;
    std::string display_name;
    std::string icon_key;
    std::uint64_t download_bytes_per_sec{};
    std::uint64_t upload_bytes_per_sec{};
    std::uint64_t total_download_bytes{};
    std::uint64_t total_upload_bytes{};

    bool operator==(const RankingApplication&) const = default;
};

struct TotalRate {
    std::uint64_t download_bytes_per_sec{};
    std::uint64_t upload_bytes_per_sec{};

    bool operator==(const TotalRate&) const = default;
};

struct RankingSnapshotMessage {
    std::uint64_t seq{};
    std::uint64_t monotonic_ms{};
    std::string status{"Ready"};
    TotalRate total_rate;
    std::vector<RankingApplication> applications;

    bool operator==(const RankingSnapshotMessage&) const = default;
};

struct CollectorStatusMessage {
    std::uint64_t monotonic_ms{};
    bool etw_session_active{true};
    std::uint64_t dropped_packets{};
    std::uint32_t active_pids{};

    bool operator==(const CollectorStatusMessage&) const = default;
};

struct ShutdownRequestMessage {
    std::string reason{"ui_exit"};

    bool operator==(const ShutdownRequestMessage&) const = default;
};

struct FrameEncodeResult {
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::vector<std::byte> bytes;

    [[nodiscard]] bool ok() const noexcept {
        return error == ProtocolErrorCode::None;
    }
};

struct FrameParseResult {
    FrameParseStatus status{FrameParseStatus::NeedMoreData};
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::size_t consumed{};
    std::size_t required{};
    std::optional<OwnedFrame> frame;
};

template <typename T> struct MessageDecodeResult {
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::optional<T> message;

    [[nodiscard]] bool ok() const noexcept {
        return error == ProtocolErrorCode::None && message.has_value();
    }
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_PROTOCOL_TYPES_H
