#include "network_flow_monitor/host/coordinator/dock_capsule_coordinator.h"

#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

using namespace nfm::host::coordinator;
using namespace std::chrono_literals;

void TestInitialStateAndGeometry(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.capsule_width_dip = 10;
    cfg.floating.capsule_height_dip = 64;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;
    cfg.floating.anchor_vertical_offset_dip = 100;
    cfg.floating.nfm_pinned = false;

    DockCapsuleCoordinator coord(cfg);
    const auto& s = coord.state();

    ctx.Check(s.presentation_state == PresentationState::CapsuleVisible,
              "Initial presentation state is CapsuleVisible");
    ctx.Check(!s.is_pinned, "Initial pinned is false");
    ctx.Check(s.is_topmost, "Initial topmost is true");
    ctx.Check(s.anchor_side == nfm::config::DockSide::Right, "Initial anchor side is Right");

    // Hit target width is at least 16 DIP (16px at 96 DPI)
    ctx.Check(s.geometry.width_px == 16,
              "Geometry width at 96 DPI is at least 16px inward hit target");
    ctx.Check(s.geometry.height_px == 64, "Geometry height at 96 DPI is 64px");
    ctx.Check(s.geometry.x_px == 1920 - 16, "Anchored to right edge at x = 1904");
    ctx.Check(s.geometry.y_px == 100, "Anchored at y = 100");
}

