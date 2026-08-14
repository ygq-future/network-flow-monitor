#include "network_flow_monitor/ui/floating_window.h"
#include "network_flow_monitor/ui/monitor_panel_layout.h"

#include <windows.h>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using nfm::ui::FloatingWindow;
using nfm::ui::InteractionState;
using nfm::ui::MonitorPanelControl;
using nfm::ui::MonitorPanelSurface;
using nfm::ui::PixelRect;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

class RecordingObserver final : public nfm::ui::UiWindowObserver {
  public:
    void OnWindowInvalidated() noexcept override {}
    void OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind event,
                             nfm::ui::PixelPoint) noexcept override {
        events.push_back(event);
    }
    void OnWindowContextMenu() noexcept override {
        ++context_menu_count;
    }

    std::vector<nfm::ui::MonitorPanelEventKind> events;
    int context_menu_count{};
};

nfm::ui::RankingSnapshotPtr MakeRows(std::size_t count) {
    auto snapshot = std::make_shared<nfm::ranking::RankingSnapshot>();
    snapshot->status = nfm::ranking::RankingStatus::Ready;
    for (std::size_t index = 0; index < count; ++index) {
        snapshot->applications.push_back(
            {.id = "app-" + std::to_string(index),
             .metadata = {.display_name = "Application", .icon_key = {}},
             .rate = {.upload_bytes_per_second = static_cast<double>(index + 1),
                      .download_bytes_per_second = static_cast<double>(index + 2)}});
    }
    return snapshot;
}

void CheckDirtyDecisions(TestContext& context) {
    context.Check(!FloatingWindow::ShouldInvalidate(4U, 4U, InteractionState::Visible,
                                                    InteractionState::Visible),
                  "identical snapshot stays clean");
    context.Check(FloatingWindow::ShouldInvalidate(4U, 5U, InteractionState::Visible,
                                                   InteractionState::Visible),
                  "new snapshot invalidates");
    context.Check(FloatingWindow::ShouldInvalidate(4U, 4U, InteractionState::Visible,
                                                   InteractionState::EdgeHidden),
                  "interaction change invalidates");
}

void CheckDpiAndWorkArea(TestContext& context) {
    const PixelRect work_area{.left = -1920, .top = 0, .right = 0, .bottom = 1080};
    const PixelRect suggestion{.left = -2100, .top = 900, .right = -1700, .bottom = 1200};
    const auto floating = FloatingWindow::ApplyDpiSuggestion(suggestion, work_area);
    context.Check(floating.left >= work_area.left && floating.right <= work_area.right &&
                      floating.top >= work_area.top && floating.bottom <= work_area.bottom,
                  "DPI suggestion is clamped");
}

void CheckWindowStyles(TestContext& context) {
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_TOOLWINDOW) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_NOACTIVATE) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_LAYERED) != 0U);
    static_cast<void>(context);
}

void CheckCapsuleHitAdapter(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "capsule adapter creates main HWND");
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule, {},
                                       {.left = 200, .top = 100, .right = 210, .bottom = 164},
                                       {.left = 200, .top = 100, .right = 216, .bottom = 164}, {});
    window.Reposition({.left = 200, .top = 100, .right = 210, .bottom = 164});
    const auto hit = window.hit_test_hwnd();
    context.Check(hit != nullptr && ::IsWindow(hit) != FALSE,
                  "capsule adapter creates a hit-testable extension HWND");
    if (hit != nullptr) {
        ::SendMessageW(hit, WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
        ::SendMessageW(hit, WM_MOUSELEAVE, 0U, 0);
        ::SendMessageW(hit, WM_DPICHANGED, 0U, 0);
    }
    context.Check(observer.events.size() >= 3U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::CapsulePointerEnter &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::CapsulePointerLeave &&
                      observer.events[2] == nfm::ui::MonitorPanelEventKind::EnvironmentChanged,
                  "hit extension normalizes pointer and environment messages");
    window.Destroy();
    context.Check(window.hwnd() == nullptr && window.hit_test_hwnd() == nullptr,
                  "capsule adapter destroys both HWNDs");
}

