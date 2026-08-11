# Secure IPC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. The user selected inline sequential execution; do not dispatch subagents. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the owner-scoped, versioned, bounded named-pipe IPC layer that carries Collector health and cumulative application counters to ordinary-user UI clients.

**Architecture:** A pure little-endian codec defines a 24-byte frame and version-1 control/catalog/snapshot payloads. A Windows adapter owns explicit SID/DACL policy and cancellable overlapped byte-mode pipes. A four-client server and one central publisher coalesce refresh subscriptions, isolate slow clients, and feed a reconnecting client state machine without depending on windows or service entry points.

**Tech Stack:** C++20, CMake/Ninja, MSVC, Win32 named pipes/security/overlapped I/O, existing `nfm_config`, `nfm_collector`, `nfm_process`, and `nfm_ranking` libraries, CTest, clang-format, clang-tidy/Clang Static Analyzer, MSVC `/analyze`.

## Global Constraints

- Target remains Windows 11 24H2 x64; current development-host evidence is Windows 11 25H2 build 26200 and must not be presented as 24H2 validation.
- Follow ADR 0010 and `docs/superpowers/specs/2026-08-11-secure-ipc-design.md` exactly: owner scope, explicit DACL, no remote clients, 24-byte little-endian frame, protocol version 1, 1 MiB payload maximum, four clients, and 100–5000ms subscriptions.
- Planning files remain under `docs/`; no root planning copies.
- `ipc` must not depend on windows, tray, settings, SCM, UAC launching, or service entry implementations.
- Never serialize native C++ layouts, raw `HANDLE`, SID buffers, `OVERLAPPED`, addresses, ports, command lines, packet payloads, or the internal full Application ID.
- All handles, ACL/security buffers, impersonation state, events, overlapped operations, threads, and cancellation paths use RAII. No exception crosses thread, callback, or IPC ABI boundaries.
- Every task runs Format, Fast, then Full. A proposed commit is documentation only; create no Git commit without explicit user authorization.
- Fast excludes only tests explicitly labeled `requires-admin`; Full must execute every non-skipped Windows integration target in Debug and Release.

---

## Task 1: Bound active application accumulation before transport

**Files:**

- Modify: `include/network_flow_monitor/ranking/application_accumulator.h`
- Modify: `src/ranking/application_accumulator.cpp`
- Modify: `include/network_flow_monitor/collector/collector_core.h`
- Modify: `include/network_flow_monitor/collector/collector_health.h`
- Modify: `src/collector/collector_core.cpp`
- Modify: `tests/application_accumulator_tests.cpp`
- Modify: `tests/collector_core_tests.cpp`

**Interfaces:**

- Consumes: `NetworkTrafficEvent::observed_at`, `CollectorCore::AdvanceWatermark`, configured maximum ranking window 10s, and maximum refresh interval 5s.
- Produces:

```cpp
namespace nfm::ranking {
using AccumulationDuration = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
using AccumulationTimePoint = std::chrono::sys_time<AccumulationDuration>;

inline constexpr auto kActiveApplicationRetention = std::chrono::seconds{15};
inline constexpr std::size_t kMaximumActiveApplications = 4'096U;
inline constexpr std::size_t kMaximumDynamicApplications = 4'094U;
inline constexpr std::string_view kOtherApplicationId{"collector:other"};

enum class AccumulationError : std::uint8_t {
    None, EmptyApplicationId, CounterOverflow, WatermarkMovedBackward,
};
enum class AccumulationDisposition : std::uint8_t { Counted, AggregatedToOther };

struct AccumulationResult {
    AccumulationError error{AccumulationError::None};
    AccumulationDisposition disposition{AccumulationDisposition::Counted};
};

struct AccumulatorAdvanceResult {
    AccumulationError error{AccumulationError::None};
    std::size_t evicted{};
};

AccumulationResult ApplicationAccumulator::AddTraffic(
    std::string_view id, const ApplicationMetadata& metadata,
    AccumulationTimePoint observed_at, TrafficDirection direction, std::uint64_t bytes);
AccumulatorAdvanceResult ApplicationAccumulator::AdvanceWatermark(
    AccumulationTimePoint watermark);
}
```

- Adds `active_application_capacity_hits`, `aggregated_application_events`, and `aggregated_application_bytes` to `CollectorHealth` in the exact order specified by the version-1 wire design.

- [x] **Step 1: Add failing accumulator and Collector tests**

Add cases for 15-second retention, equality at the expiry boundary, backward watermark rejection, 4094 dynamic identities plus two reserved fixed buckets, existing-ID updates at capacity, deterministic `collector:other` aggregation, Other counter overflow rollback, global direction totals, saturating diagnostics, and first capacity hit changing Collector health to `Degraded`.