void TestHoverAndClickTransitions(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.interaction.hover_delay = 350ms;
    cfg.interaction.dismiss_delay = 500ms;

    DockCapsuleCoordinator coord(cfg);

    // 1. Hover Capsule
    coord.on_pointer_enter_capsule();
    ctx.Check(coord.state().is_hover_timer_active, "Hover timer active after enter");

    // Advance 200ms (not yet reached 350ms)
    coord.advance_time(200ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Still CapsuleVisible before 350ms");

    // Advance remaining 150ms -> expands to PanelVisible
    coord.advance_time(150ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Expands to PanelVisible after 350ms hover");
    ctx.Check(coord.state().geometry.width_px == 340, "Panel width is 340px");
    // 2. Pointer leaves capsule to outside
    coord.on_pointer_leave_capsule();
    ctx.Check(coord.state().is_dismiss_timer_active, "Dismiss timer active after leave");

    // Re-enter panel at 250ms -> cancels dismissal
    coord.advance_time(250ms);
    coord.on_pointer_enter_panel();
    ctx.Check(!coord.state().is_dismiss_timer_active, "Dismiss timer canceled on panel re-entry");

    // Leave panel again -> advance 500ms -> collapses to CapsuleVisible
    coord.on_pointer_leave_panel();
    ctx.Check(coord.state().is_dismiss_timer_active, "Dismiss timer active after panel leave");
    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Collapses to CapsuleVisible after 500ms dismiss");

    // 3. Click immediately expands without waiting for hover delay
    coord.on_pointer_click_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Click immediately expands to PanelVisible");
}

void TestPinBehavior(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.interaction.dismiss_delay = 500ms;

    DockCapsuleCoordinator coord(cfg);
    coord.show_monitor();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "PanelVisible after show_monitor");

    // Set pin
    coord.set_pin(true);
    ctx.Check(coord.state().is_pinned, "is_pinned is true");

    // Pointer leaves panel
    coord.on_pointer_leave_panel();
    ctx.Check(!coord.state().is_dismiss_timer_active, "Dismiss timer is NOT active when pinned");

    // Advance time 2000ms
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Panel remains visible when pinned");

    // Unpin while pointer is outside
    coord.set_pin(false);
    ctx.Check(coord.state().is_dismiss_timer_active,
              "Dismiss timer activated on unpin while outside");
    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Dismissed after unpin");
}

void TestDragSnappingAndTieRules(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.capsule_width_dip = 10;
    cfg.floating.capsule_height_dip = 64;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;

    std::vector<MonitorArea> monitors = {
        MonitorArea{
            .monitor_id = "mon1",
            .left_px = 0,
            .top_px = 0,
            .right_px = 1920,
            .bottom_px = 1080,
            .work_left_px = 0,
            .work_top_px = 0,
            .work_right_px = 1920,
            .work_bottom_px = 1040,
            .is_primary = true,
            .dpi = 96,
        },
        MonitorArea{
            .monitor_id = "mon2",
            .left_px = 1920,
            .top_px = 0,
            .right_px = 3840,
            .bottom_px = 1080,
            .work_left_px = 1920,
            .work_top_px = 0,
            .work_right_px = 3840,
            .work_bottom_px = 1040,
            .is_primary = false,
            .dpi = 96,
        },
    };

    DockCapsuleCoordinator coord(cfg, monitors);

    // 1. Small movement (< threshold) should be a click, not drag
    coord.on_pointer_down_capsule(ScreenPoint{1900, 100});
    coord.on_pointer_move(ScreenPoint{1902, 101});
    ctx.Check(!coord.state().is_dragging, "Small movement below threshold is not dragging");
    coord.on_pointer_up(ScreenPoint{1902, 101});
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Pointer up after sub-threshold movement expands as click");

    // Collapse back
    coord.hide_monitor();
    coord.show_monitor();
    coord.on_pointer_leave_panel();
    coord.advance_time(500ms);

    // 2. Drag to left side of primary monitor
    coord.on_pointer_down_capsule(ScreenPoint{1900, 100});
    coord.on_pointer_move(ScreenPoint{500, 200});
    ctx.Check(coord.state().is_dragging, "Dragging is active when moved past threshold");
    ctx.Check(coord.state().geometry.x_px == 500 - (coord.state().geometry.width_px / 2),
              "Window follows pointer while dragging");
    coord.on_pointer_up(ScreenPoint{500, 200});

    ctx.Check(!coord.state().is_dragging, "Dragging ended");
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Left,
              "Snaps to Left side when released left of midpoint");
    ctx.Check(coord.state().anchor_updated, "Anchor update was emitted");
    ctx.Check(coord.state().geometry.x_px == 0, "Geometry x is 0 on Left anchor");

    // 3. Drag to exact midpoint of primary monitor (960) -> tie rule preserves Left
    coord.clear_emitted_updates();
    coord.on_pointer_down_capsule(ScreenPoint{0, 200});
    coord.on_pointer_move(ScreenPoint{960, 300});
    coord.on_pointer_up(ScreenPoint{960, 300});
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Left,
              "Tie-break preserves existing Left side");

    // 4. Drag to second monitor
    coord.clear_emitted_updates();
    coord.on_pointer_down_capsule(ScreenPoint{0, 300});
    coord.on_pointer_move(ScreenPoint{3500, 400});
    coord.on_pointer_up(ScreenPoint{3500, 400});
    ctx.Check(coord.state().anchor_monitor_id == "mon2", "Anchor monitor updated to mon2");
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Right,
              "Snaps to Right side of mon2");
    ctx.Check(coord.state().geometry.x_px == 3840 - 16,
              "Geometry positioned at right edge of mon2");

    // 5. Cancel drag restores without updating anchor
    coord.clear_emitted_updates();
    coord.on_pointer_down_capsule(ScreenPoint{3840 - 16, 400});
    coord.on_pointer_move(ScreenPoint{100, 100});
    coord.on_cancel_drag();
    ctx.Check(!coord.state().is_dragging, "Drag canceled");
    ctx.Check(coord.state().anchor_monitor_id == "mon2", "Anchor monitor unchanged on cancel");
}