void CheckPanelUsesMonitorPanelSeam(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "panel adapter creates its single HWND");
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Panel, {},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292}, {});
    window.SetMonitorPanelControls(false, true);
    ::SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
    ::SendMessageW(window.hwnd(), WM_MOUSELEAVE, 0U, 0);
    ::SendMessageW(window.hwnd(), WM_RBUTTONUP, 0U, MAKELPARAM(1, 1));
    context.Check(observer.events.size() >= 2U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::PanelPointerEnter &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelPointerLeave,
                  "panel pointer enter and leave reach MonitorPanelBehavior");
    context.Check(observer.context_menu_count == 1,
                  "right-clicking the panel opens the application menu");
    window.Destroy();
}

void CheckSurfaceTransitionRetargetsPointerTracking(TestContext& context) {
    const PixelRect tracked_bounds{.left = 100, .top = 100, .right = 300, .bottom = 200};
    context.Check(
        !FloatingWindow::ShouldDispatchPointerLeave(tracked_bounds, true, {.x = 150, .y = 150}) &&
            FloatingWindow::ShouldDispatchPointerLeave(tracked_bounds, true, {.x = 99, .y = 150}) &&
            !FloatingWindow::ShouldDispatchPointerLeave(tracked_bounds, false, {.x = 0, .y = 0}),
        "stale leave is ignored unless a cursor query proves the tracked surface was "
        "actually exited");

    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "surface-transition adapter creates its HWNDs");
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule, {},
                                       {.left = 200, .top = 100, .right = 210, .bottom = 164},
                                       {.left = 200, .top = 100, .right = 216, .bottom = 164}, {});
    window.Reposition({.left = 200, .top = 100, .right = 210, .bottom = 164});
    ::SendMessageW(window.hit_test_hwnd(), WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Panel, {},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292}, {});
    window.Reposition({.left = 200, .top = 100, .right = 600, .bottom = 292});
    ::SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
    ::SendMessageW(window.hwnd(), WM_MOUSELEAVE, 0U, 0);
    context.Check(observer.events.size() >= 3U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::CapsulePointerEnter &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelPointerEnter &&
                      observer.events[2] == nfm::ui::MonitorPanelEventKind::PanelPointerLeave,
                  "capsule reveal retargets tracking to the panel so its leave can schedule "
                  "auto-hide");
    window.Destroy();
}

void CheckPinControlActivation(TestContext& context) {
    constexpr auto kDpi = 96;
    constexpr auto kWidthPx = 400;
    context.Check(FloatingWindow::HitTestControl({.x = 382, .y = 18}, kWidthPx, kDpi) ==
                          MonitorPanelControl::Pin &&
                      FloatingWindow::HitTestControl({.x = 10, .y = 18}, kWidthPx, kDpi) ==
                          MonitorPanelControl::None,
                  "pin occupies only the rightmost header region");

    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "pin gesture adapter creates its HWND");
    const auto dpi = static_cast<int>(::GetDpiForWindow(window.hwnd()));
    const auto width = nfm::ui::DipToPixels(400, dpi);
    const auto height = nfm::ui::DipToPixels(80, dpi);
    window.SetMonitorPanelPresentation(
        MonitorPanelSurface::Panel, {},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + height},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + height}, {});
    window.SetMonitorPanelControls(false, true);
    window.Reposition({.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + height});
    const auto pin_x = width - nfm::ui::DipToPixels(18, dpi);
    const auto pin_y = nfm::ui::DipToPixels(18, dpi);
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pin_x, pin_y));
    context.Check(observer.events.size() == 1U &&
                      observer.events.back() == nfm::ui::MonitorPanelEventKind::PanelPress,
                  "control press does not activate pin before release");
    ::SendMessageW(window.hwnd(), WM_LBUTTONUP, 0U, MAKELPARAM(pin_x, pin_y));
    context.Check(observer.events.size() == 3U &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelRelease &&
                      observer.events[2] == nfm::ui::MonitorPanelEventKind::PinTogglePressed,
                  "pin activates once on release inside the same target");

    observer.events.clear();
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pin_x, pin_y));
    ::SendMessageW(window.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(pin_x - 40, pin_y + 40));
    ::SendMessageW(window.hwnd(), WM_LBUTTONUP, 0U, MAKELPARAM(pin_x, pin_y));
    context.Check(observer.events.size() == 3U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::PanelPress &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PointerMove &&
                      observer.events[2] == nfm::ui::MonitorPanelEventKind::PanelRelease,
                  "crossing the drag threshold cancels pin activation");

    observer.events.clear();
    window.SetMonitorPanelControls(true, false);
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pin_x, pin_y));
    ::SendMessageW(window.hwnd(), WM_LBUTTONUP, 0U, MAKELPARAM(pin_x, pin_y));
    context.Check(observer.events.size() == 2U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::PanelPress &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelRelease,
                  "disabled pin control does not mutate the saved preference");
    window.Destroy();
}

