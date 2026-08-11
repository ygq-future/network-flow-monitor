#include "network_flow_monitor/ipc/frame_codec.h"
#include "network_flow_monitor/ipc/ipc_client.h"
#include "network_flow_monitor/ipc/ipc_server.h"
#include "network_flow_monitor/ipc/named_pipe_transport.h"
#include "network_flow_monitor/ipc/security_policy.h"

#include <windows.h>

#include <aclapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <sddl.h>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using nfm::ipc::ApplicationCounterWire;
using nfm::ipc::ApplicationHandle;
using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::CatalogEntry;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::CollectorInstanceId;
using nfm::ipc::EncodeClientHello;
using nfm::ipc::FrameParseStatus;
using nfm::ipc::IpcClient;
using nfm::ipc::IpcClientObserver;
using nfm::ipc::IpcClientState;
using nfm::ipc::IpcServer;
using nfm::ipc::MessageType;
using nfm::ipc::NamedPipeConnection;
using nfm::ipc::PipeIoResult;
using nfm::ipc::PipeTransportFactory;
using nfm::ipc::PublishedSnapshot;
using nfm::ipc::SnapshotMessage;
using nfm::ipc::WindowsPipeTransportFactory;

class UniqueHandle final {
  public:
    explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_{handle} {}
    ~UniqueHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
        }
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_{std::exchange(other.handle_, nullptr)} {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_{};
};

class LocalSecurityDescriptor final {
  public:
    explicit LocalSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor = nullptr) noexcept
        : descriptor_{descriptor} {}
    ~LocalSecurityDescriptor() {
        if (descriptor_ != nullptr) {
            static_cast<void>(LocalFree(descriptor_));
        }
    }
    LocalSecurityDescriptor(const LocalSecurityDescriptor&) = delete;
    LocalSecurityDescriptor& operator=(const LocalSecurityDescriptor&) = delete;

  private:
    PSECURITY_DESCRIPTOR descriptor_{};
};

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

std::vector<std::byte> CopySid(PSID sid) {
    if (sid == nullptr || IsValidSid(sid) == FALSE) {
        return {};
    }
    const auto length = GetLengthSid(sid);
    std::vector<std::byte> result(length);
    if (::CopySid(length, result.data(), sid) == FALSE) {
        return {};
    }
    return result;
}

std::vector<std::byte> CurrentUserSid() {
    HANDLE raw_token{};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
        return {};
    }
    const UniqueHandle token{raw_token};
    DWORD required{};
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0U, &required));
    if (required == 0U) {
        return {};
    }
    std::vector<std::byte> storage(required);
    if (GetTokenInformation(token.get(), TokenUser, storage.data(), required, &required) == FALSE) {
        return {};
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    return CopySid(user->User.Sid);
}

AuthorizedPrincipal ServicePrincipal() {
    return {.mode = CollectorEndpointMode::Service,
            .authorized_sid = CurrentUserSid(),
            .server_logon_sid = {}};
}

CollectorInstanceId Instance(std::byte seed) {
    CollectorInstanceId instance;
    for (std::size_t index = 0U; index < instance.bytes.size(); ++index) {
        instance.bytes.at(index) = static_cast<std::byte>(std::to_integer<unsigned int>(seed) +
                                                          static_cast<unsigned int>(index));
    }
    return instance;
}

struct ClientObserver final : IpcClientObserver {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::array<std::size_t, 8U> state_counts{};
    std::size_t snapshots{};
    std::uint64_t last_sequence{};
    std::size_t last_application_count{};

    void StateChanged(IpcClientState state) noexcept override {
        const auto index = static_cast<std::size_t>(state);
        std::scoped_lock lock{mutex};
        if (index < state_counts.size()) {
            ++state_counts.at(index);
        }
        condition.notify_all();
    }

    void SnapshotReceived(nfm::ipc::ClientSnapshot snapshot) noexcept override {
        std::scoped_lock lock{mutex};
        ++snapshots;
        last_sequence = snapshot.counters.captured_at.time_since_epoch().count();
        last_application_count = snapshot.counters.applications.size();
        condition.notify_all();
    }

    template <typename Predicate>
    bool WaitFor(Predicate&& predicate, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex};
        return condition.wait_for(lock, timeout, std::forward<Predicate>(predicate));
    }

    [[nodiscard]] bool Streaming() const noexcept {
        return state_counts.at(static_cast<std::size_t>(IpcClientState::Streaming)) != 0U;
    }
};

struct SubscriptionObserver final : nfm::ipc::ClientSessionObserver {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::array<std::chrono::milliseconds, 4U> intervals{};
    std::size_t count{};

