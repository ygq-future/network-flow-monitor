#include "network_flow_monitor/collector/collector_core.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace nfm::collector {

struct CollectorCoreTestAccess {
    static void SetEventsReceived(CollectorCore& core, std::uint64_t value) noexcept {
        core.health_.events_received = value;
    }

    static bool FillDynamicApplicationCapacity(CollectorCore& core) {
        for (std::size_t index = 0U; index < ranking::kMaximumDynamicApplications; ++index) {
            const auto id = std::string{"app.capacity."} + std::to_string(index);
            const auto result = core.accumulator_.AddTraffic(id, {.display_name = "Capacity"},
                                                             process::TraceTimePoint{},
                                                             ranking::TrafficDirection::Upload, 1U);
            if (!result.ok()) {
                return false;
            }
        }
        return true;
    }

    static void SaturateCapacityDiagnostics(CollectorCore& core) noexcept {
        core.health_.active_application_capacity_hits = std::numeric_limits<std::uint64_t>::max();
        core.health_.aggregated_application_events = std::numeric_limits<std::uint64_t>::max();
        core.health_.aggregated_application_bytes = std::numeric_limits<std::uint64_t>::max();
    }
};

} // namespace nfm::collector

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

class FakeProcessProbe final : public nfm::collector::ProcessProbe {
  public:
    [[nodiscard]] nfm::collector::CollectorProcessProbeResult Probe(
        const nfm::process::ProcessProbeRequest& request) noexcept override {
        ++calls_;
        if (request.instance.pid == 200U) {
            return {.error = nfm::collector::CollectorProcessProbeError::AccessDenied};
        }
        if (request.instance.pid == 100U || request.instance.pid == 101U) {
            return {.data = {.application_user_model_id = "Family!Merged"}};
        }
        if (request.instance.pid == 300U) {
            const auto application =
                request.instance.generation == 1U ? "Family!Old" : "Family!New";
            return {.data = {.application_user_model_id = application}};
        }
        return {.data = {.image_path = "C:\\Apps\\other.exe"}};
    }

    [[nodiscard]] std::uint64_t calls() const noexcept {
        return calls_;
    }

  private:
    std::uint64_t calls_{};
};

using nfm::collector::CollectorCore;
using nfm::collector::CollectorCoreError;
using nfm::collector::CollectorCoreTestAccess;
using nfm::collector::CollectorErrorDomain;
using nfm::collector::CollectorErrorStage;
using nfm::collector::CollectorState;
using nfm::collector::EtwSessionStatistics;
using nfm::collector::IpVersion;
using nfm::collector::NetworkProtocol;
using nfm::collector::NetworkTrafficEvent;
using nfm::process::ProcessEndObservation;
using nfm::process::ProcessStartKind;
using nfm::process::ProcessStartObservation;
using nfm::process::TraceDuration;
using nfm::process::TraceTimePoint;
using nfm::ranking::ApplicationCounters;
using nfm::ranking::CounterSnapshot;
using nfm::ranking::TrafficDirection;

TraceTimePoint At(std::chrono::milliseconds offset) {
    return TraceTimePoint{std::chrono::duration_cast<TraceDuration>(offset)};
}

ProcessStartObservation Start(std::uint32_t pid, std::chrono::milliseconds observed_at,
                              std::optional<std::uint64_t> kernel_key = std::nullopt,
                              ProcessStartKind kind = ProcessStartKind::Started) {
    return {.pid = pid,
            .observed_at = At(observed_at),
            .kernel_process_key = kernel_key,
            .image_name_hint = "test.exe",
            .kind = kind};
}

NetworkTrafficEvent Traffic(std::uint32_t pid, std::chrono::milliseconds observed_at,
                            TrafficDirection direction, std::uint64_t bytes,
                            bool retransmission = false,
                            NetworkProtocol protocol = NetworkProtocol::Tcp,
                            IpVersion ip_version = IpVersion::V4) {
    return {.pid = pid,
            .observed_at = At(observed_at),
            .protocol = protocol,
            .ip_version = ip_version,
            .direction = direction,
            .bytes = bytes,
            .retransmission = retransmission};
}

