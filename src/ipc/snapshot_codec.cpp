#include "network_flow_monitor/ipc/snapshot_codec.h"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace nfm::ipc {
namespace {

constexpr std::uint8_t kMaximumCollectorState = 4U;
constexpr std::uint8_t kMaximumCollectorErrorDomain = 8U;
constexpr std::uint8_t kMaximumCollectorErrorStage = 14U;
constexpr std::uint8_t kMaximumIpcServerState = 4U;
constexpr std::uint8_t kMaximumActiveClients = 4U;

static_assert(kCollectorHealthWireSize == 324U);
static_assert(kIpcServerHealthWireSize == 112U);
static_assert(kSnapshotFixedWireSize == 468U);
static_assert(kCollectorStatusWireSize == 444U);

void AppendU8(std::vector<std::byte>& output, std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
}

void AppendU16(std::vector<std::byte>& output, std::uint16_t value) {
    AppendU8(output, static_cast<std::uint8_t>(value & 0xFFU));
    AppendU8(output, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::byte>& output, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        AppendU8(output, static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
}

void AppendU64(std::vector<std::byte>& output, std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        AppendU8(output, static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
}

void AppendString(std::vector<std::byte>& output, std::string_view value) {
    for (const char character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

class Cursor final {
  public:
    explicit Cursor(std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    bool ReadU8(std::uint8_t& value) noexcept {
        std::span<const std::byte> bytes;
        if (!ReadBytes(1U, bytes)) {
            return false;
        }
        value = std::to_integer<std::uint8_t>(bytes.front());
        return true;
    }

    bool ReadU16(std::uint16_t& value) noexcept {
        std::span<const std::byte> bytes;
        if (!ReadBytes(2U, bytes)) {
            return false;
        }
        const auto first = std::to_integer<std::uint8_t>(bytes.front());
        const auto second = std::to_integer<std::uint8_t>(bytes.back());
        value = static_cast<std::uint16_t>(first) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(second) << 8U);
        return true;
    }

    bool ReadU32(std::uint32_t& value) noexcept {
        std::span<const std::byte> bytes;
        if (!ReadBytes(4U, bytes)) {
            return false;
        }
        value = 0U;
        std::size_t shift = 0U;
        for (const auto byte : bytes) {
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(byte)) << shift;
            shift += 8U;
        }
        return true;
    }

    bool ReadU64(std::uint64_t& value) noexcept {
        std::span<const std::byte> bytes;
        if (!ReadBytes(8U, bytes)) {
            return false;
        }
        value = 0U;
        std::size_t shift = 0U;
        for (const auto byte : bytes) {
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte)) << shift;
            shift += 8U;
        }
        return true;
    }

    bool ReadBytes(std::size_t length, std::span<const std::byte>& value) noexcept {
        if (length > remaining()) {
            return false;
        }
        value = bytes_.subspan(offset_, length);
        offset_ += length;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    [[nodiscard]] bool at_end() const noexcept {
        return offset_ == bytes_.size();
    }

  private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

std::uint8_t ByteAt(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return std::to_integer<std::uint8_t>(bytes.subspan(offset, 1U).front());
}

bool IsContinuation(std::uint8_t value) noexcept {
    return value >= 0x80U && value <= 0xBFU;
}

std::size_t Utf8SequenceLength(std::uint8_t first) noexcept {
    if (first <= 0x7FU) {
        return 1U;
    }
    if (first >= 0xC2U && first <= 0xDFU) {
        return 2U;
    }
    if (first >= 0xE0U && first <= 0xEFU) {
        return 3U;
    }
    if (first >= 0xF0U && first <= 0xF4U) {
        return 4U;
    }
    return 0U;
}

bool HasValidContinuationBytes(std::span<const std::byte> bytes, std::size_t length) noexcept {
    for (std::size_t index = 1U; index < length; ++index) {
        if (!IsContinuation(ByteAt(bytes, index))) {
            return false;
        }
    }
    return true;
}

bool HasValidScalarBounds(std::span<const std::byte> bytes, std::size_t length) noexcept {
    if (length < 3U) {
        return true;
    }
    const auto first = ByteAt(bytes, 0U);
    const auto second = ByteAt(bytes, 1U);
    return (first != 0xE0U || second >= 0xA0U) && (first != 0xEDU || second <= 0x9FU) &&
           (first != 0xF0U || second >= 0x90U) && (first != 0xF4U || second <= 0x8FU);
}

bool IsValidUtf8(std::span<const std::byte> bytes) noexcept {
    while (!bytes.empty()) {
        const auto length = Utf8SequenceLength(ByteAt(bytes, 0U));
        if (length == 0U || bytes.size() < length || !HasValidContinuationBytes(bytes, length) ||
            !HasValidScalarBounds(bytes, length)) {
            return false;
        }
        bytes = bytes.subspan(length);
    }
    return true;
}

bool IsValidUtf8(std::string_view value) noexcept {
    const auto characters = std::span{value.data(), value.size()};
    return IsValidUtf8(std::as_bytes(characters));
}

std::string CopyString(std::span<const std::byte> bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return result;
}

bool IsValidCollectorHealth(const collector::CollectorHealth& health) noexcept {
    return static_cast<std::uint8_t>(health.state) <= kMaximumCollectorState &&
           static_cast<std::uint8_t>(health.first_error.domain) <= kMaximumCollectorErrorDomain &&
           static_cast<std::uint8_t>(health.last_error.domain) <= kMaximumCollectorErrorDomain &&
           static_cast<std::uint8_t>(health.first_error.stage) <= kMaximumCollectorErrorStage &&
           static_cast<std::uint8_t>(health.last_error.stage) <= kMaximumCollectorErrorStage;
}

bool IsValidIpcHealth(const IpcServerHealth& health) noexcept {
    return static_cast<std::uint8_t>(health.state) <= kMaximumIpcServerState &&
           health.active_clients <= kMaximumActiveClients;
}

void AppendCollectorError(std::vector<std::byte>& output,
                          const collector::CollectorErrorRecord& error) {
    AppendU8(output, static_cast<std::uint8_t>(error.domain));
    AppendU8(output, static_cast<std::uint8_t>(error.stage));
    AppendU16(output, 0U);
    AppendU32(output, error.code);
}

void AppendCollectorHealth(std::vector<std::byte>& output,
                           const collector::CollectorHealth& health) {
    AppendU8(output, static_cast<std::uint8_t>(health.state));
    AppendU8(output, health.system_process_rundown_confirmed ? 1U : 0U);
    AppendU16(output, 0U);
    AppendCollectorError(output, health.first_error);
    AppendCollectorError(output, health.last_error);
    AppendU64(output, health.events_received);
    AppendU64(output, health.events_decoded);
    AppendU64(output, health.events_ignored);
    AppendU64(output, health.malformed_events);
    AppendU64(output, health.unsupported_schema_events);
    AppendU64(output, health.network_events_counted);
    AppendU64(output, health.network_bytes_counted);
    AppendU64(output, health.tcp_events);
    AppendU64(output, health.tcp_bytes);
    AppendU64(output, health.udp_events);
    AppendU64(output, health.udp_bytes);
    AppendU64(output, health.ipv4_events);
    AppendU64(output, health.ipv4_bytes);
    AppendU64(output, health.ipv6_events);
    AppendU64(output, health.ipv6_bytes);
    AppendU64(output, health.upload_events);
    AppendU64(output, health.upload_bytes);
    AppendU64(output, health.download_events);
    AppendU64(output, health.download_bytes);
    AppendU64(output, health.process_start_events);
    AppendU64(output, health.process_rundown_events);
    AppendU64(output, health.process_end_events);
    AppendU64(output, health.process_probe_failures);
    AppendU64(output, health.lifecycle_errors);
    AppendU64(output, health.unmatched_network_events);
    AppendU64(output, health.unmatched_network_bytes);
    AppendU64(output, health.tcp_retransmit_events);
    AppendU64(output, health.tcp_retransmitted_bytes);
    AppendU64(output, health.events_lost);
    AppendU64(output, health.log_buffers_lost);
    AppendU64(output, health.realtime_buffers_lost);
    AppendU64(output, health.statistics_resets);
    AppendU64(output, health.callback_failures);
    AppendU64(output, health.unexpected_failures);
    AppendU64(output, health.counter_overflows);
    AppendU64(output, health.active_application_capacity_hits);
    AppendU64(output, health.aggregated_application_events);
    AppendU64(output, health.aggregated_application_bytes);
}

bool DecodeCollectorError(Cursor& cursor, collector::CollectorErrorRecord& error) noexcept {
    std::uint8_t domain{};
    std::uint8_t stage{};
    std::uint16_t reserved{};
    std::uint32_t code{};
    if (!cursor.ReadU8(domain) || !cursor.ReadU8(stage) || !cursor.ReadU16(reserved) ||
        !cursor.ReadU32(code) || domain > kMaximumCollectorErrorDomain ||
        stage > kMaximumCollectorErrorStage || reserved != 0U) {
        return false;
    }
    error = {.domain = static_cast<collector::CollectorErrorDomain>(domain),
             .stage = static_cast<collector::CollectorErrorStage>(stage),
             .code = code};
    return true;
}

bool DecodeCollectorHealth(Cursor& cursor, collector::CollectorHealth& health) noexcept {
    collector::CollectorHealth candidate;
    std::uint8_t state{};
    std::uint8_t rundown{};
    std::uint16_t reserved{};
    if (!cursor.ReadU8(state) || !cursor.ReadU8(rundown) || !cursor.ReadU16(reserved) ||
        state > kMaximumCollectorState || rundown > 1U || reserved != 0U ||
        !DecodeCollectorError(cursor, candidate.first_error) ||
        !DecodeCollectorError(cursor, candidate.last_error)) {
        return false;
    }
    candidate.state = static_cast<collector::CollectorState>(state);
    candidate.system_process_rundown_confirmed = rundown != 0U;
    if (!cursor.ReadU64(candidate.events_received) || !cursor.ReadU64(candidate.events_decoded) ||
        !cursor.ReadU64(candidate.events_ignored) || !cursor.ReadU64(candidate.malformed_events) ||
        !cursor.ReadU64(candidate.unsupported_schema_events) ||
        !cursor.ReadU64(candidate.network_events_counted) ||
        !cursor.ReadU64(candidate.network_bytes_counted) || !cursor.ReadU64(candidate.tcp_events) ||
        !cursor.ReadU64(candidate.tcp_bytes) || !cursor.ReadU64(candidate.udp_events) ||
        !cursor.ReadU64(candidate.udp_bytes) || !cursor.ReadU64(candidate.ipv4_events) ||
        !cursor.ReadU64(candidate.ipv4_bytes) || !cursor.ReadU64(candidate.ipv6_events) ||
        !cursor.ReadU64(candidate.ipv6_bytes) || !cursor.ReadU64(candidate.upload_events) ||
        !cursor.ReadU64(candidate.upload_bytes) || !cursor.ReadU64(candidate.download_events) ||
        !cursor.ReadU64(candidate.download_bytes) ||
        !cursor.ReadU64(candidate.process_start_events) ||
        !cursor.ReadU64(candidate.process_rundown_events) ||
        !cursor.ReadU64(candidate.process_end_events) ||
        !cursor.ReadU64(candidate.process_probe_failures) ||
        !cursor.ReadU64(candidate.lifecycle_errors) ||
        !cursor.ReadU64(candidate.unmatched_network_events) ||
        !cursor.ReadU64(candidate.unmatched_network_bytes) ||
        !cursor.ReadU64(candidate.tcp_retransmit_events) ||
        !cursor.ReadU64(candidate.tcp_retransmitted_bytes) ||
        !cursor.ReadU64(candidate.events_lost) || !cursor.ReadU64(candidate.log_buffers_lost) ||
        !cursor.ReadU64(candidate.realtime_buffers_lost) ||
        !cursor.ReadU64(candidate.statistics_resets) ||
        !cursor.ReadU64(candidate.callback_failures) ||
        !cursor.ReadU64(candidate.unexpected_failures) ||
        !cursor.ReadU64(candidate.counter_overflows) ||
        !cursor.ReadU64(candidate.active_application_capacity_hits) ||
        !cursor.ReadU64(candidate.aggregated_application_events) ||
        !cursor.ReadU64(candidate.aggregated_application_bytes)) {
        return false;
    }
    health = candidate;
    return true;
}

void AppendIpcHealth(std::vector<std::byte>& output, const IpcServerHealth& health) {
    AppendU8(output, static_cast<std::uint8_t>(health.state));
    AppendU8(output, health.active_clients);
    AppendU16(output, 0U);
    AppendU32(output, health.last_win32_error);
    AppendU64(output, health.accepted_connections);
    AppendU64(output, health.unauthorized_connections);
    AppendU64(output, health.protocol_errors);
    AppendU64(output, health.frames_received);
    AppendU64(output, health.frames_sent);
    AppendU64(output, health.bytes_received);
    AppendU64(output, health.bytes_sent);
    AppendU64(output, health.snapshots_captured);
    AppendU64(output, health.snapshots_published);
    AppendU64(output, health.pending_snapshots_replaced);
    AppendU64(output, health.slow_client_disconnects);
    AppendU64(output, health.cancellation_failures);
    AppendU64(output, health.internal_failures);
}

bool DecodeIpcHealth(Cursor& cursor, IpcServerHealth& health) noexcept {
    IpcServerHealth candidate;
    std::uint8_t state{};
    std::uint16_t reserved{};
    if (!cursor.ReadU8(state) || !cursor.ReadU8(candidate.active_clients) ||
        !cursor.ReadU16(reserved) || !cursor.ReadU32(candidate.last_win32_error) ||
        state > kMaximumIpcServerState || candidate.active_clients > kMaximumActiveClients ||
        reserved != 0U || !cursor.ReadU64(candidate.accepted_connections) ||
        !cursor.ReadU64(candidate.unauthorized_connections) ||
        !cursor.ReadU64(candidate.protocol_errors) || !cursor.ReadU64(candidate.frames_received) ||
        !cursor.ReadU64(candidate.frames_sent) || !cursor.ReadU64(candidate.bytes_received) ||
        !cursor.ReadU64(candidate.bytes_sent) || !cursor.ReadU64(candidate.snapshots_captured) ||
        !cursor.ReadU64(candidate.snapshots_published) ||
        !cursor.ReadU64(candidate.pending_snapshots_replaced) ||
        !cursor.ReadU64(candidate.slow_client_disconnects) ||
        !cursor.ReadU64(candidate.cancellation_failures) ||
        !cursor.ReadU64(candidate.internal_failures)) {
        return false;
    }
    candidate.state = static_cast<IpcServerState>(state);
    health = candidate;
    return true;
}

SnapshotCodecError ValidateEntry(const CatalogEntry& entry, bool full_baseline) noexcept {
    if (entry.handle.value == 0U || entry.metadata_revision == 0U ||
        (entry.removed && full_baseline) ||
        (entry.removed &&
         (!entry.metadata.display_name.empty() || !entry.metadata.icon_key.empty()))) {
        return SnapshotCodecError::MalformedPayload;
    }
    if (entry.metadata.display_name.size() > kMaximumCatalogStringBytes ||
        entry.metadata.icon_key.size() > kMaximumCatalogStringBytes) {
        return SnapshotCodecError::PayloadTooLarge;
    }
    if (!IsValidUtf8(entry.metadata.display_name) || !IsValidUtf8(entry.metadata.icon_key)) {
        return SnapshotCodecError::InvalidUtf8;
    }
    return SnapshotCodecError::None;
}

SnapshotCodecError ValidateEntries(std::span<const CatalogEntry> entries, bool full_baseline) {
    std::vector<std::uint64_t> handles;
    handles.reserve(entries.size());
    for (const auto& entry : entries) {
        const auto error = ValidateEntry(entry, full_baseline);
        if (error != SnapshotCodecError::None) {
            return error;
        }
        handles.push_back(entry.handle.value);
    }
    std::ranges::sort(handles);
    if (std::ranges::adjacent_find(handles) != handles.end()) {
        return SnapshotCodecError::DuplicateApplicationHandle;
    }
    return SnapshotCodecError::None;
}

std::size_t CatalogEntryWireSize(const CatalogEntry& entry) noexcept {
    return kCatalogEntryPrefixWireSize + entry.metadata.display_name.size() +
           entry.metadata.icon_key.size();
}

PayloadEncodeResult EncodeCatalogChunkValidated(const CatalogChunk& chunk) {
    std::size_t payload_size = kCatalogPrefixWireSize;
    for (const auto& entry : chunk.entries) {
        const auto entry_size = CatalogEntryWireSize(entry);
        if (entry_size > kMaximumFramePayload - payload_size) {
            return {.error = SnapshotCodecError::PayloadTooLarge};
        }
        payload_size += entry_size;
    }

    PayloadEncodeResult result;
    result.payload.reserve(payload_size);
    AppendU64(result.payload, chunk.sequence);
    AppendU32(result.payload, chunk.chunk_index);
    AppendU32(result.payload, chunk.chunk_count);
    AppendU32(result.payload, static_cast<std::uint32_t>(chunk.entries.size()));
    AppendU32(result.payload, chunk.full_baseline ? 1U : 0U);
    for (const auto& entry : chunk.entries) {
        AppendU64(result.payload, entry.handle.value);
        AppendU32(result.payload, entry.metadata_revision);
        AppendU32(result.payload, entry.removed ? 1U : 0U);
        AppendU32(result.payload, static_cast<std::uint32_t>(entry.metadata.display_name.size()));
        AppendU32(result.payload, static_cast<std::uint32_t>(entry.metadata.icon_key.size()));
        AppendString(result.payload, entry.metadata.display_name);
        AppendString(result.payload, entry.metadata.icon_key);
    }
    return result;
}

SnapshotDecodeResult DecodeSnapshotInternal(std::span<const std::byte> payload,
                                            const SnapshotMessage* previous) {
    if (payload.size() < kSnapshotFixedWireSize) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    Cursor cursor{payload};
    SnapshotMessage candidate;
    std::uint32_t application_count{};
    std::uint32_t flags{};
    if (!cursor.ReadU64(candidate.snapshot_sequence) ||
        !cursor.ReadU64(candidate.required_catalog_sequence) ||
        !cursor.ReadU64(candidate.uptime_microseconds) || !cursor.ReadU32(application_count) ||
        !cursor.ReadU32(flags) || candidate.snapshot_sequence == 0U ||
        candidate.required_catalog_sequence == 0U || candidate.uptime_microseconds == 0U ||
        application_count > kMaximumSnapshotApplications || flags > 1U ||
        !DecodeCollectorHealth(cursor, candidate.collector_health) ||
        !DecodeIpcHealth(cursor, candidate.ipc_health)) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    const auto expected_application_bytes =
        static_cast<std::size_t>(application_count) * kApplicationCounterWireSize;
    if (cursor.remaining() != expected_application_bytes) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    candidate.applications_aggregated_to_other = flags != 0U;
    candidate.applications.reserve(application_count);
    std::uint64_t prior_handle{};
    for (std::uint32_t index = 0U; index < application_count; ++index) {
        ApplicationCounterWire application;
        if (!cursor.ReadU64(application.handle.value) ||
            !cursor.ReadU64(application.upload_bytes) ||
            !cursor.ReadU64(application.download_bytes) || application.handle.value == 0U) {
            return {.error = SnapshotCodecError::MalformedPayload};
        }
        if (application.handle.value == prior_handle) {
            return {.error = SnapshotCodecError::DuplicateApplicationHandle};
        }
        if (application.handle.value < prior_handle) {
            return {.error = SnapshotCodecError::MalformedPayload};
        }
        prior_handle = application.handle.value;
        candidate.applications.push_back(application);
    }
    if (!cursor.at_end()) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }

    if (previous != nullptr) {
        if (candidate.snapshot_sequence <= previous->snapshot_sequence ||
            candidate.uptime_microseconds <= previous->uptime_microseconds ||
            candidate.required_catalog_sequence < previous->required_catalog_sequence) {
            return {.error = SnapshotCodecError::InvalidSnapshotOrder};
        }
        auto previous_applications = previous->applications;
        std::ranges::sort(previous_applications, {}, &ApplicationCounterWire::handle);
        for (const auto& current : candidate.applications) {
            const auto found = std::ranges::lower_bound(previous_applications, current.handle, {},
                                                        &ApplicationCounterWire::handle);
            if (found != previous_applications.end() && found->handle == current.handle &&
                (current.upload_bytes < found->upload_bytes ||
                 current.download_bytes < found->download_bytes)) {
                return {.error = SnapshotCodecError::CounterDecreased};
            }
        }
    }
    return {.message = std::move(candidate)};
}

} // namespace

bool PayloadEncodeResult::ok() const noexcept {
    return error == SnapshotCodecError::None;
}

bool CatalogBatchEncodeResult::ok() const noexcept {
    return error == SnapshotCodecError::None;
}

bool CatalogDecodeResult::ok() const noexcept {
    return error == SnapshotCodecError::None && chunk.has_value();
}

bool SnapshotDecodeResult::ok() const noexcept {
    return error == SnapshotCodecError::None && message.has_value();
}

bool CollectorStatusDecodeResult::ok() const noexcept {
    return error == SnapshotCodecError::None && message.has_value();
}

PayloadEncodeResult EncodeCatalogChunk(const CatalogChunk& chunk) {
    if (chunk.sequence == 0U || chunk.chunk_count == 0U || chunk.chunk_index >= chunk.chunk_count ||
        chunk.entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    const auto error = ValidateEntries(chunk.entries, chunk.full_baseline);
    if (error != SnapshotCodecError::None) {
        return {.error = error};
    }
    return EncodeCatalogChunkValidated(chunk);
}

CatalogBatchEncodeResult EncodeCatalog(std::uint64_t sequence, bool full_baseline,
                                       std::span<const CatalogEntry> entries) {
    if (sequence == 0U || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    const auto validation_error = ValidateEntries(entries, full_baseline);
    if (validation_error != SnapshotCodecError::None) {
        return {.error = validation_error};
    }

    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t begin{};
    std::size_t payload_size = kCatalogPrefixWireSize;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto entry_size = CatalogEntryWireSize(entries.subspan(index, 1U).front());
        if (entry_size > kMaximumFramePayload - kCatalogPrefixWireSize) {
            return {.error = SnapshotCodecError::PayloadTooLarge};
        }
        if (entry_size > kMaximumFramePayload - payload_size) {
            ranges.emplace_back(begin, index);
            begin = index;
            payload_size = kCatalogPrefixWireSize;
        }
        payload_size += entry_size;
    }
    ranges.emplace_back(begin, entries.size());
    if (ranges.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {.error = SnapshotCodecError::PayloadTooLarge};
    }

    CatalogBatchEncodeResult result;
    result.payloads.reserve(ranges.size());
    const auto chunk_count = static_cast<std::uint32_t>(ranges.size());
    for (std::size_t index = 0U; index < ranges.size(); ++index) {
        const auto [range_begin, range_end] = ranges.at(index);
        CatalogChunk chunk{.sequence = sequence,
                           .chunk_index = static_cast<std::uint32_t>(index),
                           .chunk_count = chunk_count,
                           .full_baseline = full_baseline,
                           .entries = {}};
        chunk.entries.assign(entries.begin() + static_cast<std::ptrdiff_t>(range_begin),
                             entries.begin() + static_cast<std::ptrdiff_t>(range_end));
        auto encoded = EncodeCatalogChunkValidated(chunk);
        if (!encoded.ok()) {
            return {.error = encoded.error};
        }
        result.payloads.push_back(std::move(encoded.payload));
    }
    return result;
}

CatalogDecodeResult DecodeCatalogChunk(std::span<const std::byte> payload) {
    if (payload.size() < kCatalogPrefixWireSize || payload.size() > kMaximumFramePayload) {
        return {.error = payload.size() > kMaximumFramePayload
                             ? SnapshotCodecError::PayloadTooLarge
                             : SnapshotCodecError::MalformedPayload};
    }
    Cursor cursor{payload};
    CatalogChunk candidate;
    std::uint32_t entry_count{};
    std::uint32_t flags{};
    if (!cursor.ReadU64(candidate.sequence) || !cursor.ReadU32(candidate.chunk_index) ||
        !cursor.ReadU32(candidate.chunk_count) || !cursor.ReadU32(entry_count) ||
        !cursor.ReadU32(flags) || candidate.sequence == 0U || candidate.chunk_count == 0U ||
        candidate.chunk_index >= candidate.chunk_count || flags > 1U ||
        entry_count > cursor.remaining() / kCatalogEntryPrefixWireSize) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    candidate.full_baseline = flags != 0U;
    candidate.entries.reserve(entry_count);
    std::vector<std::uint64_t> handles;
    handles.reserve(entry_count);
    for (std::uint32_t index = 0U; index < entry_count; ++index) {
        CatalogEntry entry;
        std::uint32_t entry_flags{};
        std::uint32_t display_length{};
        std::uint32_t icon_length{};
        if (!cursor.ReadU64(entry.handle.value) || !cursor.ReadU32(entry.metadata_revision) ||
            !cursor.ReadU32(entry_flags) || !cursor.ReadU32(display_length) ||
            !cursor.ReadU32(icon_length) || entry.handle.value == 0U ||
            entry.metadata_revision == 0U || entry_flags > 1U ||
            display_length > kMaximumCatalogStringBytes ||
            icon_length > kMaximumCatalogStringBytes ||
            static_cast<std::size_t>(display_length) + icon_length > cursor.remaining()) {
            return {.error = SnapshotCodecError::MalformedPayload};
        }
        entry.removed = entry_flags != 0U;
        if ((entry.removed && (display_length != 0U || icon_length != 0U)) ||
            (entry.removed && candidate.full_baseline)) {
            return {.error = SnapshotCodecError::MalformedPayload};
        }
        std::span<const std::byte> display;
        std::span<const std::byte> icon;
        if (!cursor.ReadBytes(display_length, display) || !cursor.ReadBytes(icon_length, icon)) {
            return {.error = SnapshotCodecError::MalformedPayload};
        }
        if (!IsValidUtf8(display) || !IsValidUtf8(icon)) {
            return {.error = SnapshotCodecError::InvalidUtf8};
        }
        if (std::ranges::find(handles, entry.handle.value) != handles.end()) {
            return {.error = SnapshotCodecError::DuplicateApplicationHandle};
        }
        handles.push_back(entry.handle.value);
        entry.metadata = {.display_name = CopyString(display), .icon_key = CopyString(icon)};
        candidate.entries.push_back(std::move(entry));
    }
    if (!cursor.at_end()) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    return {.chunk = std::move(candidate)};
}

PayloadEncodeResult EncodeSnapshot(const SnapshotMessage& message) {
    if (message.snapshot_sequence == 0U || message.required_catalog_sequence == 0U ||
        message.uptime_microseconds == 0U ||
        message.applications.size() > kMaximumSnapshotApplications ||
        !IsValidCollectorHealth(message.collector_health) ||
        !IsValidIpcHealth(message.ipc_health)) {
        return {.error = message.applications.size() > kMaximumSnapshotApplications
                             ? SnapshotCodecError::PayloadTooLarge
                             : SnapshotCodecError::MalformedPayload};
    }
    auto applications = message.applications;
    std::ranges::sort(applications, {}, &ApplicationCounterWire::handle);
    for (std::size_t index = 0U; index < applications.size(); ++index) {
        const auto& application = applications.at(index);
        if (application.handle.value == 0U) {
            return {.error = SnapshotCodecError::MalformedPayload};
        }
        if (index != 0U && applications.at(index - 1U).handle == application.handle) {
            return {.error = SnapshotCodecError::DuplicateApplicationHandle};
        }
    }

    PayloadEncodeResult result;
    result.payload.reserve(kSnapshotFixedWireSize +
                           (applications.size() * kApplicationCounterWireSize));
    AppendU64(result.payload, message.snapshot_sequence);
    AppendU64(result.payload, message.required_catalog_sequence);
    AppendU64(result.payload, message.uptime_microseconds);
    AppendU32(result.payload, static_cast<std::uint32_t>(applications.size()));
    AppendU32(result.payload, message.applications_aggregated_to_other ? 1U : 0U);
    AppendCollectorHealth(result.payload, message.collector_health);
    AppendIpcHealth(result.payload, message.ipc_health);
    for (const auto& application : applications) {
        AppendU64(result.payload, application.handle.value);
        AppendU64(result.payload, application.upload_bytes);
        AppendU64(result.payload, application.download_bytes);
    }
    return result;
}

SnapshotDecodeResult DecodeSnapshot(std::span<const std::byte> payload) {
    return DecodeSnapshotInternal(payload, nullptr);
}

SnapshotDecodeResult DecodeSnapshot(std::span<const std::byte> payload,
                                    const SnapshotMessage& previous) {
    return DecodeSnapshotInternal(payload, &previous);
}

PayloadEncodeResult EncodeCollectorStatus(const LegacyCollectorStatusMessage& message) {
    if (message.uptime_microseconds == 0U || !IsValidCollectorHealth(message.collector_health) ||
        !IsValidIpcHealth(message.ipc_health)) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    PayloadEncodeResult result;
    result.payload.reserve(kCollectorStatusWireSize);
    AppendU64(result.payload, message.uptime_microseconds);
    AppendCollectorHealth(result.payload, message.collector_health);
    AppendIpcHealth(result.payload, message.ipc_health);
    return result;
}

CollectorStatusDecodeResult DecodeCollectorStatus(std::span<const std::byte> payload) {
    if (payload.size() != kCollectorStatusWireSize) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    Cursor cursor{payload};
    LegacyCollectorStatusMessage candidate;
    if (!cursor.ReadU64(candidate.uptime_microseconds) || candidate.uptime_microseconds == 0U ||
        !DecodeCollectorHealth(cursor, candidate.collector_health) ||
        !DecodeIpcHealth(cursor, candidate.ipc_health) || !cursor.at_end()) {
        return {.error = SnapshotCodecError::MalformedPayload};
    }
    return {.message = candidate};
}

} // namespace nfm::ipc
