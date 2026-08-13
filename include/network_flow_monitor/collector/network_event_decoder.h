#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_NETWORK_EVENT_DECODER_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_NETWORK_EVENT_DECODER_H

#include "network_flow_monitor/collector/event_types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace nfm::collector {

enum class EtwProviderClass : std::uint8_t {
    Tcp,
    Udp,
    Other,
};

enum class NetworkDecodeStatus : std::uint8_t {
    Decoded,
    Ignored,
    Rejected,
};

enum class NetworkDecodeError : std::uint8_t {
    None,
    UnsupportedVersion,
    PayloadTooShort,
    InvalidTimestamp,
    InvariantViolation,
};

struct NetworkEventRecordView {
    EtwProviderClass provider{EtwProviderClass::Other};
    std::uint8_t version{};
    std::uint8_t opcode{};
    process::TraceTimePoint observed_at;
    std::span<const std::byte> payload;
};

struct NetworkDecodeResult {
    NetworkDecodeStatus status{NetworkDecodeStatus::Ignored};
    NetworkDecodeError error{NetworkDecodeError::None};
    std::optional<NetworkTrafficEvent> event;
};

[[nodiscard]] NetworkDecodeResult DecodeNetworkEvent(const NetworkEventRecordView& record) noexcept;

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_NETWORK_EVENT_DECODER_H
