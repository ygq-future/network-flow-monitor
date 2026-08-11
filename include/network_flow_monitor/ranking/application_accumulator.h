#ifndef NETWORK_FLOW_MONITOR_RANKING_APPLICATION_ACCUMULATOR_H
#define NETWORK_FLOW_MONITOR_RANKING_APPLICATION_ACCUMULATOR_H

#include "network_flow_monitor/ranking/counter_types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace nfm::ranking {

using AccumulationDuration = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
using AccumulationTimePoint = std::chrono::sys_time<AccumulationDuration>;

inline constexpr AccumulationDuration kActiveApplicationRetention =
    std::chrono::duration_cast<AccumulationDuration>(std::chrono::seconds{15});
inline constexpr std::size_t kMaximumActiveApplications = 4'096U;
inline constexpr std::size_t kMaximumDynamicApplications = 4'094U;
inline constexpr std::string_view kUnattributedApplicationId{"collector:unattributed"};
inline constexpr std::string_view kOtherApplicationId{"collector:other"};

enum class AccumulationError : std::uint8_t {
    None,
    EmptyApplicationId,
    CounterOverflow,
    WatermarkMovedBackward,
};

enum class AccumulationDisposition : std::uint8_t {
    Counted,
    AggregatedToOther,
};

struct AccumulationResult {
    AccumulationError error{AccumulationError::None};
    AccumulationDisposition disposition{AccumulationDisposition::Counted};

    [[nodiscard]] bool ok() const noexcept;
};

struct AccumulatorAdvanceResult {
    AccumulationError error{AccumulationError::None};
    std::size_t evicted{};
};

class ApplicationAccumulator {
  public:
    [[nodiscard]] AccumulationResult AddTraffic(std::string_view id,
                                                const ApplicationMetadata& metadata,
                                                AccumulationTimePoint observed_at,
                                                TrafficDirection direction, std::uint64_t bytes);
    [[nodiscard]] AccumulatorAdvanceResult AdvanceWatermark(AccumulationTimePoint watermark);
    [[nodiscard]] CounterSnapshot MakeSnapshot(
        std::chrono::steady_clock::time_point captured_at) const;
    [[nodiscard]] std::uint64_t metadata_conflicts() const noexcept;

  private:
    struct ActiveApplicationRecord {
        ApplicationCounters counters;
        AccumulationTimePoint last_activity;
        bool fixed_bucket{};
    };

    std::unordered_map<ApplicationId, ActiveApplicationRecord> records_;
    std::optional<AccumulationTimePoint> watermark_;
    std::size_t dynamic_application_count_{};
    std::uint64_t metadata_conflicts_{};
};

} // namespace nfm::ranking

#endif // NETWORK_FLOW_MONITOR_RANKING_APPLICATION_ACCUMULATOR_H
