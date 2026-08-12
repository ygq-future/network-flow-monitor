#include "network_flow_monitor/ui/collector_process.h"
#include "network_flow_monitor/ui/ui_controller.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_process_token{};

using nfm::config::DefaultAppConfig;
using nfm::ipc::ClientSnapshot;
using nfm::ipc::IpcClientState;
using nfm::ui::CollectorProcess;
using nfm::ui::ShellExecuteApi;
using nfm::ui::UiController;

struct FakeShellExecuteApi final : ShellExecuteApi {
    bool execute_result{true};
    std::uint32_t error{ERROR_SUCCESS};
    std::size_t wait_calls{};
    std::size_t stop_requests{};
    std::size_t terminate_calls{};
    std::size_t close_calls{};
    std::size_t stop_event_close_calls{};
    std::size_t stop_event_signal_calls{};
    HANDLE process_handle{reinterpret_cast<HANDLE>(&g_process_token)};
    std::vector<DWORD> wait_results{WAIT_TIMEOUT, WAIT_TIMEOUT, WAIT_OBJECT_0};
    SHELLEXECUTEINFOW last_info{};
    std::wstring last_file;
    std::wstring last_parameters;
    std::wstring stop_event_name{L"Local\\NetworkFlowMonitor.Stop.test"};
    HANDLE stop_event_handle{reinterpret_cast<HANDLE>(&g_process_token + 1)};

    bool Execute(SHELLEXECUTEINFOW& info) noexcept override {
        last_info = info;
        last_file = info.lpFile == nullptr ? L"" : info.lpFile;
        last_parameters = info.lpParameters == nullptr ? L"" : info.lpParameters;
        info.hProcess = process_handle;
        return execute_result;
    }
    HANDLE CreateStopEvent(std::wstring& name) noexcept override {
        name = stop_event_name;
        return stop_event_handle;
    }
    DWORD Wait(HANDLE, DWORD) noexcept override {
        const auto index = std::min(wait_calls, wait_results.size() - 1U);
        ++wait_calls;
        return wait_results.at(index);
    }
    bool RequestStop(HANDLE) noexcept override {
        ++stop_requests;
        return true;
    }
    bool SignalStopEvent(HANDLE event) noexcept override {
        ++stop_event_signal_calls;
        return event == stop_event_handle;
    }
    bool Terminate(HANDLE) noexcept override {
        ++terminate_calls;
        return true;
    }
    void Close(HANDLE) noexcept override {
        ++close_calls;
    }
    void CloseEvent(HANDLE event) noexcept override {
        if (event == stop_event_handle) {
            ++stop_event_close_calls;
        }
    }
    std::uint32_t LastError() const noexcept override {
        return error;
    }
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

ClientSnapshot MakeSnapshot(std::uint8_t instance_byte) {
    ClientSnapshot snapshot;
    snapshot.instance.bytes[0] = static_cast<std::byte>(instance_byte);
    return snapshot;
}

void CheckCollectorProcess(TestContext& context) {
    FakeShellExecuteApi api;
    CollectorProcess process{api};
    const auto started = process.StartPortable(L"C:\\Tools\\NetworkFlowMonitor.exe");
    context.Check(started.succeeded && process.running(), "portable collector starts");
    context.Check(api.last_info.lpVerb != nullptr && std::wstring{api.last_info.lpVerb} == L"runas",
                  "portable collector requests UAC elevation");
    context.Check(api.last_parameters ==
                      L"--collector --stop-event \"Local\\NetworkFlowMonitor.Stop.test\"",
                  "portable collector uses internal mode and stop event");
    context.Check(!process.StartPortable(L"C:\\Tools\\NetworkFlowMonitor.exe").succeeded,
                  "portable launch is idempotent");
    const auto stopped = process.Stop();
    context.Check(stopped.succeeded && api.stop_requests == 1U &&
                      api.stop_event_signal_calls == 1U && api.terminate_calls == 0U,
                  "graceful portable stop avoids termination fallback");
    context.Check(api.close_calls == 1U && api.stop_event_close_calls == 1U && !process.running(),
                  "portable process and stop event handles are closed");
}

void CheckCollectorFallback(TestContext& context) {
    FakeShellExecuteApi api;
    api.wait_results = {WAIT_TIMEOUT, WAIT_TIMEOUT, WAIT_OBJECT_0};
    CollectorProcess process{api};
    static_cast<void>(process.StartPortable(L"C:\\Tools\\NetworkFlowMonitor.exe"));
    const auto stopped = process.Stop();
    context.Check(stopped.succeeded && stopped.forced_termination && api.terminate_calls == 1U,
                  "running collector uses bounded termination fallback");
}

void CheckCollectorLaunchFailure(TestContext& context) {
    FakeShellExecuteApi api;
    api.execute_result = false;
    api.error = ERROR_ACCESS_DENIED;
    CollectorProcess process{api};
    const auto result = process.StartPortable(L"C:\\Tools\\NetworkFlowMonitor.exe");
    context.Check(!result.succeeded && result.win32_error == ERROR_ACCESS_DENIED &&
                      api.close_calls == 0U && !process.running(),
                  "portable launch failure leaves no owned process handle");
}

void CheckControllerBridge(TestContext& context) {
    auto config = DefaultAppConfig();
    UiController controller{nullptr, config};
    controller.SnapshotReceived(MakeSnapshot(1U));
    controller.SnapshotReceived(MakeSnapshot(2U));
    const auto pending = controller.TakePendingSnapshot();
    context.Check(pending.has_value() && pending->instance.bytes[0] == std::byte{2},
                  "snapshot queue coalesces to newest value");
    context.Check(!controller.TakePendingSnapshot().has_value(), "snapshot queue drains once");
    controller.StateChanged(IpcClientState::Streaming);
    context.Check(controller.ipc_state() == IpcClientState::Streaming,
                  "streaming state reaches controller");
    controller.StateChanged(IpcClientState::Disconnected);
    context.Check(controller.overlay_state() == nfm::ui::InteractionState::DisconnectedOverlay,
                  "disconnected state maps to overlay");
    controller.StateChanged(IpcClientState::Streaming);
    context.Check(controller.overlay_state() != nfm::ui::InteractionState::DisconnectedOverlay,
                  "reconnect clears overlay");
    context.Check(controller.UpdateRankingOptions(3U, std::chrono::milliseconds{1000}),
                  "ranking options update succeeds");
    controller.SnapshotReceived(MakeSnapshot(3U));
    context.Check(controller.ProcessPendingSnapshot() && controller.snapshot_generation() == 1U,
                  "first collector instance produces a ranking snapshot");
    controller.SnapshotReceived(MakeSnapshot(4U));
    context.Check(controller.ProcessPendingSnapshot() && controller.snapshot_generation() == 2U,
                  "new collector instance resets stale ranking history");
}

} // namespace

int main() {
    TestContext context;
    CheckCollectorProcess(context);
    CheckCollectorFallback(context);
    CheckCollectorLaunchFailure(context);
    CheckControllerBridge(context);
    return context.failures == 0 ? 0 : 1;
}
