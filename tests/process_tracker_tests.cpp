#include "network_flow_monitor/process/process_tracker.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace nfm::process {

struct ProcessTrackerTestAccess {
    static void SetNextGeneration(ProcessTracker& tracker, std::uint64_t generation) noexcept {
        tracker.next_generation_ = generation;
    }

    static void SetUnmatchedNetworkEvents(ProcessTracker& tracker, std::uint64_t value) noexcept {
        tracker.diagnostics_.unmatched_network_events = value;
    }

    [[nodiscard]] static std::size_t RetiredSize(const ProcessTracker& tracker) noexcept {
        return tracker.retired_.size();
    }
};

} // namespace nfm::process

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

using nfm::process::ProcessEndObservation;
using nfm::process::ProcessInstanceId;
using nfm::process::ProcessLookupResult;
using nfm::process::ProcessStartKind;
using nfm::process::ProcessStartObservation;
using nfm::process::ProcessTracker;
using nfm::process::ProcessTrackerError;
using nfm::process::ProcessTrackerTestAccess;
using nfm::process::TraceDuration;
using nfm::process::TraceTimePoint;

TraceTimePoint At(std::chrono::milliseconds offset) {
    return TraceTimePoint{std::chrono::duration_cast<TraceDuration>(offset)};
}

void CheckApplicationId(TestContext& context, const ProcessLookupResult& result,
                        std::string_view expected, std::string_view name) {
    if (!result.process.has_value()) {
        context.Check(false, name);
        return;
    }
    context.Check(result.process.value().application.id == expected, name);
}

void CheckInstance(TestContext& context, const ProcessLookupResult& result,
                   ProcessInstanceId expected, std::string_view name) {
    if (!result.process.has_value()) {
        context.Check(false, name);
        return;
    }
    context.Check(result.process.value().instance == expected, name);
}

void CheckDisplayName(TestContext& context, const ProcessLookupResult& result,
                      std::string_view expected, std::string_view name) {
    if (!result.process.has_value()) {
        context.Check(false, name);
        return;
    }
    context.Check(result.process.value().application.metadata.display_name == expected, name);
}

ProcessStartObservation Start(std::uint32_t pid, std::chrono::milliseconds observed_at,
                              std::optional<std::uint64_t> kernel_key = std::nullopt,
                              std::string image_name = {},
                              ProcessStartKind kind = ProcessStartKind::Started) {
    return {.pid = pid,
            .observed_at = At(observed_at),
            .kernel_process_key = kernel_key,
            .image_name_hint = std::move(image_name),
            .kind = kind};
}

ProcessEndObservation End(std::uint32_t pid, std::chrono::milliseconds observed_at,
                          std::optional<std::uint64_t> kernel_key = std::nullopt) {
    return {.pid = pid, .observed_at = At(observed_at), .kernel_process_key = kernel_key};
}

void CheckBasicLifecycle(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto started = tracker.ObserveStart(Start(100U, 0ms, 1000U, "app.exe"));
    context.Check(started.accepted(), "basic start accepted");
    context.Check(started.instance == ProcessInstanceId{.pid = 100U, .generation = 1U},
                  "first generation assigned");
    context.Check(started.probe_request.has_value(), "ordinary process requests probe");

    const auto unresolved = tracker.ResolveAt(100U, At(0ms));
    context.Check(unresolved.found(), "active process resolves at start");
    CheckApplicationId(context, unresolved, "unresolved:100:1", "active process starts unresolved");

    context.Check(tracker.ApplyProbeData(started.instance, {.image_path = "C:\\Apps\\app.exe"}) ==
                      ProcessTrackerError::None,
                  "probe data applied");
    const auto resolved = tracker.ResolveAt(100U, At(5ms));
    CheckApplicationId(context, resolved, "executable:C:\\Apps\\app.exe",
                       "resolved application returned");

    context.Check(tracker.ObserveEnd(End(100U, 10ms, 1000U)) == ProcessTrackerError::None,
                  "matching end accepted");
    context.Check(tracker.ResolveAt(100U, At(9ms)).found(),
                  "retired process resolves late pre-end event");
    context.Check(!tracker.ResolveAt(100U, At(10ms)).found(),
                  "half-open end boundary does not resolve old process");
}

