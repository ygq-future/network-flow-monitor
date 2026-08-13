#include "network_flow_monitor/collector/network_event_decoder.h"

#include <cstdint>
#include <cstring>

namespace nfm::collector {
namespace {

struct EventClassification {
    bool counted{};
    NetworkProtocol protocol{NetworkProtocol::Tcp};
    IpVersion ip_version{IpVersion::V4};
    ranking::TrafficDirection direction{ranking::TrafficDirection::Upload};
    bool retransmission{};
};

EventClassification ClassifyTcp(std::uint8_t opcode) noexcept {
    switch (opcode) {
    case 10U:
        return {.counted = true};
    case 11U:
        return {.counted = true, .direction = ranking::TrafficDirection::Download};
    case 14U:
        return {.counted = true, .retransmission = true};
    case 26U:
        return {.counted = true, .ip_version = IpVersion::V6};
    case 27U:
        return {.counted = true,
                .ip_version = IpVersion::V6,
                .direction = ranking::TrafficDirection::Download};
    case 30U:
        return {.counted = true, .ip_version = IpVersion::V6, .retransmission = true};
    default:
        return {};
    }
}

EventClassification ClassifyUdp(std::uint8_t opcode) noexcept {
    switch (opcode) {
    case 10U:
        return {.counted = true, .protocol = NetworkProtocol::Udp};
    case 11U:
        return {.counted = true,
                .protocol = NetworkProtocol::Udp,
                .direction = ranking::TrafficDirection::Download};
    case 26U:
        return {.counted = true, .protocol = NetworkProtocol::Udp, .ip_version = IpVersion::V6};
    case 27U:
        return {.counted = true,
                .protocol = NetworkProtocol::Udp,
                .ip_version = IpVersion::V6,
                .direction = ranking::TrafficDirection::Download};
    default:
        return {.protocol = NetworkProtocol::Udp};
    }
}

std::uint32_t ReadUint32(std::span<const std::byte> payload, std::size_t offset) noexcept {
    std::uint32_t value{};
    std::memcpy(&value, payload.data() + offset, sizeof(value));
    return value;
}

} // namespace

NetworkDecodeResult DecodeNetworkEvent(const NetworkEventRecordView& record) noexcept {
    if (record.provider == EtwProviderClass::Other) {
        return {};
    }
    if (record.version != 2U) {
        return {.status = NetworkDecodeStatus::Rejected,
                .error = NetworkDecodeError::UnsupportedVersion};
    }

    const EventClassification classification = record.provider == EtwProviderClass::Tcp
                                                   ? ClassifyTcp(record.opcode)
                                                   : ClassifyUdp(record.opcode);
    if (!classification.counted) {
        return {};
    }
    constexpr std::size_t required_size = sizeof(std::uint32_t) * 2U;
    if (record.payload.size() < required_size) {
        return {.status = NetworkDecodeStatus::Rejected,
                .error = NetworkDecodeError::PayloadTooShort};
    }

    return {.status = NetworkDecodeStatus::Decoded,
            .event = NetworkTrafficEvent{.pid = ReadUint32(record.payload, 0U),
                                         .observed_at = record.observed_at,
                                         .protocol = classification.protocol,
                                         .ip_version = classification.ip_version,
                                         .direction = classification.direction,
                                         .bytes = ReadUint32(record.payload, sizeof(std::uint32_t)),
                                         .retransmission = classification.retransmission}};
}

} // namespace nfm::collector
