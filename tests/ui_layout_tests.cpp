#include "network_flow_monitor/ui/dpi_layout.h"
#include "network_flow_monitor/ui/interaction_state.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

using namespace std::chrono_literals;
using nfm::ui::ChoosePopupPlacement;
using nfm::ui::ClampRectToWorkArea;
using nfm::ui::InteractionEvent;
using nfm::ui::InteractionOptions;
using nfm::ui::InteractionState;
using nfm::ui::MouseTarget;
using nfm::ui::PixelRect;
using nfm::ui::PixelSize;
using nfm::ui::PopupPlacement;
using nfm::ui::SnapRectToWorkArea;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

void CheckDpiAndRects(TestContext& context) {
    context.Check(nfm::ui::DipToPixels(12.0, 96) == 12, "96 DPI conversion");
    context.Check(nfm::ui::DipToPixels(12.0, 144) == 18, "144 DPI conversion");
    context.Check(nfm::ui::PixelsToDip(18, 144) == 12.0, "inverse DPI conversion");

    const PixelRect work_area{.left = -1920, .top = 0, .right = 0, .bottom = 1080};
    const PixelRect outside{.left = -2100, .top = 900, .right = -1700, .bottom = 1200};
    const auto clamped = ClampRectToWorkArea(outside, work_area);
    context.Check(clamped == PixelRect{.left = -1920, .top = 780, .right = -1520, .bottom = 1080},
                  "negative-coordinate rectangle is clamped");

    const auto snapped = SnapRectToWorkArea(
        PixelRect{.left = -1910, .top = 20, .right = -1810, .bottom = 120}, work_area, 12);
    context.Check(snapped.left == work_area.left, "left edge snaps within distance");
    context.Check(snapped.Width() == 100 && snapped.Height() == 100,
                  "snapping preserves dimensions");
}

void CheckPopupPlacement(TestContext& context) {
    const PixelRect work_area{.left = 0, .top = 0, .right = 1920, .bottom = 1080};
    const auto below =
        ChoosePopupPlacement(PixelRect{.left = 900, .top = 100, .right = 1020, .bottom = 140},
                             PixelSize{.width = 300, .height = 200}, work_area);
    context.Check(below.placement == PopupPlacement::Below && below.rect.top == 140,
                  "popup prefers the complete below candidate");

    const auto top_right =
        ChoosePopupPlacement(PixelRect{.left = 1800, .top = 950, .right = 1920, .bottom = 1000},
                             PixelSize{.width = 300, .height = 200}, work_area);
    context.Check(
        top_right.rect.left >= work_area.left && top_right.rect.right <= work_area.right &&
            top_right.rect.top >= work_area.top && top_right.rect.bottom <= work_area.bottom,
        "popup is clamped when no candidate fully fits");
}

void CheckInteraction(TestContext& context) {
    const InteractionOptions options{.hover_delay = 350ms,
                                     .dismiss_delay = 250ms,
                                     .auto_hide_delay = 500ms,
                                     .auto_hide_enabled = true};
    const auto start = std::chrono::steady_clock::time_point{};
    nfm::ui::InteractionStateMachine state;
    context.Check(state.state() == InteractionState::Hidden, "initial interaction state");
    state.Handle(InteractionEvent::Toggle, start, options);
    context.Check(state.state() == InteractionState::Visible, "toggle shows meter");
    state.Handle(InteractionEvent::PointerEnter, start, options, MouseTarget::Anchor);
    state.Tick(start + 349ms, options);
    context.Check(state.state() == InteractionState::Visible, "hover delay is respected");
    state.Tick(start + 350ms, options);
    context.Check(state.state() == InteractionState::RankingVisible,
                  "hover opens ranking after delay");
    state.Handle(InteractionEvent::PointerLeave, start + 350ms, options, MouseTarget::Anchor);
    state.Tick(start + 599ms, options);
    context.Check(state.state() == InteractionState::RankingVisible,
                  "dismiss delay keeps ranking open");
    state.Tick(start + 600ms, options);
    context.Check(state.state() == InteractionState::Visible, "ranking closes after dismiss delay");
    state.Tick(start + 1100ms, options);
    context.Check(state.state() == InteractionState::EdgeHidden,
                  "auto-hide moves meter to reveal strip");
    state.Handle(InteractionEvent::PointerEnter, start + 1100ms, options, MouseTarget::RevealStrip);
    context.Check(state.state() == InteractionState::Visible, "reveal strip restores meter");
    state.Handle(InteractionEvent::DataUpdated, start + 1100ms, options);
    context.Check((state.last_dirty_flags() & nfm::ui::kDirtyData) != 0U,
                  "data update marks only data-related redraw");
}

} // namespace

int main() {
    TestContext context;
    CheckDpiAndRects(context);
    CheckPopupPlacement(context);
    CheckInteraction(context);
    return context.failures == 0 ? 0 : 1;
}