```cpp
context.Check(accumulator.AdvanceWatermark(At(15s)).evicted == 0U,
              "entry survives exact retention boundary");
context.Check(accumulator.AdvanceWatermark(At(15s) + AccumulationDuration{1}).evicted == 1U,
              "entry expires only after retention boundary");
context.Check(result.disposition == AccumulationDisposition::AggregatedToOther,
              "new identity is explicitly aggregated at capacity");
```

- [x] **Step 2: Run the focused red phase**

Run Format, then build `nfm_application_accumulator_tests` and `nfm_collector_core_tests` through the configured Debug preset. Expected: compilation fails only because the new timestamp/advance/disposition API and health fields do not exist.

- [x] **Step 3: Implement bounded records and pruning**

Replace the accumulator map value with an internal record containing counters, last activity, and fixed-bucket status. Zero-byte input remains a no-op and creates no slot. Watermark is monotonic; pruning removes only dynamic records with `watermark > last_activity + 15s`. Capacity checks reserve two slots even before their buckets first receive traffic.

```cpp
struct ActiveApplicationRecord {
    ApplicationCounters counters;
    AccumulationTimePoint last_activity;
    bool fixed_bucket{};
};
```

At capacity, call the same checked counter update for `collector:other` with metadata `{.display_name = "Other"}` and return `AggregatedToOther`; do not drop bytes or replace an existing dynamic identity.

- [x] **Step 4: Wire Collector diagnostics**

Pass `event.observed_at` into `AddTraffic`. On `AggregatedToOther`, saturating-increment event/byte/capacity diagnostics and set `Degraded` unless the Collector is already `Failed`. After the process tracker accepts a watermark, advance the accumulator with the same value; any accumulator invariant failure is classified at `AccumulateTraffic` and fails closed.

- [x] **Step 5: Verify Task 1**

Run `tools/quality.ps1 -Mode Format`, `Fast`, and `Full`. Require Debug/Release CTest 15/15 and non-skipped ETW integration. Record exact gate output in `docs/planning/progress.md`. Proposed checkpoint: `feat: bound active collector applications`; leave uncommitted without explicit authorization.

---

## Task 2: Version-1 frame and control-message codec

**Files:**

