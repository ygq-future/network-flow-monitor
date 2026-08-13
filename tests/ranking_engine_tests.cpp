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

int main() {
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
