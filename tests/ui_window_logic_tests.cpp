#include "network_flow_monitor/ui/floating_window.h"
#include "network_flow_monitor/ui/ranking_popup.h"

#include <windows.h>

#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using nfm::ui::FloatingWindow;
using nfm::ui::InteractionState;
using nfm::ui::PixelRect;
using nfm::ui::RankingPopup;

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
    void OnWindowInvalidated(nfm::ui::WindowKind) noexcept override {}
    void OnWindowPointer(nfm::ui::WindowKind, nfm::ui::InteractionEvent event,
                         nfm::ui::MouseTarget target) noexcept override {
        legacy_pointer_events.emplace_back(event, target);
    }
    void OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind event,
                             nfm::ui::PixelPoint) noexcept override {
        events.push_back(event);
    }

    std::vector<nfm::ui::MonitorPanelEventKind> events;
    std::vector<std::pair<nfm::ui::InteractionEvent, nfm::ui::MouseTarget>> legacy_pointer_events;
};

void CheckDirtyDecisions(TestContext& context) {
    context.Check(!FloatingWindow::ShouldInvalidate(4U, 4U, InteractionState::Visible,
                                                    InteractionState::Visible),
                  "identical floating snapshot stays clean");
    context.Check(FloatingWindow::ShouldInvalidate(4U, 5U, InteractionState::Visible,
                                                   InteractionState::Visible),
                  "new floating snapshot invalidates");
    context.Check(FloatingWindow::ShouldInvalidate(4U, 4U, InteractionState::Visible,
                                                   InteractionState::EdgeHidden),
                  "floating interaction change invalidates");
    context.Check(!RankingPopup::ShouldInvalidate(7U, 7U),
                  "identical ranking snapshot stays clean");
    context.Check(RankingPopup::ShouldInvalidate(7U, 8U), "new ranking snapshot invalidates");
}

void CheckDpiAndWorkArea(TestContext& context) {
    const PixelRect work_area{.left = -1920, .top = 0, .right = 0, .bottom = 1080};
    const PixelRect suggestion{.left = -2100, .top = 900, .right = -1700, .bottom = 1200};
    const auto floating = FloatingWindow::ApplyDpiSuggestion(suggestion, work_area);
    const auto popup = RankingPopup::ApplyDpiSuggestion(suggestion, work_area);
    context.Check(floating.left >= work_area.left && floating.right <= work_area.right &&
                      floating.top >= work_area.top && floating.bottom <= work_area.bottom,
                  "floating DPI suggestion is clamped");
    context.Check(popup == floating, "popup and floating DPI policy agree");
}

void CheckWindowStyles(TestContext& context) {
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_TOOLWINDOW) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_NOACTIVATE) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_LAYERED) != 0U);
    static_assert((RankingPopup::kExtendedStyle & WS_EX_NOACTIVATE) != 0U);
    static_cast<void>(context);
}

void CheckCapsuleHitAdapter(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "floating adapter creates main HWND");
    window.SetMonitorPanelPresentation(nfm::ui::MonitorPanelSurface::Capsule, {},
                                       {.left = 200, .top = 100, .right = 210, .bottom = 164},
                                       {.left = 200, .top = 100, .right = 216, .bottom = 164});
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

void CheckNfmUsesOnlyMonitorPanelSeam(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "NFM adapter creates its HWND");
    window.SetMonitorPanelPresentation(nfm::ui::MonitorPanelSurface::Nfm, {},
                                       {.left = 200, .top = 100, .right = 460, .bottom = 192},
                                       {.left = 200, .top = 100, .right = 460, .bottom = 192});
    ::SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
    ::SendMessageW(window.hwnd(), WM_MOUSELEAVE, 0U, 0);
    context.Check(observer.events.size() >= 2U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::PanelPointerEnter &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelPointerLeave,
                  "NFM pointer enter and leave reach MonitorPanelBehavior");
    context.Check(observer.legacy_pointer_events.empty(),
                  "NFM pointer input does not feed the competing legacy TopA state machine");
    window.Destroy();
}

} // namespace

int main() {
    TestContext context;
    CheckDirtyDecisions(context);
    CheckDpiAndWorkArea(context);
    CheckWindowStyles(context);
    CheckCapsuleHitAdapter(context);
    CheckNfmUsesOnlyMonitorPanelSeam(context);
    return context.failures == 0 ? 0 : 1;
}