- Create: `include/network_flow_monitor/ipc/protocol_types.h`
- Create: `include/network_flow_monitor/ipc/frame_codec.h`
- Create: `src/ipc/frame_codec.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/ipc_frame_codec_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: configuration refresh bounds from `config/app_config.h`.
- Produces:

```cpp
namespace nfm::ipc {
inline constexpr std::uint32_t kFrameMagic = 0x314D464EU;
inline constexpr std::uint16_t kFrameHeaderSize = 24U;
inline constexpr std::uint16_t kCurrentProtocolVersion = 1U;
inline constexpr std::uint32_t kMaximumFramePayload = 1'048'576U;

enum class MessageType : std::uint8_t { // encoded as uint16 on wire
    ClientHello = 1, ServerHello = 2, Subscribe = 3,
    SubscriptionAccepted = 4, ApplicationCatalog = 5,
    Snapshot = 6, CollectorStatus = 7, ProtocolError = 8,
};
enum class ProtocolErrorCode : std::uint8_t { // encoded as uint16 on wire
    None = 0, MalformedHeader = 1, UnsupportedHeader = 2,
    VersionMismatch = 3, UnknownMessageType = 4, InvalidFlags = 5,
    PayloadTooLarge = 6, MalformedPayload = 7, InvalidUtf8 = 8,
    InvalidSubscription = 9, UnexpectedMessage = 10,
    CatalogOutOfOrder = 11, Unauthorized = 12, ServerBusy = 13,
    InternalFailure = 14,
};
enum class FrameParseStatus : std::uint8_t { NeedMoreData, Decoded, Rejected };
enum class MessageDirection : std::uint8_t { ClientToServer, ServerToClient };

struct FrameHeader {
    std::uint16_t protocol_version{};
    MessageType message_type{};
    std::uint64_t request_id{};
    std::uint32_t payload_length{};
};

struct CollectorInstanceId {
    std::array<std::byte, 16> bytes;
    bool operator==(const CollectorInstanceId&) const = default;
};

struct FrameView { FrameHeader header; std::span<const std::byte> payload; };
struct OwnedFrame { FrameHeader header; std::vector<std::byte> payload; };

struct ClientHello {
    std::uint16_t minimum_version{};
    std::uint16_t maximum_version{};
    std::chrono::milliseconds requested_refresh;
};
struct ServerHello {
    std::uint16_t selected_version{};
    std::uint32_t maximum_frame_payload{};
    CollectorInstanceId instance;
};
struct Subscribe { std::chrono::milliseconds requested_refresh; };
struct SubscriptionAccepted { std::chrono::milliseconds effective_refresh; };
struct ProtocolErrorMessage {
    ProtocolErrorCode code{ProtocolErrorCode::None};
    std::uint8_t retry_disposition{};
    std::uint16_t server_minimum_version{};
    std::uint16_t server_maximum_version{};
    std::uint32_t win32_error{};
};
using ControlMessage = std::variant<ClientHello, ServerHello, Subscribe,
                                    SubscriptionAccepted, ProtocolErrorMessage>;

struct FrameEncodeResult {
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::vector<std::byte> bytes;
};
struct FrameParseResult {
    FrameParseStatus status{FrameParseStatus::NeedMoreData};
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::size_t consumed{};
    std::size_t required{};
    std::optional<OwnedFrame> frame;
};
struct ControlDecodeResult {
    ProtocolErrorCode error{ProtocolErrorCode::None};
    std::optional<ControlMessage> message;
};

FrameEncodeResult EncodeFrame(FrameView frame);
FrameParseResult TryDecodeFrame(std::span<const std::byte> bytes);
FrameEncodeResult EncodeClientHello(const ClientHello& message, std::uint64_t request_id);
ControlDecodeResult DecodeControlMessage(FrameView frame, MessageDirection expected_direction);
}
```

`FrameParseResult` reports `NeedMoreData`, `Decoded`, or `Rejected`, exact consumed/required byte count, an owned frame only on success, and a classified `ProtocolErrorCode`. It never retains caller storage.

- [x] **Step 1: Register the failing codec target**

Add `ipc.frame_codec` before creating the public headers. Tests use exact golden bytes for offsets 0/4/6/8/10/12/16, all eight message values, zero/max payload, partial header, partial payload, two concatenated frames, and unaligned input.

```cpp
constexpr std::array<std::byte, 24> expected_header{
    B('N'), B('F'), B('M'), B('1'), B(24), B(0), B(1), B(0),
    B(1), B(0), B(0), B(0), B(8), B(0), B(0), B(0),
    B(1), B(0), B(0), B(0), B(0), B(0), B(0), B(0),
};
```

- [x] **Step 2: Verify the intended red failure**

Format the test and run Fast. Expected: C1083 for `network_flow_monitor/ipc/frame_codec.h` and no unrelated failure.

- [x] **Step 3: Implement explicit endian helpers and frame validation**

Use checked `ReadU16/U32/U64` and `AppendU16/U32/U64` helpers. Validate magic, header size, nonzero protocol version, known type, zero flags, payload maximum, `size_t` addition, and request-ID rules without native-struct casts.

```cpp
if (payload_length > kMaximumFramePayload ||
    payload_length > input.size() - kFrameHeaderSize) {
    return NeedOrReject(payload_length, input.size());
}
```

- [x] **Step 4: Implement control payloads**

Implement exact ClientHello, ServerHello/GUID field encoding, Subscribe, SubscriptionAccepted, and 12-byte ProtocolError codecs. Enforce min/max version order, 100–5000ms refresh, reserved zero, request direction, nonzero request IDs for requests, and request-ID echo for replies.

- [x] **Step 5: Verify Task 2**

Run Format, Fast, and Full. Require Debug/Release CTest 16/16. Record dependencies and results. Proposed checkpoint: `feat: add bounded ipc frame codec`; leave uncommitted without authorization.

---

## Task 3: Catalog, health, and snapshot payload codecs

**Files:**

- Create: `include/network_flow_monitor/ipc/snapshot_codec.h`
- Create: `src/ipc/snapshot_codec.cpp`
- Create: `tests/ipc_snapshot_codec_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `CollectorHealth`, the Task 1 capacity counters, protocol types, and `process::ApplicationMetadata`.
- Produces exact neutral DTOs:

```cpp
struct ApplicationHandle { std::uint64_t value{}; };
struct CatalogEntry {
    ApplicationHandle handle;
    std::uint32_t metadata_revision{};
    bool removed{};
    process::ApplicationMetadata metadata;
};
struct CatalogChunk {
    std::uint64_t sequence{};
    std::uint32_t chunk_index{};
    std::uint32_t chunk_count{};
    bool full_baseline{};
    std::vector<CatalogEntry> entries;
};
struct ApplicationCounterWire {
    ApplicationHandle handle;
    std::uint64_t upload_bytes{};
    std::uint64_t download_bytes{};
};
enum class IpcServerState : std::uint8_t {
    Stopped, Starting, Running, Degraded, Failed,
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
```

- [x] **Step 1: Add failing golden and invariant tests**

Cover every `CollectorHealthWire` and `IpcServerHealthWire` field with distinct values, enum/bool/reserved rejection, catalog full/delta chunks, 131,072-byte strings, removed-entry zero lengths, UTF-8 failure, duplicate handles, chunk inconsistency, snapshot sequence/catalog/uptime rules, 4096 applications, counter decrease, and exact 1 MiB boundaries.

- [x] **Step 2: Run the red phase**

Format and run Fast. Expected: missing `snapshot_codec.h` C1083 only.

- [x] **Step 3: Implement health codecs in specification order**

Write every field explicitly. Decode to a candidate value, validate every enum/bool/reserved byte, and publish only on complete success. Add compile-time expected wire-size constants derived from explicit field widths, not `sizeof(native_type)`.

