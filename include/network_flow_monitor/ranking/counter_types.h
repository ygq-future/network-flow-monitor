#ifndef NETWORK_FLOW_MONITOR_RANKING_COUNTER_TYPES_H
#define NETWORK_FLOW_MONITOR_RANKING_COUNTER_TYPES_H

#include "network_flow_monitor/process/application_identity.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace nfm::ranking {

using nfm::process::ApplicationId;
using nfm::process::ApplicationMetadata;

enum class TrafficDirection : std::uint8_t {
    Upload,
    Download,
};

struct ApplicationCounters {
    ApplicationId id;
    ApplicationMetadata metadata;
    std::uint64_t upload_bytes{};
    std::uint64_t download_bytes{};

    bool operator==(const ApplicationCounters&) const = default;
};

struct CounterSnapshot {
    std::chrono::steady_clock::time_point captured_at;
    std::vector<ApplicationCounters> applications;
};

} // namespace nfm::ranking

#endif // NETWORK_FLOW_MONITOR_RANKING_COUNTER_TYPES_H
