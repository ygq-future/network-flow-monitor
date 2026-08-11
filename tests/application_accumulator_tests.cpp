#include "network_flow_monitor/ranking/application_accumulator.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

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

int main() {
    TestContext context;
    CheckBasicAccumulation(context);
    CheckValidationAndOrdering(context);
    CheckMetadataRules(context);
    CheckOverflowRollback(context);
    CheckRetentionAndWatermark(context);
    CheckCapacityAndOtherRollback(context);
    return context.failures();
}
