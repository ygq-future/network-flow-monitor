using System;
using System.Collections.Generic;
using System.Linq;
using NetworkFlowMonitor.Presentation;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public sealed class MonitorBehaviorModelTests
{
    private static MonitorEnvironment CreateDualMonitorEnvironment()
    {
        return new MonitorEnvironment(
        [
            new MonitorWorkArea(
                Id: "left",
                Rect: new PixelRect(-1920, 0, 0, 1080),
                Dpi: 96,
                Primary: false
            ),
            new MonitorWorkArea(
                Id: "primary",
                Rect: new PixelRect(0, 0, 1920, 1040),
                Dpi: 144,
                Primary: true
            )
        ]);
    }

    private static MonitorEnvironment CreateSingleMonitorEnvironment(int bottom = 1080)
    {
        return new MonitorEnvironment(
        [
            new MonitorWorkArea(
                Id: "only",
                Rect: new PixelRect(0, 0, 1920, bottom),
                Dpi: 96,
                Primary: true
            )
        ]);
    }

    [Fact]
    public void Docking_FollowsPointerAndSnapsToReleaseMonitorEdge()
    {
        var state = new MonitorState();
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var start = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(
                MonitorEventKind.Start,
                Anchor: new PersistedMonitorAnchor("left", DockSide.Left, 20)
            ),
            env,
            config
        );

        var press = MonitorBehaviorModel.Transition(
            start.State,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(-1915, 35)),
            env,
            config
        );

        var moving = MonitorBehaviorModel.Transition(
            press.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(1900, 800)),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.CapsuleVisible, moving.State.State);
        Assert.True(moving.State.Dragging);
        Assert.Equal(785, moving.Targets.TargetBounds.Top);
        Assert.Equal(1895, moving.Targets.TargetBounds.Left);

        var released = MonitorBehaviorModel.Transition(
            moving.State,
            new MonitorEvent(MonitorEventKind.PanelRelease, Point: new PixelPoint(1900, 800)),
            env,
            config
        );

        Assert.Equal("primary", released.State.Anchor.MonitorId);
        Assert.Equal(DockSide.Right, released.State.Anchor.Side);
        Assert.Equal(released.State.Bounds.Right - MonitorLayout.DipToPixels(10, 144), released.State.Bounds.Left);
        Assert.Equal(785, released.State.Bounds.Top);
        Assert.Single(released.Effects);
        Assert.Equal(MonitorEffectKind.PersistAnchor, released.Effects[0].Kind);
    }

    [Fact]
    public void PanelDragAndCancel_RestoresOriginalAnchor()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig(Pinned: true);

        var start = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.PanelVisible, start.State.State);

        var press = MonitorBehaviorModel.Transition(
            start.State,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(1600, 120)),
            env,
            config
        );

        var moving = MonitorBehaviorModel.Transition(
            press.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(-1000, 500)),
            env,
            config
        );

        var cancelled = MonitorBehaviorModel.Transition(
            moving.State,
            new MonitorEvent(MonitorEventKind.DragCancelled),
            env,
            config
        );

        Assert.Equal(start.State.Anchor, cancelled.State.Anchor);
        Assert.Equal(start.State.Bounds, cancelled.Targets.TargetBounds);
        Assert.Empty(cancelled.Effects);
    }

    [Fact]
    public void DragRemainsStableDuringDataRefresh()
    {
        var env = CreateDualMonitorEnvironment();
        var capsuleConfig = new MonitorConfig();

        var capsule = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            capsuleConfig
        ).State;

        var capsulePress = MonitorBehaviorModel.Transition(
            capsule,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(1915, 120)),
            env,
            capsuleConfig
        );

        var capsuleMove = MonitorBehaviorModel.Transition(
            capsulePress.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(900, 500)),
            env,
            capsuleConfig
        );

        var capsuleRefresh = MonitorBehaviorModel.Transition(
            capsuleMove.State,
            new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: 5),
            env,
            capsuleConfig
        );

        Assert.True(capsuleRefresh.State.Dragging);
        Assert.Equal(capsuleMove.Targets.TargetBounds, capsuleRefresh.Targets.TargetBounds);

        var panelConfig = new MonitorConfig(Pinned: true);
        var panel = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            panelConfig
        ).State;

        var panelPress = MonitorBehaviorModel.Transition(
            panel,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(1500, 120)),
            env,
            panelConfig
        );

        var panelMove = MonitorBehaviorModel.Transition(
            panelPress.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(800, 500)),
            env,
            panelConfig
        );

        var panelRefresh = MonitorBehaviorModel.Transition(
            panelMove.State,
            new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: 4),
            env,
            panelConfig
        );

        Assert.True(panelRefresh.State.Dragging);
        Assert.Equal(panelMove.Targets.TargetBounds, panelRefresh.Targets.TargetBounds);
    }

    [Fact]
    public void CapsuleHoverAndClickReveal_WorkCorrectly()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        ).State;

        Assert.Equal(MonitorPresentationState.CapsuleVisible, state.State);

        var hover = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.CapsulePointerEnter),
            env,
            config
        );

        Assert.Equal(config.HoverDelay, hover.NextDeadline);

        var expanded = MonitorBehaviorModel.Transition(
            hover.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.PanelVisible, expanded.State.State);
        Assert.True(expanded.State.PanelPointerInside);
        Assert.True(expanded.Targets.TargetBounds.Width > state.Bounds.Width);
        Assert.True(expanded.Targets.TargetBounds.Height > state.Bounds.Height);
        Assert.Null(expanded.NextDeadline);

        const ulong gib = 1024UL * 1024UL * 1024UL;
        var rates = MonitorLayout.MapCapsuleRates(gib, 0);
        Assert.Equal(1.0f, rates.Download.Fill);
        Assert.Equal(0.0f, rates.Upload.Fill);
    }

    [Fact]
    public void ContentDerivedHeight_CalculatesBoundsFromVisibleRows()
    {
        var env = CreateSingleMonitorEnvironment();
        var config = new MonitorConfig(Pinned: true, TopN: 5);

        var ready = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );

        ready = MonitorBehaviorModel.Transition(
            ready.State,
            new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: 5),
            env,
            config
        );

        var expected5Rows = MonitorLayout.DipToPixels(
            (2 * MonitorLayout.VerticalPaddingDip) + MonitorLayout.HeaderRowDip + (5 * MonitorLayout.RowDip),
            96
        );
        Assert.Equal(expected5Rows, ready.State.Bounds.Height);
        Assert.Equal(MonitorLayout.DipToPixels(5 * MonitorLayout.RowDip, 96), ready.Targets.Viewport.Height);

        var partial = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );

        partial = MonitorBehaviorModel.Transition(
            partial.State,
            new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: 2),
            env,
            config
        );

        var expected2Rows = MonitorLayout.DipToPixels(
            (2 * MonitorLayout.VerticalPaddingDip) + MonitorLayout.HeaderRowDip + (2 * MonitorLayout.RowDip),
            96
        );
        Assert.Equal(expected2Rows, partial.State.Bounds.Height);

        var idle = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );

        idle = MonitorBehaviorModel.Transition(
            idle.State,
            new MonitorEvent(MonitorEventKind.RankingReadyIdle),
            env,
            config
        );

        var expectedIdle = MonitorLayout.DipToPixels(
            (2 * MonitorLayout.VerticalPaddingDip) + MonitorLayout.HeaderRowDip + MonitorLayout.RowDip,
            96
        );
        Assert.Equal(expectedIdle, idle.State.Bounds.Height);
        Assert.Equal(0, idle.Targets.Viewport.Height);
    }

    [Fact]
    public void MissingMonitorFallback_RestoresToPrimary()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(
                MonitorEventKind.Start,
                Anchor: new PersistedMonitorAnchor("left", DockSide.Left, 50)
            ),
            env,
            config
        ).State;

        var singleEnv = new MonitorEnvironment([env.Monitors[1]]);
        var changed = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.EnvironmentChanged),
            singleEnv,
            config
        );

        Assert.Equal("primary", changed.State.Anchor.MonitorId);
        Assert.Single(changed.Effects);
        Assert.Equal(MonitorEffectKind.PersistAnchor, changed.Effects[0].Kind);
    }

    [Fact]
    public void SmallWorkAreaAndTieBreaking_BehavesDeterministically()
    {
        var env = new MonitorEnvironment(
        [
            new MonitorWorkArea(
                Id: "tiny",
                Rect: new PixelRect(0, 20, 6, 55),
                Dpi: 96,
                Primary: true
            )
        ]);
        var config = new MonitorConfig();

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(
                MonitorEventKind.Start,
                Anchor: new PersistedMonitorAnchor("tiny", DockSide.Left, 100)
            ),
            env,
            config
        ).State;

        Assert.Equal(env.Monitors[0].Rect, state.Bounds);

        var press = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(3, 30)),
            env,
            config
        );

        var moved = MonitorBehaviorModel.Transition(
            press.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(20, 30)),
            env,
            config
        );

        var released = MonitorBehaviorModel.Transition(
            moved.State,
            new MonitorEvent(MonitorEventKind.PanelRelease, Point: new PixelPoint(3, 30)),
            env,
            config
        );

        Assert.Equal(DockSide.Left, released.State.Anchor.Side);
    }

    [Fact]
    public void FirstUseAndHitBounds_CalculatesMinimumInwardHitExtension()
    {
        var env = new MonitorEnvironment(
        [
            new MonitorWorkArea(
                Id: "only",
                Rect: new PixelRect(0, 0, 1000, 800),
                Dpi: 192,
                Primary: true
            )
        ]);

        var config = new MonitorConfig(CapsuleWidthDip: 8, CapsuleHeightDip: 40);

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        ).State;

        Assert.Equal(16, state.Bounds.Width);
        Assert.Equal(80, state.Bounds.Height);

        var layout = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.EnvironmentChanged),
            env,
            config
        );

        Assert.Equal(32, layout.Targets.HitBounds.Width);

        var press = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(992, 20)),
            env,
            config
        );

        var moved = MonitorBehaviorModel.Transition(
            press.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(500, 20)),
            env,
            config
        );

        var released = MonitorBehaviorModel.Transition(
            moved.State,
            new MonitorEvent(MonitorEventKind.PanelRelease, Point: new PixelPoint(500, 20)),
            env,
            config
        );

        Assert.Equal(DockSide.Right, released.State.Anchor.Side);

        var maximumConfig = new MonitorConfig(CapsuleWidthDip: 24, CapsuleHeightDip: 160);
        var maxState = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            maximumConfig
        );

        Assert.Equal(48, maxState.State.Bounds.Width);
        Assert.Equal(320, maxState.State.Bounds.Height);

        var clickPress = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(992, 20)),
            env,
            config
        );

        var clickRelease = MonitorBehaviorModel.Transition(
            clickPress.State,
            new MonitorEvent(MonitorEventKind.PanelRelease, Point: new PixelPoint(992, 20)),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.PanelVisible, clickRelease.State.State);
        Assert.Empty(clickRelease.Effects);
    }

    [Fact]
    public void AutoHideRevealReservation_ReservesTwoPixelsOnDockedEdge()
    {
        foreach (var side in new[] { DockSide.Left, DockSide.Right })
        {
            var env = new MonitorEnvironment(
            [
                new MonitorWorkArea(
                    Id: "tiny",
                    Rect: new PixelRect(10, 0, 16, 40),
                    Dpi: 96,
                    Primary: true,
                    AutoHideRevealOnLeft: side == DockSide.Left,
                    AutoHideRevealOnRight: side == DockSide.Right
                )
            ]);

            var config = new MonitorConfig(CapsuleWidthDip: 24, CapsuleHeightDip: 160);
            var state = MonitorBehaviorModel.Transition(
                new MonitorState(),
                new MonitorEvent(
                    MonitorEventKind.Start,
                    Anchor: new PersistedMonitorAnchor("tiny", side)
                ),
                env,
                config
            );

            Assert.Equal(4, state.State.Bounds.Width);
            Assert.True(state.State.Bounds.Left >= env.Monitors[0].Rect.Left);
            Assert.True(state.State.Bounds.Right <= env.Monitors[0].Rect.Right);

            if (side == DockSide.Left)
            {
                Assert.Equal(12, state.State.Bounds.Left);
            }
            else
            {
                Assert.Equal(14, state.State.Bounds.Right);
            }
        }
    }

    [Fact]
    public void TimingAndThresholds_HandleHoverAndDismiss()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig(
            MinimumDragDistanceXPixels: 8,
            MinimumDragDistanceYPixels: 3,
            AutoHideDelay: TimeSpan.FromMilliseconds(700)
        );

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        ).State;

        var hover = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.CapsulePointerEnter),
            env,
            config
        );

        var left = MonitorBehaviorModel.Transition(
            hover.State,
            new MonitorEvent(MonitorEventKind.CapsulePointerLeave),
            env,
            config
        );

        var staleDeadline = MonitorBehaviorModel.Transition(
            left.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.CapsuleVisible, staleDeadline.State.State);

        var press = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.PanelPress, Point: new PixelPoint(1915, 10)),
            env,
            config
        );

        var below = MonitorBehaviorModel.Transition(
            press.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(1922, 12)),
            env,
            config
        );
        Assert.False(below.State.Dragging);

        var vertical = MonitorBehaviorModel.Transition(
            below.State,
            new MonitorEvent(MonitorEventKind.PointerMove, Point: new PixelPoint(1922, 13)),
            env,
            config
        );
        Assert.True(vertical.State.Dragging);

        var panel = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.ShowPanel),
            env,
            config
        );

        var leave = MonitorBehaviorModel.Transition(
            panel.State,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            config
        );
        Assert.Equal(config.AutoHideDelay, leave.NextDeadline);

        var reentered = MonitorBehaviorModel.Transition(
            leave.State,
            new MonitorEvent(MonitorEventKind.PanelPointerEnter),
            env,
            config
        );

        var stale = MonitorBehaviorModel.Transition(
            reentered.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.PanelVisible, stale.State.State);

        var hidden = MonitorBehaviorModel.Transition(
            leave.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.CapsuleVisible, hidden.State.State);

        var pinned = MonitorBehaviorModel.Transition(
            panel.State,
            new MonitorEvent(MonitorEventKind.PinTogglePressed),
            env,
            config
        );

        var pinnedState = MonitorBehaviorModel.Transition(
            pinned.State,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            config
        );

        Assert.Null(pinnedState.NextDeadline);
        Assert.False(pinnedState.State.AutoHidePending);
    }

    [Fact]
    public void ContextMenuPausesAutoHide_AndReconcilesPointer()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var panel = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.ShowPanel),
            env,
            config
        );

        var leave = MonitorBehaviorModel.Transition(
            panel.State,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            config
        );

        var menu = MonitorBehaviorModel.Transition(
            leave.State,
            new MonitorEvent(MonitorEventKind.ContextMenuOpened),
            env,
            config
        );

        var expired = MonitorBehaviorModel.Transition(
            menu.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.PanelVisible, expired.State.State);

        var closedOutside = MonitorBehaviorModel.Transition(
            expired.State,
            new MonitorEvent(MonitorEventKind.ContextMenuClosed, Point: new PixelPoint(-100, -100)),
            env,
            config
        );
        Assert.Equal(config.AutoHideDelay, closedOutside.NextDeadline);

        var collapsed = MonitorBehaviorModel.Transition(
            closedOutside.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.CapsuleVisible, collapsed.State.State);

        var capsuleHover = MonitorBehaviorModel.Transition(
            collapsed.State,
            new MonitorEvent(MonitorEventKind.CapsulePointerEnter),
            env,
            config
        );

        var capsuleMenu = MonitorBehaviorModel.Transition(
            capsuleHover.State,
            new MonitorEvent(MonitorEventKind.ContextMenuOpened),
            env,
            config
        );

        var staleCapsuleDeadline = MonitorBehaviorModel.Transition(
            capsuleMenu.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.CapsuleVisible, staleCapsuleDeadline.State.State);
    }

    [Fact]
    public void ContextMenu_AllowsVisibilityAndPinCommands()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        ).State with
        {
            State = MonitorPresentationState.PanelVisible
        };

        var menu = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.ContextMenuOpened),
            env,
            config
        );

        var hidden = MonitorBehaviorModel.Transition(
            menu.State,
            new MonitorEvent(MonitorEventKind.HideMonitorRequested),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.TrayOnly, hidden.State.State);

        var pinned = MonitorBehaviorModel.Transition(
            menu.State,
            new MonitorEvent(MonitorEventKind.PinTogglePressed),
            env,
            config
        );
        Assert.True(pinned.State.PinPreference);
        Assert.Single(pinned.Effects);
        Assert.Equal(MonitorEffectKind.PersistPin, pinned.Effects[0].Kind);
    }

    [Fact]
    public void EnvironmentAndPersistenceFeedback_HandleDpiAndFailures()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(
                MonitorEventKind.Start,
                Anchor: new PersistedMonitorAnchor("primary", DockSide.Right, 100)
            ),
            env,
            config
        ).State;

        var originalTop = state.Bounds.Top;
        var modifiedEnv = new MonitorEnvironment(
        [
            env.Monitors[0],
            env.Monitors[1] with { Dpi = 192 }
        ]);

        var dpiChanged = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.EnvironmentChanged),
            modifiedEnv,
            config
        );

        Assert.NotEqual(originalTop, dpiChanged.State.Bounds.Top);
        Assert.Empty(dpiChanged.Effects);

        var singleEnv = new MonitorEnvironment([modifiedEnv.Monitors[1]]);
        var reconnect = MonitorBehaviorModel.Transition(
            dpiChanged.State,
            new MonitorEvent(MonitorEventKind.EnvironmentChanged),
            singleEnv,
            config
        );

        Assert.Equal("primary", reconnect.State.Anchor.MonitorId);
        Assert.Empty(reconnect.Effects);

        var failed = MonitorBehaviorModel.Transition(
            reconnect.State,
            new MonitorEvent(MonitorEventKind.PersistFailed),
            singleEnv,
            config
        );

        Assert.Equal(reconnect.State.Anchor, failed.State.Anchor);
        Assert.True(failed.State.PersistenceFailed);
        Assert.Single(failed.Effects);
        Assert.Equal(MonitorEffectKind.ReportPersistenceFailure, failed.Effects[0].Kind);

        var succeeded = MonitorBehaviorModel.Transition(
            failed.State,
            new MonitorEvent(MonitorEventKind.PersistSucceeded),
            singleEnv,
            config
        );
        Assert.False(succeeded.State.PersistenceFailed);
    }

    [Fact]
    public void RateMapping_CalculatesLogarithmicFillAndPalettes()
    {
        const ulong kib = 1024UL;
        const ulong mib = kib * 1024UL;
        const ulong gib = mib * 1024UL;

        var zero = MonitorLayout.MapCapsuleRates(0, 0);
        var oneByte = MonitorLayout.MapCapsuleRates(1, 1);
        var oneKib = MonitorLayout.MapCapsuleRates(kib, kib);
        var oneMib = MonitorLayout.MapCapsuleRates(mib, mib);
        var oneGib = MonitorLayout.MapCapsuleRates(gib, gib * 2);

        Assert.Equal(0.0f, zero.Download.Fill);
        Assert.Equal(0.0f, zero.Upload.Fill);

        Assert.True(oneByte.Download.Fill <= oneKib.Download.Fill);
        Assert.True(oneKib.Download.Fill < oneMib.Download.Fill);
        Assert.True(oneMib.Download.Fill < oneGib.Download.Fill);
        Assert.Equal(1.0f, oneGib.Download.Fill);
        Assert.Equal(1.0f, oneGib.Upload.Fill);

        Assert.Equal(0x19D8E6u, zero.Download.ColorRgb);
        Assert.Equal(0xF4D03Fu, zero.Upload.ColorRgb);
        Assert.Equal(0x8146B8u, oneGib.Download.ColorRgb);
        Assert.Equal(0xD93636u, oneGib.Upload.ColorRgb);

        var downloadOnly = MonitorLayout.MapCapsuleRates(mib, 0);
        var uploadOnly = MonitorLayout.MapCapsuleRates(0, mib);

        Assert.True(downloadOnly.Download.Fill > 0.0f && downloadOnly.Upload.Fill == 0.0f);
        Assert.True(uploadOnly.Download.Fill == 0.0f && uploadOnly.Upload.Fill > 0.0f);
    }

    [Fact]
    public void PureRateToFill_ValidatesMathematicalEndpoints()
    {
        Assert.Equal(0.0f, MonitorLayout.RateToFill(0));
        Assert.Equal(0.0f, MonitorLayout.RateToFill(1));
        Assert.InRange(MonitorLayout.RateToFill(1024), 0.333f, 0.334f);
        Assert.InRange(MonitorLayout.RateToFill(1024 * 1024), 0.666f, 0.667f);
        Assert.Equal(1.0f, MonitorLayout.RateToFill(1024 * 1024 * 1024));
        Assert.Equal(1.0f, MonitorLayout.RateToFill(2UL * 1024 * 1024 * 1024));
    }

    [Fact]
    public void PinDisconnectAndViewport_ManageStateCorrectly()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig(Pinned: true, TopN: 5);

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        ).State;

        state = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.CollectorConnected),
            env,
            config
        ).State;

        Assert.Equal(MonitorPresentationState.PanelVisible, state.State);
        Assert.Equal(RankingPresentation.WarmingUp, state.RankingPresentation);

        var disconnected = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.CollectorDisconnected),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.PanelVisible, disconnected.State.State);
        Assert.True(disconnected.State.PinPreference);
        Assert.Equal(RankingPresentation.Disconnected, disconnected.State.RankingPresentation);

        var reconnected = MonitorBehaviorModel.Transition(
            disconnected.State,
            new MonitorEvent(MonitorEventKind.CollectorConnected),
            env,
            config
        );

        Assert.Equal(RankingPresentation.WarmingUp, reconnected.State.RankingPresentation);

        var unpinned = new MonitorConfig(TopN: 5);
        var reveal = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.ShowPanel),
            env,
            unpinned
        );

        reveal = MonitorBehaviorModel.Transition(
            reveal.State,
            new MonitorEvent(MonitorEventKind.PinTogglePressed),
            env,
            unpinned
        );

        Assert.True(reveal.State.PinPreference);
        Assert.Single(reveal.Effects);
        Assert.Equal(MonitorEffectKind.PersistPin, reveal.Effects[0].Kind);

        var pinnedLeave = MonitorBehaviorModel.Transition(
            reveal.State,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            unpinned
        );

        Assert.Null(pinnedLeave.NextDeadline);
        Assert.Equal(MonitorPresentationState.PanelVisible, pinnedLeave.State.State);

        var unpin = MonitorBehaviorModel.Transition(
            pinnedLeave.State,
            new MonitorEvent(MonitorEventKind.PinTogglePressed),
            env,
            unpinned
        );

        Assert.False(unpin.State.PinPreference);
        Assert.Single(unpin.Effects);
        Assert.False(unpin.Effects[0].Pinned);

        var unpinLeave = MonitorBehaviorModel.Transition(
            unpin.State,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            unpinned
        );

        Assert.Equal(unpinned.AutoHideDelay, unpinLeave.NextDeadline);
    }

    [Fact]
    public void BottomCorrectionAndViewportScroll_FitsWorkArea()
    {
        var env = CreateSingleMonitorEnvironment(200);
        var config = new MonitorConfig(Pinned: true, TopN: 100);

        var transition = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(
                MonitorEventKind.Start,
                Anchor: new PersistedMonitorAnchor("only", DockSide.Right, 200)
            ),
            env,
            config
        );

        transition = MonitorBehaviorModel.Transition(
            transition.State,
            new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: 100),
            env,
            config
        );

        Assert.True(transition.State.Bounds.Bottom <= env.Monitors[0].Rect.Bottom);
        Assert.True(transition.Targets.Viewport.Height > 0);
        Assert.True(transition.Targets.Viewport.Height < (int)config.TopN * MonitorLayout.DipToPixels(MonitorLayout.RowDip, 96));
        Assert.True(transition.Targets.Viewport.Top >= transition.State.Bounds.Top);
        Assert.True(transition.Targets.Viewport.Bottom <= transition.State.Bounds.Bottom);
        Assert.Empty(transition.Effects);
    }

    [Fact]
    public void TrayVisibilityToggle_TogglesBetweenTrayOnlyAndRestored()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var capsule = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );

        var hidden = MonitorBehaviorModel.Transition(
            capsule.State,
            new MonitorEvent(MonitorEventKind.ToggleVisibility),
            env,
            config
        );

        var shown = MonitorBehaviorModel.Transition(
            hidden.State,
            new MonitorEvent(MonitorEventKind.ToggleVisibility),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.TrayOnly, hidden.State.State);
        Assert.Equal(MonitorPresentationState.CapsuleVisible, shown.State.State);

        var hideReq = MonitorBehaviorModel.Transition(
            shown.State,
            new MonitorEvent(MonitorEventKind.HideMonitorRequested),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.TrayOnly, hideReq.State.State);

        var showReq = MonitorBehaviorModel.Transition(
            hideReq.State,
            new MonitorEvent(MonitorEventKind.ShowMonitorRequested),
            env,
            config
        );
        Assert.Equal(MonitorPresentationState.CapsuleVisible, showReq.State.State);
    }

    [Fact]
    public void AutoHideDisabledControls_PreservesSavedPinPreference()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig(AutoHideEnabled: false, Pinned: true);

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.PanelVisible, state.State.State);
        Assert.True(state.State.PinPreference);

        var left = MonitorBehaviorModel.Transition(
            state.State,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            config
        );

        var pin = MonitorBehaviorModel.Transition(
            left.State,
            new MonitorEvent(MonitorEventKind.PinTogglePressed),
            env,
            config
        );

        Assert.Null(left.NextDeadline);
        Assert.Empty(pin.Effects);
        Assert.True(pin.State.PinPreference);
    }

    [Fact]
    public void FullscreenTopmostYield_UpdatesTopmostState()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig(YieldFullscreen: true);

        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );

        Assert.True(state.Targets.Topmost);

        var occluded = MonitorBehaviorModel.Transition(
            state.State,
            new MonitorEvent(MonitorEventKind.FullscreenOcclusionChanged, IsFullscreenActive: true),
            env,
            config
        );

        Assert.True(occluded.State.FullscreenYielded);
        Assert.False(occluded.Targets.Topmost);

        var cleared = MonitorBehaviorModel.Transition(
            occluded.State,
            new MonitorEvent(MonitorEventKind.FullscreenOcclusionChanged, IsFullscreenActive: false),
            env,
            config
        );

        Assert.False(cleared.State.FullscreenYielded);
        Assert.True(cleared.Targets.Topmost);
    }

    [Fact]
    public void AnimationTargets_EmitsAppropriateTokens()
    {
        var env = CreateDualMonitorEnvironment();
        var config = new MonitorConfig();

        var capsule = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            config
        );

        Assert.Equal(MonitorAnimationKind.None, capsule.Animation.Kind);

        var enter = MonitorBehaviorModel.Transition(
            capsule.State,
            new MonitorEvent(MonitorEventKind.CapsulePointerEnter),
            env,
            config
        );

        Assert.Equal(MonitorAnimationKind.None, enter.Animation.Kind);

        var revealed = MonitorBehaviorModel.Transition(
            enter.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );

        Assert.Equal(MonitorAnimationKind.Reveal, revealed.Animation.Kind);
        Assert.Equal(EasingKind.EaseOut, revealed.Animation.Easing);
        Assert.Equal(TimeSpan.FromMilliseconds(180), revealed.Animation.Duration);

        var panel = MonitorBehaviorModel.Transition(
            capsule.State,
            new MonitorEvent(MonitorEventKind.ShowPanel),
            env,
            config
        );

        var leave = MonitorBehaviorModel.Transition(
            panel.State,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            config
        );

        var dismissed = MonitorBehaviorModel.Transition(
            leave.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );

        Assert.Equal(MonitorAnimationKind.Dismiss, dismissed.Animation.Kind);
        Assert.Equal(EasingKind.EaseIn, dismissed.Animation.Easing);
        Assert.Equal(TimeSpan.FromMilliseconds(180), dismissed.Animation.Duration);

        var reduced = new MonitorConfig(ReducedMotion: true);
        var reducedCapsule = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.Start),
            env,
            reduced
        );

        var reducedEnter = MonitorBehaviorModel.Transition(
            reducedCapsule.State,
            new MonitorEvent(MonitorEventKind.CapsulePointerEnter),
            env,
            reduced
        );

        var reducedReveal = MonitorBehaviorModel.Transition(
            reducedEnter.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            reduced
        );

        Assert.Equal(MonitorAnimationKind.Reveal, reducedReveal.Animation.Kind);
        Assert.Equal(TimeSpan.Zero, reducedReveal.Animation.Duration);

        var themed = MonitorBehaviorModel.Transition(
            capsule.State,
            new MonitorEvent(MonitorEventKind.ThemeChanged),
            env,
            config
        );

        Assert.Equal(MonitorAnimationKind.Theme, themed.Animation.Kind);
        Assert.Equal(TimeSpan.FromMilliseconds(150), themed.Animation.Duration);
    }
}