void TestConfiguredSystemDragThreshold(TestContext& ctx) {
    DockCapsuleCoordinator coord;
    coord.update_drag_threshold(DragThresholdPx{.horizontal_px = 10, .vertical_px = 12});
    coord.on_pointer_down_capsule(ScreenPoint{100, 100});
    coord.on_pointer_move(ScreenPoint{109, 111});
    ctx.Check(!coord.state().is_dragging, "Movement below system thresholds remains a click");
    coord.on_pointer_move(ScreenPoint{110, 111});
    ctx.Check(coord.state().is_dragging, "Horizontal system drag threshold starts dragging");
}

void TestContextMenuPausesDismiss(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.interaction.dismiss_delay = 500ms;

    DockCapsuleCoordinator coord(cfg);
    coord.on_pointer_click_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible, "PanelVisible");

    // Open context menu
    coord.on_context_menu_opened();
    ctx.Check(coord.state().is_context_menu_open, "Context menu open");

    // Pointer leaves panel while context menu is open
    coord.on_pointer_leave_panel();
    ctx.Check(!coord.state().is_dismiss_timer_active,
              "Dismiss timer paused while context menu is open");

    // Advance 2000ms
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Panel remains visible with context menu open");

    // Close context menu with cursor outside
    coord.on_context_menu_closed(false);
    ctx.Check(!coord.state().is_context_menu_open, "Context menu closed");
    ctx.Check(coord.state().is_dismiss_timer_active,
              "Dismiss timer started after menu closed outside");

    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Dismissed after dismiss delay");

    // Right-click context menu on capsule pauses hover expansion
    coord.on_pointer_enter_capsule();
    ctx.Check(coord.state().is_hover_timer_active, "Hover timer starts on capsule entry");
    coord.on_context_menu_opened();
    ctx.Check(!coord.state().is_hover_timer_active,
              "Hover timer canceled when capsule context menu opens");
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Capsule remains in capsule state while context menu is open");
    coord.on_context_menu_closed(true);
    coord.advance_time(2000ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Closing context menu keeps capsule state until a new hover interaction");
}

void TestAutoHide(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.auto_hide_enabled = true;
    cfg.floating.auto_hide_delay = 3000ms;

    DockCapsuleCoordinator coord(cfg);
    coord.on_pointer_enter_capsule();
    coord.on_pointer_leave_capsule();

    ctx.Check(coord.state().is_auto_hide_timer_active,
              "Auto-hide timer active after pointer leave");

    coord.advance_time(250ms);
    ctx.Check(!coord.state().is_auto_hidden, "Not yet auto-hidden at 250ms");

    coord.advance_time(250ms);
    ctx.Check(coord.state().is_auto_hidden, "Auto-hidden at 500ms");
    ctx.Check(
        coord.state().geometry.x_px == 1920 - 16,
        "Auto-hidden capsule retains 16px inward hit surface while UI retracts to edge strip");
    // Pointer enters capsule wakes it up
    coord.on_pointer_enter_capsule();
    ctx.Check(!coord.state().is_auto_hidden, "Auto-hide cleared on pointer enter");
    ctx.Check(coord.state().geometry.x_px == 1920 - 16,
              "Pointer entry restores the full inward hit surface");

    // Hover expands to panel, then leave panel -> dismisses to capsule -> auto-hides again
    coord.advance_time(500ms);
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Hover expands capsule to panel");
    coord.on_pointer_leave_panel();
    coord.advance_time(600ms);
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Panel dismisses back to capsule");
    ctx.Check(coord.state().is_auto_hide_timer_active,
              "Auto-hide timer restarts automatically when panel dismisses to capsule");
    coord.advance_time(500ms);
    ctx.Check(coord.state().is_auto_hidden, "Capsule auto-hides again after panel dismissal");

    // Tray show_monitor clears auto-hide and opens panel; show_capsule collapses back immediately
    coord.show_monitor();
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "show_monitor transitions to PanelVisible");
    ctx.Check(!coord.state().is_auto_hidden, "show_monitor clears auto-hidden state");
    coord.show_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "show_capsule immediately collapses unpinned panel back to capsule");
}