const ApplicationCounters* Find(const CounterSnapshot& snapshot, std::string_view id) {
    for (const auto& counters : snapshot.applications) {
        if (counters.id == id) {
            return &counters;
        }
    }
    return nullptr;
}

void CheckResolvedMergeAndUnattributed(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(100U, 0ms, 1000U)).ok(), "first start consumed");
    context.Check(core.Consume(Start(101U, 0ms, 1001U)).ok(), "second start consumed");
    context.Check(core.Consume(Traffic(100U, 1ms, TrafficDirection::Upload, 100U)).ok(),
                  "first upload consumed");
    context.Check(core.Consume(Traffic(101U, 1ms, TrafficDirection::Download, 50U)).ok(),
                  "second download consumed");

    context.Check(core.Consume(Traffic(999U, 1ms, TrafficDirection::Upload, 7U)).ok(),
                  "unmatched traffic retained");
    context.Check(core.Consume(Traffic(101U, 2ms, TrafficDirection::Download, 13U, false,
                                       NetworkProtocol::Udp, IpVersion::V6))
                      .ok(),
                  "UDP IPv6 download consumed");
    const auto snapshot = core.MakeSnapshot(std::chrono::steady_clock::time_point{});
    const auto* merged = Find(snapshot, "package-app:Family!Merged");
    context.Check(merged != nullptr, "same application identity merged");
    if (merged != nullptr) {
        context.Check(merged->upload_bytes == 100U, "merged upload correct");
        context.Check(merged->download_bytes == 63U, "merged download correct");
    }
    const auto* unattributed = Find(snapshot, nfm::collector::kUnattributedApplicationId);
    context.Check(unattributed != nullptr && unattributed->upload_bytes == 7U,
                  "unmatched bytes use unattributed identity");
    context.Check(core.health().unmatched_network_events == 1U, "unmatched event diagnosed");
    context.Check(core.health().unmatched_network_bytes == 7U, "unmatched bytes diagnosed");
    context.Check(core.health().network_events_counted == 4U &&
                      core.health().network_bytes_counted == 170U,
                  "aggregate network health counted");
    context.Check(core.health().tcp_events == 3U && core.health().tcp_bytes == 157U &&
                      core.health().udp_events == 1U && core.health().udp_bytes == 13U,
                  "protocol health counted");
    context.Check(core.health().ipv4_events == 3U && core.health().ipv4_bytes == 157U &&
                      core.health().ipv6_events == 1U && core.health().ipv6_bytes == 13U,
                  "IP version health counted");
    context.Check(core.health().upload_events == 2U && core.health().upload_bytes == 107U &&
                      core.health().download_events == 2U && core.health().download_bytes == 63U,
                  "traffic direction health counted");
    context.Check(probe.calls() == 2U, "each ordinary start probed once");
}

void CheckProbeFailureAndReuseBoundary(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(200U, 0ms, 2000U)).ok(),
                  "access-denied process start retained");
    context.Check(core.Consume(Traffic(200U, 1ms, TrafficDirection::Upload, 5U)).ok(),
                  "unresolved process traffic retained");
    auto snapshot = core.MakeSnapshot(std::chrono::steady_clock::time_point{});
    context.Check(Find(snapshot, "unresolved:200:1") != nullptr,
                  "probe failure remains instance-scoped unresolved");
    context.Check(core.health().process_probe_failures == 1U, "probe failure diagnosed");
    context.Check(core.health().first_error.domain == CollectorErrorDomain::ProcessProbe &&
                      core.health().first_error.stage == CollectorErrorStage::ProbeProcess,
                  "probe failure classified");

    CollectorCore reuse_core{probe};
    context.Check(reuse_core.Consume(Start(300U, 0ms, 3000U)).ok(), "old PID start consumed");
    context.Check(reuse_core.Consume(Start(300U, 10ms, 3001U)).ok(), "reused PID start consumed");
    context.Check(reuse_core.Consume(Traffic(300U, 9ms, TrafficDirection::Upload, 9U)).ok(),
                  "late old event consumed");
    context.Check(reuse_core.Consume(Traffic(300U, 10ms, TrafficDirection::Upload, 10U)).ok(),
                  "boundary new event consumed");
    snapshot = reuse_core.MakeSnapshot(std::chrono::steady_clock::time_point{});
    const auto* old_process = Find(snapshot, "package-app:Family!Old");
    const auto* new_process = Find(snapshot, "package-app:Family!New");
    context.Check(old_process != nullptr && old_process->upload_bytes == 9U,
                  "pre-boundary event maps old generation");
    context.Check(new_process != nullptr && new_process->upload_bytes == 10U,
                  "boundary event maps new generation");
}

