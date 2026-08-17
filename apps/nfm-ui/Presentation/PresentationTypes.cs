using System;
using System.Collections.Generic;

namespace NetworkFlowMonitor.Presentation;

public enum DockSide : byte {
    Right = 0,
    Left = 1
}

public enum MonitorPresentationState : byte {
    TrayOnly,
    CapsuleVisible,
    PanelVisible
}

public enum MonitorEventKind : byte {
    Start,
    ShowCapsule,
    ShowPanel,
    HideMonitorRequested,
    ShowMonitorRequested,
    ToggleVisibility,
    CapsulePointerEnter,
    CapsulePointerLeave,
    PanelPointerEnter,
    PanelPointerLeave,
    PanelPress,
    PointerMove,
    PanelRelease,
    DragCancelled,
    HoverDeadline,
    DismissDeadline,
    PinTogglePressed,
    CollectorConnected,
    CollectorDisconnected,
    RankingWarming,
    RankingReadyIdle,
    RankingReadyRows,
    ContextMenuOpened,
    ContextMenuClosed,
    FullscreenOcclusionChanged,
    ConfigurationChanged,
    EnvironmentChanged,
    ThemeChanged,
    PersistSucceeded,
    PersistFailed
}

public enum MonitorEffectKind : byte {
    PersistAnchor,
    PersistPin,
    ReportPersistenceFailure,
    LogDiagnostic
}

public enum MonitorDeadline : byte {
    None,
    CapsuleReveal,
    Dismiss
}

public enum RankingPresentation : byte {
    Disconnected,
    WarmingUp,
    ReadyIdle,
    ReadyWithRows
}

public enum EasingKind : byte {
    Linear,
    EaseOut,
    EaseIn
}

public enum MonitorAnimationKind : byte {
    None,
    Reveal,
    Dismiss,
    Theme
}

public readonly record struct AnimationTarget(
    MonitorAnimationKind Kind,
    EasingKind Easing,
    TimeSpan Duration
) {
    public static AnimationTarget None => new(MonitorAnimationKind.None, EasingKind.EaseOut, TimeSpan.Zero);
}

public readonly record struct PixelPoint(int X, int Y);

public readonly record struct PixelSize(int Width, int Height);

public readonly record struct PixelRect(int Left, int Top, int Right, int Bottom) {
    public int Width => Math.Max(0, Right - Left);
    public int Height => Math.Max(0, Bottom - Top);
    public PixelSize Size => new(Width, Height);

    public bool Contains(PixelPoint point) =>
        point.X >= Left && point.X < Right && point.Y >= Top && point.Y < Bottom;
}

public sealed record MonitorWorkArea(
    string Id,
    PixelRect Rect,
    uint Dpi = 96,
    bool Primary = false,
    bool AutoHideRevealOnLeft = false,
    bool AutoHideRevealOnRight = false
);

public sealed record MonitorEnvironment(
    IReadOnlyList<MonitorWorkArea> Monitors,
    bool FullscreenOccluded = false
);

public sealed record MonitorConfig(
    TimeSpan HoverDelay = default,
    int CapsuleWidthDip = 10,
    int CapsuleHeightDip = 64,
    int MinimumDragDistanceXPixels = 4,
    int MinimumDragDistanceYPixels = 4,
    TimeSpan AutoHideDelay = default,
    bool AutoHideEnabled = true,
    bool Pinned = false,
    byte TopN = 5,
    bool ReducedMotion = false,
    bool YieldFullscreen = true
) {
    public TimeSpan HoverDelay { get; init; } = HoverDelay == default ? TimeSpan.FromMilliseconds(350) : HoverDelay;
    public TimeSpan AutoHideDelay { get; init; } = AutoHideDelay == default ? TimeSpan.FromMilliseconds(500) : AutoHideDelay;
}

public readonly record struct PersistedMonitorAnchor(
    string MonitorId,
    DockSide Side = DockSide.Right,
    int VerticalOffsetDip = 0
);

public readonly record struct CapsuleTrack(
    float Fill,
    uint ColorRgb
);

public readonly record struct CapsulePresentation(
    CapsuleTrack Download,
    CapsuleTrack Upload
);

public readonly record struct EffectIntent(
    MonitorEffectKind Kind,
    PersistedMonitorAnchor Anchor = default,
    bool Pinned = false,
    string? Message = null
);

public readonly record struct MonitorEvent(
    MonitorEventKind Kind,
    PixelPoint Point = default,
    TimeSpan Elapsed = default,
    PersistedMonitorAnchor? Anchor = null,
    byte VisibleRows = 0,
    bool IsFullscreenActive = false
);

public sealed record MonitorState {
    public MonitorPresentationState State { get; init; } = MonitorPresentationState.CapsuleVisible;
    public PersistedMonitorAnchor Anchor { get; init; }
    public PixelRect Bounds { get; init; }
    public PixelPoint PressPoint { get; init; }
    public PixelPoint PressOffset { get; init; }
    public PersistedMonitorAnchor DragOrigin { get; init; }
    public bool Pressing { get; init; }
    public bool Dragging { get; init; }
    public bool HoverPending { get; init; }
    public bool AutoHidePending { get; init; }
    public bool PinPreference { get; init; }
    public bool PanelPointerInside { get; init; }
    public MonitorDeadline Deadline { get; init; } = MonitorDeadline.None;
    public RankingPresentation RankingPresentation { get; init; } = RankingPresentation.Disconnected;
    public byte VisibleRows { get; init; }
    public bool ContextMenuActive { get; init; }
    public bool PersistenceFailed { get; init; }
    public bool FullscreenYielded { get; init; }
}

public readonly record struct PresentationTargets(
    MonitorPresentationState VisibilityState,
    PixelRect TargetBounds,
    PixelRect Viewport,
    PixelRect HitBounds,
    DockSide DockSide,
    bool Topmost
);

public sealed record MonitorTransition(
    MonitorState State,
    PresentationTargets Targets,
    TimeSpan? NextDeadline,
    IReadOnlyList<EffectIntent> Effects,
    AnimationTarget Animation
);