void TestFullscreenTopmostYield(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.nfm_pinned = true;
    cfg.floating.ignore_hover_when_fullscreen = true;
    cfg.floating.hide_capsule_when_fullscreen = false;
    cfg.interaction.hover_delay = std::chrono::milliseconds{350};

    DockCapsuleCoordinator coord(cfg);
    ctx.Check(coord.state().is_topmost, "Topmost true by default");
    ctx.Check(coord.state().is_pinned, "Pinned initially");
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "PanelVisible initially when pinned");

    // Fullscreen window active on same monitor: breaks pin, collapses to capsule, enables
    // click-through
    coord.set_fullscreen_active_on_same_monitor(true);
    ctx.Check(!coord.state().is_pinned, "Fullscreen breaks pin state");
    ctx.Check(coord.state().pin_updated, "Fullscreen emits pin_updated");
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Fullscreen collapses panel back to capsule");
    ctx.Check(coord.state().is_click_through,
              "Capsule enables click-through when fullscreen active and ignore_hover is true");
    ctx.Check(coord.state().is_topmost, "Capsule stays topmost when not fullscreen-hidden");

    coord.on_pointer_enter_capsule();
    ctx.Check(!coord.state().is_hover_timer_active,
              "Hover timer ignored during fullscreen click-through");
    coord.on_pointer_click_capsule();
    ctx.Check(coord.state().presentation_state == PresentationState::CapsuleVisible,
              "Click ignored during fullscreen click-through");

    // Enable hide_capsule_when_fullscreen
    cfg.floating.nfm_pinned = false;
    cfg.floating.hide_capsule_when_fullscreen = true;
    coord.update_config(cfg);
    ctx.Check(coord.state().is_fullscreen_hidden,
              "Capsule marked fullscreen-hidden when hide_capsule_when_fullscreen is enabled");
    ctx.Check(!coord.state().is_click_through,
              "Click-through false when capsule is completely hidden");

    // Fullscreen window inactive
    coord.set_fullscreen_active_on_same_monitor(false);
    ctx.Check(coord.state().is_topmost, "Topmost restored when fullscreen inactive");
    ctx.Check(!coord.state().is_fullscreen_hidden,
              "Fullscreen hidden cleared when fullscreen inactive");
    ctx.Check(!coord.state().is_click_through, "Click-through cleared when fullscreen inactive");

    // If both fullscreen policies are disabled in config
    cfg.floating.ignore_hover_when_fullscreen = false;
    cfg.floating.hide_capsule_when_fullscreen = false;
    coord.update_config(cfg);
    coord.set_fullscreen_active_on_same_monitor(true);
    ctx.Check(!coord.state().is_click_through,
              "Click-through NOT enabled when ignore_hover_when_fullscreen is disabled");
    coord.on_pointer_enter_capsule();
    ctx.Check(coord.state().is_hover_timer_active,
              "Hover timer starts when ignore_hover_when_fullscreen is disabled");
    // Simulate periodic clock tick calling set_fullscreen_active_on_same_monitor(true)
    coord.set_fullscreen_active_on_same_monitor(true);
    coord.advance_time(std::chrono::milliseconds{360});
    ctx.Check(coord.state().presentation_state == PresentationState::PanelVisible,
              "Hover expands to panel during fullscreen when lock is disabled");
}

