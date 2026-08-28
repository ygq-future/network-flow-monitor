#include "network_flow_monitor/ranking/application_accumulator.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace nfm::ranking {
namespace {

bool EnrichField(std::string& current, const std::string& observed) {
    if (observed.empty()) {
        return false;
    }
    if (current.empty()) {
        current = observed;
        return false;
    }
    return current != observed;
}

bool EnrichMetadata(ApplicationMetadata& current, const ApplicationMetadata& observed) {
    const bool display_conflict = EnrichField(current.display_name, observed.display_name);
    const bool icon_conflict = EnrichField(current.icon_key, observed.icon_key);
    return display_conflict || icon_conflict;
}

bool IsFixedBucket(std::string_view id) noexcept {
    return id == kUnattributedApplicationId || id == kOtherApplicationId;
}

bool IsExpired(AccumulationTimePoint watermark, AccumulationTimePoint last_activity) noexcept {
    if (last_activity > AccumulationTimePoint::max() - kActiveApplicationRetention) {
        return false;
    }
    return watermark > last_activity + kActiveApplicationRetention;
}

void IncrementSaturating(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

} // namespace

bool AccumulationResult::ok() const noexcept {
    return error == AccumulationError::None;
}

AccumulationResult ApplicationAccumulator::AddTraffic( // NOLINT(misc-no-recursion)
    std::string_view id, const ApplicationMetadata& metadata, AccumulationTimePoint observed_at,
    TrafficDirection direction, std::uint64_t bytes) {
    if (id.empty()) {
        return {.error = AccumulationError::EmptyApplicationId};
    }
    if (bytes == 0U) {
        return {};
    }

    const auto existing = records_.find(std::string{id});
    if (existing == records_.end()) {
        const bool fixed_bucket = IsFixedBucket(id);
        if (!fixed_bucket && dynamic_application_count_ >= kMaximumDynamicApplications) {
            const ApplicationMetadata other_metadata{.display_name = "Other"};
            auto result =
                AddTraffic(kOtherApplicationId, other_metadata, observed_at, direction, bytes);
            result.disposition = AccumulationDisposition::AggregatedToOther;
            return result;
        }

        const ApplicationId application_id{id};
        ApplicationCounters counters{.id = application_id, .metadata = metadata};
        if (direction == TrafficDirection::Upload) {
            counters.upload_bytes = bytes;
        } else {
            counters.download_bytes = bytes;
        }
        records_.emplace(application_id, ActiveApplicationRecord{.counters = std::move(counters),
                                                                 .last_activity = observed_at,
                                                                 .fixed_bucket = fixed_bucket});
        if (!fixed_bucket) {
            ++dynamic_application_count_;
        }
        return {};
    }

    auto& record = existing->second;
    auto& counters = record.counters;
    auto& current =
        direction == TrafficDirection::Upload ? counters.upload_bytes : counters.download_bytes;
    if (current > std::numeric_limits<std::uint64_t>::max() - bytes) {
        return {.error = AccumulationError::CounterOverflow};
    }

    auto merged_metadata = counters.metadata;
    const bool metadata_conflict = EnrichMetadata(merged_metadata, metadata);
    current += bytes;
    counters.metadata = std::move(merged_metadata);
    record.last_activity = std::max(record.last_activity, observed_at);
    if (metadata_conflict) {
        IncrementSaturating(metadata_conflicts_);
    }
    return {};
}

AccumulatorAdvanceResult ApplicationAccumulator::AdvanceWatermark(AccumulationTimePoint watermark) {
    if (watermark_.has_value() && watermark < *watermark_) {
        return {.error = AccumulationError::WatermarkMovedBackward};
    }
    watermark_ = watermark;

    std::size_t evicted = 0U;
    for (auto current = records_.begin(); current != records_.end();) {
        if (!current->second.fixed_bucket && IsExpired(watermark, current->second.last_activity)) {
            current = records_.erase(current);
            --dynamic_application_count_;
            ++evicted;
        } else {
            ++current;
        }
    }
    return {.evicted = evicted};
}

CounterSnapshot ApplicationAccumulator::MakeSnapshot(
    std::chrono::steady_clock::time_point captured_at) const {
    CounterSnapshot snapshot{.captured_at = captured_at};
    snapshot.applications.reserve(records_.size());
    for (const auto& entry : records_) {
        snapshot.applications.push_back(entry.second.counters);
    }
    std::ranges::sort(snapshot.applications, {}, &ApplicationCounters::id);
    return snapshot;
}

std::uint64_t ApplicationAccumulator::metadata_conflicts() const noexcept {
    return metadata_conflicts_;
}

} // namespace nfm::ranking
