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
constexpr auto kTestMessageWindowClass = L"NfmUiControllerTestMessageWindow";

using nfm::config::DefaultAppConfig;
using nfm::ipc::IpcClientState;
using nfm::ipc::TrafficTickMessage;
using nfm::ui::CollectorProcess;
using nfm::ui::ShellExecuteApi;
using nfm::ui::UiController;

LRESULT CALLBACK TestMessageWindowProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 stores the test controller in LONG_PTR.
    auto* controller = reinterpret_cast<UiController*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): LPARAM carries CREATESTRUCTW in WM_NCCREATE.
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        controller = static_cast<UiController*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controller));
        return TRUE;
    }
    if (message == WM_TIMER && controller != nullptr) {
        controller->OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::HoverDeadline, {});
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateTestMessageWindow(UiController& controller) {
    const WNDCLASSW window_class{.lpfnWndProc = TestMessageWindowProc,
                                 .hInstance = ::GetModuleHandleW(nullptr),
                                 .lpszClassName = kTestMessageWindowClass};
    if (::RegisterClassW(&window_class) == 0U && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    return ::CreateWindowExW(0U, kTestMessageWindowClass, L"", 0U, 0, 0, 0, 0, HWND_MESSAGE,
                             nullptr, ::GetModuleHandleW(nullptr), &controller);
}

template <typename Predicate>
bool PumpMessagesUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < end) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        static_cast<void>(::MsgWaitForMultipleObjects(0U, nullptr, FALSE, 5U, QS_ALLINPUT));
    }
    return predicate();
}

void PumpMessagesFor(std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        static_cast<void>(::MsgWaitForMultipleObjects(0U, nullptr, FALSE, 1U, QS_ALLINPUT));
    }
}

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

