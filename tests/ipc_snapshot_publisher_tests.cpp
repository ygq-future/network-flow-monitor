#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono_literals;
using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::CaptureError;
using nfm::ipc::CaptureResult;
using nfm::ipc::CollectorCapture;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::CollectorInstanceId;
using nfm::ipc::IpcServer;
using nfm::ipc::PublisherError;
using nfm::ipc::SnapshotProvider;
using nfm::ipc::SnapshotPublisher;
using nfm::ipc::WindowsPipeTransportFactory;
using nfm::ranking::ApplicationCounters;
using nfm::ranking::CounterSnapshot;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

std::vector<std::byte> CurrentUserSid() {
    HANDLE token{};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return {};
    }
    DWORD required{};
    static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0U, &required));
    if (required == 0U) {
        static_cast<void>(CloseHandle(token));
        return {};
    }
    std::vector<std::byte> storage(required);
    if (GetTokenInformation(token, TokenUser, storage.data(), required, &required) == FALSE) {
        static_cast<void>(CloseHandle(token));
        return {};
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    const DWORD length = GetLengthSid(user->User.Sid);
    std::vector<std::byte> sid(length);
    if (CopySid(length, sid.data(), user->User.Sid) == FALSE) {
        sid.clear();
    }
    static_cast<void>(CloseHandle(token));
    return sid;
}

AuthorizedPrincipal Principal() {
    return {.mode = CollectorEndpointMode::Service,
            .authorized_sid = CurrentUserSid(),
            .server_logon_sid = {}};
}

CollectorInstanceId Instance(std::uint8_t seed = 1U) {
    CollectorInstanceId result;
    for (std::size_t index = 0U; index < result.bytes.size(); ++index) {
        result.bytes.at(index) = static_cast<std::byte>(seed + index);
    }
    return result;
}

class FakeProvider final : public SnapshotProvider {
  public:
    CaptureResult Capture() noexcept override {
        ++calls;
        if (failure != CaptureError::None) {
            return {.error = failure, .capture = CollectorCapture{.health = {}, .counters = {}}};
        }
        CounterSnapshot snapshot;
        snapshot.captured_at = capture_time;
        snapshot.applications.push_back(
            ApplicationCounters{.id = "app.test",
                                .metadata = {.display_name = "Test", .icon_key = {}},
                                .upload_bytes = static_cast<std::uint64_t>(calls),
                                .download_bytes = static_cast<std::uint64_t>(calls)});
        return {.capture = CollectorCapture{.health = {}, .counters = std::move(snapshot)}};
    }

    int calls{};
    CaptureError failure{CaptureError::None};
    std::chrono::steady_clock::time_point capture_time = std::chrono::steady_clock::now();
};

void CheckScheduler(TestContext& context) {
    WindowsPipeTransportFactory factory;
    IpcServer server{Instance(), Principal(), factory};
    context.Check(server.Start().ok(), "publisher test server starts");

    const auto started = std::chrono::steady_clock::now() - 10s;
    FakeProvider provider;
    SnapshotPublisher publisher{Instance(), started, provider, server};
    const auto first = started + 10s;

    context.Check(publisher.RunDue(first).ok() && provider.calls == 0,
                  "no subscriptions do not capture");
    publisher.SubscriptionChanged({.client_id = 1U, .refresh_interval = 100ms});
    publisher.SubscriptionChanged({.client_id = 2U, .refresh_interval = 500ms});
    context.Check(publisher.RunDue(first).ok() && provider.calls == 1,
                  "multiple due clients share one capture");
    context.Check(publisher.next_deadline().has_value() &&
                      *publisher.next_deadline() == first + 100ms,
                  "next deadline uses the shortest interval");
    context.Check(publisher.RunDue(first + 50ms).ok() && provider.calls == 1,
                  "future clients do not trigger an early capture");
    provider.capture_time += 100ms;
    context.Check(publisher.RunDue(first + 100ms).ok() && provider.calls == 2,
                  "the shortest subscription is independently due");
    publisher.ClientDisconnected(1U);
    context.Check(publisher.next_deadline().has_value() &&
                      *publisher.next_deadline() == first + 500ms,
                  "disconnect removes the client deadline");
    context.Check(server.Stop().ok(), "publisher test server stops");
}

void CheckFailures(TestContext& context) {
    WindowsPipeTransportFactory factory;
    IpcServer server{Instance(2U), Principal(), factory};
    context.Check(server.Start().ok(), "failure test server starts");
    const auto started = std::chrono::steady_clock::now() - 10s;
    FakeProvider provider;
    SnapshotPublisher publisher{Instance(2U), started, provider, server};
    publisher.SubscriptionChanged({.client_id = 1U, .refresh_interval = 100ms});
    provider.failure = CaptureError::CollectorFailed;
    const auto now = started + 10s;
    context.Check(publisher.RunDue(now).error == PublisherError::CaptureFailed,
                  "provider failure is surfaced without a fabricated snapshot");
    context.Check(publisher.RunDue(now + 1ms).ok() && provider.calls == 1,
                  "failed capture receives a bounded retry deadline");
    publisher.SubscriptionChanged({.client_id = 0U, .refresh_interval = 100ms});
    context.Check(publisher.RunDue(now + 200ms).error == PublisherError::InvalidSubscription,
                  "invalid subscription is reported at the scheduler boundary");
    context.Check(server.Stop().ok(), "failure test server stops");
}

} // namespace

int main() {
    TestContext context;
    CheckScheduler(context);
    CheckFailures(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " publisher test failure(s)\n";
        return 1;
    }
    std::cout << "ipc snapshot publisher tests passed\n";
    return 0;
}