- [x] **Step 4: Implement catalog and snapshot codecs**

Chunk catalog entries without splitting one entry across frames. Reject a single entry that cannot fit the 1 MiB frame after the fixed prefix. Decode UTF-8 through one bounded validator. Snapshot application records remain fixed 24 bytes and are sorted/unique by handle before encoding.

```cpp
if (!seen_handles.emplace(record.handle.value).second) {
    return {.error = SnapshotCodecError::DuplicateApplicationHandle};
}
```

- [x] **Step 5: Verify Task 3**

Run Format, Fast, and Full. Require Debug/Release CTest 17/17 and record exact codec sizes. Proposed checkpoint: `feat: encode ipc catalog and snapshots`; leave uncommitted without authorization.

---

## Task 4: Instance-local application catalog state

**Files:**

- Create: `include/network_flow_monitor/ipc/application_catalog.h`
- Create: `src/ipc/application_catalog.cpp`
- Create: `tests/ipc_application_catalog_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: active `ranking::CounterSnapshot` values from Task 1 and catalog/snapshot DTOs from Task 3.
- Produces:

```cpp
struct PublishedApplicationSet {
    std::uint64_t catalog_sequence{};
    std::vector<CatalogEntry> changes;
    std::vector<CatalogEntry> full_catalog;
    std::vector<ApplicationCounterWire> counters;
};

enum class ApplicationCatalogError : std::uint8_t {
    None, EmptyApplicationId, DuplicateApplicationId,
    UnsortedApplicationId, HandleExhausted, RevisionExhausted,
    SequenceExhausted, TooManyApplications, CounterDecreased,
};
struct ApplicationCatalogResult {
    ApplicationCatalogError error{ApplicationCatalogError::None};
    PublishedApplicationSet published;
    [[nodiscard]] bool accepted() const noexcept;
};

struct ApplicationCatalogSeed {
    std::uint64_t first_handle{1U};
    std::uint32_t initial_revision{1U};
};

class ApplicationCatalog {
public:
    explicit ApplicationCatalog(ApplicationCatalogSeed seed = {});
    [[nodiscard]] ApplicationCatalogResult Publish(
        const ranking::CounterSnapshot& snapshot);
};
```

The named seed fields are exhaustion test seams; production uses both defaults. Same-handle metadata enrichment increments revision. Removed applications create removal entries and delete the ID mapping; handles never reuse.

- [x] **Step 1: Register failing catalog tests**

Cover deterministic handle assignment from sorted Application IDs, same-ID stability, same metadata no revision, enrichment revision, removal, remove/reappear new handle, full baseline, empty/duplicate IDs, 4096 maximum, counter decrease, handle exhaustion, revision exhaustion, and strong rollback on every failure.

- [x] **Step 2: Run the intended red Fast gate**

Expected: C1083 for `application_catalog.h` only.

- [x] **Step 3: Implement candidate-state publication**

Build the next maps/vectors in candidate state and swap only after all validation and allocations succeed. Preserve deterministic handle order by consuming the already sorted snapshot; still validate sorted uniqueness rather than trusting callers.

```cpp
auto candidate = state_;
// validate, allocate handles, build removals and counters
state_ = std::move(candidate);
```

- [x] **Step 4: Verify Task 4**

Run Format, Fast, and Full. Require Debug/Release CTest 18/18. Proposed checkpoint: `feat: track ipc application catalog`; leave uncommitted without authorization.

---

## Task 5: Explicit owner-scoped pipe security

**Files:**

- Create: `include/network_flow_monitor/ipc/security_policy.h`
- Create: `include/network_flow_monitor/ipc/windows_pipe_security.h`
- Create: `src/ipc/security_policy.cpp`
- Create: `src/ipc/windows_pipe_security.cpp`
- Create: `tests/ipc_security_policy_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: canonical token user/logon SID bytes supplied by the future host.
- Produces:

