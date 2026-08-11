#include "network_flow_monitor/ipc/snapshot_codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using nfm::collector::CollectorErrorDomain;
using nfm::collector::CollectorErrorStage;
using nfm::collector::CollectorHealth;
using nfm::collector::CollectorState;
using nfm::ipc::ApplicationHandle;
using nfm::ipc::CatalogChunk;
using nfm::ipc::CatalogEntry;
using nfm::ipc::CollectorStatusMessage;
using nfm::ipc::IpcServerHealth;
using nfm::ipc::IpcServerState;
using nfm::ipc::SnapshotCodecError;
using nfm::ipc::SnapshotMessage;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

std::byte B(std::uint8_t value) {
    return static_cast<std::byte>(value);
}

void StoreU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.at(offset + index) = B(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

void StoreU64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes.at(offset + index) = B(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

std::uint64_t ReadU64(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint64_t value{};
    std::size_t shift{};
    for (const auto byte : bytes.subspan(offset, 8U)) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte)) << shift;
        shift += 8U;
    }
    return value;
}

CollectorHealth MakeCollectorHealth() {
    CollectorHealth health;
    health.state = CollectorState::Degraded;
    health.system_process_rundown_confirmed = true;
    health.first_error = {.domain = CollectorErrorDomain::Etw,
                          .stage = CollectorErrorStage::StartSession,
                          .code = 0x11223344U};
    health.last_error = {.domain = CollectorErrorDomain::Internal,
                         .stage = CollectorErrorStage::BufferCallback,
                         .code = 0x55667788U};
    std::uint64_t value = 101U;
    health.events_received = value++;
    health.events_decoded = value++;
    health.events_ignored = value++;
    health.malformed_events = value++;
    health.unsupported_schema_events = value++;
    health.network_events_counted = value++;
    health.network_bytes_counted = value++;
    health.tcp_events = value++;
    health.tcp_bytes = value++;
    health.udp_events = value++;
    health.udp_bytes = value++;
    health.ipv4_events = value++;
    health.ipv4_bytes = value++;
    health.ipv6_events = value++;
    health.ipv6_bytes = value++;
    health.upload_events = value++;
    health.upload_bytes = value++;
    health.download_events = value++;
    health.download_bytes = value++;
    health.process_start_events = value++;
    health.process_rundown_events = value++;
    health.process_end_events = value++;
    health.process_probe_failures = value++;
    health.lifecycle_errors = value++;
    health.unmatched_network_events = value++;
    health.unmatched_network_bytes = value++;
    health.tcp_retransmit_events = value++;
    health.tcp_retransmitted_bytes = value++;
    health.events_lost = value++;
    health.log_buffers_lost = value++;
    health.realtime_buffers_lost = value++;
    health.statistics_resets = value++;
    health.callback_failures = value++;
    health.unexpected_failures = value++;
    health.counter_overflows = value++;
    health.active_application_capacity_hits = value++;
    health.aggregated_application_events = value++;
    health.aggregated_application_bytes = value;
    return health;
}

std::array<std::uint64_t, 38> CollectorCounters(const CollectorHealth& health) {
    return {health.events_received,
            health.events_decoded,
            health.events_ignored,
            health.malformed_events,
            health.unsupported_schema_events,
            health.network_events_counted,
            health.network_bytes_counted,
            health.tcp_events,
            health.tcp_bytes,
            health.udp_events,
            health.udp_bytes,
            health.ipv4_events,
            health.ipv4_bytes,
            health.ipv6_events,
            health.ipv6_bytes,
            health.upload_events,
            health.upload_bytes,
            health.download_events,
            health.download_bytes,
            health.process_start_events,
            health.process_rundown_events,
            health.process_end_events,
            health.process_probe_failures,
            health.lifecycle_errors,
            health.unmatched_network_events,
            health.unmatched_network_bytes,
            health.tcp_retransmit_events,
            health.tcp_retransmitted_bytes,
            health.events_lost,
            health.log_buffers_lost,
            health.realtime_buffers_lost,
            health.statistics_resets,
            health.callback_failures,
            health.unexpected_failures,
            health.counter_overflows,
            health.active_application_capacity_hits,
            health.aggregated_application_events,
            health.aggregated_application_bytes};
}

