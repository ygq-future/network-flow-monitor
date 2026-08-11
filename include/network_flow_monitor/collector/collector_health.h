#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_HEALTH_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_HEALTH_H

#include <cstdint>

namespace nfm::collector {

enum class CollectorState : std::uint8_t {
    Stopped,
    Starting,
    Running,
    Degraded,
    Failed,
};

enum class CollectorEventDisposition : std::uint8_t {
    Decoded,
    Ignored,
    Malformed,
    UnsupportedSchema,
};

enum class CollectorErrorDomain : std::uint8_t {
    None,
    Etw,
    Win32,
    Tdh,
    ProcessProbe,
    ProcessTracker,
    Accumulator,
    Callback,
    Internal,
};

enum class CollectorErrorStage : std::uint8_t {
    None,
    StartSession,
    QuerySession,
    FlushSession,
    StopSession,
    OpenConsumer,
    DecodeTimestamp,
    DecodeNetworkEvent,
    DecodeProcessEvent,
    ProbeProcess,
    ApplyProcessLifecycle,
    AccumulateTraffic,
    ProcessTrace,
    EventCallback,
    BufferCallback,
};

struct CollectorErrorRecord {
    CollectorErrorDomain domain{CollectorErrorDomain::None};
    CollectorErrorStage stage{CollectorErrorStage::None};
    std::uint32_t code{};

    [[nodiscard]] bool present() const noexcept {
        return domain != CollectorErrorDomain::None;
    }

    bool operator==(const CollectorErrorRecord&) const = default;
};

struct EtwSessionStatistics {
    std::uint64_t events_lost{};
    std::uint64_t log_buffers_lost{};
    std::uint64_t realtime_buffers_lost{};

    bool operator==(const EtwSessionStatistics&) const = default;
};

struct CollectorHealth {
    CollectorState state{CollectorState::Running};
    CollectorErrorRecord first_error;
    CollectorErrorRecord last_error;
    std::uint64_t events_received{};
    std::uint64_t events_decoded{};
    std::uint64_t events_ignored{};
    std::uint64_t malformed_events{};
    std::uint64_t unsupported_schema_events{};
    std::uint64_t network_events_counted{};
    std::uint64_t network_bytes_counted{};
    std::uint64_t tcp_events{};
    std::uint64_t tcp_bytes{};
    std::uint64_t udp_events{};
    std::uint64_t udp_bytes{};
    std::uint64_t ipv4_events{};
    std::uint64_t ipv4_bytes{};
    std::uint64_t ipv6_events{};
    std::uint64_t ipv6_bytes{};
    std::uint64_t upload_events{};
    std::uint64_t upload_bytes{};
    std::uint64_t download_events{};
    std::uint64_t download_bytes{};
    std::uint64_t process_start_events{};
    std::uint64_t process_rundown_events{};
    std::uint64_t process_end_events{};
    bool system_process_rundown_confirmed{};
    std::uint64_t process_probe_failures{};
    std::uint64_t lifecycle_errors{};
    std::uint64_t unmatched_network_events{};
    std::uint64_t unmatched_network_bytes{};
    std::uint64_t tcp_retransmit_events{};
    std::uint64_t tcp_retransmitted_bytes{};
    std::uint64_t events_lost{};
    std::uint64_t log_buffers_lost{};
    std::uint64_t realtime_buffers_lost{};
    std::uint64_t statistics_resets{};
    std::uint64_t callback_failures{};
    std::uint64_t unexpected_failures{};
    std::uint64_t counter_overflows{};
    std::uint64_t active_application_capacity_hits{};
    std::uint64_t aggregated_application_events{};
    std::uint64_t aggregated_application_bytes{};
};

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_HEALTH_H