void CheckRankingScrollInput(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "scrollable panel creates its HWND");
    const auto dpi = static_cast<int>(::GetDpiForWindow(window.hwnd()));
    const auto width = nfm::ui::DipToPixels(400, dpi);
    const auto row = nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kRowDip, dpi);
    const auto header = nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kHeaderRowDip, dpi);
    const auto padding =
        nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kVerticalPaddingDip, dpi);
    const auto panel_height = padding + header + (2 * row) + padding;
    window.SetMonitorPanelPresentation(
        MonitorPanelSurface::Panel, {},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + panel_height},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + panel_height},
        {.left = 200 + padding,
         .top = 100 + padding + header,
         .right = 200 + width - padding,
         .bottom = 100 + panel_height - padding});
    window.Reposition(
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + panel_height});
    window.Show();
    window.SetSnapshot(MakeRows(9), 1U);
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 70));
    context.Check(::GetFocus() == window.hwnd(), "clicking the panel assigns keyboard focus");
    ::SendMessageW(window.hwnd(), WM_KEYDOWN, VK_END, 0);
    context.Check(window.scroll_row_offset() > 0, "End scrolls to the last ranking viewport");
    ::SendMessageW(window.hwnd(), WM_KEYDOWN, VK_HOME, 0);
    context.Check(window.scroll_row_offset() == 0, "Home scrolls to the first ranking row");
    ::SendMessageW(window.hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    context.Check(window.scroll_row_offset() == 1,
                  "mouse wheel and touchpad-compatible wheel input scroll rows");
    auto reordered = std::make_shared<nfm::ranking::RankingSnapshot>(*MakeRows(9));
    std::swap(reordered->applications.at(0), reordered->applications.at(1));
    window.SetSnapshot(reordered, 2U);
    context.Check(window.row_animation_active(),
                  "a new snapshot retargets retained rows from their current positions");
    window.SetSnapshot(nullptr, 3U);
    context.Check(window.scroll_row_offset() == 0,
                  "disconnect clears scroll state before a reconnect warm-up");
    window.Destroy();
}

void CheckApplicationIconFallback(TestContext& context) {
    nfm::ui::ApplicationIconCache cache;
    const auto fallback = cache.Lookup({});
    context.Check(fallback != nullptr && fallback->fallback,
                  "empty/system identities receive the stable application icon fallback");
    cache.Request("Z:\\path-that-does-not-exist\\missing.exe");
    const auto pending = cache.Lookup("Z:\\path-that-does-not-exist\\missing.exe");
    context.Check(pending == fallback,
                  "icon extraction requests return fallback immediately instead of blocking UI");
}

} // namespace

int main() {
    TestContext context;
    CheckDirtyDecisions(context);
    CheckDpiAndWorkArea(context);
    CheckWindowStyles(context);
    CheckCapsuleHitAdapter(context);
    CheckPanelUsesMonitorPanelSeam(context);
    CheckSurfaceTransitionRetargetsPointerTracking(context);
    CheckPinControlActivation(context);
    CheckRankingScrollInput(context);
    CheckApplicationIconFallback(context);
    return context.failures == 0 ? 0 : 1;
}