bool CollectorHealthEquals(const CollectorHealth& left, const CollectorHealth& right) {
    return left.state == right.state && left.first_error == right.first_error &&
           left.last_error == right.last_error &&
           left.system_process_rundown_confirmed == right.system_process_rundown_confirmed &&
           CollectorCounters(left) == CollectorCounters(right);
}

IpcServerHealth MakeIpcHealth() {
    IpcServerHealth health;
    health.state = IpcServerState::Degraded;
    health.active_clients = 4U;
    health.last_win32_error = 0x99AABBCCU;
    std::uint64_t value = 201U;
    health.accepted_connections = value++;
    health.unauthorized_connections = value++;
    health.protocol_errors = value++;
    health.frames_received = value++;
    health.frames_sent = value++;
    health.bytes_received = value++;
    health.bytes_sent = value++;
    health.snapshots_captured = value++;
    health.snapshots_published = value++;
    health.pending_snapshots_replaced = value++;
    health.slow_client_disconnects = value++;
    health.cancellation_failures = value++;
    health.internal_failures = value;
    return health;
}

SnapshotMessage MakeSnapshot() {
    return {.snapshot_sequence = 7U,
            .required_catalog_sequence = 5U,
            .uptime_microseconds = 123'456U,
            .applications_aggregated_to_other = true,
            .collector_health = MakeCollectorHealth(),
            .ipc_health = MakeIpcHealth(),
            .applications = {
                {.handle = ApplicationHandle{2U}, .upload_bytes = 200U, .download_bytes = 400U},
                {.handle = ApplicationHandle{1U}, .upload_bytes = 100U, .download_bytes = 300U}}};
}

void CheckHealthGoldenAndRoundTrip(TestContext& context) {
    const auto source = MakeSnapshot();
    const auto encoded = nfm::ipc::EncodeSnapshot(source);
    context.Check(encoded.ok(), "snapshot with distinct health fields encodes");
    context.Check(encoded.payload.size() == nfm::ipc::kSnapshotFixedWireSize +
                                                (2U * nfm::ipc::kApplicationCounterWireSize),
                  "snapshot uses exact fixed and record sizes");
    if (!encoded.ok()) {
        return;
    }

    context.Check(ReadU64(encoded.payload, 0U) == 7U && ReadU64(encoded.payload, 8U) == 5U &&
                      ReadU64(encoded.payload, 16U) == 123'456U,
                  "snapshot prefix uses exact little-endian offsets");
    context.Check(encoded.payload.at(32U) == B(3U) && encoded.payload.at(33U) == B(1U) &&
                      encoded.payload.at(34U) == B(0U) && encoded.payload.at(35U) == B(0U),
                  "CollectorHealth state, bool and reserved bytes are exact");
    for (std::size_t index = 0U; index < 38U; ++index) {
        context.Check(ReadU64(encoded.payload, 52U + (index * 8U)) == 101U + index,
                      "CollectorHealth counter order is exact");
    }
    context.Check(encoded.payload.at(356U) == B(3U) && encoded.payload.at(357U) == B(4U) &&
                      encoded.payload.at(358U) == B(0U) && encoded.payload.at(359U) == B(0U),
                  "IpcServerHealth prefix is exact");
    for (std::size_t index = 0U; index < 13U; ++index) {
        context.Check(ReadU64(encoded.payload, 364U + (index * 8U)) == 201U + index,
                      "IpcServerHealth counter order is exact");
    }

    const auto decoded = nfm::ipc::DecodeSnapshot(encoded.payload);
    context.Check(decoded.ok() && decoded.message.has_value(), "snapshot decodes");
    if (!decoded.message.has_value()) {
        return;
    }
    const auto& message = *decoded.message;
    context.Check(message.snapshot_sequence == source.snapshot_sequence &&
                      message.required_catalog_sequence == source.required_catalog_sequence &&
                      message.uptime_microseconds == source.uptime_microseconds &&
                      message.applications_aggregated_to_other &&
                      CollectorHealthEquals(message.collector_health, source.collector_health) &&
                      message.ipc_health == source.ipc_health,
                  "snapshot decodes every health field");
    context.Check(message.applications.size() == 2U &&
                      message.applications.front().handle.value == 1U &&
                      message.applications.front().upload_bytes == 100U &&
                      message.applications.front().download_bytes == 300U &&
                      message.applications.back().handle.value == 2U &&
                      message.applications.back().upload_bytes == 200U &&
                      message.applications.back().download_bytes == 400U,
                  "snapshot encoder canonicalizes application order");
}