void CheckEndRetransmitOverflowAndHealth(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(400U, 0ms, 4000U)).ok(), "overflow process starts");
    context.Check(core.Consume(Traffic(400U, 1ms, TrafficDirection::Upload,
                                       std::numeric_limits<std::uint64_t>::max(), true))
                      .ok(),
                  "maximum counter value accepted once");
    context.Check(core.health().tcp_retransmit_events == 1U, "retransmit event diagnosed");
    context.Check(core.health().tcp_retransmitted_bytes ==
                      std::numeric_limits<std::uint64_t>::max(),
                  "retransmit bytes saturate exactly");
    const auto overflow = core.Consume(Traffic(400U, 2ms, TrafficDirection::Upload, 1U));
    context.Check(overflow.error == CollectorCoreError::CounterOverflow,
                  "counter overflow classified");
    context.Check(core.health().state == CollectorState::Failed,
                  "counter overflow fails collector");
    context.Check(core.health().first_error.domain == CollectorErrorDomain::Accumulator &&
                      core.health().first_error.stage == CollectorErrorStage::AccumulateTraffic,
                  "counter overflow classified in health");

    CollectorCore lifecycle{probe};
    auto system_rundown = Start(4U, 0ms, 4U, ProcessStartKind::Rundown);
    system_rundown.image_name_hint = "sYsTeM";
    context.Check(lifecycle.Consume(std::move(system_rundown)).ok(),
                  "system process rundown consumed");
    context.Check(lifecycle.health().system_process_rundown_confirmed,
                  "PID 4 classification requires System rundown hint");
    context.Check(lifecycle.Consume(Start(500U, 5ms, 5000U)).ok(), "end process starts");
    const auto bad_end = lifecycle.Consume(
        ProcessEndObservation{.pid = 500U, .observed_at = At(4ms), .kernel_process_key = 5000U});
    context.Check(bad_end.error == CollectorCoreError::LifecycleRejected,
                  "end before start classified");
    context.Check(lifecycle.health().lifecycle_errors == 1U, "lifecycle failure diagnosed");
    context.Check(lifecycle.health().first_error.domain == CollectorErrorDomain::ProcessTracker &&
                      lifecycle.health().first_error.stage ==
                          CollectorErrorStage::ApplyProcessLifecycle,
                  "lifecycle failure classified in health");

    lifecycle.UpdateEtwStatistics(
        {.events_lost = 10U, .log_buffers_lost = 0U, .realtime_buffers_lost = 2U});
    context.Check(lifecycle.health().state == CollectorState::Degraded, "ETW loss degrades health");
    context.Check(lifecycle.health().events_lost == 10U &&
                      lifecycle.health().realtime_buffers_lost == 2U,
                  "ETW loss values retained");
    lifecycle.UpdateEtwStatistics(
        {.events_lost = 1U, .log_buffers_lost = 0U, .realtime_buffers_lost = 0U});
    context.Check(lifecycle.health().statistics_resets == 1U,
                  "decreasing ETW counters establishes new baseline");

    CollectorCoreTestAccess::SetEventsReceived(lifecycle,
                                               std::numeric_limits<std::uint64_t>::max());
    lifecycle.RecordEtwEvent(nfm::collector::CollectorEventDisposition::Decoded);
    context.Check(lifecycle.Consume(Traffic(999U, 6ms, TrafficDirection::Download, 1U)).ok(),
                  "event remains consumable at diagnostic saturation");
    context.Check(lifecycle.health().events_received == std::numeric_limits<std::uint64_t>::max(),
                  "shared diagnostic increment never wraps");
}