```cpp
enum class CollectorEndpointMode : std::uint8_t { Portable, Service };
enum class PipeSecurityError : std::uint8_t {
    None, MissingSid, InvalidSid, InvalidMode, NameTooLong,
    DescriptorBuildFailed, ImpersonationFailed, TokenQueryFailed,
    Unauthorized, RevertFailed,
};

struct AuthorizedPrincipal {
    CollectorEndpointMode mode{};
    std::vector<std::byte> authorized_sid;
    std::vector<std::byte> server_logon_sid; // required only for Portable
};
struct PipeNameResult {
    PipeSecurityError error{PipeSecurityError::None};
    std::wstring name;
};
struct QueriedClientIdentity {
    std::vector<std::byte> user_sid;
    std::vector<std::vector<std::byte>> group_sids;
};

class PipeSecurityApi {
  public:
    virtual ~PipeSecurityApi() = default;
    virtual bool Impersonate(HANDLE pipe, std::uint32_t& error) noexcept = 0;
    virtual bool QueryThreadIdentity(QueriedClientIdentity& identity,
                                     std::uint32_t& error) noexcept = 0;
    virtual bool Revert(std::uint32_t& error) noexcept = 0;
};

struct PipeSecurityDescriptor {
    PipeSecurityDescriptor(PipeSecurityDescriptor&&) noexcept;
    ~PipeSecurityDescriptor();
    [[nodiscard]] SECURITY_ATTRIBUTES* attributes() noexcept;
};
struct PipeSecurityResult {
    PipeSecurityError error{PipeSecurityError::None};
    std::unique_ptr<PipeSecurityDescriptor> descriptor;
};
struct ClientIdentityResult {
    PipeSecurityError error{PipeSecurityError::None};
    std::uint32_t win32_error{};
    [[nodiscard]] bool authorized() const noexcept;
};

PipeNameResult BuildPipeName(const AuthorizedPrincipal& principal);
PipeSecurityResult BuildPipeSecurity(const AuthorizedPrincipal& principal);
ClientIdentityResult VerifyPipeClient(HANDLE server_pipe,
                                      const AuthorizedPrincipal& principal,
                                      PipeSecurityApi& api) noexcept;
```

The public neutral policy header contains no Windows types; `windows_pipe_security.h` is the explicit adapter boundary. SID buffers are validated with `IsValidSid`, copied into owned storage, and never logged.

- [ ] **Step 1: Register failing policy/security tests**

Pure tests cover mode/SID/name validation, canonical names, 256-character limit and no raw arbitrary name input. Injected Win32 tests cover impersonation failure, token-open/query failure, wrong user/logon SID, exact match, and guaranteed single `RevertToSelf` on every successful impersonation path.

Inspect the built ACL and assert:

```cpp
context.Check(!HasAllowAce(WinWorldSid) && !HasAllowAce(WinAnonymousSid) &&
                  !HasAllowAce(WinAuthenticatedUserSid) &&
                  !HasAllowAce(WinBuiltinAdministratorsSid),
              "broad principals receive no allow ACE");
```

- [ ] **Step 2: Run the missing-header red phase**

Format and run Fast; require only the expected C1083.

- [ ] **Step 3: Build the descriptor with owned ACL/SID memory**

Use explicit ACEs for LocalSystem full server access, Portable server logon SID server access, and authorized SID data/synchronize access. Service mode omits the extra server SID. Do not use a default descriptor or SDDL assembled from unvalidated text.

- [ ] **Step 4: Implement fail-closed impersonation verification**

Use an RAII `ImpersonationGuard` created only after `ImpersonateNamedPipeClient` succeeds. Query only TokenUser/token groups and compare binary SIDs with `EqualSid`. Any failure returns Unauthorized and performs no privileged client-directed operation.

- [ ] **Step 5: Verify Task 5**

Run Format, Fast, and Full. Require Debug/Release CTest 19/19. Inspect `dumpbin /dependents` for Advapi32 reachability separately from the still-unlinked product. Proposed checkpoint: `feat: secure ipc pipe ownership`; leave uncommitted.

---

## Task 6: Cancellable overlapped named-pipe transport

**Files:**

- Create: `include/network_flow_monitor/ipc/named_pipe_transport.h`
- Create: `src/ipc/named_pipe_transport.cpp`
- Create: `tests/ipc_named_pipe_transport_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: pipe name/security from Task 5 and frames from Task 2.
- Produces a non-copyable Windows transport boundary:

```cpp
enum class PipeOperationKind : std::uint8_t { Connect, Read, Write };
enum class PipeIoState : std::uint8_t { Idle, Pending, Completed, CancelRequested };
enum class PipeIoError : std::uint8_t {
    None, Busy, Disconnected, Timeout, InvalidState,
    ZeroProgress, UnexpectedMessageMode, WindowsFailure,
};
struct PipeIoResult {
    PipeIoError error{PipeIoError::None};
    std::uint32_t win32_error{};
    std::size_t transferred{};
    bool pending{};
    [[nodiscard]] bool ok() const noexcept;
};

class OverlappedPipeOperation {
  public:
    [[nodiscard]] PipeIoResult BeginConnect();
    [[nodiscard]] PipeIoResult BeginRead(std::span<std::byte> destination);
    [[nodiscard]] PipeIoResult BeginWrite(std::span<const std::byte> source);
    [[nodiscard]] PipeIoResult Complete();
    [[nodiscard]] PipeIoResult RequestCancel() noexcept;
    [[nodiscard]] HANDLE completion_event() const noexcept;
};

class NamedPipeConnection {
  public:
    [[nodiscard]] PipeIoResult BeginReadExact(std::span<std::byte> destination);
    [[nodiscard]] PipeIoResult ContinueReadExact();
    [[nodiscard]] PipeIoResult BeginWriteAll(std::span<const std::byte> source);
    [[nodiscard]] PipeIoResult ContinueWriteAll();
    [[nodiscard]] PipeIoResult CancelAndDrain() noexcept;
};