void CheckHealthRejections(TestContext& context) {
    const auto encoded = nfm::ipc::EncodeSnapshot(MakeSnapshot());
    auto CheckRejected = [&context, &encoded](std::size_t offset, std::uint8_t value,
                                              std::string_view name) {
        auto candidate = encoded.payload;
        candidate.at(offset) = B(value);
        const auto result = nfm::ipc::DecodeSnapshot(candidate);
        context.Check(result.error == SnapshotCodecError::MalformedPayload &&
                          !result.message.has_value(),
                      name);
    };
    CheckRejected(28U, 2U, "unknown snapshot flag rejected");
    CheckRejected(32U, 5U, "unknown Collector state rejected");
    CheckRejected(33U, 2U, "non-bool Collector field rejected");
    CheckRejected(34U, 1U, "Collector reserved field rejected");
    CheckRejected(36U, 9U, "unknown Collector error domain rejected");
    CheckRejected(37U, 15U, "unknown Collector error stage rejected");
    CheckRejected(38U, 1U, "Collector error reserved field rejected");
    CheckRejected(356U, 5U, "unknown IPC server state rejected");
    CheckRejected(358U, 1U, "IPC server reserved field rejected");
}

CatalogEntry MakeEntry(std::uint64_t handle, std::uint32_t revision, std::string display,
                       std::string icon) {
    return {.handle = ApplicationHandle{handle},
            .metadata_revision = revision,
            .removed = false,
            .metadata = {.display_name = std::move(display), .icon_key = std::move(icon)}};
}

