using System;
using System.Collections.Generic;
using System.Linq;

namespace NetworkFlowMonitor.Presentation;

public static class MonitorBehaviorModel {
    private static readonly TimeSpan MonitorMotionDuration = TimeSpan.FromMilliseconds(180);
    private static readonly TimeSpan ThemeTransitionDuration = TimeSpan.FromMilliseconds(150);

    private static TimeSpan EffectiveDuration(TimeSpan token, bool reducedMotion) =>
        reducedMotion ? TimeSpan.Zero : token;

    public static MonitorWorkArea? PrimaryMonitor(MonitorEnvironment environment) {
        if (environment.Monitors == null || environment.Monitors.Count == 0) {
            return null;
        }
        return environment.Monitors.FirstOrDefault(m => m.Primary) ?? environment.Monitors[0];
    }

    public static MonitorWorkArea? MonitorForPoint(MonitorEnvironment environment, PixelPoint point) {
        if (environment.Monitors == null || environment.Monitors.Count == 0) {
            return null;
        }

        foreach (var monitor in environment.Monitors) {
            if (point.X >= monitor.Rect.Left && point.X < monitor.Rect.Right &&
                point.Y >= monitor.Rect.Top && point.Y < monitor.Rect.Bottom) {
                return monitor;
            }
        }

        MonitorWorkArea? nearest = null;
        var nearestDistance = long.MaxValue;

        foreach (var monitor in environment.Monitors) {
            long dx = point.X < monitor.Rect.Left
                ? (long)monitor.Rect.Left - point.X
                : point.X >= monitor.Rect.Right
                    ? (long)point.X - monitor.Rect.Right + 1
                    : 0;

            long dy = point.Y < monitor.Rect.Top
                ? (long)monitor.Rect.Top - point.Y
                : point.Y >= monitor.Rect.Bottom
                    ? (long)point.Y - monitor.Rect.Bottom + 1
                    : 0;

            var distance = (dx * dx) + (dy * dy);
            if (distance < nearestDistance) {
                nearest = monitor;
                nearestDistance = distance;
            }
        }

        return nearest;
    }

    public static MonitorWorkArea? MonitorForAnchor(MonitorEnvironment environment, PersistedMonitorAnchor anchor) {
        if (environment.Monitors == null || environment.Monitors.Count == 0) {
            return null;
        }

        if (!string.IsNullOrEmpty(anchor.MonitorId)) {
            var match = environment.Monitors.FirstOrDefault(m => m.Id == anchor.MonitorId);
            if (match != null) {
                return match;
            }
        }

        return PrimaryMonitor(environment);
    }