struct PipeConnectionResult {
    PipeIoError error{PipeIoError::None};
    std::uint32_t win32_error{};
    std::unique_ptr<NamedPipeConnection> connection;
    [[nodiscard]] bool ok() const noexcept;
};

class PipeTransportFactory {
  public:
    virtual ~PipeTransportFactory() = default;
    [[nodiscard]] virtual PipeConnectionResult CreateServer(
        const AuthorizedPrincipal& principal,
        bool acquire_endpoint_ownership) noexcept = 0;
};
```

Buffers referenced by pending operations remain owned by `NamedPipeConnection` until completion. No caller may free/reuse an `OVERLAPPED` after only requesting cancellation.

- [ ] **Step 1: Register injected-API lifecycle tests**

Cover immediate completion, `ERROR_IO_PENDING`, partial read/write, `ERROR_MORE_DATA` rejection in byte mode, broken pipe, already-connected race, cancel success, `ERROR_NOT_FOUND`, cancel-vs-normal-completion race, completion error, repeated shutdown, destructor cleanup and no handle inheritance.

Assert CreateNamedPipe flags include duplex/overlapped/first-instance on the first call, byte/read-byte/wait/reject-remote, max instances 4, and the exact explicit security attributes pointer.

- [ ] **Step 2: Run the red phase**

Format and run Fast for the expected missing transport header.

- [ ] **Step 3: Implement operation state machines and RAII**

Use separate manual-reset events and `OVERLAPPED` objects for connect/read/write. Treat immediate success and pending completion distinctly. `RequestCancel` changes state but never finalizes it; `Complete` is the only transition back to Idle.

- [ ] **Step 4: Implement server/client open and exact transfer**

Server creation always receives Task 5 security attributes. Client opens with specific data/synchronize rights and `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`; use `WaitNamedPipeW` only for classified busy retry. Exact read/write loops reject zero-progress success.

- [ ] **Step 5: Verify Task 6**

Run Format, Fast, and Full. Require Debug/Release CTest 20/20. Proposed checkpoint: `feat: add cancellable named pipe transport`; leave uncommitted.

---

## Task 7: Server handshake and bounded client sessions

**Files:**

- Create: `include/network_flow_monitor/ipc/ipc_server.h`
- Create: `src/ipc/ipc_server.cpp`
- Create: `tests/ipc_server_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 2/3 codecs, Task 5 client verifier, Task 6 connection, and immutable published application sets.
- Produces:

```cpp
inline constexpr std::size_t kMaximumIpcClients = 4U;
inline constexpr auto kHandshakeTimeout = std::chrono::seconds{3};
inline constexpr auto kWriteTimeout = std::chrono::seconds{5};

struct ClientSubscription {
    std::uint64_t client_id{};
    std::chrono::milliseconds refresh_interval;
};

struct PublishedSnapshot {
    SnapshotMessage message;
    std::vector<CatalogEntry> latest_changes;
    std::vector<CatalogEntry> full_catalog;
};
enum class IpcServerError : std::uint8_t {
    None, AlreadyRunning, EndpointAlreadyOwned, ServerBusy,
    InvalidSnapshot, TransportFailure, CancellationFailure, InternalFailure,
};
struct IpcServerResult {
    IpcServerError error{IpcServerError::None};
    std::uint32_t win32_error{};
    [[nodiscard]] bool ok() const noexcept;
};

class ClientSessionObserver {
  public:
    virtual ~ClientSessionObserver() = default;
    virtual void SubscriptionChanged(ClientSubscription subscription) noexcept = 0;
    virtual void ClientDisconnected(std::uint64_t client_id) noexcept = 0;
};

class IpcServer {
  public:
    IpcServer(CollectorInstanceId instance, AuthorizedPrincipal principal,
              PipeTransportFactory& transport_factory);
    [[nodiscard]] IpcServerResult SetSubscriptionObserver(
        ClientSessionObserver& observer) noexcept; // only while stopped
    [[nodiscard]] IpcServerResult Start();
    [[nodiscard]] IpcServerResult Publish(std::shared_ptr<const PublishedSnapshot> snapshot);
    [[nodiscard]] IpcServerResult Stop() noexcept;
    [[nodiscard]] IpcServerHealth health() const noexcept;
};
```

Session/transport scheduling uses an injected wait/API seam in pure tests. The server owns at most one pending snapshot reference per client and never calls ETW or RankingEngine.
Construct the server first, construct `SnapshotPublisher` with that stopped server, call
`SetSubscriptionObserver(publisher)`, then call `Start()`. `SetSubscriptionObserver` rejects
replacement while running, so the observer always outlives every session callback. The first
successful `CreateServer` call acquires endpoint ownership; later instances pass `false`.

- [ ] **Step 1: Register failing session-state tests**

Cover Hello-first ordering, 3s timeout, version mismatch, unauthorized verification, invalid/second Hello, valid subscription update, request-ID echo, client-only message direction, four-client limit, busy classification, protocol-error-close, status push, and repeated Stop.