void CheckCatalogRoundTripsAndBounds(TestContext& context) {
    const CatalogChunk baseline{.sequence = 11U,
                                .chunk_index = 0U,
                                .chunk_count = 1U,
                                .full_baseline = true,
                                .entries = {MakeEntry(1U, 2U, "浏览器", "path:browser")}};
    const auto encoded = nfm::ipc::EncodeCatalogChunk(baseline);
    const auto decoded = nfm::ipc::DecodeCatalogChunk(encoded.payload);
    context.Check(encoded.ok() && decoded.ok() && decoded.chunk == baseline,
                  "full catalog with UTF-8 metadata round trips");

    CatalogChunk delta{
        .sequence = 12U,
        .chunk_index = 0U,
        .chunk_count = 1U,
        .full_baseline = false,
        .entries = {MakeEntry(2U, 1U, std::string(131'072U, 'a'), std::string(131'072U, 'b')),
                    {.handle = ApplicationHandle{1U},
                     .metadata_revision = 3U,
                     .removed = true,
                     .metadata = {}}}};
    const auto max_strings = nfm::ipc::EncodeCatalogChunk(delta);
    context.Check(max_strings.ok() && nfm::ipc::DecodeCatalogChunk(max_strings.payload).ok(),
                  "131072-byte strings and zero-length removal round trip");

    std::vector<CatalogEntry> exact_entries;
    exact_entries.push_back(
        MakeEntry(1U, 1U, std::string(131'072U, 'a'), std::string(131'072U, 'b')));
    exact_entries.push_back(
        MakeEntry(2U, 1U, std::string(131'072U, 'c'), std::string(131'072U, 'd')));
    exact_entries.push_back(
        MakeEntry(3U, 1U, std::string(131'072U, 'e'), std::string(131'072U, 'f')));
    exact_entries.push_back(
        MakeEntry(4U, 1U, std::string(131'072U, 'g'), std::string(130'952U, 'h')));
    const CatalogChunk exact{.sequence = 20U,
                             .chunk_index = 0U,
                             .chunk_count = 1U,
                             .full_baseline = false,
                             .entries = exact_entries};
    const auto exact_result = nfm::ipc::EncodeCatalogChunk(exact);
    context.Check(exact_result.ok() &&
                      exact_result.payload.size() == nfm::ipc::kMaximumFramePayload,
                  "catalog accepts an exact 1 MiB payload");
    exact_entries.back().metadata.icon_key.push_back('i');
    const auto oversized = nfm::ipc::EncodeCatalogChunk({.sequence = 20U,
                                                         .chunk_index = 0U,
                                                         .chunk_count = 1U,
                                                         .full_baseline = false,
                                                         .entries = exact_entries});
    context.Check(oversized.error == SnapshotCodecError::PayloadTooLarge,
                  "catalog rejects a payload one byte above 1 MiB");

    std::vector<CatalogEntry> split_entries;
    for (std::uint64_t handle = 1U; handle <= 4U; ++handle) {
        split_entries.push_back(
            MakeEntry(handle, 1U, std::string(131'072U, 'x'), std::string(131'072U, 'y')));
    }
    const auto split = nfm::ipc::EncodeCatalog(30U, true, split_entries);
    context.Check(split.ok() && split.payloads.size() == 2U,
                  "catalog chunker does not split an entry across 1 MiB frames");
    if (split.payloads.size() == 2U) {
        const auto first = nfm::ipc::DecodeCatalogChunk(split.payloads.front());
        const auto second = nfm::ipc::DecodeCatalogChunk(split.payloads.back());
        if (first.chunk.has_value() && second.chunk.has_value()) {
            context.Check(first.chunk->chunk_index == 0U && second.chunk->chunk_index == 1U &&
                              first.chunk->chunk_count == 2U && second.chunk->chunk_count == 2U &&
                              first.chunk->full_baseline && second.chunk->full_baseline,
                          "catalog chunks carry consistent count, index and flags");
        } else {
            context.Check(false, "catalog chunks decode");
        }
    }
}

