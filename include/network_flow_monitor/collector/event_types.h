#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_EVENT_TYPES_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_EVENT_TYPES_H

#include "network_flow_monitor/process/process_tracker.h"
#include "network_flow_monitor/ranking/counter_types.h"

#include <cstdint>
#include <variant>

namespace nfm::collector {

enum class NetworkProtocol : std::uint8_t {
    Tcp,
    Udp,
};

enum class IpVersion : std::uint8_t {
    V4,
    V6,
};

struct NetworkTrafficEvent {
    std::uint32_t pid{};
    process::TraceTimePoint observed_at;
    NetworkProtocol protocol{NetworkProtocol::Tcp};
    IpVersion ip_version{IpVersion::V4};
    ranking::TrafficDirection direction{ranking::TrafficDirection::Upload};
    std::uint64_t bytes{};
    bool retransmission{};
};

using CollectorEvent = std::variant<NetworkTrafficEvent, process::ProcessStartObservation,
                                    process::ProcessEndObservation>;

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_EVENT_TYPES_H