Slow-client cases require: pending unsent snapshot replacement, no replacement of a started frame, full catalog baseline on new/reconnected session, delta when exactly current, full baseline after sequence gap, catalog-before-snapshot, 5s write cancel/drain, and another client continuing.

- [ ] **Step 2: Run the expected red gate**

Format and run Fast; require missing `ipc_server.h` only.

- [ ] **Step 3: Implement candidate handshake/session state**

Decode into candidates; authenticate after a complete bounded Hello and before accepting its requested subscription. Send at most one classified error response where framing permits, then cancel/drain and disconnect. Catch all worker exceptions and store a preallocated internal-failure result.

- [ ] **Step 4: Implement catalog-aware snapshot mailbox**

For a latest published set, compare the session catalog sequence. Send a full baseline if empty or non-contiguous; otherwise send the exact delta. Chunk catalog first, then snapshot. Only mark a sequence known after its final chunk completes.

- [ ] **Step 5: Verify Task 7**

Run Format, Fast, and Full. Require Debug/Release CTest 21/21. Proposed checkpoint: `feat: serve bounded ipc sessions`; leave uncommitted.

---

## Task 8: Central subscription-aware snapshot publisher

**Files:**

- Create: `include/network_flow_monitor/ipc/snapshot_publisher.h`
- Create: `src/ipc/snapshot_publisher.cpp`
- Create: `tests/ipc_snapshot_publisher_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `ApplicationCatalog`, `IpcServer::Publish`, and a future-host capture seam.
- Produces:

```cpp
struct CollectorCapture {
    collector::CollectorHealth health;
    ranking::CounterSnapshot counters;
};
enum class CaptureError : std::uint8_t {
    None, FlushFailed, SnapshotFailed, InvalidTimestamp, CollectorFailed,
};
struct CaptureResult {
    CaptureError error{CaptureError::None};
    CollectorCapture capture;
    std::uint32_t source_error{};
};
enum class PublisherError : std::uint8_t {
    None, InvalidSubscription, TimeMovedBackward, CaptureFailed,
    CatalogFailed, ServerFailed, SequenceExhausted, UptimeOverflow,
};
struct PublisherResult {
    PublisherError error{PublisherError::None};
    [[nodiscard]] bool ok() const noexcept;
};

class SnapshotProvider {
  public:
    virtual ~SnapshotProvider() = default;
    [[nodiscard]] virtual CaptureResult Capture() noexcept = 0;
};

