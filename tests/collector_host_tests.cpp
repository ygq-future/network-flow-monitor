#include "network_flow_monitor/collector/collector_host.h"
#include "network_flow_monitor/collector/etw_snapshot_provider.h"

#include <windows.h>

#include "support/current_logon_sid.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono_literals;
using nfm::collector::CollectorHost;
using nfm::collector::CollectorHostError;
using nfm::collector::CollectorHostState;
using nfm::collector::EtwApi;
using nfm::collector::EtwSnapshotProvider;
using nfm::collector::HostWait;
using nfm::collector::WindowsEtwSession;
using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::HelloMessage;
using nfm::ipc::IpcServer;
using nfm::ipc::SnapshotPublisher;
using nfm::ipc::WindowsPipeTransportFactory;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

class FakeEtwApi final : public EtwApi {
  public:
    ULONG StartController(PTRACEHANDLE handle, LPCWSTR /*name*/,
                          PEVENT_TRACE_PROPERTIES /*properties*/) noexcept override {
        ++start_calls;
        if (start_status == ERROR_SUCCESS) {
            *handle = 11U;
        }
        return start_status;
    }

    TRACEHANDLE OpenConsumer(PEVENT_TRACE_LOGFILEW /*logfile*/) noexcept override {
        ++open_calls;
        if (open_status == INVALID_PROCESSTRACE_HANDLE) {
            SetLastError(open_error);
        }
        return open_status;
    }

    ULONG ProcessConsumer(PTRACEHANDLE /*handles*/) noexcept override {
        ++process_calls;
        return process_status;
    }

    ULONG ControlController(TRACEHANDLE /*handle*/, LPCWSTR /*name*/,
                            PEVENT_TRACE_PROPERTIES /*properties*/,
                            ULONG control_code) noexcept override {
        if (control_code == EVENT_TRACE_CONTROL_STOP) {
            ++stop_calls;
            return stop_status;
        }
        if (control_code == EVENT_TRACE_CONTROL_FLUSH) {
            ++flush_calls;
            return flush_status;
        }
        if (control_code == EVENT_TRACE_CONTROL_QUERY) {
            ++query_calls;
            return query_status;
        }
        return ERROR_INVALID_FUNCTION;
    }

    ULONG CloseConsumer(TRACEHANDLE /*handle*/) noexcept override {
        ++close_calls;
        return close_status;
    }

    ULONG start_status{ERROR_SUCCESS};
    TRACEHANDLE open_status{77U};
    DWORD open_error{ERROR_INVALID_HANDLE};
    ULONG process_status{ERROR_SUCCESS};
    ULONG flush_status{ERROR_SUCCESS};
    ULONG query_status{ERROR_SUCCESS};
    ULONG stop_status{ERROR_SUCCESS};
    ULONG close_status{ERROR_SUCCESS};
    std::atomic_uint32_t start_calls{};
    std::atomic_uint32_t open_calls{};
    std::atomic_uint32_t process_calls{};
    std::atomic_uint32_t flush_calls{};
    std::atomic_uint32_t query_calls{};
    std::atomic_uint32_t stop_calls{};
    std::atomic_uint32_t close_calls{};
};

class FakeWait final : public HostWait {
  public:
    bool Until(const std::stop_token&,
               std::optional<std::chrono::steady_clock::time_point>) noexcept override {
        ++calls;
        if (return_true_once) {
            return_true_once = false;
            return true;
        }
        return false;
    }

    std::uint32_t calls{};
    bool return_true_once{};
};

AuthorizedPrincipal Principal() {
    const auto sid = nfm::test::CurrentLogonSid();
    return {
        .mode = CollectorEndpointMode::Portable, .authorized_sid = sid, .server_logon_sid = sid};
}

