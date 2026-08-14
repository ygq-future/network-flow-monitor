#include "network_flow_monitor/ui/monitor_panel_behavior.h"

#include <iostream>

namespace {
using namespace nfm::ui;
struct Test {
    int failed{};
    void Check(bool value, const char* name) {
        if (!value) {
            std::cerr << "FAIL: " << name << '\n';
            ++failed;
        }
    }
};
MonitorPanelEnvironment Environment() {
    return {.monitors = {{.id = "left",
                          .rect = {.left = -1920, .top = 0, .right = 0, .bottom = 1080},
                          .dpi = 96},
                         {.id = "primary",
                          .rect = {.left = 0, .top = 0, .right = 1920, .bottom = 1040},
                          .dpi = 144,
                          .primary = true}}};
}
void Docking(Test& test) {
    auto state = MonitorPanelState{};
    const auto env = Environment();
    const MonitorPanelConfig config{};
    auto start = MonitorPanelBehavior::Transition(
        state,
        {.kind = MonitorPanelEventKind::Start,
         .anchor = PersistedMonitorAnchor{.monitor_id = "left",
                                          .side = DockSide::Left,
                                          .vertical_offset_dip = 20}},
        env, config);
    auto press = MonitorPanelBehavior::Transition(
        start.state, {.kind = MonitorPanelEventKind::PanelPress, .point = {.x = -1915, .y = 35}},
        env, config);
    auto moving = MonitorPanelBehavior::Transition(
        press.state, {.kind = MonitorPanelEventKind::PointerMove, .point = {.x = 1900, .y = 800}},
        env, config);
    test.Check(moving.state.surface == MonitorPanelSurface::Capsule && moving.state.dragging,
               "capsule stays collapsed while dragging");
    test.Check(moving.target_bounds.top == 785,
               "drag target follows the pointer using the original grab offset");
    test.Check(moving.target_bounds.left == 1895,
               "drag remains free from edge docking until pointer release");
    auto released = MonitorPanelBehavior::Transition(
        moving.state, {.kind = MonitorPanelEventKind::PanelRelease, .point = {.x = 1900, .y = 800}},
        env, config);
    test.Check(released.state.anchor.monitor_id == "primary" &&
                   released.state.anchor.side == DockSide::Right,
               "release monitor and nearest horizontal edge win");
    test.Check(released.state.bounds.left == released.state.bounds.right - DipToPixels(10, 144),
               "capsule keeps configured width");
    test.Check(released.state.bounds.top == 785,
               "persisted vertical offset describes the window top rather than the pointer");
    test.Check(released.effects.size() == 1U &&
                   released.effects.front().kind == MonitorPanelEffectKind::PersistAnchor,
               "completed drag persists once");
}
void NfmDragAndCancel(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{.pinned = true};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    test.Check(state.surface == MonitorPanelSurface::Nfm, "pinned startup restores NFM only");
    auto press = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::PanelPress, .point = {.x = 1600, .y = 120}}, env,
        config);
    auto moving = MonitorPanelBehavior::Transition(
        press.state, {.kind = MonitorPanelEventKind::PointerMove, .point = {.x = -1000, .y = 500}},
        env, config);
    auto cancelled = MonitorPanelBehavior::Transition(
        moving.state, {.kind = MonitorPanelEventKind::DragCancelled}, env, config);
    test.Check(cancelled.state.anchor == state.anchor && cancelled.target_bounds == state.bounds,
               "cancelled NFM drag restores its original anchor");
    test.Check(cancelled.effects.empty(), "cancelled drag does not persist");
}
void Capsule(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    auto hover = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::CapsulePointerEnter}, env, config);
    test.Check(hover.next_deadline == config.hover_delay, "hover schedules explicit deadline");
    auto expanded = MonitorPanelBehavior::Transition(
        hover.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(expanded.state.surface == MonitorPanelSurface::Nfm, "hover deadline expands nfm");
    test.Check(expanded.target_bounds.Width() > state.bounds.Width() &&
                   expanded.target_bounds.Height() > state.bounds.Height(),
               "expansion returns an NFM-sized layout target");
    constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
    const auto rates = MonitorPanelBehavior::MapCapsuleRates(kGiB, 0U);
    test.Check(rates.download.fill == 1.0F && rates.upload.fill == 0.0F,
               "rate tracks saturate independently");
}
void MissingMonitorFallback(Test& test) {
    auto env = Environment();
    const MonitorPanelConfig config{};
    const auto state = MonitorPanelBehavior::Transition(
                           {},
                           {.kind = MonitorPanelEventKind::Start,
                            .anchor = PersistedMonitorAnchor{.monitor_id = "left",
                                                             .side = DockSide::Left,
                                                             .vertical_offset_dip = 50}},
                           env, config)
                           .state;
    env.monitors.erase(env.monitors.begin());
    const auto changed = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::EnvironmentChanged}, env, config);
    test.Check(changed.state.anchor.monitor_id == "primary",
               "missing display falls back to primary");
    test.Check(changed.effects.size() == 1U &&
                   changed.effects.front().kind == MonitorPanelEffectKind::PersistAnchor,
               "fallback anchor is persisted once");
}
void SmallWorkAreaAndTie(Test& test) {
    const MonitorPanelEnvironment env{
        .monitors = {{.id = "tiny",
                      .rect = {.left = 0, .top = 20, .right = 6, .bottom = 55},
                      .dpi = 96,
                      .primary = true}}};
    const MonitorPanelConfig config{};
    auto state = MonitorPanelBehavior::Transition(
                     {},
                     {.kind = MonitorPanelEventKind::Start,
                      .anchor = PersistedMonitorAnchor{.monitor_id = "tiny",
                                                       .side = DockSide::Left,
                                                       .vertical_offset_dip = 100}},
                     env, config)
                     .state;
    test.Check(state.bounds == env.monitors.front().rect,
               "oversized capsule is clamped to an unusually small work area");
    auto press = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::PanelPress, .point = {.x = 3, .y = 30}}, env,
        config);
    auto moved = MonitorPanelBehavior::Transition(
        press.state, {.kind = MonitorPanelEventKind::PointerMove, .point = {.x = 20, .y = 30}}, env,
        config);
    const auto released = MonitorPanelBehavior::Transition(
        moved.state, {.kind = MonitorPanelEventKind::PanelRelease, .point = {.x = 3, .y = 30}}, env,
        config);
    test.Check(released.state.anchor.side == DockSide::Left,
               "equal edge distances preserve the previous side");
}
void FirstUseAndHitBounds(Test& test) {
    const MonitorPanelEnvironment env{
        .monitors = {{.id = "only",
                      .rect = {.left = 0, .top = 0, .right = 1000, .bottom = 800},
                      .dpi = 192,
                      .primary = true}}};
    const MonitorPanelConfig config{.capsule_width_dip = 8, .capsule_height_dip = 40};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    test.Check(state.bounds.Width() == 16 && state.bounds.Height() == 80,
               "minimum capsule dimensions scale with DPI");
    const auto layout = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::EnvironmentChanged}, env, config);
    test.Check(layout.hit_bounds.Width() == 32,
               "capsule pointer target extends inward to sixteen DIP");
    auto press = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::PanelPress, .point = {.x = 992, .y = 20}}, env,
        config);
    auto moved = MonitorPanelBehavior::Transition(
        press.state, {.kind = MonitorPanelEventKind::PointerMove, .point = {.x = 500, .y = 20}},
        env, config);
    const auto released = MonitorPanelBehavior::Transition(
        moved.state, {.kind = MonitorPanelEventKind::PanelRelease, .point = {.x = 500, .y = 20}},
        env, config);
    test.Check(released.state.anchor.side == DockSide::Right,
               "first-use equal edge release defaults to the right");
    const MonitorPanelConfig maximum{.capsule_width_dip = 24, .capsule_height_dip = 160};
    const auto maximum_state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, maximum);
    test.Check(maximum_state.state.bounds.Width() == 48 &&
                   maximum_state.state.bounds.Height() == 320,
               "maximum capsule dimensions scale with DPI");
    auto click_press = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::PanelPress, .point = {.x = 992, .y = 20}}, env,
        config);
    auto click_release = MonitorPanelBehavior::Transition(
        click_press.state,
        {.kind = MonitorPanelEventKind::PanelRelease, .point = {.x = 992, .y = 20}}, env, config);
    test.Check(click_release.state.surface == MonitorPanelSurface::Nfm &&
                   click_release.effects.empty(),
               "capsule click expands immediately without persistence");
}
void AutoHideRevealReservation(Test& test) {
    for (const auto side : {DockSide::Left, DockSide::Right}) {
        MonitorPanelEnvironment env{
            .monitors = {{.id = "tiny",
                          .rect = {.left = 10, .top = 0, .right = 16, .bottom = 40},
                          .dpi = 96,
                          .primary = true,
                          .auto_hide_reveal_on_left = side == DockSide::Left,
                          .auto_hide_reveal_on_right = side == DockSide::Right}}};
        const MonitorPanelConfig config{.capsule_width_dip = 24, .capsule_height_dip = 160};
        const auto state = MonitorPanelBehavior::Transition(
            {},
            {.kind = MonitorPanelEventKind::Start,
             .anchor = PersistedMonitorAnchor{.monitor_id = "tiny", .side = side}},
            env, config);
        test.Check(state.state.bounds.Width() == 4 &&
                       state.state.bounds.left >= env.monitors.front().rect.left &&
                       state.state.bounds.right <= env.monitors.front().rect.right,
                   "oversized capsule reserves two pixels for same-edge app bar");
        test.Check(side == DockSide::Left ? state.state.bounds.left == 12
                                          : state.state.bounds.right == 14,
                   "system reveal strip remains exposed on the dock edge");
    }
}
void TimingAndThresholds(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{.minimum_drag_distance_x_pixels = 8,
                                    .minimum_drag_distance_y_pixels = 3,
                                    .auto_hide_delay = std::chrono::milliseconds{700}};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    auto hover = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::CapsulePointerEnter}, env, config);
    auto left = MonitorPanelBehavior::Transition(
        hover.state, {.kind = MonitorPanelEventKind::CapsulePointerLeave}, env, config);
    auto stale_deadline = MonitorPanelBehavior::Transition(
        left.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(stale_deadline.state.surface == MonitorPanelSurface::Capsule,
               "leaving cancels capsule hover expansion");
    auto press = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::PanelPress, .point = {.x = 1915, .y = 10}}, env,
        config);
    auto below = MonitorPanelBehavior::Transition(
        press.state, {.kind = MonitorPanelEventKind::PointerMove, .point = {.x = 1922, .y = 12}},
        env, config);
    test.Check(!below.state.dragging, "movement below both axis thresholds remains a click");
    auto vertical = MonitorPanelBehavior::Transition(
        below.state, {.kind = MonitorPanelEventKind::PointerMove, .point = {.x = 1922, .y = 13}},
        env, config);
    test.Check(vertical.state.dragging, "vertical system threshold is independent");

    auto nfm = MonitorPanelBehavior::Transition(state, {.kind = MonitorPanelEventKind::ShowNfm},
                                                env, config);
    auto leave = MonitorPanelBehavior::Transition(
        nfm.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, config);
    test.Check(leave.next_deadline == config.auto_hide_delay,
               "unpinned NFM leave schedules model-owned auto hide");
    auto reentered = MonitorPanelBehavior::Transition(
        leave.state, {.kind = MonitorPanelEventKind::PanelPointerEnter}, env, config);
    auto stale = MonitorPanelBehavior::Transition(
        reentered.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(stale.state.surface == MonitorPanelSurface::Nfm,
               "panel re-entry cancels a stale auto-hide deadline");
    auto hidden = MonitorPanelBehavior::Transition(
        leave.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(hidden.state.surface == MonitorPanelSurface::Capsule,
               "auto-hide deadline returns NFM to capsule");
    const MonitorPanelConfig pinned{.pinned = true};
    auto pinned_state = MonitorPanelBehavior::Transition(
        nfm.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, pinned);
    test.Check(!pinned_state.next_deadline && !pinned_state.state.auto_hide_pending,
               "pin prevents model auto hide");
}

void ContextMenuPausesAutoHide(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{};
    auto nfm =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::ShowNfm}, env, config);
    auto leave = MonitorPanelBehavior::Transition(
        nfm.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, config);
    auto menu = MonitorPanelBehavior::Transition(
        leave.state, {.kind = MonitorPanelEventKind::ContextMenuOpened}, env, config);
    auto expired = MonitorPanelBehavior::Transition(
        menu.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(expired.state.surface == MonitorPanelSurface::Nfm,
               "a deadline cannot collapse NFM while its context menu is open");
    auto closed_outside = MonitorPanelBehavior::Transition(
        expired.state,
        {.kind = MonitorPanelEventKind::ContextMenuClosed, .point = {.x = -100, .y = -100}}, env,
        config);
    test.Check(closed_outside.next_deadline == config.auto_hide_delay,
               "closing a menu outside NFM restarts one auto-hide delay");
    auto collapsed = MonitorPanelBehavior::Transition(
        closed_outside.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(collapsed.state.surface == MonitorPanelSurface::Capsule,
               "the restarted auto-hide delay returns NFM to its capsule");

    auto capsule_hover = MonitorPanelBehavior::Transition(
        collapsed.state, {.kind = MonitorPanelEventKind::CapsulePointerEnter}, env, config);
    auto capsule_menu = MonitorPanelBehavior::Transition(
        capsule_hover.state, {.kind = MonitorPanelEventKind::ContextMenuOpened}, env, config);
    auto stale_capsule_deadline = MonitorPanelBehavior::Transition(
        capsule_menu.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(stale_capsule_deadline.state.surface == MonitorPanelSurface::Capsule,
               "opening a capsule menu cancels its pending hover expansion");
}
void EnvironmentAndPersistenceFeedback(Test& test) {
    auto env = Environment();
    const MonitorPanelConfig config{};
    auto state = MonitorPanelBehavior::Transition(
                     {},
                     {.kind = MonitorPanelEventKind::Start,
                      .anchor = PersistedMonitorAnchor{.monitor_id = "primary",
                                                       .side = DockSide::Right,
                                                       .vertical_offset_dip = 100}},
                     env, config)
                     .state;
    const auto original_top = state.bounds.top;
    env.monitors.back().dpi = 192;
    auto dpi_changed = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::EnvironmentChanged}, env, config);
    test.Check(dpi_changed.state.bounds.top != original_top && dpi_changed.effects.empty(),
               "DPI change recomputes pixels without changing the persisted DIP anchor");
    env.monitors.erase(env.monitors.begin());
    auto reconnect = MonitorPanelBehavior::Transition(
        dpi_changed.state, {.kind = MonitorPanelEventKind::EnvironmentChanged}, env, config);
    test.Check(reconnect.state.anchor.monitor_id == "primary" && reconnect.effects.empty(),
               "unrelated display reconnect does not jump or persist the panel");
    auto failed = MonitorPanelBehavior::Transition(
        reconnect.state, {.kind = MonitorPanelEventKind::PersistFailed}, env, config);
    test.Check(failed.state.anchor == reconnect.state.anchor && failed.state.persistence_failed &&
                   failed.effects.size() == 1U &&
                   failed.effects.front().kind == MonitorPanelEffectKind::ReportPersistenceFailure,
               "save failure retains the current anchor and requests diagnostic reporting");
    auto succeeded = MonitorPanelBehavior::Transition(
        failed.state, {.kind = MonitorPanelEventKind::PersistSucceeded}, env, config);
    test.Check(!succeeded.state.persistence_failed,
               "successful persistence feedback clears the failure marker");
}
void RateMapping(Test& test) {
    constexpr std::uint64_t kKiB = 1024ULL;
    constexpr std::uint64_t kMiB = kKiB * 1024ULL;
    constexpr std::uint64_t kGiB = kMiB * 1024ULL;
    const auto zero = MonitorPanelBehavior::MapCapsuleRates(0U, 0U);
    const auto byte = MonitorPanelBehavior::MapCapsuleRates(1U, 1U);
    const auto kib = MonitorPanelBehavior::MapCapsuleRates(kKiB, kKiB);
    const auto mib = MonitorPanelBehavior::MapCapsuleRates(kMiB, kMiB);
    const auto gib = MonitorPanelBehavior::MapCapsuleRates(kGiB, kGiB * 2U);
    test.Check(zero.download.fill == 0.0F && zero.upload.fill == 0.0F,
               "zero rates produce empty tracks");
    test.Check(byte.download.fill < kib.download.fill && kib.download.fill < mib.download.fill &&
                   mib.download.fill < gib.download.fill,
               "rate fill is monotonic across Byte KiB MiB and GiB");
    test.Check(gib.download.fill == 1.0F && gib.upload.fill == 1.0F, "GiB and above saturate");
    test.Check(zero.download.color == 0x19D8E6U && zero.upload.color == 0xF4D03FU &&
                   gib.download.color == 0x8146B8U && gib.upload.color == 0xD93636U,
               "download and upload retain their independent endpoint palettes");
    const auto download_only = MonitorPanelBehavior::MapCapsuleRates(kMiB, 0U);
    const auto upload_only = MonitorPanelBehavior::MapCapsuleRates(0U, kMiB);
    test.Check(download_only.download.fill > 0.0F && download_only.upload.fill == 0.0F &&
                   upload_only.download.fill == 0.0F && upload_only.upload.fill > 0.0F,
               "single-direction traffic affects only its own track");
}
} // namespace
int main() {
    Test test;
    Docking(test);
    NfmDragAndCancel(test);
    Capsule(test);
    MissingMonitorFallback(test);
    SmallWorkAreaAndTie(test);
    FirstUseAndHitBounds(test);
    AutoHideRevealReservation(test);
    TimingAndThresholds(test);
    ContextMenuPausesAutoHide(test);
    EnvironmentAndPersistenceFeedback(test);
    RateMapping(test);
    return test.failed == 0 ? 0 : 1;
}
