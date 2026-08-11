#include "network_flow_monitor/ranking/ranking_engine.h"

#include "network_flow_monitor/config/app_config.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace nfm::ranking {
namespace {

RankingError ValidateOptions(RankingOptions options) noexcept {
    if (options.window < config::kMinimumRankingWindow ||
        options.window > config::kMaximumRankingWindow) {
        return RankingError::InvalidWindow;
    }
    if (options.top_n < config::kMinimumTopN || options.top_n > config::kMaximumTopN) {
        return RankingError::InvalidTopN;
    }
    return RankingError::None;
}

RankingError ValidateApplications(const CounterSnapshot& snapshot) {
    std::unordered_set<std::string_view> ids;
    ids.reserve(snapshot.applications.size());
    for (const auto& application : snapshot.applications) {
        if (application.id.empty()) {
            return RankingError::EmptyApplicationId;
        }
        if (!ids.emplace(application.id).second) {
            return RankingError::DuplicateApplicationId;
        }
    }
    return RankingError::None;
}

const ApplicationCounters* FindApplication(const CounterSnapshot& snapshot, std::string_view id) {
    const auto application = std::ranges::find(snapshot.applications, id, &ApplicationCounters::id);
    return application == snapshot.applications.end() ? nullptr : &*application;
}

bool IsFinite(const TransferRate& rate) noexcept {
    return std::isfinite(rate.upload_bytes_per_second) &&
           std::isfinite(rate.download_bytes_per_second) &&
           std::isfinite(rate.total_bytes_per_second());
}

bool RankingOrder(const RankedApplication& left, const RankedApplication& right) {
    if (left.rate.total_bytes_per_second() != right.rate.total_bytes_per_second()) {
        return left.rate.total_bytes_per_second() > right.rate.total_bytes_per_second();
    }
    if (left.rate.download_bytes_per_second != right.rate.download_bytes_per_second) {
        return left.rate.download_bytes_per_second > right.rate.download_bytes_per_second;
    }
    if (left.rate.upload_bytes_per_second != right.rate.upload_bytes_per_second) {
        return left.rate.upload_bytes_per_second > right.rate.upload_bytes_per_second;
    }
    return left.id < right.id;
}

RankingSnapshot WarmingSnapshot(std::chrono::steady_clock::duration sampled_span,
                                RankingDiagnostics diagnostics) {
    return RankingSnapshot{.status = RankingStatus::WarmingUp,
                           .sampled_span = sampled_span,
                           .diagnostics = diagnostics};
}

} // namespace

double TransferRate::total_bytes_per_second() const noexcept {
    return upload_bytes_per_second + download_bytes_per_second;
}

bool RankingResult::accepted() const noexcept {
    return error == RankingError::None;
}

RankingEngine::RankingEngine(ConstructionToken, RankingOptions options) noexcept
    : options_{options}, retention_window_{options.window} {}