void CheckProvider(TestContext& context) {
    FakeEtwApi api;
    WindowsEtwSession session{api};
    EtwSnapshotProvider provider{session};
    context.Check(session.Start().ok(), "provider session starts");
    const auto capture = provider.Capture();
    context.Check(capture.error == nfm::ipc::CaptureError::None &&
                      capture.capture.counters.captured_at !=
                          std::chrono::steady_clock::time_point{},
                  "provider flushes and timestamps one snapshot");
    context.Check(api.flush_calls == 1U && api.query_calls == 1U,
                  "provider performs one flush and query");
    context.Check(session.Stop().ok(), "provider session stops");

    FakeEtwApi failed_api;
    failed_api.flush_status = ERROR_WRITE_FAULT;
    WindowsEtwSession failed_session{failed_api};
    EtwSnapshotProvider failed_provider{failed_session};
    context.Check(failed_session.Start().ok(), "failure session starts");
    const auto failed = failed_provider.Capture();
    context.Check(failed.error == nfm::ipc::CaptureError::FlushFailed &&
                      failed.source_error == ERROR_WRITE_FAULT,
                  "provider classifies flush failure without fabricating data");
    context.Check(failed_session.Stop().ok(), "failure session stops");
}

void CheckHostLifecycle(TestContext& context) {
    const auto principal = Principal();
    context.Check(!principal.authorized_sid.empty(), "host test has current user SID");
    if (principal.authorized_sid.empty()) {
        return;
    }

    FakeEtwApi api;
    WindowsEtwSession session{api};
    EtwSnapshotProvider provider{session};
    WindowsPipeTransportFactory transport;
    IpcServer server{"cinst_host", principal, transport};
    SnapshotPublisher publisher{"cinst_host", std::chrono::steady_clock::now() - 1s, provider,
                                server};
    FakeWait wait;
    CollectorHost host{session, server, publisher, wait};

    context.Check(host.Start().ok() && host.state() == CollectorHostState::Running,
                  "host starts ETW and IPC in order");
    context.Check(api.start_calls == 1U &&
                      server.health().state == nfm::ipc::IpcServerState::Running,
                  "host owns one live ETW session and server");
    context.Check(host.Run(std::stop_token{}).ok() && wait.calls == 1U && api.flush_calls == 0U,
                  "host run waits without capturing when no client is subscribed");
    publisher.ClientConnected(
        1U, HelloMessage{.client_pid = 12344U, .protocol_version = 1U, .client_name = "HostTest"});
    wait.return_true_once = true;
    const auto calls_before_due_run = api.flush_calls.load();
    context.Check(host.Run(std::stop_token{}).ok() &&
                      api.flush_calls.load() == calls_before_due_run + 1U,
                  "host captures once a connected client is due");
    context.Check(host.Stop().ok() && host.state() == CollectorHostState::Stopped,
                  "host stops IPC before ETW and reaches stopped state");
    context.Check(host.Stop().ok(), "host stop is idempotent");
}

void CheckStartFailure(TestContext& context) {
    const auto principal = Principal();
    if (principal.authorized_sid.empty()) {
        return;
    }
    FakeEtwApi api;
    api.start_status = ERROR_ACCESS_DENIED;
    WindowsEtwSession session{api};
    EtwSnapshotProvider provider{session};
    WindowsPipeTransportFactory transport;
    IpcServer server{"cinst_host_fail", principal, transport};
    SnapshotPublisher publisher{"cinst_host_fail", std::chrono::steady_clock::now() - 1s, provider,
                                server};
    FakeWait wait;
    CollectorHost host{session, server, publisher, wait};
    const auto result = host.Start();
    context.Check(result.error == CollectorHostError::EtwStartFailed &&
                      host.state() == CollectorHostState::Failed,
                  "host classifies ETW startup denial before opening IPC");
    context.Check(host.Stop().ok(), "failed host can be stopped safely");
}

} // namespace

int main() {
    TestContext context;
    CheckProvider(context);
    CheckHostLifecycle(context);
    CheckStartFailure(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " collector host test failure(s)\n";
        return 1;
    }
    std::cout << "collector host tests passed\n";
    return 0;
}
