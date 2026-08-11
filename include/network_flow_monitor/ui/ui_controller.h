#ifndef NETWORK_FLOW_MONITOR_UI_UI_CONTROLLER_H
#define NETWORK_FLOW_MONITOR_UI_UI_CONTROLLER_H

#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/ipc/ipc_client.h"
#include "network_flow_monitor/ipc/named_pipe_transport.h"
#include "network_flow_monitor/ranking/ranking_engine.h"
#include "network_flow_monitor/ui/collector_process.h"
#include "network_flow_monitor/ui/floating_window.h"
#include "network_flow_monitor/ui/interaction_state.h"
#include "network_flow_monitor/ui/ranking_popup.h"
#include "network_flow_monitor/ui/tray_controller.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace nfm::ui {

inline constexpr UINT kUiSnapshotMessage = WM_APP + 0x32U;
inline constexpr UINT kUiStateMessage = WM_APP + 0x33U;
inline constexpr UINT_PTR kUiInteractionTimerId = 1U;

class UiController final : public ipc::IpcClientObserver,
                           public TrayCallbacks,
                           public UiWindowObserver {
  public:
    UiController(HINSTANCE instance, const config::AppConfig& config);
    ~UiController() override;
    UiController(const UiController&) = delete;
    UiController& operator=(const UiController&) = delete;
    UiController(UiController&&) = delete;
    UiController& operator=(UiController&&) = delete;

    [[nodiscard]] bool Start() noexcept;
    [[nodiscard]] int Run(bool smoke_test = false) noexcept;
    void Shutdown() noexcept;
    void AttachMessageWindow(HWND window) noexcept;
    [[nodiscard]] bool ProcessPendingSnapshot() noexcept;
    [[nodiscard]] std::optional<ipc::ClientSnapshot> TakePendingSnapshot() noexcept;
    [[nodiscard]] bool UpdateRankingOptions(std::uint8_t top_n,
                                            std::chrono::milliseconds window) noexcept;

    [[nodiscard]] ipc::IpcClientState ipc_state() const noexcept {
        return ipc_state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] InteractionState overlay_state() const noexcept {
        return overlay_state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t snapshot_generation() const noexcept {
        return snapshot_generation_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool memory_error() const noexcept {
        return memory_error_.load(std::memory_order_acquire);
    }

    void StateChanged(ipc::IpcClientState state) noexcept override;
    void SnapshotReceived(ipc::ClientSnapshot snapshot) noexcept override;

    void OnTrayLeftClick() noexcept override;
    void OnTrayRightClick() noexcept override;
    void OnTrayHover() noexcept override;
    void OnTraySettings() noexcept override;
    void OnTrayExit() noexcept override;
    void OnWindowInvalidated(WindowKind kind) noexcept override;
    void OnWindowPointer(WindowKind kind, InteractionEvent event,
                         MouseTarget target) noexcept override;

  private:
    [[nodiscard]] InteractionOptions InteractionOptionsFromConfig() const noexcept;
    [[nodiscard]] bool RecreateRankingEngine() noexcept;
    void ApplyInteractionState() noexcept;
    void HandleInteraction(InteractionEvent event, MouseTarget target = MouseTarget::None) noexcept;
    void ScheduleInteractionTimer() noexcept;
    void HandleStateOnUiThread(ipc::IpcClientState state) noexcept;
    [[nodiscard]] PixelRect CurrentWorkArea() const noexcept;
    [[nodiscard]] bool CreateMessageWindow() noexcept;
    void DestroyMessageWindow() noexcept;
    void PostSnapshotMessage() noexcept;
    static LRESULT CALLBACK MessageWindowProc(HWND window, UINT message, WPARAM wparam,
                                              LPARAM lparam) noexcept;

    HINSTANCE instance_{};
    config::AppConfig config_;
    ipc::WindowsPipeTransportFactory transport_;
    WindowsShellExecuteApi shell_api_;
    CollectorProcess collector_process_;
    WindowsTrayApi tray_api_;
    RenderResources render_resources_;
    InteractionStateMachine interaction_;
    FloatingWindow floating_window_;
    RankingPopup ranking_popup_;
    TrayController tray_controller_;
    std::unique_ptr<ipc::IpcClient> ipc_client_;
    std::unique_ptr<ranking::RankingEngine> ranking_engine_;
    std::optional<ipc::CollectorInstanceId> ranking_instance_;
    std::shared_ptr<const ranking::RankingSnapshot> ranking_snapshot_;
    std::atomic<ipc::IpcClientState> ipc_state_{ipc::IpcClientState::Disconnected};
    std::atomic<InteractionState> overlay_state_{InteractionState::Hidden};
    std::atomic<std::uint64_t> snapshot_generation_;
    std::atomic<bool> memory_error_;
    std::mutex pending_mutex_;
    std::optional<ipc::ClientSnapshot> pending_snapshot_;
    bool pending_message_posted_{};
    HWND message_window_{};
    bool started_{};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_UI_CONTROLLER_H
