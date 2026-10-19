#include "network_flow_monitor/ranking/application_accumulator.h"
#include "network_flow_monitor/ranking/ranking_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// === application_accumulator_suite ===
namespace application_accumulator_suite {
namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

using nfm::ranking::AccumulationDisposition;
using nfm::ranking::AccumulationDuration;
using nfm::ranking::AccumulationTimePoint;
using nfm::ranking::ApplicationAccumulator;
using nfm::ranking::ApplicationCounters;
using nfm::ranking::ApplicationMetadata;
using nfm::ranking::CounterSnapshot;
using nfm::ranking::TrafficDirection;

AccumulationTimePoint At(std::chrono::milliseconds offset) {
    return AccumulationTimePoint{std::chrono::duration_cast<AccumulationDuration>(offset)};
}

const ApplicationCounters* FindApplication(const CounterSnapshot& snapshot, std::string_view id) {
    for (const auto& application : snapshot.applications) {
        if (application.id == id) {
            return &application;
        }
    }
    return nullptr;
}

void CheckBasicAccumulation(TestContext& context) {
    ApplicationAccumulator accumulator;
    const ApplicationMetadata metadata{.display_name = "Browser", .icon_key = "browser.exe"};

    context.Check(accumulator
                      .AddTraffic("app.browser", metadata, At(std::chrono::milliseconds{0}),
                                  TrafficDirection::Upload, 128U)
                      .ok(),
                  "one upload accepted");
    context.Check(accumulator
                      .AddTraffic("app.browser", metadata, At(std::chrono::milliseconds{1}),
                                  TrafficDirection::Download, 512U)
                      .ok(),
                  "download accepted");
    context.Check(accumulator
                      .AddTraffic("app.browser", metadata, At(std::chrono::milliseconds{2}),
                                  TrafficDirection::Upload, 64U)
                      .ok(),
                  "second process upload accepted");

    const auto captured_at = std::chrono::steady_clock::time_point{std::chrono::seconds{1}};
    const auto snapshot = accumulator.MakeSnapshot(captured_at);
    context.Check(snapshot.captured_at == captured_at, "snapshot preserves timestamp");
    context.Check(snapshot.applications.size() == 1U, "same application ID is merged");
    const auto* const application = FindApplication(snapshot, "app.browser");
    context.Check(application != nullptr, "merged application exists");
    if (application != nullptr) {
        context.Check(application->metadata == metadata, "metadata preserved");
        context.Check(application->upload_bytes == 192U, "uploads accumulated");
        context.Check(application->download_bytes == 512U, "downloads accumulated separately");
    }
}

void CheckValidationAndOrdering(TestContext& context) {
    ApplicationAccumulator accumulator;
    const ApplicationMetadata empty_metadata{};

    const auto empty_id = accumulator.AddTraffic(
        "", empty_metadata, At(std::chrono::milliseconds{0}), TrafficDirection::Upload, 1U);
    context.Check(empty_id.error == nfm::ranking::AccumulationError::EmptyApplicationId,
                  "empty ID rejected");

    context.Check(accumulator
                      .AddTraffic("app.zero", empty_metadata, At(std::chrono::milliseconds{0}),
                                  TrafficDirection::Upload, 0U)
                      .ok(),
                  "zero byte input accepted as no-op");
    context.Check(accumulator
                      .AddTraffic("app.z", empty_metadata, At(std::chrono::milliseconds{0}),
                                  TrafficDirection::Upload, 1U)
                      .ok(),
                  "high ID accepted");
    context.Check(accumulator
                      .AddTraffic("app.a", empty_metadata, At(std::chrono::milliseconds{0}),
                                  TrafficDirection::Download, 2U)
                      .ok(),
                  "low ID accepted");

    const auto snapshot = accumulator.MakeSnapshot(std::chrono::steady_clock::time_point{});
    context.Check(snapshot.applications.size() == 2U, "empty ID and zero-byte input add no slot");
    if (snapshot.applications.size() == 2U) {
        context.Check(snapshot.applications[0].id == "app.a", "snapshot first ID sorted");
        context.Check(snapshot.applications[1].id == "app.z", "snapshot second ID sorted");
    }
}

void CheckMetadataRules(TestContext& context) {
    ApplicationAccumulator accumulator;
    const ApplicationMetadata display_only{.display_name = "Editor", .icon_key = {}};
    const ApplicationMetadata icon_only{.display_name = {}, .icon_key = "editor.exe"};
    const ApplicationMetadata conflicting{.display_name = "Other Editor", .icon_key = "editor.exe"};

    context.Check(accumulator
                      .AddTraffic("app.editor", display_only, At(std::chrono::milliseconds{0}),
                                  TrafficDirection::Upload, 1U)
                      .ok(),
                  "initial metadata accepted");
    context.Check(accumulator
                      .AddTraffic("app.editor", icon_only, At(std::chrono::milliseconds{1}),
                                  TrafficDirection::Download, 1U)
                      .ok(),
                  "empty metadata field enriched");
    context.Check(accumulator
                      .AddTraffic("app.editor", conflicting, At(std::chrono::milliseconds{2}),
                                  TrafficDirection::Upload, 1U)
                      .ok(),
                  "conflicting metadata observation accepted");

    const auto snapshot = accumulator.MakeSnapshot(std::chrono::steady_clock::time_point{});
    const auto* const application = FindApplication(snapshot, "app.editor");
    context.Check(application != nullptr, "metadata application exists");
    if (application != nullptr) {
        context.Check(application->metadata.display_name == "Editor",
                      "first non-empty display name wins");
        context.Check(application->metadata.icon_key == "editor.exe", "empty icon is enriched");
    }
    context.Check(accumulator.metadata_conflicts() == 1U, "metadata conflict counted once");
}

void CheckOverflowRollback(TestContext& context) {
    ApplicationAccumulator accumulator;
    const ApplicationMetadata original{.display_name = "Stable", .icon_key = "stable.exe"};
    const ApplicationMetadata conflicting{.display_name = "Changed", .icon_key = "changed.exe"};

    context.Check(accumulator
                      .AddTraffic("app.stable", original, At(std::chrono::milliseconds{0}),
                                  TrafficDirection::Upload,
                                  std::numeric_limits<std::uint64_t>::max())
                      .ok(),
                  "maximum counter accepted");
    const auto overflow = accumulator.AddTraffic(
        "app.stable", conflicting, At(std::chrono::milliseconds{1}), TrafficDirection::Upload, 1U);
    context.Check(overflow.error == nfm::ranking::AccumulationError::CounterOverflow,
                  "overflow rejected");

    const auto snapshot = accumulator.MakeSnapshot(std::chrono::steady_clock::time_point{});
    const auto* const application = FindApplication(snapshot, "app.stable");
    context.Check(application != nullptr, "overflow application remains");
    if (application != nullptr) {
        context.Check(application->upload_bytes == std::numeric_limits<std::uint64_t>::max(),
                      "overflow preserves counter");
        context.Check(application->metadata == original, "overflow preserves metadata");
    }
    context.Check(accumulator.metadata_conflicts() == 0U,
                  "overflow preserves metadata diagnostics");
}

void CheckRetentionAndWatermark(TestContext& context) {
    using namespace std::chrono_literals;
    ApplicationAccumulator accumulator;
    const ApplicationMetadata metadata{.display_name = "Short lived"};

    context.Check(
        accumulator.AddTraffic("app.short", metadata, At(0ms), TrafficDirection::Upload, 1U).ok(),
        "dynamic entry accepted");
    context.Check(accumulator
                      .AddTraffic(nfm::ranking::kUnattributedApplicationId,
                                  {.display_name = "Unattributed"}, At(0ms),
                                  TrafficDirection::Download, 2U)
                      .ok(),
                  "fixed unattributed bucket accepted");
    context.Check(accumulator.AdvanceWatermark(At(15s)).evicted == 0U,
                  "entry survives exact retention boundary");
    const auto expired = accumulator.AdvanceWatermark(At(15s) + AccumulationDuration{1});
    context.Check(expired.error == nfm::ranking::AccumulationError::None && expired.evicted == 1U,
                  "dynamic entry expires only after retention boundary");
    context.Check(FindApplication(accumulator.MakeSnapshot({}),
                                  nfm::ranking::kUnattributedApplicationId) != nullptr,
                  "fixed bucket is never pruned");
    context.Check(accumulator.AdvanceWatermark(At(15s)).error ==
                      nfm::ranking::AccumulationError::WatermarkMovedBackward,
                  "backward watermark rejected");
}

bool FillDynamicCapacity(ApplicationAccumulator& accumulator) {
    const ApplicationMetadata metadata{.display_name = "Capacity"};
    for (std::size_t index = 0U; index < nfm::ranking::kMaximumDynamicApplications; ++index) {
        const auto id = std::string{"app.capacity."} + std::to_string(index);
        if (!accumulator
                 .AddTraffic(id, metadata, At(std::chrono::milliseconds{0}),
                             TrafficDirection::Upload, 1U)
                 .ok()) {
            return false;
        }
    }
    return true;
}

void CheckCapacityAndOtherRollback(TestContext& context) {
    ApplicationAccumulator accumulator;
    context.Check(FillDynamicCapacity(accumulator), "dynamic capacity filled");
    context.Check(accumulator
                      .AddTraffic(nfm::ranking::kUnattributedApplicationId,
                                  {.display_name = "Unattributed"},
                                  At(std::chrono::milliseconds{0}), TrafficDirection::Download, 3U)
                      .ok(),
                  "first reserved bucket remains available at capacity");

    const auto aggregated = accumulator.AddTraffic(
        "app.overflow.first", {.display_name = "Overflow"}, At(std::chrono::milliseconds{1}),
        TrafficDirection::Download, std::numeric_limits<std::uint64_t>::max());
    context.Check(aggregated.error == nfm::ranking::AccumulationError::None &&
                      aggregated.disposition == AccumulationDisposition::AggregatedToOther,
                  "new identity aggregates into Other at capacity");
    context.Check(accumulator.MakeSnapshot({}).applications.size() ==
                      nfm::ranking::kMaximumActiveApplications,
                  "4094 dynamic identities and two fixed buckets fit exactly");

    const auto existing =
        accumulator.AddTraffic("app.capacity.0", {.display_name = "Capacity"},
                               At(std::chrono::milliseconds{2}), TrafficDirection::Download, 5U);
    context.Check(existing.error == nfm::ranking::AccumulationError::None &&
                      existing.disposition == AccumulationDisposition::Counted,
                  "existing identity updates normally at capacity");

    const auto overflow =
        accumulator.AddTraffic("app.overflow.second", {.display_name = "Overflow"},
                               At(std::chrono::milliseconds{3}), TrafficDirection::Download, 1U);
    context.Check(overflow.error == nfm::ranking::AccumulationError::CounterOverflow &&
                      overflow.disposition == AccumulationDisposition::AggregatedToOther,
                  "Other overflow is classified without dropping into a new slot");
    const auto snapshot = accumulator.MakeSnapshot({});
    const auto* const other = FindApplication(snapshot, nfm::ranking::kOtherApplicationId);
    context.Check(other != nullptr &&
                      other->download_bytes == std::numeric_limits<std::uint64_t>::max(),
                  "Other overflow preserves its previous counter");
    context.Check(other != nullptr && other->metadata.display_name == "Other",
                  "Other bucket uses deterministic metadata");
    context.Check(FindApplication(snapshot, "app.overflow.second") == nullptr,
                  "overflowing identity is never inserted");
}

} // namespace

