#ifndef NETWORK_FLOW_MONITOR_RANKING_RANKING_ENGINE_H
#define NETWORK_FLOW_MONITOR_RANKING_RANKING_ENGINE_H

#include "network_flow_monitor/ranking/counter_types.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nfm::ranking {

struct RankingOptions {
    std::chrono::milliseconds window;
    std::uint8_t top_n;
};

enum class RankingStatus : std::uint8_t {
    WarmingUp,
    Ready,
};

enum class RankingError : std::uint8_t {
    None,
    InvalidWindow,
    InvalidTopN,
    NonIncreasingTimestamp,
    EmptyApplicationId,
    DuplicateApplicationId,
    NonFiniteRate,
};

struct TransferRate {
    double upload_bytes_per_second{};
    double download_bytes_per_second{};

    [[nodiscard]] double total_bytes_per_second() const noexcept;
};

struct RankedApplication {
    ApplicationId id;
    ApplicationMetadata metadata;
    TransferRate rate;
};

struct RankingDiagnostics {
    std::uint64_t counter_discontinuities{};
    std::uint64_t ignored_single_sample_applications{};
};

struct RankingSnapshot {
    RankingStatus status{RankingStatus::WarmingUp};
    std::chrono::steady_clock::duration sampled_span{};
    TransferRate total_rate;
    std::vector<RankedApplication> applications;
    RankingDiagnostics diagnostics;
};

struct RankingResult {
    RankingError error{RankingError::None};
    RankingSnapshot snapshot;

    [[nodiscard]] bool accepted() const noexcept;
};

class RankingEngine;

struct RankingEngineCreateResult {
    std::unique_ptr<RankingEngine> engine;
    RankingError error{RankingError::None};
};

class RankingEngine {
  public:
    class ConstructionToken {
      private:
        ConstructionToken() noexcept = default;
        friend RankingEngineCreateResult CreateRankingEngine(RankingOptions options);
    };

    explicit RankingEngine(ConstructionToken token, RankingOptions options) noexcept;
    [[nodiscard]] RankingResult PushSnapshot(CounterSnapshot snapshot);
    [[nodiscard]] RankingError UpdateOptions(RankingOptions options) noexcept;

  private:
    struct EpochBaseline {
        std::chrono::steady_clock::time_point captured_at;
        std::uint64_t value{};
    };

    struct DirectionEpochs {
        std::optional<EpochBaseline> upload;
        std::optional<EpochBaseline> download;
    };

    RankingOptions options_;
    std::chrono::milliseconds retention_window_;
    std::deque<CounterSnapshot> history_;
    std::unordered_map<ApplicationId, DirectionEpochs> epochs_;
};

[[nodiscard]] RankingEngineCreateResult CreateRankingEngine(RankingOptions options);

} // namespace nfm::ranking

#endif // NETWORK_FLOW_MONITOR_RANKING_RANKING_ENGINE_H