    void SubscriptionChanged(nfm::ipc::ClientSubscription subscription) noexcept override {
        std::scoped_lock lock{mutex};
        if (count < intervals.size()) {
            intervals.at(count++) = subscription.refresh_interval;
        }
        condition.notify_all();
    }

    void ClientDisconnected(std::uint64_t) noexcept override {
        condition.notify_all();
    }

    bool WaitForCount(std::size_t expected, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex};
        return condition.wait_for(lock, timeout, [this, expected] { return count >= expected; });
    }
};

template <typename Completion>
bool WaitOperation(HANDLE event, Completion&& completion, PipeIoResult& result) {
    if (WaitForSingleObject(event, 5000U) != WAIT_OBJECT_0) {
        return false;
    }
    result = std::forward<Completion>(completion)();
    return result.ok();
}

bool WriteWire(NamedPipeConnection& connection, std::span<const std::byte> wire) {
    auto result = connection.BeginWriteAll(wire);
    if (!result.ok() && !result.pending) {
        return false;
    }
    if (result.pending && !WaitOperation(
                              connection.write_event(),
                              [&connection] { return connection.ContinueWriteAll(); }, result)) {
        static_cast<void>(connection.CancelAndDrain());
        return false;
    }
    return result.ok();
}

bool ReadWire(NamedPipeConnection& connection, std::vector<std::byte>& wire) {
    std::array<std::byte, nfm::ipc::kFrameHeaderSize> header{};
    auto result = connection.BeginReadExact(header);
    if (!result.ok() && !result.pending) {
        return false;
    }
    if (result.pending && !WaitOperation(
                              connection.read_event(),
                              [&connection] { return connection.ContinueReadExact(); }, result)) {
        return false;
    }
    const auto parsed_header = nfm::ipc::TryDecodeFrame(header);
    if (parsed_header.status != FrameParseStatus::NeedMoreData ||
        parsed_header.required < nfm::ipc::kFrameHeaderSize ||
        parsed_header.required > nfm::ipc::kFrameHeaderSize + nfm::ipc::kMaximumFramePayload) {
        return false;
    }
    wire.assign(parsed_header.required, std::byte{});
    std::ranges::copy(header, wire.begin());
    auto payload = std::span{wire}.subspan(nfm::ipc::kFrameHeaderSize);
    if (!payload.empty()) {
        result = connection.BeginReadExact(payload);
        if (!result.ok() && !result.pending) {
            return false;
        }
        if (result.pending &&
            !WaitOperation(
                connection.read_event(), [&connection] { return connection.ContinueReadExact(); },
                result)) {
            return false;
        }
    }
    return nfm::ipc::TryDecodeFrame(wire).status == FrameParseStatus::Decoded;
}

bool Handshake(NamedPipeConnection& connection, std::uint64_t request_id) {
    const auto hello = EncodeClientHello(
        {.minimum_version = 1U, .maximum_version = 1U, .requested_refresh = 250ms}, request_id);
    std::vector<std::byte> wire;
    return hello.ok() && WriteWire(connection, hello.bytes) && ReadWire(connection, wire) &&
           nfm::ipc::TryDecodeFrame(wire).frame.has_value() && ReadWire(connection, wire) &&
           nfm::ipc::TryDecodeFrame(wire).frame.has_value();
}

std::shared_ptr<const PublishedSnapshot> Snapshot(std::uint64_t sequence,
                                                  std::uint64_t uptime_microseconds) {
    auto result = std::make_shared<PublishedSnapshot>();
    result->message = SnapshotMessage{
        .snapshot_sequence = sequence,
        .required_catalog_sequence = 1U,
        .uptime_microseconds = uptime_microseconds,
        .applications_aggregated_to_other = false,
        .collector_health = {},
        .ipc_health = {},
        .applications = {ApplicationCounterWire{.handle = ApplicationHandle{1U},
                                                .upload_bytes = sequence * 100U,
                                                .download_bytes = sequence * 200U}}};
    result->latest_changes = {
        CatalogEntry{.handle = ApplicationHandle{1U},
                     .metadata_revision = 1U,
                     .removed = false,
                     .metadata = {.display_name = "Integration", .icon_key = {}}}};
    result->full_catalog = result->latest_changes;
    return result;
}

bool WriteLittleEndian(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value,
                       std::size_t width) noexcept {
    if (offset > bytes.size() || width > bytes.size() - offset || width > sizeof(value)) {
        return false;
    }
    for (std::size_t index = 0U; index < width; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
    return true;
}

bool SameSid(const SID* left, const std::vector<std::byte>& right) noexcept {
    if (left == nullptr || right.empty()) {
        return false;
    }
    // Win32 EqualSid predates const-correct SID parameters; both buffers remain owned by callers.
    return EqualSid(const_cast<SID*>(left),
                    reinterpret_cast<PSID>(const_cast<std::byte*>(right.data()))) != FALSE;
}

bool WaitForActiveClients(IpcServer& server, std::uint8_t maximum,
                          std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (server.health().active_clients <= maximum) {
            return true;
        }
        std::this_thread::sleep_for(25ms);
    }
    return server.health().active_clients <= maximum;
}