int Run() {
    TestContext context;
    CheckBasicAccumulation(context);
    CheckValidationAndOrdering(context);
    CheckMetadataRules(context);
    CheckOverflowRollback(context);
    CheckRetentionAndWatermark(context);
    CheckCapacityAndOtherRollback(context);
    return context.failures();
}
} // namespace application_accumulator_suite

// === ranking_engine_suite ===
namespace ranking_engine_suite {
namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

struct ByteCounts {
    std::uint64_t upload{};
    std::uint64_t download{};
};

using nfm::ranking::ApplicationCounters;
using nfm::ranking::CounterSnapshot;
using nfm::ranking::RankingEngine;
using nfm::ranking::RankingError;
using nfm::ranking::RankingOptions;
using nfm::ranking::RankingSnapshot;
using nfm::ranking::RankingStatus;

ApplicationCounters Application(std::string id, ByteCounts bytes) {
    return ApplicationCounters{.id = std::move(id),
                               .metadata = {.display_name = "Application", .icon_key = "app.exe"},
                               .upload_bytes = bytes.upload,
                               .download_bytes = bytes.download};
}

CounterSnapshot Snapshot(std::chrono::milliseconds captured_after_epoch,
                         std::initializer_list<ApplicationCounters> applications) {
    return CounterSnapshot{
        .captured_at = std::chrono::steady_clock::time_point{captured_after_epoch},
        .applications = std::vector<ApplicationCounters>{applications},
    };
}

std::unique_ptr<RankingEngine> CreateValidEngine(TestContext& context) {
    using namespace std::chrono_literals;
    auto created = nfm::ranking::CreateRankingEngine(RankingOptions{.window = 2000ms, .top_n = 5U});
    context.Check(created.error == RankingError::None, "valid options accepted");
    context.Check(created.engine != nullptr, "valid options create engine");
    return std::move(created.engine);
}

void CheckNear(TestContext& context, double actual, double expected, std::string_view name) {
    context.Check(std::abs(actual - expected) < 0.000001, name);
}

void CheckOptionValidation(TestContext& context) {
    using namespace std::chrono_literals;

    const auto zero_window =
        nfm::ranking::CreateRankingEngine(RankingOptions{.window = 0ms, .top_n = 5U});
    context.Check(zero_window.engine == nullptr, "zero window creates no engine");
    context.Check(zero_window.error == RankingError::InvalidWindow, "zero window rejected");

    const auto negative_window =
        nfm::ranking::CreateRankingEngine(RankingOptions{.window = -1ms, .top_n = 5U});
    context.Check(negative_window.error == RankingError::InvalidWindow, "negative window rejected");

    const auto short_window =
        nfm::ranking::CreateRankingEngine(RankingOptions{.window = 499ms, .top_n = 5U});
    context.Check(short_window.error == RankingError::InvalidWindow, "sub-minimum window rejected");

    const auto long_window =
        nfm::ranking::CreateRankingEngine(RankingOptions{.window = 10001ms, .top_n = 5U});
    context.Check(long_window.error == RankingError::InvalidWindow, "over-maximum window rejected");

    const auto zero_top =
        nfm::ranking::CreateRankingEngine(RankingOptions{.window = 2000ms, .top_n = 0U});
    context.Check(zero_top.error == RankingError::InvalidTopN, "zero Top N rejected");

    const auto high_top =
        nfm::ranking::CreateRankingEngine(RankingOptions{.window = 2000ms, .top_n = 21U});
    context.Check(high_top.error == RankingError::InvalidTopN, "Top N above maximum rejected");
}

void CheckWarmUpAndReadyRate(TestContext& context) {
    using namespace std::chrono_literals;
    auto engine = CreateValidEngine(context);
    if (engine == nullptr) {
        return;
    }

    const auto first = engine->PushSnapshot(Snapshot(0ms, {Application("app", {100U, 100U})}));
    context.Check(first.accepted(), "first snapshot accepted");
    context.Check(first.snapshot.status == RankingStatus::WarmingUp, "first snapshot warms up");
    context.Check(first.snapshot.applications.empty(), "warm-up list is empty");
    CheckNear(context, first.snapshot.total_rate.total_bytes_per_second(), 0.0,
              "warm-up total is zero");

    const auto almost = engine->PushSnapshot(Snapshot(1999ms, {Application("app", {200U, 300U})}));
    context.Check(almost.accepted(), "pre-window snapshot accepted");
    context.Check(almost.snapshot.status == RankingStatus::WarmingUp, "1999ms remains warming up");

    const auto ready = engine->PushSnapshot(Snapshot(2000ms, {Application("app", {300U, 500U})}));
    context.Check(ready.accepted(), "window-complete snapshot accepted");
    context.Check(ready.snapshot.status == RankingStatus::Ready, "ready at exact window");
    context.Check(ready.snapshot.sampled_span == 2000ms, "ready span is actual baseline span");
    context.Check(ready.snapshot.applications.size() == 1U, "ready application emitted");
    if (!ready.snapshot.applications.empty()) {
        CheckNear(context, ready.snapshot.applications.front().rate.upload_bytes_per_second, 100.0,
                  "upload rate uses elapsed seconds");
        CheckNear(context, ready.snapshot.applications.front().rate.download_bytes_per_second,
                  200.0, "download rate uses elapsed seconds");
    }
    CheckNear(context, ready.snapshot.total_rate.total_bytes_per_second(), 300.0,
              "ready total rate");
}

void CheckIrregularIntervalsAndLateApplication(TestContext& context) {
    using namespace std::chrono_literals;
    auto engine = CreateValidEngine(context);
    if (engine == nullptr) {
        return;
    }

    context.Check(engine->PushSnapshot(Snapshot(0ms, {Application("base", {})})).accepted(),
                  "irregular baseline accepted");
    context.Check(engine
                      ->PushSnapshot(Snapshot(700ms, {Application("base", {700U, 0U}),
                                                      Application("late", {100U, 0U})}))
                      .accepted(),
                  "700ms snapshot accepted");
    context.Check(engine
                      ->PushSnapshot(Snapshot(1300ms, {Application("base", {1300U, 0U}),
                                                       Application("late", {400U, 0U})}))
                      .accepted(),
                  "1300ms snapshot accepted");
    const auto result = engine->PushSnapshot(
        Snapshot(2400ms, {Application("base", {2400U, 0U}), Application("late", {1800U, 0U}),
                          Application("single", {500U, 0U})}));

    context.Check(result.snapshot.status == RankingStatus::Ready, "irregular history ready");
    context.Check(result.snapshot.sampled_span == 2400ms, "actual 2400ms span retained");
    context.Check(result.snapshot.applications.size() == 2U, "late application has two samples");
    context.Check(result.snapshot.diagnostics.ignored_single_sample_applications == 1U,
                  "single-sample application is diagnosed and omitted");
    for (const auto& application : result.snapshot.applications) {
        CheckNear(context, application.rate.upload_bytes_per_second, 1000.0,
                  "actual per-application span used");
    }
}

void CheckRejectedPushesPreserveState(TestContext& context) {
    using namespace std::chrono_literals;
    auto clean = CreateValidEngine(context);
    auto challenged = CreateValidEngine(context);
    if (clean == nullptr || challenged == nullptr) {
        return;
    }

    const auto baseline = Snapshot(0ms, {Application("app", {})});
    context.Check(clean->PushSnapshot(baseline).accepted(), "clean baseline accepted");
    context.Check(challenged->PushSnapshot(baseline).accepted(), "challenged baseline accepted");

    const auto repeated = challenged->PushSnapshot(Snapshot(0ms, {Application("app", {})}));
    context.Check(repeated.error == RankingError::NonIncreasingTimestamp,
                  "repeated timestamp rejected");
    const auto decreasing = challenged->PushSnapshot(Snapshot(-1ms, {Application("app", {})}));
    context.Check(decreasing.error == RankingError::NonIncreasingTimestamp,
                  "decreasing timestamp rejected");
    const auto empty_id = challenged->PushSnapshot(Snapshot(1000ms, {Application("", {})}));
    context.Check(empty_id.error == RankingError::EmptyApplicationId, "empty snapshot ID rejected");
    const auto duplicate = challenged->PushSnapshot(
        Snapshot(1000ms, {Application("app", {}), Application("app", {1U, 1U})}));
    context.Check(duplicate.error == RankingError::DuplicateApplicationId,
                  "duplicate snapshot ID rejected");

    const auto clean_result =
        clean->PushSnapshot(Snapshot(2000ms, {Application("app", {2000U, 1000U})}));
    const auto challenged_result =
        challenged->PushSnapshot(Snapshot(2000ms, {Application("app", {2000U, 1000U})}));
    context.Check(clean_result.snapshot.status == challenged_result.snapshot.status,
                  "rejections preserve readiness");
    context.Check(clean_result.snapshot.sampled_span == challenged_result.snapshot.sampled_span,
                  "rejections preserve history span");
    CheckNear(context, clean_result.snapshot.total_rate.upload_bytes_per_second,
              challenged_result.snapshot.total_rate.upload_bytes_per_second,
              "rejections preserve upload rate");
    CheckNear(context, clean_result.snapshot.total_rate.download_bytes_per_second,
              challenged_result.snapshot.total_rate.download_bytes_per_second,
              "rejections preserve download rate");
}

void CheckCounterEpochs(TestContext& context) {
    using namespace std::chrono_literals;
    auto engine = CreateValidEngine(context);
    if (engine == nullptr) {
        return;
    }

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    context.Check(engine
                      ->PushSnapshot(Snapshot(0ms, {Application("reset", {1000U, 1000U}),
                                                    Application("wrap", {0U, maximum})}))
                      .accepted(),
                  "epoch baseline accepted");
    const auto reset = engine->PushSnapshot(
        Snapshot(2000ms, {Application("reset", {100U, 1500U}), Application("wrap", {0U, 5U})}));
    context.Check(reset.accepted(), "reset observation accepted");
    context.Check(reset.snapshot.diagnostics.counter_discontinuities == 2U,
                  "reset and wrap counted by direction");
    const auto reset_interval_application = std::ranges::find(reset.snapshot.applications, "reset",
                                                              &nfm::ranking::RankedApplication::id);
    context.Check(reset_interval_application != reset.snapshot.applications.end(),
                  "unaffected reset direction remains visible");
    if (reset_interval_application != reset.snapshot.applications.end()) {
        CheckNear(context, reset_interval_application->rate.upload_bytes_per_second, 0.0,
                  "reset upload suppressed for one result");
        CheckNear(context, reset_interval_application->rate.download_bytes_per_second, 250.0,
                  "unaffected download continues");
    }

    const auto recovered = engine->PushSnapshot(
        Snapshot(3000ms, {Application("reset", {600U, 2000U}), Application("wrap", {0U, 1005U})}));
    context.Check(recovered.accepted(), "post-reset observation accepted");
    context.Check(recovered.snapshot.diagnostics.counter_discontinuities == 0U,
                  "new epochs prevent repeated discontinuities");
    const auto reset_application = std::ranges::find(recovered.snapshot.applications, "reset",
                                                     &nfm::ranking::RankedApplication::id);
    context.Check(reset_application != recovered.snapshot.applications.end(),
                  "reset application resumes ranking");
    if (reset_application != recovered.snapshot.applications.end()) {
        CheckNear(context, reset_application->rate.upload_bytes_per_second, 500.0,
                  "upload resumes from reset epoch");
    }
    const auto wrap_application = std::ranges::find(recovered.snapshot.applications, "wrap",
                                                    &nfm::ranking::RankedApplication::id);
    context.Check(wrap_application != recovered.snapshot.applications.end(),
                  "wrapped application resumes ranking");
    if (wrap_application != recovered.snapshot.applications.end()) {
        CheckNear(context, wrap_application->rate.download_bytes_per_second, 1000.0,
                  "download resumes after uint64 wrap");
    }
}

void CheckWarmUpCounterReset(TestContext& context) {
    using namespace std::chrono_literals;
    auto engine = CreateValidEngine(context);
    if (engine == nullptr) {
        return;
    }

    context.Check(engine->PushSnapshot(Snapshot(0ms, {Application("app", {1000U, 0U})})).accepted(),
                  "warm reset baseline accepted");
    const auto reset = engine->PushSnapshot(Snapshot(500ms, {Application("app", {100U, 0U})}));
    context.Check(reset.snapshot.status == RankingStatus::WarmingUp,
                  "counter reset can occur during warm-up");
    context.Check(reset.snapshot.diagnostics.counter_discontinuities == 1U,
                  "warm-up counter reset is diagnosed immediately");

    const auto recovered =
        engine->PushSnapshot(Snapshot(2000ms, {Application("app", {1100U, 0U})}));
    context.Check(recovered.snapshot.status == RankingStatus::Ready,
                  "warm-up reset history becomes ready");
    context.Check(recovered.snapshot.applications.size() == 1U,
                  "warm-up reset application recovers");
    if (!recovered.snapshot.applications.empty()) {
        CheckNear(context, recovered.snapshot.applications.front().rate.upload_bytes_per_second,
                  1000.0 / 1.5, "rate starts from the warm-up reset epoch");
    }
    context.Check(recovered.snapshot.diagnostics.counter_discontinuities == 0U,
                  "warm-up reset is not diagnosed twice");
}

void CheckTopNOrderingAndTotals(TestContext& context) {
    using namespace std::chrono_literals;
    auto created = nfm::ranking::CreateRankingEngine(RankingOptions{.window = 2000ms, .top_n = 2U});
    context.Check(created.engine != nullptr, "Top N engine created");
    if (created.engine == nullptr) {
        return;
    }
    auto& engine = *created.engine;

    context.Check(engine
                      .PushSnapshot(Snapshot(0ms, {Application("a", {}), Application("b", {}),
                                                   Application("c", {}), Application("d", {}),
                                                   Application("zero", {})}))
                      .accepted(),
                  "Top N baseline accepted");
    const auto ranked = engine.PushSnapshot(
        Snapshot(2000ms, {Application("a", {100U, 100U}), Application("b", {0U, 200U}),
                          Application("c", {200U, 0U}), Application("d", {100U, 100U}),
                          Application("zero", {})}));
    context.Check(ranked.snapshot.applications.size() == 2U, "Top N truncates visible list");
    if (ranked.snapshot.applications.size() == 2U) {
        context.Check(ranked.snapshot.applications[0].id == "b", "download breaks total-rate tie");
        context.Check(ranked.snapshot.applications[1].id == "a", "application ID breaks exact tie");
    }
    CheckNear(context, ranked.snapshot.total_rate.upload_bytes_per_second, 200.0,
              "truncated uploads remain in total");
    CheckNear(context, ranked.snapshot.total_rate.download_bytes_per_second, 200.0,
              "truncated downloads remain in total");

    context.Check(engine.UpdateOptions(RankingOptions{.window = 2000ms, .top_n = 4U}) ==
                      RankingError::None,
                  "Top N increase accepted");
    const auto expanded = engine.PushSnapshot(
        Snapshot(4000ms, {Application("a", {200U, 200U}), Application("b", {0U, 400U}),
                          Application("c", {400U, 0U}), Application("d", {200U, 200U}),
                          Application("zero", {})}));
    context.Check(expanded.snapshot.applications.size() == 4U, "Top N increase preserves history");
    if (expanded.snapshot.applications.size() == 4U) {
        context.Check(expanded.snapshot.applications[0].id == "b", "expanded order first");
        context.Check(expanded.snapshot.applications[1].id == "a", "expanded order second");
        context.Check(expanded.snapshot.applications[2].id == "d", "expanded order third");
        context.Check(expanded.snapshot.applications[3].id == "c", "expanded order fourth");
    }

    context.Check(engine.UpdateOptions(RankingOptions{.window = 2000ms, .top_n = 1U}) ==
                      RankingError::None,
                  "Top N decrease accepted");
    context.Check(engine.UpdateOptions(RankingOptions{.window = 2000ms, .top_n = 0U}) ==
                      RankingError::InvalidTopN,
                  "invalid Top N update rejected");
    const auto reduced = engine.PushSnapshot(
        Snapshot(6000ms, {Application("a", {300U, 300U}), Application("b", {0U, 600U}),
                          Application("c", {600U, 0U}), Application("d", {300U, 300U}),
                          Application("zero", {})}));
    context.Check(reduced.snapshot.applications.size() == 1U,
                  "invalid update leaves prior Top N active");
}

void CheckWindowUpdates(TestContext& context) {
    using namespace std::chrono_literals;
    auto engine = CreateValidEngine(context);
    if (engine == nullptr) {
        return;
    }

    context.Check(engine->PushSnapshot(Snapshot(0ms, {Application("app", {})})).accepted(),
                  "window baseline accepted");
    context.Check(
        engine->PushSnapshot(Snapshot(500ms, {Application("app", {500U, 0U})})).accepted(),
        "window 500ms observation accepted");
    context.Check(
        engine->PushSnapshot(Snapshot(1000ms, {Application("app", {1000U, 0U})})).accepted(),
        "window 1000ms observation accepted");

    context.Check(engine->UpdateOptions(RankingOptions{.window = 500ms, .top_n = 5U}) ==
                      RankingError::None,
                  "window decrease accepted");
    const auto decreased =
        engine->PushSnapshot(Snapshot(1500ms, {Application("app", {1500U, 0U})}));
    context.Check(decreased.snapshot.status == RankingStatus::Ready,
                  "window decrease reuses retained history");
    context.Check(decreased.snapshot.sampled_span == 500ms, "decreased window baseline selected");

    context.Check(engine->UpdateOptions(RankingOptions{.window = 2000ms, .top_n = 5U}) ==
                      RankingError::None,
                  "window increase accepted");
    const auto increased =
        engine->PushSnapshot(Snapshot(2000ms, {Application("app", {2000U, 0U})}));
    context.Check(increased.snapshot.status == RankingStatus::Ready,
                  "previously retained larger-window history is reusable");

    context.Check(engine->UpdateOptions(RankingOptions{.window = 10001ms, .top_n = 5U}) ==
                      RankingError::InvalidWindow,
                  "invalid window update rejected");
    const auto after_invalid =
        engine->PushSnapshot(Snapshot(2500ms, {Application("app", {2500U, 0U})}));
    context.Check(after_invalid.snapshot.status == RankingStatus::Ready,
                  "invalid window update leaves previous options active");

    auto short_created =
        nfm::ranking::CreateRankingEngine(RankingOptions{.window = 500ms, .top_n = 5U});
    context.Check(short_created.engine != nullptr, "short-window engine created");
    if (short_created.engine == nullptr) {
        return;
    }
    context.Check(
        short_created.engine->PushSnapshot(Snapshot(0ms, {Application("app", {})})).accepted(),
        "short baseline accepted");
    context.Check(
        short_created.engine->PushSnapshot(Snapshot(500ms, {Application("app", {500U, 0U})}))
            .accepted(),
        "short window ready observation accepted");
    context.Check(short_created.engine->UpdateOptions(
                      RankingOptions{.window = 2000ms, .top_n = 5U}) == RankingError::None,
                  "short window increased");
    const auto rewarmed =
        short_created.engine->PushSnapshot(Snapshot(1000ms, {Application("app", {1000U, 0U})}));
    context.Check(rewarmed.snapshot.status == RankingStatus::WarmingUp,
                  "window increase warms until sufficient history");
}

void CheckApplicationDisappearance(TestContext& context) {
    using namespace std::chrono_literals;
    auto engine = CreateValidEngine(context);
    if (engine == nullptr) {
        return;
    }
    context.Check(
        engine->PushSnapshot(Snapshot(0ms, {Application("gone", {100U, 100U})})).accepted(),
        "disappearance baseline accepted");
    const auto disappeared = engine->PushSnapshot(Snapshot(2000ms, {}));
    context.Check(disappeared.snapshot.status == RankingStatus::Ready,
                  "empty current snapshot can be ready");
    context.Check(disappeared.snapshot.applications.empty(),
                  "disappeared application does not emit stale rate");
    CheckNear(context, disappeared.snapshot.total_rate.total_bytes_per_second(), 0.0,
              "disappeared application does not affect total");
}

} // namespace

int Run() {
    TestContext context;
    CheckOptionValidation(context);
    CheckWarmUpAndReadyRate(context);
    CheckIrregularIntervalsAndLateApplication(context);
    CheckRejectedPushesPreserveState(context);
    CheckCounterEpochs(context);
    CheckWarmUpCounterReset(context);
    CheckTopNOrderingAndTotals(context);
    CheckWindowUpdates(context);
    CheckApplicationDisappearance(context);
    return context.failures();
}
} // namespace ranking_engine_suite

int main() {
    int failures = 0;
    failures += (application_accumulator_suite::Run() != 0 ? 1 : 0);
    failures += (ranking_engine_suite::Run() != 0 ? 1 : 0);
    return failures;
}