void CheckCollectorProcess(TestContext& context) {
    FakeShellExecuteApi api;
    // Probe reports "still running", and the collector then exits gracefully on stop.
    api.wait_results = {WAIT_TIMEOUT, WAIT_TIMEOUT, WAIT_TIMEOUT, WAIT_OBJECT_0};
    CollectorProcess process{api};
    const auto started = process.StartPortable(L"C:\\Tools\\NetworkFlowMonitor.exe");
    context.Check(started.succeeded && process.running(), "portable collector starts");
    context.Check(api.last_info.lpVerb == nullptr,
                  "portable collector prefers non-elevated launch");
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

void CheckCollectorElevationFallback(TestContext& context) {
    FakeShellExecuteApi api;
    // An unauthorized collector exits immediately; the non-elevated probe then triggers runas.
    api.wait_results = {WAIT_OBJECT_0, WAIT_TIMEOUT};
    CollectorProcess process{api};
    const auto started = process.StartPortable(L"C:\\Tools\\NetworkFlowMonitor.exe");
    context.Check(started.succeeded, "portable collector retries after non-elevated failure");
    context.Check(api.last_info.lpVerb != nullptr && std::wstring{api.last_info.lpVerb} == L"runas",
                  "portable collector falls back to UAC elevation");
    context.Check(api.close_calls == 1U, "failed non-elevated process handle is closed");
}

void CheckCollectorFallback(TestContext& context) {
    FakeShellExecuteApi api;
    api.wait_results = {WAIT_TIMEOUT, WAIT_TIMEOUT, WAIT_TIMEOUT, WAIT_OBJECT_0};
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
    controller.TrafficTickReceived(TrafficTickMessage{.seq = 1U, .download_bytes_per_sec = 100U});
    controller.TrafficTickReceived(TrafficTickMessage{.seq = 2U, .download_bytes_per_sec = 200U});
    const auto pending = controller.TakePendingTick();
    context.Check(pending.has_value() && pending->seq == 2U,
                  "tick queue coalesces to newest value");
    context.Check(!controller.TakePendingTick().has_value(), "tick queue drains once");
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
    controller.TrafficTickReceived(TrafficTickMessage{.seq = 3U, .download_bytes_per_sec = 300U});
    context.Check(controller.ProcessPendingSnapshot() && controller.snapshot_generation() == 1U,
                  "tick produces a snapshot generation update");
    controller.TrafficTickReceived(TrafficTickMessage{.seq = 4U, .download_bytes_per_sec = 400U});
    context.Check(controller.ProcessPendingSnapshot() && controller.snapshot_generation() == 2U,
                  "new tick produces a second generation update");
}

void CheckMonitorPanelRuntimeBridge(TestContext& context) {
    auto config = DefaultAppConfig();
    config.collector.preferred_mode = nfm::config::CollectorMode::Service;
    config.interaction.hover_delay = std::chrono::milliseconds{10};
    config.floating.auto_hide_delay = std::chrono::milliseconds{10};
    UiController controller{::GetModuleHandleW(nullptr), config};
    const auto message_window = CreateTestMessageWindow(controller);
    context.Check(message_window != nullptr, "controller test creates a real timer target HWND");
    if (message_window == nullptr) {
        return;
    }
    controller.AttachMessageWindow(message_window);
    context.Check(controller.Start(), "controller creates the real monitor-panel HWND");
    const auto panel = controller.monitor_panel_hwnd();
    context.Check(panel != nullptr && ::IsWindowVisible(panel) != FALSE,
                  "controller starts with a visible capsule HWND");
    if (panel == nullptr) {
        controller.Shutdown();
        static_cast<void>(::DestroyWindow(message_window));
        return;
    }

    controller.OnTrayLeftClick();
    context.Check(controller.monitor_panel_state().surface ==
                          nfm::ui::MonitorPanelSurface::Hidden &&
                      ::IsWindowVisible(panel) == FALSE,
                  "tray hide applies to the real monitor-panel HWND");
    controller.OnTrayLeftClick();
    context.Check(controller.monitor_panel_state().surface == nfm::ui::MonitorPanelSurface::Panel &&
                      ::IsWindowVisible(panel) != FALSE,
                  "tray show restores the real panel HWND");

    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::CollectorConnected, {});
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::ShowCapsule, {});
    const auto hit = controller.monitor_panel_hit_hwnd();
    context.Check(hit != nullptr && ::IsWindowVisible(hit) != FALSE,
                  "controller exposes the real inward capsule hit HWND");
    if (hit == nullptr) {
        controller.Shutdown();
        static_cast<void>(::DestroyWindow(message_window));
        return;
    }
    controller.OnTrayLeftClick();
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::ShowCapsule, {});
    ::ShowWindow(hit, SW_HIDE);
    ::ShowWindow(panel, SW_HIDE);
    PumpMessagesFor(std::chrono::milliseconds{20});
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::CapsulePointerEnter, {});
    context.Check(controller.monitor_panel_state().deadline ==
                      nfm::ui::MonitorPanelDeadline::CapsuleReveal,
                  "native capsule hover schedules the reveal deadline");
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::RankingReadyIdle, {});
    context.Check(controller.monitor_panel_state().deadline ==
                      nfm::ui::MonitorPanelDeadline::CapsuleReveal,
                  "a ranking event does not cancel the pending reveal deadline");
    context.Check(PumpMessagesUntil(
                      [&controller] {
                          return controller.monitor_panel_state().surface ==
                                 nfm::ui::MonitorPanelSurface::Panel;
                      },
                      std::chrono::milliseconds{250}),
                  "capsule deadline reveals the single panel through the controller adapter");
    context.Check(controller.monitor_panel_state().deadline == nfm::ui::MonitorPanelDeadline::None,
                  "panel reveal does not chain a second ranking deadline");
    ::SendMessageW(hit, WM_MOUSELEAVE, 0U, 0);
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::PanelPointerLeave, {});
    context.Check(PumpMessagesUntil(
                      [&controller] {
                          return controller.monitor_panel_state().surface ==
                                 nfm::ui::MonitorPanelSurface::Capsule;
                      },
                      std::chrono::milliseconds{250}),
                  "one dismiss deadline returns the panel to its capsule");

    const auto click_hit = controller.monitor_panel_hit_hwnd();
    context.Check(click_hit != nullptr, "collapsed monitor recreates the capsule hit HWND");
    if (click_hit == nullptr) {
        controller.Shutdown();
        static_cast<void>(::DestroyWindow(message_window));
        return;
    }
    ::SendMessageW(click_hit, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(1, 1));
    ::SendMessageW(click_hit, WM_LBUTTONUP, 0U, MAKELPARAM(1, 1));
    context.Check(controller.monitor_panel_state().surface == nfm::ui::MonitorPanelSurface::Panel,
                  "native capsule click reveals the panel");
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::PanelPointerEnter, {});
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::PanelPointerLeave, {});
    context.Check(PumpMessagesUntil(
                      [&controller] {
                          return controller.monitor_panel_state().deadline ==
                                 nfm::ui::MonitorPanelDeadline::Dismiss;
                      },
                      std::chrono::milliseconds{250}),
                  "leaving the click-revealed panel schedules dismiss");
    context.Check(PumpMessagesUntil(
                      [&controller] {
                          return controller.monitor_panel_state().surface ==
                                 nfm::ui::MonitorPanelSurface::Capsule;
                      },
                      std::chrono::milliseconds{250}),
                  "the click-revealed panel returns to the capsule after its dismiss deadline");

    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::ShowPanel, {});
    controller.OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind::CollectorDisconnected, {});
    context.Check(controller.monitor_panel_state().surface == nfm::ui::MonitorPanelSurface::Panel &&
                      controller.monitor_panel_state().ranking_presentation ==
                          nfm::ui::RankingPresentation::Disconnected,
                  "disconnect preserves the panel while clearing ranking rows");
    controller.Shutdown();
    static_cast<void>(::DestroyWindow(message_window));
}

} // namespace

int main() {
    TestContext context;
    CheckCollectorProcess(context);
    CheckCollectorElevationFallback(context);
    CheckCollectorFallback(context);
    CheckCollectorLaunchFailure(context);
    CheckControllerBridge(context);
    CheckMonitorPanelRuntimeBridge(context);
    return context.failures == 0 ? 0 : 1;
}
