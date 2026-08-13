#include "network_flow_monitor/process/process_tracker.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace nfm::process {
namespace {

void IncrementSaturating(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

bool SamePresentKernelKey(const std::optional<std::uint64_t>& left,
                          const std::optional<std::uint64_t>& right) noexcept {
    return left.has_value() && right.has_value() && left == right;
}

} // namespace

bool ObserveStartResult::accepted() const noexcept {
    return error == ProcessTrackerError::None;
}

bool ProcessLookupResult::found() const noexcept {
    return error == ProcessTrackerError::None && process.has_value();
}

ObserveStartResult ProcessTracker::ObserveStart(ProcessStartObservation observation) {
    const auto active = active_.find(observation.pid);
    if (active != active_.end()) {
        if (observation.observed_at < active->second.started_at) {
            return {.error = ProcessTrackerError::NonIncreasingStartTime};
        }
        if (SamePresentKernelKey(active->second.kernel_process_key,
                                 observation.kernel_process_key)) {
            if (active->second.image_name_hint.empty() && !observation.image_name_hint.empty()) {
                active->second.image_name_hint = std::move(observation.image_name_hint);
                if (active->second.application.metadata.display_name.empty()) {
                    active->second.application.metadata.display_name =
                        active->second.image_name_hint;
                }
            }
            return {.instance = active->second.instance};
        }
        if (observation.observed_at == active->second.started_at) {
            return {.error = ProcessTrackerError::NonIncreasingStartTime};
        }
    }

    if (next_generation_ == 0U) {
        return {.error = ProcessTrackerError::GenerationExhausted};
    }
    const ProcessInstanceId instance{.pid = observation.pid, .generation = next_generation_};
    const ProcessProbeData initial_data{.image_name_hint = observation.image_name_hint};
    auto initial_identity = ResolveApplicationIdentity(instance, initial_data);
    if (!initial_identity.ok()) {
        return {.error = ProcessTrackerError::InvalidProbeData};
    }

    ProcessEntry entry{.instance = instance,
                       .started_at = observation.observed_at,
                       .kernel_process_key = observation.kernel_process_key,
                       .image_name_hint = std::move(observation.image_name_hint),
                       .application = std::move(initial_identity.identity)};
    if (active != active_.end()) {
        RetireActive(active, observation.observed_at);
        IncrementSaturating(diagnostics_.implicit_pid_reuses);
    }
    active_.emplace(observation.pid, std::move(entry));

    if (next_generation_ == std::numeric_limits<std::uint64_t>::max()) {
        next_generation_ = 0U;
    } else {
        ++next_generation_;
    }

    const auto& application = active_.at(observation.pid).application;
    std::optional<ProcessProbeRequest> probe_request;
    if (application.kind != IdentityKind::System) {
        probe_request = ProcessProbeRequest{
            .instance = instance, .observed_at = observation.observed_at, .kind = observation.kind};
    }
    return {.instance = instance, .probe_request = probe_request};
}

ProcessTrackerError ProcessTracker::ObserveEnd(ProcessEndObservation observation) {
    const auto active = active_.find(observation.pid);
    if (active == active_.end()) {
        IncrementSaturating(diagnostics_.stale_end_events);
        return ProcessTrackerError::StaleEnd;
    }
    if (observation.observed_at < active->second.started_at) {
        return ProcessTrackerError::EndBeforeStart;
    }
    if (active->second.kernel_process_key.has_value() &&
        observation.kernel_process_key.has_value() &&
        active->second.kernel_process_key != observation.kernel_process_key) {
        IncrementSaturating(diagnostics_.stale_end_events);
        return ProcessTrackerError::StaleEnd;
    }

    RetireActive(active, observation.observed_at);
    return ProcessTrackerError::None;
}

ProcessTrackerError ProcessTracker::ApplyProbeData(ProcessInstanceId instance,
                                                   const ProcessProbeData& data) {
    ProcessEntry* entry = FindEntry(instance);
    if (entry == nullptr) {
        IncrementSaturating(diagnostics_.stale_probe_results);
        return ProcessTrackerError::StaleProbeResult;
    }

    ProcessProbeData combined = data;
    if (combined.image_name_hint.empty()) {
        combined.image_name_hint = entry->image_name_hint;
    }
    auto resolved = ResolveApplicationIdentity(instance, combined);
    if (!resolved.ok()) {
        return ProcessTrackerError::InvalidProbeData;
    }
    entry->application = std::move(resolved.identity);
    return ProcessTrackerError::None;
}

ProcessLookupResult ProcessTracker::ResolveAt(std::uint32_t pid, TraceTimePoint event_time) {
    const ProcessEntry* selected = nullptr;
    const auto active = active_.find(pid);
    if (active != active_.end() && active->second.started_at <= event_time) {
        selected = &active->second;
    }
    for (auto& retired : retired_) {
        if (retired.instance.pid != pid || retired.started_at > event_time ||
            !retired.ended_at.has_value() || event_time >= *retired.ended_at) {
            continue;
        }
        if (selected == nullptr || retired.started_at > selected->started_at) {
            selected = &retired;
        }
    }
    if (selected == nullptr) {
        IncrementSaturating(diagnostics_.unmatched_network_events);
        return {.error = ProcessTrackerError::NotFound};
    }
    return {.process = TrackedProcess{.instance = selected->instance,
                                      .application = selected->application}};
}

ProcessTrackerError ProcessTracker::AdvanceWatermark(TraceTimePoint watermark) {
    if (watermark_.has_value() && watermark < *watermark_) {
        return ProcessTrackerError::WatermarkMovedBackward;
    }
    watermark_ = watermark;
    for (auto retired = retired_.begin(); retired != retired_.end();) {
        const bool expired = retired->ended_at.has_value() && watermark >= *retired->ended_at &&
                             watermark - *retired->ended_at >= kRetiredProcessRetention;
        if (expired) {
            retired = retired_.erase(retired);
            IncrementSaturating(diagnostics_.expired_retired_processes);
        } else {
            ++retired;
        }
    }
    EnforceCapacity();
    return ProcessTrackerError::None;
}

const ProcessTrackerDiagnostics& ProcessTracker::diagnostics() const noexcept {
    return diagnostics_;
}

ProcessTracker::ProcessEntry* ProcessTracker::FindEntry(ProcessInstanceId instance) noexcept {
    const auto active = active_.find(instance.pid);
    if (active != active_.end() && active->second.instance == instance) {
        return &active->second;
    }
    const auto retired = std::ranges::find(retired_, instance, &ProcessEntry::instance);
    return retired == retired_.end() ? nullptr : &*retired;
}

void ProcessTracker::RetireActive(
    const std::unordered_map<std::uint32_t, ProcessEntry>::iterator& active,
    TraceTimePoint ended_at) {
    ProcessEntry retired = active->second;
    retired.ended_at = ended_at;
    retired_.push_back(std::move(retired));
    active_.erase(active);
    EnforceCapacity();
}

void ProcessTracker::EnforceCapacity() {
    while (retired_.size() > kMaximumRetiredProcesses) {
        const auto oldest =
            std::ranges::min_element(retired_, [](const auto& left, const auto& right) {
                if (left.ended_at != right.ended_at) {
                    return left.ended_at < right.ended_at;
                }
                return left.instance.generation < right.instance.generation;
            });
        retired_.erase(oldest);
        IncrementSaturating(diagnostics_.capacity_evictions);
    }
}

} // namespace nfm::process
