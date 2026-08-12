#ifndef NETWORK_FLOW_MONITOR_UI_FLOATING_WINDOW_H
#define NETWORK_FLOW_MONITOR_UI_FLOATING_WINDOW_H

#include "network_flow_monitor/ranking/ranking_engine.h"
#include "network_flow_monitor/ui/display_format.h"
#include "network_flow_monitor/ui/dpi_layout.h"
#include "network_flow_monitor/ui/interaction_state.h"
#include "network_flow_monitor/ui/render_resources.h"

#include <windows.h>

#include <cstdint>
#include <memory>

namespace nfm::ui {

enum class WindowKind : std::uint8_t {
    Floating,
    RankingPopup
};

class UiWindowObserver {
  public:
    UiWindowObserver() = default;
    virtual ~UiWindowObserver() = default;
    UiWindowObserver(const UiWindowObserver&) = delete;
    UiWindowObserver& operator=(const UiWindowObserver&) = delete;
    UiWindowObserver(UiWindowObserver&&) = delete;
    UiWindowObserver& operator=(UiWindowObserver&&) = delete;

    virtual void OnWindowInvalidated(WindowKind kind) noexcept = 0;
    virtual void OnWindowPointer(WindowKind kind, InteractionEvent event,
                                 MouseTarget target) noexcept {
        static_cast<void>(kind);
        static_cast<void>(event);
        static_cast<void>(target);
    }
    virtual void OnWindowContextMenu(WindowKind kind) noexcept {
        static_cast<void>(kind);
    }
};

using RankingSnapshotPtr = std::shared_ptr<const ranking::RankingSnapshot>;

class FloatingWindow final {
  public:
    static constexpr DWORD kExtendedStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;

    FloatingWindow(HINSTANCE instance, RenderResources& resources, UiWindowObserver& observer)
        : instance_{instance}, resources_{&resources}, observer_{&observer} {}
    ~FloatingWindow();
    FloatingWindow(const FloatingWindow&) = delete;
    FloatingWindow& operator=(const FloatingWindow&) = delete;
    FloatingWindow(FloatingWindow&&) = delete;
    FloatingWindow& operator=(FloatingWindow&&) = delete;

    [[nodiscard]] bool Create() noexcept;
    void Show() noexcept;
    void Hide() noexcept;
    void Destroy() noexcept;
    void SetSnapshot(RankingSnapshotPtr snapshot, std::uint64_t generation) noexcept;
    void SetInteractionState(InteractionState state) noexcept;
    void SetDisplayOptions(WindowDisplayOptions options) noexcept;
    void SetEdgeHidden(bool hidden, std::int32_t reveal_strip_dip, PixelRect work_area) noexcept;
    void SetSnapDistance(std::int32_t snap_distance_dip) noexcept;
    void Reposition(PixelRect rect) noexcept;

    [[nodiscard]] HWND hwnd() const noexcept {
        return window_;
    }
    [[nodiscard]] PixelRect rect() const noexcept {
        return rect_;
    }
    [[nodiscard]] static bool ShouldInvalidate(std::uint64_t old_generation,
                                               std::uint64_t new_generation,
                                               InteractionState old_state,
                                               InteractionState new_state) noexcept;
    [[nodiscard]] static PixelRect ApplyDpiSuggestion(PixelRect suggested,
                                                      PixelRect work_area) noexcept;
    [[nodiscard]] static PixelRect ComputeEdgeHiddenRect(PixelRect normal_rect, PixelRect work_area,
                                                         std::int32_t reveal_strip_pixels) noexcept;

  private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    [[nodiscard]] static bool RegisterWindowClass(HINSTANCE instance) noexcept;
    void Invalidate() noexcept;
    void Paint() noexcept;
    void HandleDpiChanged(LPARAM lparam) noexcept;
    void HandleMouseMove(LPARAM lparam) noexcept;
    void FinishDrag() noexcept;
    void SnapToWorkArea() noexcept;
    void ApplyWindowRect(PixelRect rect) noexcept;

    HINSTANCE instance_{};
    RenderResources* resources_;
    UiWindowObserver* observer_;
    HWND window_{};
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    RankingSnapshotPtr snapshot_;
    std::uint64_t generation_{};
    InteractionState interaction_state_{InteractionState::Hidden};
    WindowDisplayOptions display_options_{};
    PixelRect rect_{.left = 20, .top = 20, .right = 340, .bottom = 84};
    PixelRect normal_rect_{rect_};
    std::int32_t snap_distance_dip_{12};
    bool edge_hidden_{};
    bool dragging_{};
    POINT drag_anchor_{};
    PixelRect drag_origin_{};
    bool tracking_mouse_{};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_FLOATING_WINDOW_H
