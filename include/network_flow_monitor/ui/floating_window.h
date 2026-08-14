#ifndef NETWORK_FLOW_MONITOR_UI_FLOATING_WINDOW_H
#define NETWORK_FLOW_MONITOR_UI_FLOATING_WINDOW_H

#include "network_flow_monitor/ranking/ranking_engine.h"
#include "network_flow_monitor/ui/appearance.h"
#include "network_flow_monitor/ui/application_icon_cache.h"
#include "network_flow_monitor/ui/display_format.h"
#include "network_flow_monitor/ui/dpi_layout.h"
#include "network_flow_monitor/ui/interaction_state.h"
#include "network_flow_monitor/ui/monitor_panel_behavior.h"
#include "network_flow_monitor/ui/render_resources.h"
#include "network_flow_monitor/ui/visual_tokens.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace nfm::ui {

class UiWindowObserver {
  public:
    UiWindowObserver() = default;
    virtual ~UiWindowObserver() = default;
    UiWindowObserver(const UiWindowObserver&) = delete;
    UiWindowObserver& operator=(const UiWindowObserver&) = delete;
    UiWindowObserver(UiWindowObserver&&) = delete;
    UiWindowObserver& operator=(UiWindowObserver&&) = delete;

    virtual void OnWindowInvalidated() noexcept = 0;
    virtual void OnWindowContextMenu() noexcept {}
    virtual void OnWindowMoved() noexcept {}
    virtual void OnMonitorPanelEvent(MonitorPanelEventKind event, PixelPoint point) noexcept {
        static_cast<void>(event);
        static_cast<void>(point);
    }
    virtual void OnSystemAppearanceChanged() noexcept {}
};

using RankingSnapshotPtr = std::shared_ptr<const ranking::RankingSnapshot>;

enum class MonitorPanelControl : std::uint8_t {
    None,
    Pin,
};

// The single monitor-panel surface. It is either a dock capsule or a complete
// panel containing one header row followed immediately by ranking rows (or a
// single status row). There is no separate ranking HWND.
class FloatingWindow final {
  public:
    // The main surface is not per-pixel-layered: DWM supplies the Acrylic system
    // backdrop and rounds the window corners with antialiasing, so content never
    // shows the aliased color-key edges the old LWA_COLORKEY approach produced.
    static constexpr DWORD kExtendedStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    // The capsule's inward hit-test extension is an invisible per-pixel-layered HWND.
    static constexpr DWORD kHitExtendedStyle =
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED;

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
    void SetAppearance(VisualTokens tokens, ResolvedLanguage language) noexcept;
    void SetMonitorPanelPresentation(MonitorPanelSurface surface, CapsulePresentation capsule,
                                     PixelRect visual_bounds, PixelRect hit_bounds,
                                     PixelRect viewport) noexcept;
    void SetMonitorPanelControls(bool pinned, bool pin_enabled) noexcept;
    void Reposition(PixelRect rect) noexcept;

    [[nodiscard]] HWND hwnd() const noexcept {
        return window_;
    }
    [[nodiscard]] HWND hit_test_hwnd() const noexcept {
        return hit_window_;
    }
    [[nodiscard]] PixelRect rect() const noexcept {
        return rect_;
    }
    [[nodiscard]] std::int32_t scroll_row_offset() const noexcept {
        return scroll_row_offset_;
    }
    [[nodiscard]] bool row_animation_active() const noexcept {
        return row_animation_active_;
    }
    [[nodiscard]] static bool ShouldInvalidate(std::uint64_t old_generation,
                                               std::uint64_t new_generation,
                                               InteractionState old_state,
                                               InteractionState new_state) noexcept;
    [[nodiscard]] static PixelRect ApplyDpiSuggestion(PixelRect suggested,
                                                      PixelRect work_area) noexcept;
    [[nodiscard]] static MonitorPanelControl HitTestControl(PixelPoint client_point,
                                                            std::int32_t client_width_pixels,
                                                            int dpi) noexcept;
    [[nodiscard]] static bool ShouldDispatchPointerLeave(PixelRect tracked_bounds,
                                                         bool cursor_query_succeeded,
                                                         PixelPoint cursor) noexcept;

  private:
    struct RowVisual {
        ranking::RankedApplication application;
        float from_position{};
        float target_position{};
        float from_opacity{1.0F};
        float target_opacity{1.0F};
        bool exiting{};
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept;
    [[nodiscard]] static bool RegisterWindowClass(HINSTANCE instance) noexcept;
    void Invalidate() noexcept;
    void Paint() noexcept;
    void DrawPanel(ID2D1RenderTarget& target, ID2D1SolidColorBrush& text_brush, int window_dpi,
                   ID2D1SolidColorBrush& accent_brush) noexcept;
    void PositionOnFirstShow() noexcept;
    void ApplyWindowRect(PixelRect rect) noexcept;
    void ApplyHitWindowRect() noexcept;
    void ApplyDwmAttributes() noexcept;
    void CancelPointerTracking() noexcept;
    void TransferPanelTracking() noexcept;
    void HandleEnvironmentChanged() noexcept;
    void ScrollRows(std::int32_t delta) noexcept;
    [[nodiscard]] std::int32_t VisibleRowCount() const noexcept;
    void RetargetRows(const ranking::RankingSnapshot* snapshot);
    void TickRowAnimation() noexcept;
    void SetAnimationFramePeriod(bool active) noexcept;
    [[nodiscard]] float RowAnimationProgress() const noexcept;

    HINSTANCE instance_{};
    RenderResources* resources_;
    UiWindowObserver* observer_;
    HWND window_{};
    HWND hit_window_{};
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    RankingSnapshotPtr snapshot_;
    std::uint64_t generation_{};
    InteractionState interaction_state_{InteractionState::Hidden};
    WindowDisplayOptions display_options_{};
    VisualTokens tokens_{ResolveVisualTokens(ResolvedTheme::Dark, false)};
    ResolvedLanguage language_{ResolvedLanguage::English};
    MonitorPanelSurface panel_surface_{MonitorPanelSurface::Panel};
    CapsulePresentation capsule_{};
    bool pinned_{};
    bool pin_enabled_{true};
    PixelRect rect_{.left = 20, .top = 20, .right = 380, .bottom = 212};
    PixelRect visual_bounds_{rect_};
    PixelRect hit_bounds_{rect_};
    PixelRect viewport_{};
    PixelPoint panel_press_point_{};
    MonitorPanelControl pressed_control_{MonitorPanelControl::None};
    bool panel_pressing_{};
    HWND tracking_window_{};
    MonitorPanelSurface tracking_surface_{MonitorPanelSurface::Panel};
    bool positioned_{};
    ApplicationIconCache icon_cache_;
    std::vector<RowVisual> row_visuals_;
    std::chrono::steady_clock::time_point row_animation_started_;
    bool row_animation_active_{};
    bool tracking_mouse_{};
    bool scrollbar_active_{};
    bool animation_timer_period_{};
    std::int32_t scroll_row_offset_{};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_FLOATING_WINDOW_H