void TestDpiScalingAndDisplayChangeFallback(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.capsule_width_dip = 10;
    cfg.floating.capsule_height_dip = 64;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;
    cfg.floating.anchor_vertical_offset_dip = 100;

    std::vector<MonitorArea> monitors = {
        MonitorArea{
            .monitor_id = "4k_mon",
            .left_px = 0,
            .top_px = 0,
            .right_px = 3840,
            .bottom_px = 2160,
            .work_left_px = 0,
            .work_top_px = 0,
            .work_right_px = 3840,
            .work_bottom_px = 2100,
            .is_primary = true,
            .dpi = 144, // 1.5x scaling
        },
    };

    DockCapsuleCoordinator coord(cfg, monitors);
    const auto& s = coord.state();

    // 16 DIP * 1.5 = 24 px
    ctx.Check(s.geometry.width_px == 24, "Scaled width is 24px at 144 DPI");
    // 64 DIP * 1.5 = 96 px
    ctx.Check(s.geometry.height_px == 96, "Scaled height is 96px at 144 DPI");
    ctx.Check(s.geometry.x_px == 3840 - 24, "Scaled x position is 3816");
    // 100 DIP * 1.5 = 150 px
    ctx.Check(s.geometry.y_px == 150, "Scaled y position is 150");

    // Display unplug fallback: replace monitors with new 1080p monitor
    std::vector<MonitorArea> new_monitors = {
        MonitorArea{
            .monitor_id = "new_mon",
            .left_px = 0,
            .top_px = 0,
            .right_px = 1920,
            .bottom_px = 1080,
            .work_left_px = 0,
            .work_top_px = 0,
            .work_right_px = 1920,
            .work_bottom_px = 1040,
            .is_primary = true,
            .dpi = 96,
        },
    };
    coord.update_monitors(new_monitors);
    ctx.Check(coord.state().geometry.width_px == 16, "Width updated to 16px on new 96 DPI monitor");
    ctx.Check(coord.state().geometry.x_px == 1920 - 16,
              "X position updated to 1904 on new monitor");
}

void TestAutoHideTaskbarEdgeReservation(TestContext& ctx) {
    nfm::config::AppConfig cfg;
    cfg.floating.anchor_side = nfm::config::DockSide::Right;
    std::vector<MonitorArea> monitors = {MonitorArea{
        .monitor_id = "taskbar-monitor",
        .left_px = 0,
        .top_px = 0,
        .right_px = 1920,
        .bottom_px = 1080,
        .work_left_px = 0,
        .work_top_px = 0,
        .work_right_px = 1920,
        .work_bottom_px = 1080,
        .auto_hide_right_reservation_px = 4,
        .is_primary = true,
        .dpi = 96,
    }};
    DockCapsuleCoordinator coord(cfg, monitors);
    ctx.Check(coord.state().geometry.x_px == 1900,
              "Right auto-hide taskbar edge keeps its four-pixel reveal reservation");
}

void TestPointerMoveAutoRecoversLostPointerUp(TestContext& ctx) {
    DockCapsuleCoordinator coord;
    coord.on_pointer_down_capsule(ScreenPoint{1900, 100});
    coord.on_pointer_move(ScreenPoint{500, 200});
    ctx.Check(coord.state().is_dragging, "Dragging active after pointer move past threshold");

    // If WM_LBUTTONUP was lost (e.g., focus stolen) and pointer later moves without button pressed,
    // calling on_pointer_up snaps cleanly back to edge and exits dragging state.
    coord.on_pointer_up(ScreenPoint{500, 200});
    ctx.Check(!coord.state().is_dragging, "Dragging cleared after recovery pointer up");
    ctx.Check(coord.state().anchor_side == nfm::config::DockSide::Left,
              "Snapped to Left edge on recovery");
}

} // namespace

int main() {
    TestContext ctx;
    TestInitialStateAndGeometry(ctx);
    TestHoverAndClickTransitions(ctx);
    TestPinBehavior(ctx);
    TestDragSnappingAndTieRules(ctx);
    TestConfiguredSystemDragThreshold(ctx);
    TestContextMenuPausesDismiss(ctx);
    TestAutoHide(ctx);
    TestFullscreenTopmostYield(ctx);
    TestDpiScalingAndDisplayChangeFallback(ctx);
    TestAutoHideTaskbarEdgeReservation(ctx);
    TestPointerMoveAutoRecoversLostPointerUp(ctx);

    if (ctx.failures() > 0) {
        std::cerr << ctx.failures() << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All DockCapsuleCoordinator tests passed.\n";
    return 0;
}