class SnapshotPublisher final : public ClientSessionObserver {
  public:
    SnapshotPublisher(CollectorInstanceId instance,
                      std::chrono::steady_clock::time_point instance_started_at,
                      SnapshotProvider& provider, IpcServer& server);
    void SubscriptionChanged(ClientSubscription subscription) noexcept override;
    void ClientDisconnected(std::uint64_t client_id) noexcept override;
    [[nodiscard]] PublisherResult RunDue(std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline() const noexcept;
};
```

Production `Capture()` wiring to `WindowsEtwSession::FlushAndQuery`/`MakeSnapshot` is deferred to the dual-mode host milestone; this task uses a fake and preserves the module boundary.

- [ ] **Step 1: Register failing scheduler tests**

Cover no-client no-capture, immediate first capture, min of 100/500/5000ms subscriptions, one provider call for multiple due clients, per-client due filtering, update/remove, actual-sample-time deadlines without drift, backward time rejection, provider failure status, snapshot/catalog sequence, strictly increasing uptime/sequence and saturation of publisher diagnostics.

- [ ] **Step 2: Run the red phase**

Format and run Fast for missing `snapshot_publisher.h`.

- [ ] **Step 3: Implement deterministic deadline state**

Store each client's next deadline. The first valid subscription is due immediately. After a capture at `now`, advance each due client's deadline to `now + interval`, not `old_deadline + interval`. A failed capture publishes status if available and applies a bounded retry deadline without fabricating counters.

- [ ] **Step 4: Integrate ApplicationCatalog and server publication**

On successful capture, validate strict `captured_at`, publish through `ApplicationCatalog`, assign instance-local snapshot sequence/uptime, and pass one immutable `PublishedSnapshot` to the server. Strong rollback preserves prior sequence/catalog on any failure.

- [ ] **Step 5: Verify Task 8**

Run Format, Fast, and Full. Require Debug/Release CTest 22/22. Proposed checkpoint: `feat: coalesce ipc snapshot subscriptions`; leave uncommitted.

---

## Task 9: Reconnecting IPC client and Ranking adapter

**Files:**

- Create: `include/network_flow_monitor/ipc/reconnect_policy.h`
- Create: `include/network_flow_monitor/ipc/ipc_client.h`
- Create: `src/ipc/reconnect_policy.cpp`
- Create: `src/ipc/ipc_client.cpp`
- Create: `tests/ipc_client_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 2/3 codecs and Task 6 client transport.
- Produces:

```cpp
enum class IpcClientState : std::uint8_t {
    Disconnected, Connecting, Handshaking, Streaming,
    Backoff, Unauthorized, Incompatible, Failed,
};

inline constexpr std::array<std::chrono::milliseconds, 5> kReconnectDelays{
    250ms, 500ms, 1'000ms, 2'000ms, 5'000ms,
};

struct ClientSnapshot {
    CollectorInstanceId instance;
    collector::CollectorHealth collector_health;
    IpcServerHealth ipc_health;
    ranking::CounterSnapshot counters;
};

class IpcClientObserver {
  public:
    virtual void StateChanged(IpcClientState state) noexcept = 0;
    virtual void SnapshotReceived(ClientSnapshot snapshot) noexcept = 0;
};
```

The adapter constructs `ipc:<guid>:<handle>` IDs and converts validated uptime microseconds to an arbitrary-epoch `steady_clock::time_point`. It clears catalog and caller-visible ranking history on instance change.

- [ ] **Step 1: Register failing client/reconnect tests**

Cover exact delay sequence/cap, deterministic injected jitter, success reset, endpoint missing/busy/broken-pipe recovery, Unauthorized/Incompatible terminal states, Hello/reply IDs, catalog full/delta/removal, snapshot-before-catalog rejection, strict sequence/uptime/counters, instance reset, same-instance reconnect baseline, UI non-real-time state on every Streaming exit, and no stale snapshot callback.

- [ ] **Step 2: Run the red phase**

Format and run Fast for missing client headers.

- [ ] **Step 3: Implement reconnect and protocol receive state**

Keep the reconnect policy pure with injected jitter. Client worker catches exceptions at its thread boundary, cancels/drains transport, and posts only value callbacks. Protocol invariant failures enter Failed without retry; version and authorization use their dedicated terminal states.

- [ ] **Step 4: Implement catalog-to-ranking adaptation**

Apply a complete catalog sequence atomically. Reject unknown handles, duplicate records, counter decrease or non-increasing snapshot/uptime. On instance change emit state transition before the first new snapshot so the future UI can clear its RankingEngine deterministically.

- [ ] **Step 5: Verify Task 9**

Run Format, Fast, and Full. Require Debug/Release CTest 23/23. Proposed checkpoint: `feat: reconnect ipc clients`; leave uncommitted.

---

## Task 10: Real local-pipe integration, audit, and Milestone 4 closure

**Files:**

- Create: `tests/ipc_integration_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/configuration.md`
- Modify: `docs/planning/findings.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`
- Modify: `docs/superpowers/plans/2026-08-11-secure-ipc.md`

**Interfaces:**

- Consumes: all Task 1–9 public interfaces and a deterministic fake SnapshotProvider.
- Produces: a non-skipped `ipc.integration` CTest and complete specification-to-evidence mapping.

- [ ] **Step 1: Add the serialized Windows integration target**

Create a unique owner-scoped endpoint for the current logon and use the real descriptor/transport/server/client. Test two simultaneous clients at different intervals, catalog/snapshot delivery, subscription update, one client that stops reading, independent healthy delivery, disconnect/reconnect and no remaining named pipe/thread after Stop.

- [ ] **Step 2: Verify actual security and hostile inputs**

Inspect the live descriptor with `GetSecurityInfo`, compare exact ACE SIDs/masks, impersonate anonymous for a connection-denied check, evaluate an unrelated SID token with `AccessCheck`, verify first-instance conflict, confirm remote-reject mode, and send real truncated/oversized/unknown-version frames that terminate only their connection.

If the host cannot manufacture a genuinely different Windows user/session, record that limitation; do not replace descriptor/token evidence with a false multi-user claim. Cross-user installed-service validation remains Milestone 5.

- [ ] **Step 3: Verify cancellation and slow-client cleanup**

Force pending connect/read/write operations, call Stop in each state, and require completion before handle/thread destruction. Confirm the 5-second slow-client path uses cancel-and-drain while another client receives a later snapshot.

- [ ] **Step 4: Run the milestone quality and hygiene gates**

Run Format, Fast, and Full. Require Debug/Release CTest 24/24 with `ipc.integration` non-skipped. Then run `git diff --check`, navigational Markdown validation, ignore probes, `dumpbin /dependents`, exact Release EXE/test sizes, and `git status --short`.

- [ ] **Step 5: Audit and document every specification requirement**

Map security, framing, catalog, snapshot, active-capacity, publisher, slow-client, cancellation, reconnect, privacy and Windows integration requirements to named tests or explicit invariants. Correct every uncovered gap before closing.

- [ ] **Step 6: Close Milestone 4**

Mark secure IPC complete and exactly one next item in progress: dual portable/service host design. Preserve exact 24H2 validation as Phase 5 debt. Proposed checkpoint: `feat: complete secure collector ipc`; leave the verified worktree uncommitted unless the user explicitly authorizes a Git commit.