nfm::ipc::PipeConnectionResult CreateClientWithRetry(PipeTransportFactory& transport,
                                                     const AuthorizedPrincipal& principal,
                                                     std::chrono::milliseconds timeout) {
    auto result = transport.CreateClient(principal);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!result.ok() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(25ms);
        result = transport.CreateClient(principal);
    }
    return result;
}

bool CheckLiveDacl(TestContext& context, HANDLE pipe,
                   const std::vector<std::byte>& authorized_sid) {
    PSECURITY_DESCRIPTOR raw_descriptor{};
    PACL dacl{};
    const auto status = GetSecurityInfo(pipe, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                        nullptr, &dacl, nullptr, &raw_descriptor);
    const LocalSecurityDescriptor descriptor_holder{raw_descriptor};
    context.Check(status == ERROR_SUCCESS && dacl != nullptr, "live pipe DACL is queryable");
    if (status != ERROR_SUCCESS || dacl == nullptr) {
        return false;
    }

    std::vector<std::byte> world_sid(SECURITY_MAX_SID_SIZE);
    DWORD world_size = static_cast<DWORD>(world_sid.size());
    context.Check(CreateWellKnownSid(WinWorldSid, nullptr, world_sid.data(), &world_size) != FALSE,
                  "world SID is available");
    bool authorized_found = false;
    bool authorized_mask_exact = false;
    bool broad_found = false;
    for (DWORD index = 0U; index < dacl->AceCount; ++index) {
        void* raw_ace{};
        if (GetAce(dacl, index, &raw_ace) == FALSE) {
            continue;
        }
        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            continue;
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        const auto* sid = reinterpret_cast<const SID*>(&ace->SidStart);
        broad_found = broad_found || SameSid(sid, world_sid);
        if (SameSid(sid, authorized_sid)) {
            authorized_found = true;
            authorized_mask_exact = ace->Mask == nfm::ipc::kPipeClientAccessMask;
        }
    }
    context.Check(!broad_found, "live pipe DACL has no World allow ACE");
    context.Check(authorized_found, "live pipe DACL contains the authorized SID");
    context.Check(authorized_mask_exact, "live pipe DACL grants exact client data rights");
    return !broad_found && authorized_found && authorized_mask_exact;
}

