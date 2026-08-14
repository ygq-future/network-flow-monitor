#include "network_flow_monitor/ui/monitor_panel_behavior.h"
#include "network_flow_monitor/ui/monitor_panel_layout.h"

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
MonitorPanelEnvironment SingleMonitor(std::int32_t bottom = 1080) {
    return {.monitors = {{.id = "only",
                          .rect = {.left = 0, .top = 0, .right = 1920, .bottom = bottom},
                          .dpi = 96,
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
void PanelDragAndCancel(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{.pinned = true};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    test.Check(state.surface == MonitorPanelSurface::Panel, "pinned startup restores the panel");
    auto press = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::PanelPress, .point = {.x = 1600, .y = 120}}, env,
        config);
    auto moving = MonitorPanelBehavior::Transition(
        press.state, {.kind = MonitorPanelEventKind::PointerMove, .point = {.x = -1000, .y = 500}},
        env, config);
    auto cancelled = MonitorPanelBehavior::Transition(
        moving.state, {.kind = MonitorPanelEventKind::DragCancelled}, env, config);
    test.Check(cancelled.state.anchor == state.anchor && cancelled.target_bounds == state.bounds,
               "cancelled panel drag restores its original anchor");
    test.Check(cancelled.effects.empty(), "cancelled drag does not persist");
}
void Capsule(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    test.Check(state.surface == MonitorPanelSurface::Capsule,
               "unpinned startup restores the capsule");
    auto hover = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::CapsulePointerEnter}, env, config);
    test.Check(hover.next_deadline == config.hover_delay, "hover schedules explicit deadline");
    auto expanded = MonitorPanelBehavior::Transition(
        hover.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(expanded.state.surface == MonitorPanelSurface::Panel,
               "hover deadline reveals the single panel");
    test.Check(expanded.state.panel_pointer_inside,
               "reveal treats capsule-to-panel movement as continuous occupancy");
    test.Check(expanded.target_bounds.Width() > state.bounds.Width() &&
                   expanded.target_bounds.Height() > state.bounds.Height(),
               "expansion returns a panel-sized layout target");
    test.Check(!expanded.next_deadline, "reveal does not chain a second ranking deadline");
    constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
    const auto rates = MonitorPanelBehavior::MapCapsuleRates(kGiB, 0U);
    test.Check(rates.download.fill == 1.0F && rates.upload.fill == 0.0F,
               "rate tracks saturate independently");
}
void ContentDerivedHeight(Test& test) {
    const auto env = SingleMonitor();
    const MonitorPanelConfig config{.pinned = true, .top_n = 5};
    auto ready =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config);
    ready = MonitorPanelBehavior::Transition(
        ready.state, {.kind = MonitorPanelEventKind::RankingReadyRows, .visible_rows = 5}, env,
        config);
    test.Check(ready.state.bounds.Height() ==
                   DipToPixels((2 * MonitorPanelLayout::kVerticalPaddingDip) +
                                   MonitorPanelLayout::kHeaderRowDip +
                                   (5 * MonitorPanelLayout::kRowDip),
                               96),
               "panel height is derived from header plus five rows plus padding");
    test.Check(ready.viewport.Height() == DipToPixels(5 * MonitorPanelLayout::kRowDip, 96),
               "ranking viewport holds exactly the configured rows when space permits");

    auto partial =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config);
    partial = MonitorPanelBehavior::Transition(
        partial.state, {.kind = MonitorPanelEventKind::RankingReadyRows, .visible_rows = 2}, env,
        config);
    test.Check(partial.state.bounds.Height() ==
                   DipToPixels((2 * MonitorPanelLayout::kVerticalPaddingDip) +
                                   MonitorPanelLayout::kHeaderRowDip +
                                   (2 * MonitorPanelLayout::kRowDip),
                               96),
               "fewer ranking rows than top_n leave no empty footer");

    auto idle =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config);
    idle = MonitorPanelBehavior::Transition(
        idle.state, {.kind = MonitorPanelEventKind::RankingReadyIdle}, env, config);
    test.Check(idle.state.bounds.Height() ==
                   DipToPixels((2 * MonitorPanelLayout::kVerticalPaddingDip) +
                                   MonitorPanelLayout::kHeaderRowDip + MonitorPanelLayout::kRowDip,
                               96),
               "status presentation occupies one content row without extra blank height");
    test.Check(idle.viewport.Height() == 0, "non-row presentations do not expose a viewport");
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
    test.Check(click_release.state.surface == MonitorPanelSurface::Panel &&
                   click_release.effects.empty(),
               "capsule click reveals the panel immediately without persistence");
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

    auto panel = MonitorPanelBehavior::Transition(state, {.kind = MonitorPanelEventKind::ShowPanel},
                                                  env, config);
    auto leave = MonitorPanelBehavior::Transition(
        panel.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, config);
    test.Check(leave.next_deadline == config.auto_hide_delay,
               "unpinned panel leave schedules model-owned auto hide");
    auto reentered = MonitorPanelBehavior::Transition(
        leave.state, {.kind = MonitorPanelEventKind::PanelPointerEnter}, env, config);
    auto stale = MonitorPanelBehavior::Transition(
        reentered.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(stale.state.surface == MonitorPanelSurface::Panel,
               "panel re-entry cancels a stale auto-hide deadline");
    auto hidden = MonitorPanelBehavior::Transition(
        leave.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(hidden.state.surface == MonitorPanelSurface::Capsule,
               "auto-hide deadline returns the panel to its capsule");
    auto pinned = MonitorPanelBehavior::Transition(
        panel.state, {.kind = MonitorPanelEventKind::PinTogglePressed}, env, config);
    auto pinned_state = MonitorPanelBehavior::Transition(
        pinned.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, config);
    test.Check(!pinned_state.next_deadline && !pinned_state.state.auto_hide_pending,
               "pin prevents model auto hide");
}
void ContextMenuPausesAutoHide(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{};
    auto panel = MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::ShowPanel},
                                                  env, config);
    auto leave = MonitorPanelBehavior::Transition(
        panel.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, config);
    auto menu = MonitorPanelBehavior::Transition(
        leave.state, {.kind = MonitorPanelEventKind::ContextMenuOpened}, env, config);
    auto expired = MonitorPanelBehavior::Transition(
        menu.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(expired.state.surface == MonitorPanelSurface::Panel,
               "a deadline cannot collapse the panel while its context menu is open");
    auto closed_outside = MonitorPanelBehavior::Transition(
        expired.state,
        {.kind = MonitorPanelEventKind::ContextMenuClosed, .point = {.x = -100, .y = -100}}, env,
        config);
    test.Check(closed_outside.next_deadline == config.auto_hide_delay,
               "closing a menu outside the panel restarts one auto-hide delay");
    auto collapsed = MonitorPanelBehavior::Transition(
        closed_outside.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(collapsed.state.surface == MonitorPanelSurface::Capsule,
               "the restarted auto-hide delay returns the panel to its capsule");

    auto capsule_hover = MonitorPanelBehavior::Transition(
        collapsed.state, {.kind = MonitorPanelEventKind::CapsulePointerEnter}, env, config);
    auto capsule_menu = MonitorPanelBehavior::Transition(
        capsule_hover.state, {.kind = MonitorPanelEventKind::ContextMenuOpened}, env, config);
    auto stale_capsule_deadline = MonitorPanelBehavior::Transition(
        capsule_menu.state, {.kind = MonitorPanelEventKind::HoverDeadline}, env, config);
    test.Check(stale_capsule_deadline.state.surface == MonitorPanelSurface::Capsule,
               "opening a capsule menu cancels its pending hover expansion");
}
void ContextMenuAllowsVisibilityCommand(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    state.surface = MonitorPanelSurface::Panel;
    const auto menu = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::ContextMenuOpened}, env, config);
    const auto hidden = MonitorPanelBehavior::Transition(
        menu.state, {.kind = MonitorPanelEventKind::ToggleVisibility}, env, config);
    test.Check(hidden.state.surface == MonitorPanelSurface::Hidden,
               "Show/hide monitor remains actionable while its native context menu is open");
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
void PinDisconnectAndViewport(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{.pinned = true, .top_n = 5};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config)
            .state;
    state = MonitorPanelBehavior::Transition(
                state, {.kind = MonitorPanelEventKind::CollectorConnected}, env, config)
                .state;
    test.Check(state.surface == MonitorPanelSurface::Panel &&
                   state.ranking_presentation == RankingPresentation::WarmingUp,
               "pinned connected startup is a warming single panel");

    auto disconnected = MonitorPanelBehavior::Transition(
        state, {.kind = MonitorPanelEventKind::CollectorDisconnected}, env, config);
    test.Check(disconnected.state.surface == MonitorPanelSurface::Panel &&
                   disconnected.state.pin_preference &&
                   disconnected.state.ranking_presentation == RankingPresentation::Disconnected,
               "disconnect preserves the panel surface and pin while clearing rows");
    auto reconnected = MonitorPanelBehavior::Transition(
        disconnected.state, {.kind = MonitorPanelEventKind::CollectorConnected}, env, config);
    test.Check(reconnected.state.ranking_presentation == RankingPresentation::WarmingUp,
               "reconnection restarts warm-up");

    const MonitorPanelConfig unpinned{.top_n = 5};
    auto reveal = MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::ShowPanel},
                                                   env, unpinned);
    reveal = MonitorPanelBehavior::Transition(
        reveal.state, {.kind = MonitorPanelEventKind::PinTogglePressed}, env, unpinned);
    test.Check(reveal.state.pin_preference && reveal.effects.size() == 1U &&
                   reveal.effects.front().kind == MonitorPanelEffectKind::PersistPin,
               "pinning persists the preference");
    auto pinned_leave = MonitorPanelBehavior::Transition(
        reveal.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, unpinned);
    test.Check(!pinned_leave.next_deadline &&
                   pinned_leave.state.surface == MonitorPanelSurface::Panel,
               "pinned panel does not dismiss on leave");
    auto unpin = MonitorPanelBehavior::Transition(
        pinned_leave.state, {.kind = MonitorPanelEventKind::PinTogglePressed}, env, unpinned);
    test.Check(!unpin.state.pin_preference && unpin.effects.size() == 1U &&
                   unpin.effects.front().pinned == false,
               "unpinning persists the cleared preference");
    auto unpin_leave = MonitorPanelBehavior::Transition(
        unpin.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, unpinned);
    test.Check(unpin_leave.next_deadline == unpinned.auto_hide_delay,
               "unpinned panel schedules dismissal after leave");
}
void BottomCorrectionAndViewportScroll(Test& test) {
    const auto env = SingleMonitor(200);
    const MonitorPanelConfig config{.pinned = true, .top_n = 100};
    auto transition = MonitorPanelBehavior::Transition(
        {},
        {.kind = MonitorPanelEventKind::Start,
         .anchor = PersistedMonitorAnchor{.monitor_id = "only",
                                          .side = DockSide::Right,
                                          .vertical_offset_dip = 200}},
        env, config);
    transition = MonitorPanelBehavior::Transition(
        transition.state, {.kind = MonitorPanelEventKind::RankingReadyRows, .visible_rows = 100},
        env, config);
    test.Check(transition.state.bounds.bottom <= env.monitors.front().rect.bottom,
               "near-bottom panel is corrected upward into the work area");
    test.Check(transition.viewport.Height() > 0 &&
                   transition.viewport.Height() < static_cast<std::int32_t>(config.top_n) *
                                                      DipToPixels(MonitorPanelLayout::kRowDip, 96),
               "intrinsically tall ranking content is constrained to an internal viewport");
    test.Check(transition.viewport.top >= transition.state.bounds.top &&
                   transition.viewport.bottom <= transition.state.bounds.bottom,
               "ranking viewport stays within the panel bounds");
    test.Check(transition.effects.empty(),
               "temporary vertical correction never persists the anchor");
}
void TrayVisibilityToggle(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{};
    auto capsule =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config);
    const auto hidden = MonitorPanelBehavior::Transition(
        capsule.state, {.kind = MonitorPanelEventKind::ToggleVisibility}, env, config);
    const auto shown = MonitorPanelBehavior::Transition(
        hidden.state, {.kind = MonitorPanelEventKind::ToggleVisibility}, env, config);
    test.Check(hidden.state.surface == MonitorPanelSurface::Hidden &&
                   shown.state.surface == MonitorPanelSurface::Panel,
               "tray visibility toggle hides the complete panel then restores the panel");
}
void AutoHideDisabledControls(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig config{.auto_hide_enabled = false, .pinned = true};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, config);
    test.Check(state.state.surface == MonitorPanelSurface::Panel && state.state.pin_preference,
               "disabled auto-hide keeps the panel visible and preserves the saved pin");
    auto left = MonitorPanelBehavior::Transition(
        state.state, {.kind = MonitorPanelEventKind::PanelPointerLeave}, env, config);
    auto pin = MonitorPanelBehavior::Transition(
        left.state, {.kind = MonitorPanelEventKind::PinTogglePressed}, env, config);
    test.Check(!left.next_deadline && pin.effects.empty() && pin.state.pin_preference,
               "disabled auto-hide disables pin without erasing the saved preference");
}
void ConfigurationChangesUseBehaviorAuthority(Test& test) {
    const auto env = Environment();
    const MonitorPanelConfig initial{};
    auto state =
        MonitorPanelBehavior::Transition({}, {.kind = MonitorPanelEventKind::Start}, env, initial);
    const MonitorPanelConfig always_visible{.auto_hide_enabled = false, .pinned = true, .top_n = 9};
    auto changed = MonitorPanelBehavior::Transition(
        state.state, {.kind = MonitorPanelEventKind::ConfigurationChanged}, env, always_visible);
    test.Check(changed.state.surface == MonitorPanelSurface::Panel &&
                   changed.state.pin_preference && !changed.next_deadline,
               "settings changes make auto-hide and saved pin state effective immediately");
    auto restored = MonitorPanelBehavior::Transition(
        changed.state, {.kind = MonitorPanelEventKind::ConfigurationChanged}, env, initial);
    test.Check(!restored.state.pin_preference && restored.next_deadline == initial.auto_hide_delay,
               "re-enabling auto-hide resumes the normal dismiss policy");
}
} // namespace
int main() {
    Test test;
    Docking(test);
    PanelDragAndCancel(test);
    Capsule(test);
    ContentDerivedHeight(test);
    MissingMonitorFallback(test);
    SmallWorkAreaAndTie(test);
    FirstUseAndHitBounds(test);
    AutoHideRevealReservation(test);
    TimingAndThresholds(test);
    ContextMenuPausesAutoHide(test);
    ContextMenuAllowsVisibilityCommand(test);
    EnvironmentAndPersistenceFeedback(test);
    RateMapping(test);
    PinDisconnectAndViewport(test);
    BottomCorrectionAndViewportScroll(test);
    TrayVisibilityToggle(test);
    AutoHideDisabledControls(test);
    ConfigurationChangesUseBehaviorAuthority(test);
    return test.failed == 0 ? 0 : 1;
}