void CheckDuplicateStartAndStaleEnd(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto first = tracker.ObserveStart(Start(200U, 0ms, 2000U));
    const auto duplicate = tracker.ObserveStart(Start(200U, 1ms, 2000U, "dup.exe"));
    context.Check(duplicate.accepted(), "duplicate start accepted as observation");
    context.Check(duplicate.instance == first.instance, "duplicate start keeps generation");
    context.Check(!duplicate.probe_request.has_value(), "duplicate start avoids second probe");
    CheckDisplayName(context, tracker.ResolveAt(200U, At(1ms)), "dup.exe",
                     "duplicate start enriches empty display name");

    context.Check(tracker.ObserveEnd(End(200U, 2ms, 9999U)) == ProcessTrackerError::StaleEnd,
                  "mismatched kernel key is stale end");
    context.Check(tracker.ResolveAt(200U, At(3ms)).found(),
                  "stale end does not close active process");
    context.Check(tracker.diagnostics().stale_end_events == 1U, "stale end diagnostic increments");

    const auto backwards = tracker.ObserveStart(Start(200U, -1ms, 2000U));
    context.Check(backwards.error == ProcessTrackerError::NonIncreasingStartTime,
                  "backward duplicate start rejected");
    CheckInstance(context, tracker.ResolveAt(200U, At(3ms)), first.instance,
                  "rejected start preserves active instance");
}

void CheckPidReuseBoundaries(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto old_process = tracker.ObserveStart(Start(300U, 0ms, 3000U, "old.exe"));
    context.Check(
        tracker.ApplyProbeData(old_process.instance, {.image_path = "C:\\Old\\same.exe"}) ==
            ProcessTrackerError::None,
        "old identity applied");

    const auto new_process = tracker.ObserveStart(Start(300U, 10ms, 3001U, "new.exe"));
    context.Check(new_process.accepted(), "PID reuse start accepted");
    context.Check(new_process.instance.generation == 2U, "PID reuse gets new generation");
    context.Check(tracker.diagnostics().implicit_pid_reuses == 1U, "implicit PID reuse diagnosed");

    const auto before = tracker.ResolveAt(300U, At(9ms));
    const auto boundary = tracker.ResolveAt(300U, At(10ms));
    CheckInstance(context, before, old_process.instance,
                  "event before reuse resolves old instance");
    CheckInstance(context, boundary, new_process.instance,
                  "event at reuse boundary resolves new instance");

    context.Check(
        tracker.ApplyProbeData(old_process.instance, {.image_path = "C:\\Old\\late.exe"}) ==
            ProcessTrackerError::None,
        "late probe can enrich retained old instance");
    CheckApplicationId(context, tracker.ResolveAt(300U, At(9ms)), "executable:C:\\Old\\late.exe",
                       "late old probe updates only old generation");
    CheckApplicationId(context, tracker.ResolveAt(300U, At(10ms)), "unresolved:300:2",
                       "late old probe never overwrites reused PID");

    const auto no_key_reuse = tracker.ObserveStart(Start(300U, 20ms));
    context.Check(no_key_reuse.instance.generation == 3U,
                  "later start without comparable key is new generation");
}

void CheckExplicitPidReuse(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto old_process = tracker.ObserveStart(Start(350U, 0ms, 3500U));
    context.Check(tracker.ObserveEnd(End(350U, 10ms, 3500U)) == ProcessTrackerError::None,
                  "explicit reuse old process ends");

    const auto new_process = tracker.ObserveStart(Start(350U, 20ms, 3501U));
    context.Check(new_process.accepted(), "explicit reuse new process starts");
    context.Check(new_process.instance.generation == 2U, "explicit reuse gets a new generation");
    CheckInstance(context, tracker.ResolveAt(350U, At(9ms)), old_process.instance,
                  "explicit reuse retains old interval");
    context.Check(!tracker.ResolveAt(350U, At(10ms)).found(),
                  "explicit reuse gap does not guess an instance");
    CheckInstance(context, tracker.ResolveAt(350U, At(20ms)), new_process.instance,
                  "explicit reuse resolves new interval");
    context.Check(tracker.diagnostics().implicit_pid_reuses == 0U,
                  "explicit reuse is not diagnosed as implicit reuse");
}

