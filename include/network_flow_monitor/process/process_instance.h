#ifndef NETWORK_FLOW_MONITOR_PROCESS_PROCESS_INSTANCE_H
#define NETWORK_FLOW_MONITOR_PROCESS_PROCESS_INSTANCE_H

#include <chrono>
#include <cstdint>
#include <ratio>

namespace nfm::process {

using TraceDuration = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
using TraceTimePoint = std::chrono::sys_time<TraceDuration>;

struct ProcessInstanceId {
    std::uint32_t pid{};
    std::uint64_t generation{};

    bool operator==(const ProcessInstanceId&) const = default;
};

} // namespace nfm::process

#endif // NETWORK_FLOW_MONITOR_PROCESS_PROCESS_INSTANCE_H
