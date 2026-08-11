#ifndef NETWORK_FLOW_MONITOR_IPC_PROTOCOL_TYPES_H
#define NETWORK_FLOW_MONITOR_IPC_PROTOCOL_TYPES_H

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace nfm::ipc {

inline constexpr std::uint32_t kFrameMagic = 0x314D464EU;
inline constexpr std::uint16_t kFrameHeaderSize = 24U;
inline constexpr std::uint16_t kCurrentProtocolVersion = 1U;
inline constexpr std::uint32_t kMaximumFramePayload = 1'048'576U;

enum class MessageType : std::uint8_t {
    ClientHello = 1,
    ServerHello = 2,
    Subscribe = 3,
    SubscriptionAccepted = 4,
    ApplicationCatalog = 5,
    Snapshot = 6,
    CollectorStatus = 7,
    ProtocolError = 8,
};

enum class ProtocolErrorCode : std::uint8_t {
    None = 0,
    MalformedHeader = 1,
    UnsupportedHeader = 2,
    VersionMismatch = 3,
    UnknownMessageType = 4,
    InvalidFlags = 5,
    PayloadTooLarge = 6,
    MalformedPayload = 7,
    InvalidUtf8 = 8,
    InvalidSubscription = 9,
    UnexpectedMessage = 10,
    CatalogOutOfOrder = 11,
    Unauthorized = 12,
    ServerBusy = 13,
    InternalFailure = 14,
};

enum class FrameParseStatus : std::uint8_t {
    NeedMoreData,
    Decoded,
    Rejected,
};

enum class MessageDirection : std::uint8_t {
    ClientToServer,
    ServerToClient,
};

struct FrameHeader {
    std::uint16_t protocol_version{};
    MessageType message_type{MessageType::ClientHello};
    std::uint64_t request_id{};
    std::uint32_t payload_length{};

    bool operator==(const FrameHeader&) const = default;
};

struct CollectorInstanceId {
    std::array<std::byte, 16> bytes{};

    bool operator==(const CollectorInstanceId&) const = default;
};

struct FrameView {
    FrameHeader header;
    std::span<const std::byte> payload;
};

struct OwnedFrame {
    FrameHeader header;
    std::vector<std::byte> payload;
};

struct ClientHello {
    std::uint16_t minimum_version{};
    std::uint16_t maximum_version{};
    std::chrono::milliseconds requested_refresh;

    bool operator==(const ClientHello&) const = default;
};

struct ServerHello {
    std::uint16_t selected_version{};
    std::uint32_t maximum_frame_payload{};
    CollectorInstanceId instance;

    bool operator==(const ServerHello&) const = default;
};

struct Subscribe {
    std::chrono::milliseconds requested_refresh;

    bool operator==(const Subscribe&) const = default;
};

struct SubscriptionAccepted {
    std::chrono::milliseconds effective_refresh;

    bool operator==(const SubscriptionAccepted&) const = default;
};

struct ProtocolErrorMessage {
    ProtocolErrorCode code{ProtocolErrorCode::None};
    std::uint8_t retry_disposition{};
    std::uint16_t server_minimum_version{};
    std::uint16_t server_maximum_version{};
    std::uint32_t win32_error{};

    bool operator==(const ProtocolErrorMessage&) const = default;
};

using ControlMessage =
    std::variant<ClientHello, ServerHello, Subscribe, SubscriptionAccepted, ProtocolErrorMessage>;

struct FrameEncodeResult {
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::vector<std::byte> bytes;

    [[nodiscard]] bool ok() const noexcept;
};

struct FrameParseResult {
    FrameParseStatus status{FrameParseStatus::NeedMoreData};
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::size_t consumed{};
    std::size_t required{};
    std::optional<OwnedFrame> frame;
};

struct ControlDecodeResult {
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::optional<ControlMessage> message;

    [[nodiscard]] bool ok() const noexcept;
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_PROTOCOL_TYPES_H
