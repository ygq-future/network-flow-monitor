#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_CORE_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_CORE_H

#include "network_flow_monitor/collector/collector_health.h"
#include "network_flow_monitor/collector/event_types.h"
#include "network_flow_monitor/process/process_tracker.h"
#include "network_flow_monitor/ranking/application_accumulator.h"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace nfm::collector {

inline constexpr std::string_view kUnattributedApplicationId = ranking::kUnattributedApplicationId;

enum class CollectorProcessProbeError : std::uint8_t {
    None,
    ProcessUnavailable,
    AccessDenied,
    PidReusedBeforeProbe,
    InvalidData,
    OutOfMemory,
    WindowsFailure,
};

struct CollectorProcessProbeResult {
    CollectorProcessProbeError error{CollectorProcessProbeError::None};
    CollectorErrorRecord diagnostic;
    std::uint32_t source_stage{};
    process::ProcessProbeData data;

    [[nodiscard]] bool ok() const noexcept;
};

class ProcessProbe {
  public:
    virtual ~ProcessProbe() = default;
    ProcessProbe(const ProcessProbe&) = delete;
    ProcessProbe& operator=(const ProcessProbe&) = delete;
    ProcessProbe(ProcessProbe&&) = delete;
    ProcessProbe& operator=(ProcessProbe&&) = delete;

    [[nodiscard]] virtual CollectorProcessProbeResult Probe(
        const process::ProcessProbeRequest& request) noexcept = 0;

  protected:
    ProcessProbe() = default;
};

enum class CollectorCoreError : std::uint8_t {
    None,
    LifecycleRejected,
    CounterOverflow,
    CollectorFailed,
};

struct CollectorCoreResult {
    CollectorCoreError error{CollectorCoreError::None};

    [[nodiscard]] bool ok() const noexcept;
};

class CollectorCore {
  public:
    explicit CollectorCore(ProcessProbe& process_probe);
    ~CollectorCore() = default;
    CollectorCore(const CollectorCore&) = delete;
    CollectorCore& operator=(const CollectorCore&) = delete;
    CollectorCore(CollectorCore&&) = delete;
    CollectorCore& operator=(CollectorCore&&) = delete;

    [[nodiscard]] CollectorCoreResult Consume(const NetworkTrafficEvent& event);
    [[nodiscard]] CollectorCoreResult Consume(process::ProcessStartObservation observation);
    [[nodiscard]] CollectorCoreResult Consume(process::ProcessEndObservation observation);
    [[nodiscard]] CollectorCoreResult AdvanceWatermark(process::TraceTimePoint watermark);
    void RecordEtwEvent(CollectorEventDisposition disposition) noexcept;
    void RecordExternalError(CollectorErrorRecord error, bool fatal, bool callback_failure,
                             bool unexpected_failure) noexcept;
    void SetState(CollectorState state) noexcept;
    void UpdateEtwStatistics(EtwSessionStatistics statistics) noexcept;
    [[nodiscard]] ranking::CounterSnapshot MakeSnapshot(
        std::chrono::steady_clock::time_point captured_at) const;
    [[nodiscard]] const CollectorHealth& health() const noexcept;
    [[nodiscard]] std::size_t active_process_count() const noexcept;

  private:
    friend struct CollectorCoreTestAccess;

    [[nodiscard]] bool IsFailed() const noexcept;
    void RecordError(CollectorErrorRecord error) noexcept;
    void RecordLifecycleError(process::ProcessTrackerError error) noexcept;

    ProcessProbe& process_probe_;
    process::ProcessTracker process_tracker_;
    ranking::ApplicationAccumulator accumulator_;
    CollectorHealth health_;
    EtwSessionStatistics etw_statistics_;
};

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_CORE_H
