#include "network_flow_monitor/collector/collector_core.h"

#include <limits>
#include <utility>

namespace nfm::collector {
namespace {

void IncrementSaturating(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

void AddSaturating(std::uint64_t& value, std::uint64_t increment) noexcept {
    if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
        value = std::numeric_limits<std::uint64_t>::max();
    } else {
        value += increment;
    }
}

bool IsSystemImageHint(std::string_view image_name_hint) noexcept {
    constexpr std::string_view expected{"System"};
    if (image_name_hint.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        char value = image_name_hint.at(index);
        char expected_value = expected.at(index);
        if (value >= 'A' && value <= 'Z') {
            value = static_cast<char>(value + ('a' - 'A'));
        }
        if (expected_value >= 'A' && expected_value <= 'Z') {
            expected_value = static_cast<char>(expected_value + ('a' - 'A'));
        }
        if (value != expected_value) {
            return false;
        }
    }
    return true;
}

} // namespace

bool CollectorProcessProbeResult::ok() const noexcept {
    return error == CollectorProcessProbeError::None;
}

bool CollectorCoreResult::ok() const noexcept {
    return error == CollectorCoreError::None;
}

CollectorCore::CollectorCore(ProcessProbe& process_probe) : process_probe_{process_probe} {}

CollectorCoreResult CollectorCore::Consume(const NetworkTrafficEvent& event) {
    if (IsFailed()) {
        return {.error = CollectorCoreError::CollectorFailed};
    }
    IncrementSaturating(health_.network_events_counted);
    AddSaturating(health_.network_bytes_counted, event.bytes);
    if (event.protocol == NetworkProtocol::Tcp) {
        IncrementSaturating(health_.tcp_events);
        AddSaturating(health_.tcp_bytes, event.bytes);
    } else {
        IncrementSaturating(health_.udp_events);
        AddSaturating(health_.udp_bytes, event.bytes);
    }
    if (event.ip_version == IpVersion::V4) {
        IncrementSaturating(health_.ipv4_events);
        AddSaturating(health_.ipv4_bytes, event.bytes);
    } else {
        IncrementSaturating(health_.ipv6_events);
        AddSaturating(health_.ipv6_bytes, event.bytes);
    }
    if (event.direction == ranking::TrafficDirection::Upload) {
        IncrementSaturating(health_.upload_events);
        AddSaturating(health_.upload_bytes, event.bytes);
    } else {
        IncrementSaturating(health_.download_events);
        AddSaturating(health_.download_bytes, event.bytes);
    }
    if (event.retransmission) {
        IncrementSaturating(health_.tcp_retransmit_events);
        AddSaturating(health_.tcp_retransmitted_bytes, event.bytes);
    }

    const auto lookup = process_tracker_.ResolveAt(event.pid, event.observed_at);
    std::string_view application_id = kUnattributedApplicationId;
    process::ApplicationMetadata metadata{.display_name = "Unattributed"};
    if (lookup.process.has_value()) {
        const auto& tracked_process = lookup.process.value();
        application_id = tracked_process.application.id;
        metadata = tracked_process.application.metadata;
    } else {
        IncrementSaturating(health_.unmatched_network_events);
        AddSaturating(health_.unmatched_network_bytes, event.bytes);
    }

    const auto accumulated = accumulator_.AddTraffic(application_id, metadata, event.observed_at,
                                                     event.direction, event.bytes);
    if (accumulated.disposition == ranking::AccumulationDisposition::AggregatedToOther) {
        IncrementSaturating(health_.active_application_capacity_hits);
        IncrementSaturating(health_.aggregated_application_events);
        AddSaturating(health_.aggregated_application_bytes, event.bytes);
        if (!IsFailed()) {
            health_.state = CollectorState::Degraded;
        }
    }
    if (accumulated.error == ranking::AccumulationError::CounterOverflow) {
        IncrementSaturating(health_.counter_overflows);
        RecordError({.domain = CollectorErrorDomain::Accumulator,
                     .stage = CollectorErrorStage::AccumulateTraffic,
                     .code = static_cast<std::uint32_t>(accumulated.error)});
        health_.state = CollectorState::Failed;
        return {.error = CollectorCoreError::CounterOverflow};
    }
    if (!accumulated.ok()) {
        RecordError({.domain = CollectorErrorDomain::Accumulator,
                     .stage = CollectorErrorStage::AccumulateTraffic,
                     .code = static_cast<std::uint32_t>(accumulated.error)});
        health_.state = CollectorState::Failed;
        return {.error = CollectorCoreError::CollectorFailed};
    }
    return {};
}

CollectorCoreResult CollectorCore::Consume(process::ProcessStartObservation observation) {
    if (IsFailed()) {
        return {.error = CollectorCoreError::CollectorFailed};
    }
    if (observation.kind == process::ProcessStartKind::Rundown) {
        IncrementSaturating(health_.process_rundown_events);
        if (observation.pid == 4U && IsSystemImageHint(observation.image_name_hint)) {
            health_.system_process_rundown_confirmed = true;
        }
    } else {
        IncrementSaturating(health_.process_start_events);
    }

    const auto started = process_tracker_.ObserveStart(std::move(observation));
    if (!started.accepted()) {
        RecordLifecycleError(started.error);
        return {.error = CollectorCoreError::LifecycleRejected};
    }
    if (!started.probe_request.has_value()) {
        return {};
    }

    auto probe_result = process_probe_.Probe(*started.probe_request);
    if (!probe_result.ok()) {
        IncrementSaturating(health_.process_probe_failures);
        RecordError(
            probe_result.diagnostic.present()
                ? probe_result.diagnostic
                : CollectorErrorRecord{.domain = CollectorErrorDomain::ProcessProbe,
                                       .stage = CollectorErrorStage::ProbeProcess,
                                       .code = static_cast<std::uint32_t>(probe_result.error)});
        return {};
    }
    const auto applied = process_tracker_.ApplyProbeData(started.instance, probe_result.data);
    if (applied != process::ProcessTrackerError::None) {
        RecordLifecycleError(applied);
        return {.error = CollectorCoreError::LifecycleRejected};
    }
    return {};
}

CollectorCoreResult CollectorCore::Consume(process::ProcessEndObservation observation) {
    if (IsFailed()) {
        return {.error = CollectorCoreError::CollectorFailed};
    }
    IncrementSaturating(health_.process_end_events);
    const auto ended = process_tracker_.ObserveEnd(observation);
    if (ended != process::ProcessTrackerError::None) {
        RecordLifecycleError(ended);
        return {.error = CollectorCoreError::LifecycleRejected};
    }
    return {};
}

CollectorCoreResult CollectorCore::AdvanceWatermark(process::TraceTimePoint watermark) {
    if (IsFailed()) {
        return {.error = CollectorCoreError::CollectorFailed};
    }
    const auto advanced = process_tracker_.AdvanceWatermark(watermark);
    if (advanced != process::ProcessTrackerError::None) {
        RecordLifecycleError(advanced);
        return {.error = CollectorCoreError::LifecycleRejected};
    }
    const auto accumulator_advanced = accumulator_.AdvanceWatermark(watermark);
    if (accumulator_advanced.error != ranking::AccumulationError::None) {
        RecordError({.domain = CollectorErrorDomain::Accumulator,
                     .stage = CollectorErrorStage::AccumulateTraffic,
                     .code = static_cast<std::uint32_t>(accumulator_advanced.error)});
        health_.state = CollectorState::Failed;
        return {.error = CollectorCoreError::CollectorFailed};
    }
    return {};
}

void CollectorCore::RecordEtwEvent(CollectorEventDisposition disposition) noexcept {
    IncrementSaturating(health_.events_received);
    switch (disposition) {
    case CollectorEventDisposition::Decoded:
        IncrementSaturating(health_.events_decoded);
        break;
    case CollectorEventDisposition::Ignored:
        IncrementSaturating(health_.events_ignored);
        break;
    case CollectorEventDisposition::Malformed:
        IncrementSaturating(health_.malformed_events);
        break;
    case CollectorEventDisposition::UnsupportedSchema:
        IncrementSaturating(health_.unsupported_schema_events);
        break;
    }
}

void CollectorCore::RecordExternalError(CollectorErrorRecord error, bool fatal,
                                        bool callback_failure, bool unexpected_failure) noexcept {
    RecordError(error);
    if (callback_failure) {
        IncrementSaturating(health_.callback_failures);
    }
    if (unexpected_failure) {
        IncrementSaturating(health_.unexpected_failures);
    }
    if (fatal) {
        health_.state = CollectorState::Failed;
    }
}

void CollectorCore::SetState(CollectorState state) noexcept {
    if (!IsFailed()) {
        health_.state = state;
    }
}

void CollectorCore::UpdateEtwStatistics(EtwSessionStatistics statistics) noexcept {
    const bool reset = statistics.events_lost < etw_statistics_.events_lost ||
                       statistics.log_buffers_lost < etw_statistics_.log_buffers_lost ||
                       statistics.realtime_buffers_lost < etw_statistics_.realtime_buffers_lost;
    if (reset) {
        IncrementSaturating(health_.statistics_resets);
    }
    const bool loss_increased =
        reset ? statistics.events_lost != 0U || statistics.log_buffers_lost != 0U ||
                    statistics.realtime_buffers_lost != 0U
              : statistics.events_lost > etw_statistics_.events_lost ||
                    statistics.log_buffers_lost > etw_statistics_.log_buffers_lost ||
                    statistics.realtime_buffers_lost > etw_statistics_.realtime_buffers_lost;
    etw_statistics_ = statistics;
    health_.events_lost = statistics.events_lost;
    health_.log_buffers_lost = statistics.log_buffers_lost;
    health_.realtime_buffers_lost = statistics.realtime_buffers_lost;
    if (loss_increased && health_.state == CollectorState::Running) {
        health_.state = CollectorState::Degraded;
    }
}

ranking::CounterSnapshot CollectorCore::MakeSnapshot(
    std::chrono::steady_clock::time_point captured_at) const {
    return accumulator_.MakeSnapshot(captured_at);
}

const CollectorHealth& CollectorCore::health() const noexcept {
    return health_;
}

bool CollectorCore::IsFailed() const noexcept {
    return health_.state == CollectorState::Failed;
}

void CollectorCore::RecordError(CollectorErrorRecord error) noexcept {
    if (!health_.first_error.present()) {
        health_.first_error = error;
    }
    health_.last_error = error;
}

void CollectorCore::RecordLifecycleError(process::ProcessTrackerError error) noexcept {
    IncrementSaturating(health_.lifecycle_errors);
    RecordError({.domain = CollectorErrorDomain::ProcessTracker,
                 .stage = CollectorErrorStage::ApplyProcessLifecycle,
                 .code = static_cast<std::uint32_t>(error)});
}

} // namespace nfm::collector