    public static MonitorTransition Transition(
        MonitorState state,
        MonitorEvent @event,
        MonitorEnvironment environment,
        MonitorConfig config) {
        var nextState = state with { };
        var effects = new List<EffectIntent>();
        TimeSpan? nextDeadline = null;
        var animation = AnimationTarget.None;
        var previousSurface = state.State;

        switch (@event.Kind) {
            case MonitorEventKind.Start:
                var requestedAnchor = @event.Anchor ?? nextState.Anchor;
                var startMonitor = MonitorForAnchor(environment, requestedAnchor);
                if (startMonitor != null) {
                    nextState = nextState with {
                        Anchor = requestedAnchor,
                        PinPreference = config.Pinned,
                        State = (!config.AutoHideEnabled || config.Pinned)
                            ? MonitorPresentationState.PanelVisible
                            : MonitorPresentationState.CapsuleVisible
                    };

                    if (requestedAnchor.MonitorId != startMonitor.Id) {
                        var fallbackAnchor = nextState.Anchor with { MonitorId = startMonitor.Id };
                        nextState = nextState with { Anchor = fallbackAnchor };
                        effects.Add(new EffectIntent(MonitorEffectKind.PersistAnchor, Anchor: fallbackAnchor));
                    }
                }
                break;

            case MonitorEventKind.ConfigurationChanged:
                nextState = nextState with {
                    PinPreference = config.Pinned
                };

                if (!config.AutoHideEnabled || nextState.PinPreference) {
                    nextState = ClearDeadline(nextState) with {
                        State = MonitorPresentationState.PanelVisible
                    };
                } else {
                    (nextState, nextDeadline) = ScheduleDismiss(nextState, config);
                }
                break;

            case MonitorEventKind.EnvironmentChanged:
                var changedMonitor = MonitorForAnchor(environment, nextState.Anchor);
                if (changedMonitor != null && nextState.Anchor.MonitorId != changedMonitor.Id) {
                    var updatedAnchor = nextState.Anchor with { MonitorId = changedMonitor.Id };
                    nextState = nextState with { Anchor = updatedAnchor };
                    effects.Add(new EffectIntent(MonitorEffectKind.PersistAnchor, Anchor: updatedAnchor));
                }
                break;

            case MonitorEventKind.PersistFailed:
                nextState = nextState with { PersistenceFailed = true };
                effects.Add(new EffectIntent(MonitorEffectKind.ReportPersistenceFailure, Anchor: nextState.Anchor));
                break;

            case MonitorEventKind.PersistSucceeded:
                nextState = nextState with { PersistenceFailed = false };
                break;

            case MonitorEventKind.ThemeChanged:
                animation = new AnimationTarget(
                    MonitorAnimationKind.Theme,
                    EasingKind.Linear,
                    EffectiveDuration(ThemeTransitionDuration, config.ReducedMotion)
                );
                break;

            case MonitorEventKind.FullscreenOcclusionChanged:
                nextState = nextState with {
                    FullscreenYielded = config.YieldFullscreen && @event.IsFullscreenActive
                };
                break;

            default:
                (nextState, nextDeadline) = ApplyPointerOrActionEvent(nextState, @event, environment, config, effects);
                break;
        }

        // Compute geometry
        var activeMonitor = MonitorForAnchor(environment, nextState.Anchor);
        if (activeMonitor != null) {
            if (nextState.Dragging && @event.Kind != MonitorEventKind.PointerMove) {
                nextState = nextState with { Bounds = state.Bounds };
            } else {
                var bounds = MonitorLayout.BoundsFor(
                    nextState.Anchor,
                    activeMonitor,
                    config,
                    nextState.State,
                    nextState.RankingPresentation,
                    nextState.VisibleRows
                );
                nextState = nextState with { Bounds = bounds };
            }

            if (nextState.Dragging && @event.Kind == MonitorEventKind.PointerMove) {
                var width = nextState.Bounds.Width;
                var height = nextState.Bounds.Height;
                var left = Math.Clamp(
                    @event.Point.X - nextState.PressOffset.X,
                    activeMonitor.Rect.Left,
                    Math.Max(activeMonitor.Rect.Left, activeMonitor.Rect.Right - width)
                );
                var top = Math.Clamp(
                    @event.Point.Y - nextState.PressOffset.Y,
                    activeMonitor.Rect.Top,
                    Math.Max(activeMonitor.Rect.Top, activeMonitor.Rect.Bottom - height)
                );
                nextState = nextState with {
                    Bounds = new PixelRect(left, top, left + width, top + height)
                };
            }
        }

        var targetBounds = nextState.Bounds;
        PixelRect viewport = default;

        if (nextState.State == MonitorPresentationState.PanelVisible &&
            activeMonitor != null &&
            nextState.RankingPresentation == RankingPresentation.ReadyWithRows) {
            var dpi = activeMonitor.Dpi > 0 ? activeMonitor.Dpi : 96;
            var padding = MonitorLayout.DipToPixels(MonitorLayout.VerticalPaddingDip, dpi);
            var header = MonitorLayout.DipToPixels(MonitorLayout.HeaderRowDip, dpi);
            viewport = new PixelRect(
                targetBounds.Left + padding,
                targetBounds.Top + padding + header,
                targetBounds.Right - padding,
                Math.Max(targetBounds.Top + padding + header, targetBounds.Bottom - padding)
            );
        }

        var hitBounds = activeMonitor != null
            ? MonitorLayout.HitBoundsFor(targetBounds, nextState.Anchor.Side, activeMonitor, config, nextState.State)
            : targetBounds;

        var nextSurface = nextState.State;
        if (nextSurface != previousSurface) {
            if (nextSurface == MonitorPresentationState.PanelVisible) {
                animation = new AnimationTarget(
                    MonitorAnimationKind.Reveal,
                    EasingKind.EaseOut,
                    EffectiveDuration(MonitorMotionDuration, config.ReducedMotion)
                );
            } else if (previousSurface == MonitorPresentationState.PanelVisible) {
                animation = new AnimationTarget(
                    MonitorAnimationKind.Dismiss,
                    EasingKind.EaseIn,
                    EffectiveDuration(MonitorMotionDuration, config.ReducedMotion)
                );
            }
        }

        var topmost = !nextState.FullscreenYielded;
        var targets = new PresentationTargets(
            VisibilityState: nextState.State,
            TargetBounds: targetBounds,
            Viewport: viewport,
            HitBounds: hitBounds,
            DockSide: nextState.Anchor.Side,
            Topmost: topmost
        );

        return new MonitorTransition(
            State: nextState,
            Targets: targets,
            NextDeadline: nextDeadline,
            Effects: effects,
            Animation: animation
        );
    }