void CheckCatalogRejections(TestContext& context) {
    const CatalogChunk valid{.sequence = 1U,
                             .chunk_index = 0U,
                             .chunk_count = 1U,
                             .full_baseline = false,
                             .entries = {MakeEntry(1U, 1U, "App", "icon")}};
    const auto encoded = nfm::ipc::EncodeCatalogChunk(valid);

    auto zero_chunks = encoded.payload;
    StoreU32(zero_chunks, 12U, 0U);
    context.Check(nfm::ipc::DecodeCatalogChunk(zero_chunks).error ==
                      SnapshotCodecError::MalformedPayload,
                  "zero catalog chunk count rejected");
    auto bad_index = encoded.payload;
    StoreU32(bad_index, 8U, 1U);
    context.Check(nfm::ipc::DecodeCatalogChunk(bad_index).error ==
                      SnapshotCodecError::MalformedPayload,
                  "catalog chunk index outside count rejected");

    auto invalid_utf8 = encoded.payload;
    invalid_utf8.at(48U) = B(0xC0U);
    context.Check(nfm::ipc::DecodeCatalogChunk(invalid_utf8).error ==
                      SnapshotCodecError::InvalidUtf8,
                  "overlong UTF-8 lead byte rejected");

    auto invalid_removed = encoded.payload;
    StoreU32(invalid_removed, 36U, 1U);
    context.Check(nfm::ipc::DecodeCatalogChunk(invalid_removed).error ==
                      SnapshotCodecError::MalformedPayload,
                  "removed entry with metadata rejected");

    auto invalid_baseline = encoded.payload;
    StoreU32(invalid_baseline, 20U, 1U);
    StoreU32(invalid_baseline, 36U, 1U);
    StoreU32(invalid_baseline, 40U, 0U);
    StoreU32(invalid_baseline, 44U, 0U);
    invalid_baseline.resize(48U);
    context.Check(nfm::ipc::DecodeCatalogChunk(invalid_baseline).error ==
                      SnapshotCodecError::MalformedPayload,
                  "full baseline removal rejected");

    const auto duplicate = nfm::ipc::EncodeCatalogChunk(
        {.sequence = 2U,
         .chunk_index = 0U,
         .chunk_count = 1U,
         .full_baseline = false,
         .entries = {MakeEntry(1U, 1U, "A", "a"), MakeEntry(1U, 2U, "B", "b")}});
    context.Check(duplicate.error == SnapshotCodecError::DuplicateApplicationHandle,
                  "duplicate catalog handle rejected");

    const auto unique_pair = nfm::ipc::EncodeCatalogChunk(
        {.sequence = 2U,
         .chunk_index = 0U,
         .chunk_count = 1U,
         .full_baseline = false,
         .entries = {MakeEntry(1U, 1U, "A", "a"), MakeEntry(2U, 1U, "B", "b")}});
    auto duplicate_wire = unique_pair.payload;
    StoreU64(duplicate_wire, 50U, 1U);
    context.Check(nfm::ipc::DecodeCatalogChunk(duplicate_wire).error ==
                      SnapshotCodecError::DuplicateApplicationHandle,
                  "duplicate catalog handle on wire rejected");
}

