#ifndef NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_CODEC_H
#define NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_CODEC_H

#include "network_flow_monitor/collector/collector_health.h"
#include "network_flow_monitor/ipc/protocol_types.h"
#include "network_flow_monitor/process/application_identity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nfm::ipc {

inline constexpr std::size_t kMaximumCatalogStringBytes = 131'072U;
inline constexpr std::size_t kMaximumSnapshotApplications = 4'096U;
inline constexpr std::size_t kCatalogPrefixWireSize = 24U;
inline constexpr std::size_t kCatalogEntryPrefixWireSize = 24U;
inline constexpr std::size_t kCollectorHealthCounterCount = 38U;
inline constexpr std::size_t kIpcServerHealthCounterCount = 13U;
inline constexpr std::size_t kCollectorErrorWireSize = 8U;
inline constexpr std::size_t kCollectorHealthWireSize =
    4U + (2U * kCollectorErrorWireSize) + (kCollectorHealthCounterCount * 8U);
inline constexpr std::size_t kIpcServerHealthWireSize = 8U + (kIpcServerHealthCounterCount * 8U);
inline constexpr std::size_t kApplicationCounterWireSize = 24U;
inline constexpr std::size_t kSnapshotFixedWireSize =
    32U + kCollectorHealthWireSize + kIpcServerHealthWireSize;
inline constexpr std::size_t kCollectorStatusWireSize =
    8U + kCollectorHealthWireSize + kIpcServerHealthWireSize;

struct ApplicationHandle {
    std::uint64_t value{};

    auto operator<=>(const ApplicationHandle&) const = default;
};

struct CatalogEntry {
    ApplicationHandle handle;
    std::uint32_t metadata_revision{};
    bool removed{};
    process::ApplicationMetadata metadata;

    bool operator==(const CatalogEntry&) const = default;
};

struct CatalogChunk {
    std::uint64_t sequence{};
    std::uint32_t chunk_index{};
    std::uint32_t chunk_count{};
    bool full_baseline{};
    std::vector<CatalogEntry> entries;

    bool operator==(const CatalogChunk&) const = default;
};

struct ApplicationCounterWire {
    ApplicationHandle handle;
    std::uint64_t upload_bytes{};
    std::uint64_t download_bytes{};

    bool operator==(const ApplicationCounterWire&) const = default;
};

enum class IpcServerState : std::uint8_t {
    Stopped,
    Starting,
    Running,
    Degraded,
    Failed,
};

struct IpcServerHealth {
    IpcServerState state{IpcServerState::Stopped};
    std::uint8_t active_clients{};
    std::uint32_t last_win32_error{};
    std::uint64_t accepted_connections{};
    std::uint64_t unauthorized_connections{};
    std::uint64_t protocol_errors{};
    std::uint64_t frames_received{};
    std::uint64_t frames_sent{};
    std::uint64_t bytes_received{};
    std::uint64_t bytes_sent{};
    std::uint64_t snapshots_captured{};
    std::uint64_t snapshots_published{};
    std::uint64_t pending_snapshots_replaced{};
    std::uint64_t slow_client_disconnects{};
    std::uint64_t cancellation_failures{};
    std::uint64_t internal_failures{};

    bool operator==(const IpcServerHealth&) const = default;
};

struct SnapshotMessage {
    std::uint64_t snapshot_sequence{};
    std::uint64_t required_catalog_sequence{};
    std::uint64_t uptime_microseconds{};
    bool applications_aggregated_to_other{};
    collector::CollectorHealth collector_health;
    IpcServerHealth ipc_health;
    std::vector<ApplicationCounterWire> applications;
};

struct CollectorStatusMessage {
    std::uint64_t uptime_microseconds{};
    collector::CollectorHealth collector_health;
    IpcServerHealth ipc_health;
};

enum class SnapshotCodecError : std::uint8_t {
    None,
    PayloadTooLarge,
    MalformedPayload,
    InvalidUtf8,
    DuplicateApplicationHandle,
    InvalidSnapshotOrder,
    CounterDecreased,
};

struct PayloadEncodeResult {
    SnapshotCodecError error{SnapshotCodecError::None};
    std::vector<std::byte> payload;

    [[nodiscard]] bool ok() const noexcept;
};

struct CatalogBatchEncodeResult {
    SnapshotCodecError error{SnapshotCodecError::None};
    std::vector<std::vector<std::byte>> payloads;

    [[nodiscard]] bool ok() const noexcept;
};

struct CatalogDecodeResult {
    SnapshotCodecError error{SnapshotCodecError::None};
    std::optional<CatalogChunk> chunk;

    [[nodiscard]] bool ok() const noexcept;
};

struct SnapshotDecodeResult {
    SnapshotCodecError error{SnapshotCodecError::None};
    std::optional<SnapshotMessage> message;

    [[nodiscard]] bool ok() const noexcept;
};

struct CollectorStatusDecodeResult {
    SnapshotCodecError error{SnapshotCodecError::None};
    std::optional<CollectorStatusMessage> message;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] PayloadEncodeResult EncodeCatalogChunk(const CatalogChunk& chunk);
[[nodiscard]] CatalogBatchEncodeResult EncodeCatalog(std::uint64_t sequence, bool full_baseline,
                                                     std::span<const CatalogEntry> entries);
[[nodiscard]] CatalogDecodeResult DecodeCatalogChunk(std::span<const std::byte> payload);

[[nodiscard]] PayloadEncodeResult EncodeSnapshot(const SnapshotMessage& message);
[[nodiscard]] SnapshotDecodeResult DecodeSnapshot(std::span<const std::byte> payload);
[[nodiscard]] SnapshotDecodeResult DecodeSnapshot(std::span<const std::byte> payload,
                                                  const SnapshotMessage& previous);

[[nodiscard]] PayloadEncodeResult EncodeCollectorStatus(const CollectorStatusMessage& message);
[[nodiscard]] CollectorStatusDecodeResult DecodeCollectorStatus(std::span<const std::byte> payload);

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_SNAPSHOT_CODEC_H
