#include "network_flow_monitor/ui/floating_window.h"
#include "network_flow_monitor/ui/ranking_popup.h"

#include <windows.h>

#include <iostream>
#include <memory>
#include <string_view>

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

    const auto hidden_left = FloatingWindow::ComputeEdgeHiddenRect(
        PixelRect{.left = 12, .top = 20, .right = 332, .bottom = 84},
        PixelRect{.left = 0, .top = 0, .right = 1920, .bottom = 1080}, 4);
    context.Check(hidden_left.left == 0 && hidden_left.right == 4,
                  "edge hiding leaves a left reveal strip");

    const auto hidden_right = FloatingWindow::ComputeEdgeHiddenRect(
        PixelRect{.left = 1588, .top = 20, .right = 1908, .bottom = 84},
        PixelRect{.left = 0, .top = 0, .right = 1920, .bottom = 1080}, 4);
    context.Check(hidden_right.left == 1916 && hidden_right.right == 1920,
                  "edge hiding leaves a right reveal strip");
}

void CheckWindowStyles(TestContext& context) {
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_TOOLWINDOW) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_NOACTIVATE) != 0U);
    static_assert((RankingPopup::kExtendedStyle & WS_EX_NOACTIVATE) != 0U);
    static_cast<void>(context);
}

} // namespace

int main() {
    TestContext context;
    CheckDirtyDecisions(context);
    CheckDpiAndWorkArea(context);
    CheckWindowStyles(context);
    return context.failures == 0 ? 0 : 1;
}