void CheckSnapshotInvariants(TestContext& context) {
    auto many = MakeSnapshot();
    many.applications.clear();
    many.applications.reserve(nfm::ipc::kMaximumSnapshotApplications);
    for (std::uint64_t handle = 1U; handle <= nfm::ipc::kMaximumSnapshotApplications; ++handle) {
        many.applications.push_back({.handle = ApplicationHandle{handle},
                                     .upload_bytes = handle * 2U,
                                     .download_bytes = handle * 3U});
    }
    const auto maximum = nfm::ipc::EncodeSnapshot(many);
    const auto decoded_maximum = nfm::ipc::DecodeSnapshot(maximum.payload);
    if (decoded_maximum.message.has_value()) {
        context.Check(maximum.ok() && decoded_maximum.message->applications.size() ==
                                          nfm::ipc::kMaximumSnapshotApplications,
                      "4096-application snapshot round trips");
    } else {
        context.Check(false, "4096-application snapshot decodes");
    }
    many.applications.push_back({.handle = ApplicationHandle{4097U}});
    context.Check(nfm::ipc::EncodeSnapshot(many).error == SnapshotCodecError::PayloadTooLarge,
                  "snapshot rejects more than 4096 applications");

    const auto duplicate = nfm::ipc::EncodeSnapshot(
        {.snapshot_sequence = 1U,
         .required_catalog_sequence = 1U,
         .uptime_microseconds = 1U,
         .applications = {{.handle = ApplicationHandle{1U}}, {.handle = ApplicationHandle{1U}}}});
    context.Check(duplicate.error == SnapshotCodecError::DuplicateApplicationHandle,
                  "snapshot encoder rejects duplicate handles");

    const auto unique_wire = nfm::ipc::EncodeSnapshot(MakeSnapshot());
    auto duplicate_wire = unique_wire.payload;
    StoreU64(duplicate_wire,
             nfm::ipc::kSnapshotFixedWireSize + nfm::ipc::kApplicationCounterWireSize, 1U);
    context.Check(nfm::ipc::DecodeSnapshot(duplicate_wire).error ==
                      SnapshotCodecError::DuplicateApplicationHandle,
                  "duplicate snapshot handle on wire rejected");
    auto malformed_count = unique_wire.payload;
    StoreU32(malformed_count, 24U,
             static_cast<std::uint32_t>(nfm::ipc::kMaximumSnapshotApplications + 1U));
    context.Check(nfm::ipc::DecodeSnapshot(malformed_count).error ==
                      SnapshotCodecError::MalformedPayload,
                  "snapshot application count above 4096 rejected");

    SnapshotMessage previous{
        .snapshot_sequence = 10U,
        .required_catalog_sequence = 8U,
        .uptime_microseconds = 1'000U,
        .applications = {
            {.handle = ApplicationHandle{1U}, .upload_bytes = 100U, .download_bytes = 200U}}};
    auto next = previous;
    next.snapshot_sequence = 11U;
    next.uptime_microseconds = 2'000U;
    next.applications.front().upload_bytes = 99U;
    const auto decreased = nfm::ipc::EncodeSnapshot(next);
    context.Check(nfm::ipc::DecodeSnapshot(decreased.payload, previous).error ==
                      SnapshotCodecError::CounterDecreased,
                  "counter decrease for a retained handle rejected");

    next.applications.front().upload_bytes = 100U;
    next.snapshot_sequence = previous.snapshot_sequence;
    const auto same_sequence = nfm::ipc::EncodeSnapshot(next);
    context.Check(nfm::ipc::DecodeSnapshot(same_sequence.payload, previous).error ==
                      SnapshotCodecError::InvalidSnapshotOrder,
                  "non-increasing snapshot sequence rejected");
    next.snapshot_sequence = 11U;
    next.uptime_microseconds = previous.uptime_microseconds;
    const auto same_uptime = nfm::ipc::EncodeSnapshot(next);
    context.Check(nfm::ipc::DecodeSnapshot(same_uptime.payload, previous).error ==
                      SnapshotCodecError::InvalidSnapshotOrder,
                  "non-increasing snapshot uptime rejected");
    next.uptime_microseconds = 2'000U;
    next.required_catalog_sequence = previous.required_catalog_sequence - 1U;
    const auto old_catalog = nfm::ipc::EncodeSnapshot(next);
    context.Check(nfm::ipc::DecodeSnapshot(old_catalog.payload, previous).error ==
                      SnapshotCodecError::InvalidSnapshotOrder,
                  "decreasing required catalog sequence rejected");
}

void CheckCollectorStatus(TestContext& context) {
    const CollectorStatusMessage source{.uptime_microseconds = 44U,
                                        .collector_health = MakeCollectorHealth(),
                                        .ipc_health = MakeIpcHealth()};
    const auto encoded = nfm::ipc::EncodeCollectorStatus(source);
    const auto decoded = nfm::ipc::DecodeCollectorStatus(encoded.payload);
    if (decoded.message.has_value()) {
        context.Check(
            encoded.ok() && encoded.payload.size() == nfm::ipc::kCollectorStatusWireSize &&
                decoded.message->uptime_microseconds == 44U &&
                CollectorHealthEquals(decoded.message->collector_health, source.collector_health) &&
                decoded.message->ipc_health == source.ipc_health,
            "CollectorStatus reuses exact health wire layouts");
    } else {
        context.Check(false, "CollectorStatus decodes");
    }

    auto trailing = encoded.payload;
    trailing.push_back(B(0U));
    context.Check(nfm::ipc::DecodeCollectorStatus(trailing).error ==
                      SnapshotCodecError::MalformedPayload,
                  "CollectorStatus rejects trailing bytes");
}

} // namespace

int main() {
    TestContext context;
    CheckHealthGoldenAndRoundTrip(context);
    CheckHealthRejections(context);
    CheckCatalogRoundTripsAndBounds(context);
    CheckCatalogRejections(context);
    CheckSnapshotInvariants(context);
    CheckCollectorStatus(context);

    if (context.failures == 0) {
        std::cout << "ipc snapshot codec tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