    private static MonitorState ClearDeadline(MonitorState state) =>
        state with {
            HoverPending = false,
            AutoHidePending = false,
            Deadline = MonitorDeadline.None
        };

    private static (MonitorState State, TimeSpan? NextDeadline) ScheduleDismiss(
        MonitorState state,
        MonitorConfig config) {
        if (state.ContextMenuActive || state.PanelPointerInside) {
            return (state, null);
        }

        var isPinned = !config.AutoHideEnabled || state.PinPreference;
        if (state.State == MonitorPresentationState.PanelVisible && !isPinned) {
            var updated = state with {
                AutoHidePending = true,
                Deadline = MonitorDeadline.Dismiss
            };
            return (updated, config.AutoHideDelay);
        }

        return (state, null);
    }

    private static (MonitorState State, TimeSpan? NextDeadline) ApplyPointerOrActionEvent(
        MonitorState state,
        MonitorEvent @event,
        MonitorEnvironment environment,
        MonitorConfig config,
        List<EffectIntent> effects) {
        if (state.ContextMenuActive &&
            @event.Kind != MonitorEventKind.ContextMenuClosed &&
            @event.Kind != MonitorEventKind.ContextMenuOpened &&
            @event.Kind != MonitorEventKind.ToggleVisibility &&
            @event.Kind != MonitorEventKind.HideMonitorRequested &&
            @event.Kind != MonitorEventKind.ShowMonitorRequested &&
            @event.Kind != MonitorEventKind.PinTogglePressed) {
            return (state, null);
        }

        var cur = state;
        TimeSpan? nextDeadline = null;

        switch (@event.Kind) {
            case MonitorEventKind.ShowCapsule:
                cur = ClearDeadline(cur) with { State = MonitorPresentationState.CapsuleVisible };
                break;

            case MonitorEventKind.ShowPanel:
                cur = ClearDeadline(cur) with { State = MonitorPresentationState.PanelVisible };
                break;

            case MonitorEventKind.HideMonitorRequested:
                cur = ClearDeadline(cur) with { State = MonitorPresentationState.TrayOnly };
                break;

            case MonitorEventKind.ShowMonitorRequested:
                var restoreState = (!config.AutoHideEnabled || cur.PinPreference)
                    ? MonitorPresentationState.PanelVisible
                    : MonitorPresentationState.CapsuleVisible;
                cur = ClearDeadline(cur) with { State = restoreState };
                break;

            case MonitorEventKind.ToggleVisibility:
                if (cur.State == MonitorPresentationState.TrayOnly) {
                    var showState = (!config.AutoHideEnabled || cur.PinPreference)
                        ? MonitorPresentationState.PanelVisible
                        : MonitorPresentationState.CapsuleVisible;
                    cur = ClearDeadline(cur) with { State = showState };
                } else {
                    cur = ClearDeadline(cur) with { State = MonitorPresentationState.TrayOnly };
                }
                break;

            case MonitorEventKind.CapsulePointerEnter:
                cur = cur with { AutoHidePending = false };
                if (cur.State == MonitorPresentationState.CapsuleVisible) {
                    cur = cur with {
                        HoverPending = true,
                        Deadline = MonitorDeadline.CapsuleReveal
                    };
                    nextDeadline = config.HoverDelay;
                }
                break;

            case MonitorEventKind.CapsulePointerLeave:
                cur = cur with {
                    HoverPending = false,
                    Deadline = MonitorDeadline.None
                };
                break;

            case MonitorEventKind.PanelPointerEnter:
                cur = ClearDeadline(cur) with { PanelPointerInside = true };
                break;

            case MonitorEventKind.PanelPointerLeave:
                cur = ClearDeadline(cur) with { PanelPointerInside = false };
                (cur, nextDeadline) = ScheduleDismiss(cur, config);
                break;

            case MonitorEventKind.HoverDeadline:
            case MonitorEventKind.DismissDeadline:
                if (cur.Deadline == MonitorDeadline.CapsuleReveal && cur.HoverPending && !cur.Dragging) {
                    cur = cur with {
                        State = MonitorPresentationState.PanelVisible,
                        PanelPointerInside = true
                    };
                } else if (cur.Deadline == MonitorDeadline.Dismiss && cur.AutoHidePending && !cur.Dragging) {
                    if (config.AutoHideEnabled && !cur.PinPreference) {
                        cur = cur with { State = MonitorPresentationState.CapsuleVisible };
                    }
                }
                cur = ClearDeadline(cur);
                break;

            case MonitorEventKind.PinTogglePressed:
                if (config.AutoHideEnabled && !cur.Dragging) {
                    var newPin = !cur.PinPreference;
                    cur = cur with { PinPreference = newPin };
                    effects.Add(new EffectIntent(MonitorEffectKind.PersistPin, Pinned: newPin));
                    cur = ClearDeadline(cur);
                    if (!newPin) {
                        (cur, nextDeadline) = ScheduleDismiss(cur, config);
                    }
                }
                break;

            case MonitorEventKind.CollectorDisconnected:
                cur = cur with {
                    RankingPresentation = RankingPresentation.Disconnected,
                    VisibleRows = 0
                };
                break;

            case MonitorEventKind.CollectorConnected:
            case MonitorEventKind.RankingWarming:
                cur = cur with {
                    RankingPresentation = RankingPresentation.WarmingUp,
                    VisibleRows = 0
                };
                break;

            case MonitorEventKind.RankingReadyIdle:
                cur = cur with {
                    RankingPresentation = RankingPresentation.ReadyIdle,
                    VisibleRows = 0
                };
                break;

            case MonitorEventKind.RankingReadyRows:
                cur = cur with {
                    RankingPresentation = RankingPresentation.ReadyWithRows,
                    VisibleRows = @event.VisibleRows
                };
                break;

            case MonitorEventKind.ContextMenuOpened:
                cur = ClearDeadline(cur) with { ContextMenuActive = true };
                break;

            case MonitorEventKind.ContextMenuClosed:
                var inside = cur.Bounds.Contains(@event.Point);
                cur = ClearDeadline(cur) with {
                    ContextMenuActive = false,
                    PanelPointerInside = inside
                };
                (cur, nextDeadline) = ScheduleDismiss(cur, config);
                break;

            case MonitorEventKind.PanelPress:
                cur = ClearDeadline(cur) with {
                    Pressing = true,
                    PressPoint = @event.Point,
                    PressOffset = new PixelPoint(@event.Point.X - cur.Bounds.Left, @event.Point.Y - cur.Bounds.Top),
                    DragOrigin = cur.Anchor
                };
                break;

            case MonitorEventKind.PointerMove:
                if (cur.Pressing &&
                    (Math.Abs(@event.Point.X - cur.PressPoint.X) >= config.MinimumDragDistanceXPixels ||
                     Math.Abs(@event.Point.Y - cur.PressPoint.Y) >= config.MinimumDragDistanceYPixels)) {
                    cur = cur with {
                        Dragging = true,
                        HoverPending = false
                    };

                    var monitor = MonitorForPoint(environment, @event.Point);
                    if (monitor != null) {
                        var docked = MonitorLayout.DockAt(
                            monitor,
                            @event.Point,
                            cur.PressOffset,
                            cur.DragOrigin.Side,
                            config,
                            cur.State,
                            cur.RankingPresentation,
                            cur.VisibleRows
                        );
                        cur = cur with { Anchor = docked };
                    }
                }
                break;

            case MonitorEventKind.PanelRelease:
                if (cur.Pressing && !cur.Dragging) {
                    if (cur.State == MonitorPresentationState.CapsuleVisible) {
                        cur = cur with { State = MonitorPresentationState.PanelVisible };
                    }
                } else if (cur.Dragging) {
                    var releaseMonitor = MonitorForPoint(environment, @event.Point);
                    if (releaseMonitor != null) {
                        var releaseAnchor = MonitorLayout.DockAt(
                            releaseMonitor,
                            @event.Point,
                            cur.PressOffset,
                            cur.DragOrigin.Side,
                            config,
                            cur.State,
                            cur.RankingPresentation,
                            cur.VisibleRows
                        );
                        cur = cur with { Anchor = releaseAnchor };
                        effects.Add(new EffectIntent(MonitorEffectKind.PersistAnchor, Anchor: releaseAnchor));
                    }
                }

                cur = ClearDeadline(cur) with {
                    Pressing = false,
                    Dragging = false
                };
                break;

            case MonitorEventKind.DragCancelled:
                if (cur.Dragging) {
                    cur = cur with { Anchor = cur.DragOrigin };
                }
                cur = ClearDeadline(cur) with {
                    Pressing = false,
                    Dragging = false
                };
                break;
        }

        return (cur, nextDeadline);
    }
}
