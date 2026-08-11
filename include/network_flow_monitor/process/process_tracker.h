#ifndef NETWORK_FLOW_MONITOR_PROCESS_PROCESS_TRACKER_H
#define NETWORK_FLOW_MONITOR_PROCESS_PROCESS_TRACKER_H

#include "network_flow_monitor/process/identity_resolver.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nfm::process {

inline constexpr TraceDuration kRetiredProcessRetention =
    std::chrono::duration_cast<TraceDuration>(std::chrono::seconds{30});
inline constexpr std::size_t kMaximumRetiredProcesses = 65'536U;

enum class ProcessStartKind : std::uint8_t {
    Started,
    Rundown,
};

enum class ProcessTrackerError : std::uint8_t {
    None,
    NonIncreasingStartTime,
    EndBeforeStart,
    WatermarkMovedBackward,
    GenerationExhausted,
    NotFound,
    StaleEnd,
    StaleProbeResult,
    InvalidProbeData,
};

struct ProcessStartObservation {
    std::uint32_t pid{};
    TraceTimePoint observed_at;
    std::optional<std::uint64_t> kernel_process_key;
    std::string image_name_hint;
    ProcessStartKind kind{ProcessStartKind::Started};
};

struct ProcessEndObservation {
    std::uint32_t pid{};
    TraceTimePoint observed_at;
    std::optional<std::uint64_t> kernel_process_key;
};

struct ProcessProbeRequest {
    ProcessInstanceId instance;
    TraceTimePoint observed_at;
    ProcessStartKind kind{ProcessStartKind::Started};
};

struct TrackedProcess {
    ProcessInstanceId instance;
    ResolvedApplicationIdentity application;
};

struct ProcessTrackerDiagnostics {
    std::uint64_t implicit_pid_reuses{};
    std::uint64_t stale_end_events{};
    std::uint64_t stale_probe_results{};
    std::uint64_t unmatched_network_events{};
    std::uint64_t expired_retired_processes{};
    std::uint64_t capacity_evictions{};
};

struct ObserveStartResult {
    ProcessTrackerError error{ProcessTrackerError::None};
    ProcessInstanceId instance;
    std::optional<ProcessProbeRequest> probe_request;

    [[nodiscard]] bool accepted() const noexcept;
};

struct ProcessLookupResult {
    ProcessTrackerError error{ProcessTrackerError::None};
    std::optional<TrackedProcess> process;

    [[nodiscard]] bool found() const noexcept;
};

class ProcessTracker {
  public:
    [[nodiscard]] ObserveStartResult ObserveStart(ProcessStartObservation observation);
    [[nodiscard]] ProcessTrackerError ObserveEnd(ProcessEndObservation observation);
    [[nodiscard]] ProcessTrackerError ApplyProbeData(ProcessInstanceId instance,
                                                     const ProcessProbeData& data);
    [[nodiscard]] ProcessLookupResult ResolveAt(std::uint32_t pid, TraceTimePoint event_time);
    [[nodiscard]] ProcessTrackerError AdvanceWatermark(TraceTimePoint watermark);
    [[nodiscard]] const ProcessTrackerDiagnostics& diagnostics() const noexcept;

  private:
    struct ProcessEntry {
        ProcessInstanceId instance;
        TraceTimePoint started_at;
        std::optional<TraceTimePoint> ended_at;
        std::optional<std::uint64_t> kernel_process_key;
        std::string image_name_hint;
        ResolvedApplicationIdentity application;
    };

    friend struct ProcessTrackerTestAccess;

    [[nodiscard]] ProcessEntry* FindEntry(ProcessInstanceId instance) noexcept;
    void RetireActive(const std::unordered_map<std::uint32_t, ProcessEntry>::iterator& active,
                      TraceTimePoint ended_at);
    void EnforceCapacity();

    std::uint64_t next_generation_{1U};
    std::unordered_map<std::uint32_t, ProcessEntry> active_;
    std::vector<ProcessEntry> retired_;
    std::optional<TraceTimePoint> watermark_;
    ProcessTrackerDiagnostics diagnostics_;
};

} // namespace nfm::process

#endif // NETWORK_FLOW_MONITOR_PROCESS_PROCESS_TRACKER_H