void CheckIntegration(TestContext& context) {
    const auto principal = ServicePrincipal();
    context.Check(!principal.authorized_sid.empty(), "current user SID is available");
    if (principal.authorized_sid.empty()) {
        return;
    }

    WindowsPipeTransportFactory transport;
    SubscriptionObserver subscriptions;
    IpcServer server{Instance(std::byte{0x10}), principal, transport};
    context.Check(server.SetSubscriptionObserver(subscriptions).ok(), "observer is configured");
    context.Check(server.Start().ok(), "real integration server starts");
    WindowsPipeTransportFactory competing_transport;
    const auto competing = competing_transport.CreateServer(principal, true);
    context.Check(competing.error == nfm::ipc::PipeIoError::Busy,
                  "first-instance ownership rejects a competing server");

    ClientObserver first_observer;
    ClientObserver second_observer;
    IpcClient first{principal, transport, first_observer, 100ms};
    IpcClient second{principal, transport, second_observer, 500ms};
    context.Check(first.Start() && second.Start(), "two reconnecting clients start");
    context.Check(
        first_observer.WaitFor([&first_observer] { return first_observer.Streaming(); }, 5s),
        "first client reaches streaming");
    context.Check(
        second_observer.WaitFor([&second_observer] { return second_observer.Streaming(); }, 5s),
        "second client reaches streaming");
    context.Check(subscriptions.WaitForCount(2U, 5s), "server receives both intervals");

    context.Check(server.Publish(Snapshot(1U, 1'000U)).ok(), "first snapshot publishes");
    context.Check(
        first_observer.WaitFor([&first_observer] { return first_observer.snapshots >= 1U; }, 5s),
        "first client receives first snapshot");
    context.Check(
        second_observer.WaitFor([&second_observer] { return second_observer.snapshots >= 1U; }, 5s),
        "second client receives first snapshot");
    second.Stop();
    context.Check(WaitForActiveClients(server, 1U, 2s),
                  "stopped second client is removed before hostile probes");

    auto malformed_result = CreateClientWithRetry(transport, principal, 5s);
    context.Check(malformed_result.ok(), "malformed-input connection opens");
    if (malformed_result.ok()) {
        auto& malformed = *malformed_result.connection;
        auto hello = EncodeClientHello(
            {.minimum_version = 1U, .maximum_version = 1U, .requested_refresh = 250ms}, 901U);
        if (hello.ok()) {
            hello.bytes.at(6U) = std::byte{2};
            context.Check(WriteWire(malformed, hello.bytes), "unknown-version frame is sent");
        }
        malformed_result.connection.reset();
    }

    context.Check(WaitForActiveClients(server, 2U, 5s),
                  "malformed client session is cleaned up before the next probe");
    auto oversized_result = CreateClientWithRetry(transport, principal, 5s);
    context.Check(oversized_result.ok(), "oversized-input connection opens");
    if (oversized_result.ok()) {
        std::array<std::byte, nfm::ipc::kFrameHeaderSize> header{};
        context.Check(WriteLittleEndian(header, 0U, nfm::ipc::kFrameMagic, 4U) &&
                          WriteLittleEndian(header, 4U, nfm::ipc::kFrameHeaderSize, 2U) &&
                          WriteLittleEndian(header, 6U, nfm::ipc::kCurrentProtocolVersion, 2U) &&
                          WriteLittleEndian(header, 8U,
                                            static_cast<std::uint8_t>(MessageType::ClientHello),
                                            2U) &&
                          WriteLittleEndian(header, 12U, nfm::ipc::kMaximumFramePayload + 1U, 4U) &&
                          WriteLittleEndian(header, 16U, 902U, 8U),
                      "oversized header is encoded");
        context.Check(WriteWire(*oversized_result.connection, header), "oversized frame is sent");
        oversized_result.connection.reset();
    }

    context.Check(WaitForActiveClients(server, 1U, 5s),
                  "oversized client session is cleaned up before the next probe");
    auto truncated_result = CreateClientWithRetry(transport, principal, 5s);
    context.Check(truncated_result.ok(), "truncated-input connection opens");
    if (truncated_result.ok()) {
        const std::array<std::byte, 4U> truncated{std::byte{0x4E}, std::byte{0x46}, std::byte{0x4D},
                                                  std::byte{0x31}};
        context.Check(WriteWire(*truncated_result.connection, truncated),
                      "truncated frame prefix is sent");
        static_cast<void>(truncated_result.connection->CancelAndDrain());
        truncated_result.connection.reset();
    }

    auto slow_result = CreateClientWithRetry(transport, principal, 5s);
    context.Check(slow_result.ok(), "slow-client connection opens");
    if (slow_result.ok()) {
        auto& slow = *slow_result.connection;
        context.Check(Handshake(slow, 900U), "slow client completes handshake");
        context.Check(CheckLiveDacl(context, slow.native_handle(), principal.authorized_sid),
                      "live security descriptor is owner-scoped");
    }

    context.Check(server.Publish(Snapshot(2U, 2'000U)).ok(), "second snapshot publishes");
    context.Check(
        first_observer.WaitFor([&first_observer] { return first_observer.snapshots >= 2U; }, 5s),
        "healthy client continues while slow client does not read");
    if (slow_result.connection != nullptr) {
        static_cast<void>(slow_result.connection->CancelAndDrain());
        slow_result.connection.reset();
    }

    const auto backoff_index = static_cast<std::size_t>(IpcClientState::Backoff);
    context.Check(server.Stop().ok(), "server stops for reconnect test");
    context.Check(first_observer.WaitFor(
                      [&first_observer, backoff_index] {
                          return first_observer.state_counts.at(backoff_index) >= 1U;
                      },
                      5s),
                  "client observes server disconnect and enters backoff");
    context.Check(server.Start().ok(), "server restarts for reconnect test");
    const auto streaming_index = static_cast<std::size_t>(IpcClientState::Streaming);
    context.Check(first_observer.WaitFor(
                      [&first_observer, streaming_index] {
                          return first_observer.state_counts.at(streaming_index) >= 2U;
                      },
                      5s),
                  "client reconnects and completes a fresh handshake");
    context.Check(server.Publish(Snapshot(1U, 3'000U)).ok(),
                  "reconnected client receives a fresh baseline snapshot");
    context.Check(
        first_observer.WaitFor([&first_observer] { return first_observer.snapshots >= 3U; }, 5s),
        "hostile inputs and reconnect do not interrupt healthy delivery");

    first.Stop();
    second.Stop();
    context.Check(server.Stop().ok() && server.Stop().ok(), "server stop is idempotent");
}

} // namespace

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    TestContext context;
    CheckIntegration(context);
    if (context.failures == 0) {
        std::cout << "ipc integration tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