void CheckCapacityAggregationAndHealth(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(CollectorCoreTestAccess::FillDynamicApplicationCapacity(core),
                  "collector dynamic capacity filled");
    context.Check(core.Consume(Start(600U, 0ms, 6000U)).ok(), "overflow identity process starts");
    context.Check(core.Consume(Traffic(600U, 1ms, TrafficDirection::Upload, 9U)).ok(),
                  "capacity event is retained through Other");

    const auto snapshot = core.MakeSnapshot({});
    const auto* const other = Find(snapshot, nfm::ranking::kOtherApplicationId);
    context.Check(other != nullptr && other->upload_bytes == 9U,
                  "capacity bytes are visible in deterministic Other bucket");
    context.Check(core.health().active_application_capacity_hits == 1U &&
                      core.health().aggregated_application_events == 1U &&
                      core.health().aggregated_application_bytes == 9U,
                  "capacity aggregation diagnostics counted");
    context.Check(core.health().network_events_counted == 1U &&
                      core.health().network_bytes_counted == 9U &&
                      core.health().upload_events == 1U && core.health().upload_bytes == 9U,
                  "global direction totals include aggregated event");
    context.Check(core.health().state == CollectorState::Degraded,
                  "first capacity hit degrades a running collector");

    CollectorCoreTestAccess::SaturateCapacityDiagnostics(core);
    context.Check(core.Consume(Traffic(600U, 2ms, TrafficDirection::Upload, 4U)).ok(),
                  "aggregation remains available with saturated diagnostics");
    context.Check(core.health().active_application_capacity_hits ==
                          std::numeric_limits<std::uint64_t>::max() &&
                      core.health().aggregated_application_events ==
                          std::numeric_limits<std::uint64_t>::max() &&
                      core.health().aggregated_application_bytes ==
                          std::numeric_limits<std::uint64_t>::max(),
                  "capacity diagnostics saturate without wrapping");
}

void CheckCollectorWatermarkPrunesAccumulator(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(700U, 0ms, 7000U)).ok(), "retained process starts");
    context.Check(core.Consume(Traffic(700U, 0ms, TrafficDirection::Upload, 5U)).ok(),
                  "dynamic traffic counted before watermark");
    context.Check(core.Consume(Traffic(999U, 0ms, TrafficDirection::Download, 2U)).ok(),
                  "fixed unattributed traffic counted before watermark");
    context.Check(core.AdvanceWatermark(At(15s)).ok(), "exact retention watermark accepted");
    auto snapshot = core.MakeSnapshot({});
    context.Check(Find(snapshot, "executable:C:\\Apps\\other.exe") != nullptr,
                  "collector preserves dynamic entry at exact boundary");

    context.Check(core.AdvanceWatermark(At(15s) + TraceDuration{1}).ok(),
                  "post-retention watermark accepted");
    snapshot = core.MakeSnapshot({});
    context.Check(Find(snapshot, "executable:C:\\Apps\\other.exe") == nullptr,
                  "collector watermark prunes expired dynamic entry");
    context.Check(Find(snapshot, nfm::collector::kUnattributedApplicationId) != nullptr,
                  "collector watermark preserves fixed unattributed entry");
}

} // namespace

int main() {
    TestContext context;
    CheckResolvedMergeAndUnattributed(context);
    CheckProbeFailureAndReuseBoundary(context);
    CheckEndRetransmitOverflowAndHealth(context);
    CheckCapacityAggregationAndHealth(context);
    CheckCollectorWatermarkPrunesAccumulator(context);
    return context.failures();
}
