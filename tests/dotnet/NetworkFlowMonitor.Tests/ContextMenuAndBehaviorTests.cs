using NetworkFlowMonitor.Presentation;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class ContextMenuAndBehaviorTests {
    [Fact]
    public void ContextMenu_ShowHideItem_ReflectsTrayOnlyVersusVisible() {
        var env = new MonitorEnvironment([
            new MonitorWorkArea("mon1", new PixelRect(0, 0, 1920, 1080), 96, false, false)
        ]);
        var config = new MonitorConfig(AutoHideEnabled: true);

        var state = new MonitorState { State = MonitorPresentationState.CapsuleVisible };
        var t1 = MonitorBehaviorModel.Transition(state, new MonitorEvent(MonitorEventKind.HideMonitorRequested), env, config);
        Assert.Equal(MonitorPresentationState.TrayOnly, t1.State.State);

        var t2 = MonitorBehaviorModel.Transition(t1.State, new MonitorEvent(MonitorEventKind.ShowMonitorRequested), env, config);
        Assert.Equal(MonitorPresentationState.CapsuleVisible, t2.State.State);
    }

    [Fact]
    public void ContextMenu_OpeningPausesDismiss_ClosingReconcilesPointer() {
        var env = new MonitorEnvironment([
            new MonitorWorkArea("mon1", new PixelRect(0, 0, 1920, 1080), 96, false, false)
        ]);
        var config = new MonitorConfig(AutoHideEnabled: true, AutoHideDelay: System.TimeSpan.FromMilliseconds(500));

        // Panel is visible, pointer leaves panel -> dismiss deadline set
        var state = new MonitorState {
            State = MonitorPresentationState.PanelVisible,
            Bounds = new PixelRect(100, 100, 440, 200)
        };
        var t1 = MonitorBehaviorModel.Transition(state, new MonitorEvent(MonitorEventKind.PanelPointerLeave), env, config);
        Assert.Equal(MonitorDeadline.Dismiss, t1.State.Deadline);

        // Context menu opened -> dismiss deadline paused
        var t2 = MonitorBehaviorModel.Transition(t1.State, new MonitorEvent(MonitorEventKind.ContextMenuOpened), env, config);
        Assert.Equal(MonitorDeadline.None, t2.State.Deadline);
        Assert.True(t2.State.ContextMenuActive);

        // Context menu closed with pointer outside -> dismiss delay resumed
        var t3 = MonitorBehaviorModel.Transition(t2.State, new MonitorEvent(
            MonitorEventKind.ContextMenuClosed,
            Point: new PixelPoint(500, 500) // outside panel (bounds are 100..440, 100..200)
        ), env, config);
        Assert.Equal(MonitorDeadline.Dismiss, t3.State.Deadline);
    }
}
