using System;
using System.Threading;
using System.Windows;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class BehaviorTimingBugTests
{
    [Fact]
    public void TelemetrySnapshot_DuringDismissDelay_MustNotCancelDismissDeadline()
    {
        var store = new TelemetryStore();
        var env = new MonitorEnvironment([new MonitorWorkArea("primary", new PixelRect(0, 0, 1920, 1080), 96, true)]);
        var config = new MonitorConfig(AutoHideDelay: TimeSpan.FromMilliseconds(500));

        // Start in PanelVisible
        var state = MonitorBehaviorModel.Transition(
            new MonitorState(),
            new MonitorEvent(MonitorEventKind.ShowPanel),
            env,
            config
        ).State;

        // Pointer leaves panel -> schedules dismiss (500ms)
        var leave = MonitorBehaviorModel.Transition(
            state,
            new MonitorEvent(MonitorEventKind.PanelPointerLeave),
            env,
            config
        );

        Assert.True(leave.State.AutoHidePending);
        Assert.Equal(MonitorDeadline.Dismiss, leave.State.Deadline);
        Assert.Equal(TimeSpan.FromMilliseconds(500), leave.NextDeadline);

        // While dismiss deadline is pending (e.g. 50ms later), a 4Hz / 1Hz telemetry snapshot arrives
        var telemetryUpdate = MonitorBehaviorModel.Transition(
            leave.State,
            new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: 5),
            env,
            config
        );

        // The state MUST still retain the pending dismiss deadline!
        Assert.True(telemetryUpdate.State.AutoHidePending, "AutoHidePending must not be wiped by telemetry updates");
        Assert.Equal(MonitorDeadline.Dismiss, telemetryUpdate.State.Deadline);

        // If the timer expires, it must collapse to CapsuleVisible
        var expired = MonitorBehaviorModel.Transition(
            telemetryUpdate.State,
            new MonitorEvent(MonitorEventKind.HoverDeadline),
            env,
            config
        );

        Assert.Equal(MonitorPresentationState.CapsuleVisible, expired.State.State);
    }
}
