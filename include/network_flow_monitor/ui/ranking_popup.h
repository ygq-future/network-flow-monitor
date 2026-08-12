#ifndef NETWORK_FLOW_MONITOR_UI_RANKING_POPUP_H
#define NETWORK_FLOW_MONITOR_UI_RANKING_POPUP_H

#include "network_flow_monitor/ui/floating_window.h"

#include <windows.h>

namespace nfm::ui {

struct PopupPlacementContext {
    PixelRect anchor{};
    PixelRect work_area{};
};

class RankingPopup final {
  public:
    static constexpr DWORD kExtendedStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;

    RankingPopup(HINSTANCE instance, RenderResources& resources, UiWindowObserver& observer)
        : instance_{instance}, resources_{&resources}, observer_{&observer} {}
    ~RankingPopup();
    RankingPopup(const RankingPopup&) = delete;
    RankingPopup& operator=(const RankingPopup&) = delete;
    RankingPopup(RankingPopup&&) = delete;
    RankingPopup& operator=(RankingPopup&&) = delete;

    [[nodiscard]] bool Create() noexcept;
    void ShowAt(PopupPlacementContext context) noexcept;
    void Hide() noexcept;
    void Destroy() noexcept;
    void SetSnapshot(RankingSnapshotPtr snapshot, std::uint64_t generation) noexcept;
    void SetDisplayOptions(WindowDisplayOptions options) noexcept;
    void Reposition(PopupPlacementContext context) noexcept;

    [[nodiscard]] HWND hwnd() const noexcept {
        return window_;
    }
    [[nodiscard]] PixelRect anchor_rect() const noexcept {
        return anchor_;
    }
    [[nodiscard]] PixelRect rect() const noexcept {
        return rect_;
    }
    [[nodiscard]] static bool ShouldInvalidate(std::uint64_t old_generation,
                                               std::uint64_t new_generation) noexcept;
    [[nodiscard]] static PixelRect ApplyDpiSuggestion(PixelRect suggested,
                                                      PixelRect work_area) noexcept;

  private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    [[nodiscard]] static bool RegisterWindowClass(HINSTANCE instance) noexcept;
    void Invalidate() noexcept;
    void Paint() noexcept;
    void HandleDpiChanged(LPARAM lparam) noexcept;

    HINSTANCE instance_{};
    RenderResources* resources_;
    UiWindowObserver* observer_;
    HWND window_{};
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    RankingSnapshotPtr snapshot_;
    std::uint64_t generation_{};
    PixelRect anchor_{};
    PixelRect rect_{};
    PixelSize popup_size_{.width = 320, .height = 220};
    WindowDisplayOptions display_options_{};
    bool tracking_mouse_{};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_RANKING_POPUP_H