void CheckRetiredExpiryAndStaleProbe(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto started = tracker.ObserveStart(Start(400U, 0ms, 4000U));
    context.Check(tracker.ObserveEnd(End(400U, 1ms, 4000U)) == ProcessTrackerError::None,
                  "expiry process ended");
    context.Check(tracker.AdvanceWatermark(At(30'000ms)) == ProcessTrackerError::None,
                  "pre-expiry watermark accepted");
    context.Check(tracker.ResolveAt(400U, At(0ms)).found(),
                  "retired process retained before deadline");
    context.Check(tracker.AdvanceWatermark(At(30'001ms)) == ProcessTrackerError::None,
                  "expiry watermark accepted");
    context.Check(!tracker.ResolveAt(400U, At(0ms)).found(), "retired process removed at deadline");
    context.Check(tracker.diagnostics().expired_retired_processes == 1U,
                  "expiry diagnostic increments");
    context.Check(tracker.ApplyProbeData(started.instance, {.image_path = "C:\\Late\\app.exe"}) ==
                      ProcessTrackerError::StaleProbeResult,
                  "probe after expiry is stale");
    context.Check(tracker.diagnostics().stale_probe_results == 1U,
                  "stale probe diagnostic increments");
    context.Check(tracker.AdvanceWatermark(At(30'000ms)) ==
                      ProcessTrackerError::WatermarkMovedBackward,
                  "watermark rollback rejected");
}

void CheckSystemProbeSuppressionAndInvalidData(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto idle = tracker.ObserveStart(Start(0U, 0ms));
    context.Check(idle.accepted() && !idle.probe_request.has_value(), "idle identity skips probe");
    CheckApplicationId(context, tracker.ResolveAt(0U, At(0ms)), "system:idle",
                       "idle identity resolved");

    const auto kernel = tracker.ObserveStart(Start(4U, 0ms, 4U, "System"));
    context.Check(kernel.accepted() && !kernel.probe_request.has_value(),
                  "verified kernel identity skips probe");
    CheckApplicationId(context, tracker.ResolveAt(4U, At(0ms)), "system:kernel",
                       "kernel identity resolved");

    const auto ordinary = tracker.ObserveStart(Start(500U, 0ms));
    context.Check(tracker.ApplyProbeData(ordinary.instance, {.image_path = "relative\\app.exe"}) ==
                      ProcessTrackerError::InvalidProbeData,
                  "invalid probe path rejected");
    CheckApplicationId(context, tracker.ResolveAt(500U, At(1ms)), "unresolved:500:3",
                       "invalid probe preserves unresolved identity");
}

void CheckCapacityGenerationAndSaturation(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    constexpr std::uint32_t first_pid = 10'000U;
    for (std::size_t index = 0; index <= nfm::process::kMaximumRetiredProcesses; ++index) {
        const auto time = std::chrono::milliseconds{static_cast<std::int64_t>(index)};
        const auto started =
            tracker.ObserveStart(Start(first_pid + static_cast<std::uint32_t>(index), time));
        context.Check(started.accepted(), "capacity start accepted");
        context.Check(tracker.ObserveEnd(End(first_pid + static_cast<std::uint32_t>(index),
                                             time + 1ms)) == ProcessTrackerError::None,
                      "capacity end accepted");
    }
    context.Check(ProcessTrackerTestAccess::RetiredSize(tracker) ==
                      nfm::process::kMaximumRetiredProcesses,
                  "retired cache remains bounded");
    context.Check(tracker.diagnostics().capacity_evictions == 1U, "capacity eviction diagnosed");
    context.Check(!tracker.ResolveAt(first_pid, At(0ms)).found(),
                  "oldest retired instance evicted first");

    ProcessTracker exhausted;
    ProcessTrackerTestAccess::SetNextGeneration(exhausted,
                                                std::numeric_limits<std::uint64_t>::max());
    const auto last = exhausted.ObserveStart(Start(600U, 0ms));
    context.Check(last.accepted() &&
                      last.instance.generation == std::numeric_limits<std::uint64_t>::max(),
                  "maximum generation allocated once");
    const auto overflow = exhausted.ObserveStart(Start(601U, 1ms));
    context.Check(overflow.error == ProcessTrackerError::GenerationExhausted,
                  "generation never wraps to zero");

    ProcessTracker saturation;
    ProcessTrackerTestAccess::SetUnmatchedNetworkEvents(saturation,
                                                        std::numeric_limits<std::uint64_t>::max());
    context.Check(!saturation.ResolveAt(999U, At(0ms)).found(),
                  "unmatched lookup remains diagnostic at saturation");
    context.Check(saturation.diagnostics().unmatched_network_events ==
                      std::numeric_limits<std::uint64_t>::max(),
                  "diagnostic counter saturates");
}

} // namespace

int main() {
    TestContext context;
    CheckBasicLifecycle(context);
    CheckDuplicateStartAndStaleEnd(context);
    CheckPidReuseBoundaries(context);
    CheckExplicitPidReuse(context);
    CheckRetiredExpiryAndStaleProbe(context);
    CheckSystemProbeSuppressionAndInvalidData(context);
    CheckCapacityGenerationAndSaturation(context);
    return context.failures();
}