RankingResult RankingEngine::PushSnapshot(CounterSnapshot snapshot) {
    if (!history_.empty() && snapshot.captured_at <= history_.back().captured_at) {
        return {.error = RankingError::NonIncreasingTimestamp};
    }
    if (const auto error = ValidateApplications(snapshot); error != RankingError::None) {
        return {.error = error};
    }

    auto candidate_history = history_;
    candidate_history.push_back(std::move(snapshot));
    const auto& current = candidate_history.back();
    const auto retention_cutoff = current.captured_at - retention_window_;
    while (candidate_history.size() > 1U && candidate_history[1].captured_at <= retention_cutoff) {
        candidate_history.pop_front();
    }

    auto candidate_epochs = epochs_;
    RankingDiagnostics diagnostics;
    for (const auto& current_application : current.applications) {
        const ApplicationCounters* previous_application = nullptr;
        for (auto previous = std::next(candidate_history.crbegin());
             previous != candidate_history.crend(); ++previous) {
            previous_application = FindApplication(*previous, current_application.id);
            if (previous_application != nullptr) {
                break;
            }
        }
        if (previous_application == nullptr) {
            continue;
        }

        auto& application_epochs = candidate_epochs[current_application.id];
        if (current_application.upload_bytes < previous_application->upload_bytes) {
            application_epochs.upload = EpochBaseline{.captured_at = current.captured_at,
                                                      .value = current_application.upload_bytes};
            ++diagnostics.counter_discontinuities;
        }
        if (current_application.download_bytes < previous_application->download_bytes) {
            application_epochs.download = EpochBaseline{
                .captured_at = current.captured_at, .value = current_application.download_bytes};
            ++diagnostics.counter_discontinuities;
        }
    }

    const auto cutoff = current.captured_at - options_.window;
    auto baseline = candidate_history.end();
    for (auto candidate = candidate_history.begin(); candidate != candidate_history.end();
         ++candidate) {
        if (candidate->captured_at > cutoff) {
            break;
        }
        baseline = candidate;
    }

    const auto elapsed_history = current.captured_at - candidate_history.front().captured_at;
    if (baseline == candidate_history.end()) {
        history_ = std::move(candidate_history);
        epochs_ = std::move(candidate_epochs);
        return {.snapshot = WarmingSnapshot(elapsed_history, diagnostics)};
    }

    RankingSnapshot output{.status = RankingStatus::Ready,
                           .sampled_span = current.captured_at - baseline->captured_at,
                           .diagnostics = diagnostics};

    for (const auto& current_application : current.applications) {
        const ApplicationCounters* application_baseline =
            FindApplication(*baseline, current_application.id);
        auto application_baseline_time = baseline->captured_at;
        auto application_span = output.sampled_span;
        if (application_baseline == nullptr) {
            for (auto history = std::next(baseline); history != candidate_history.end();
                 ++history) {
                application_baseline = FindApplication(*history, current_application.id);
                if (application_baseline != nullptr) {
                    application_baseline_time = history->captured_at;
                    application_span = current.captured_at - history->captured_at;
                    break;
                }
            }
        }
        if (application_baseline == nullptr ||
            application_span <= std::chrono::steady_clock::duration::zero()) {
            ++output.diagnostics.ignored_single_sample_applications;
            continue;
        }

        auto& application_epochs = candidate_epochs[current_application.id];
        auto calculate_direction = [&](std::uint64_t baseline_value,
                                       std::chrono::steady_clock::time_point baseline_time,
                                       std::uint64_t current_value,
                                       std::optional<EpochBaseline>& epoch) {
            if (epoch.has_value() && epoch->captured_at > baseline_time) {
                baseline_time = epoch->captured_at;
                baseline_value = epoch->value;
            }
            if (current_value < baseline_value) {
                epoch = EpochBaseline{.captured_at = current.captured_at, .value = current_value};
                ++output.diagnostics.counter_discontinuities;
                return 0.0;
            }
            const auto span = current.captured_at - baseline_time;
            if (span <= std::chrono::steady_clock::duration::zero()) {
                return 0.0;
            }
            const double seconds = std::chrono::duration<double>{span}.count();
            return static_cast<double>(current_value - baseline_value) / seconds;
        };

        TransferRate rate{
            .upload_bytes_per_second =
                calculate_direction(application_baseline->upload_bytes, application_baseline_time,
                                    current_application.upload_bytes, application_epochs.upload),
            .download_bytes_per_second = calculate_direction(
                application_baseline->download_bytes, application_baseline_time,
                current_application.download_bytes, application_epochs.download),
        };
        if (!IsFinite(rate)) {
            return {.error = RankingError::NonFiniteRate};
        }
        output.total_rate.upload_bytes_per_second += rate.upload_bytes_per_second;
        output.total_rate.download_bytes_per_second += rate.download_bytes_per_second;
        if (rate.total_bytes_per_second() != 0.0) {
            output.applications.push_back(
                RankedApplication{.id = current_application.id,
                                  .metadata = current_application.metadata,
                                  .rate = rate});
        }
    }

    if (!IsFinite(output.total_rate)) {
        return {.error = RankingError::NonFiniteRate};
    }
    std::ranges::sort(output.applications, RankingOrder);
    if (output.applications.size() > options_.top_n) {
        output.applications.resize(options_.top_n);
    }
    for (auto epoch = candidate_epochs.begin(); epoch != candidate_epochs.end();) {
        const bool is_current = FindApplication(current, epoch->first) != nullptr;
        const bool is_retained = std::ranges::any_of(candidate_history, [&](const auto& retained) {
            return FindApplication(retained, epoch->first) != nullptr;
        });
        if (!is_current && !is_retained) {
            epoch = candidate_epochs.erase(epoch);
        } else {
            ++epoch;
        }
    }
    history_ = std::move(candidate_history);
    epochs_ = std::move(candidate_epochs);
    return {.snapshot = std::move(output)};
}

RankingError RankingEngine::UpdateOptions(RankingOptions options) noexcept {
    if (const auto error = ValidateOptions(options); error != RankingError::None) {
        return error;
    }
    options_ = options;
    retention_window_ = std::max(retention_window_, options.window);
    return RankingError::None;
}

RankingEngineCreateResult CreateRankingEngine(RankingOptions options) {
    if (const auto error = ValidateOptions(options); error != RankingError::None) {
        return {.engine = nullptr, .error = error};
    }
    return {.engine = std::make_unique<RankingEngine>(RankingEngine::ConstructionToken{}, options),
            .error = RankingError::None};
}

} // namespace nfm::ranking
