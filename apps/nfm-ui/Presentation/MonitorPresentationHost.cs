using System;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;
using NetworkFlowMonitor.Interop;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.Views;

namespace NetworkFlowMonitor.Presentation;

public sealed class MonitorPresentationHost : IDisposable {
    private readonly TelemetryStore _store;
    private readonly DockCapsuleWindow _capsuleWindow;
    private readonly MonitorPanelWindow _panelWindow;
    private readonly DispatcherTimer _deadlineTimer;
    private readonly DispatcherTimer _fullscreenTimer;

    private MonitorState _state = new();
    private MonitorConfig _config = new();
    private MonitorEnvironment _environment;
    private bool _disposed;
    private bool _lastFullscreenActive;

    public event Action<PersistedMonitorAnchor>? AnchorPersisted;
    public event Action<bool>? PinPersisted;
    public event Action<MonitorState>? StateChanged;

    public MonitorPresentationHost(
        TelemetryStore store,
        DockCapsuleWindow capsuleWindow,
        MonitorPanelWindow panelWindow,
        MonitorConfig? config = null,
        Action? onOpenSettings = null,
        Action? onRestartCollector = null) {
        _store = store ?? throw new ArgumentNullException(nameof(store));
        _capsuleWindow = capsuleWindow ?? throw new ArgumentNullException(nameof(capsuleWindow));
        _panelWindow = panelWindow ?? throw new ArgumentNullException(nameof(panelWindow));
        _config = config ?? new MonitorConfig();

        _environment = MonitorEnvironmentProvider.GetCurrentEnvironment();

        _capsuleWindow.Initialize(Dispatch, onOpenSettings, onRestartCollector);
        _panelWindow.Initialize(Dispatch, onOpenSettings, onRestartCollector);

        _deadlineTimer = new DispatcherTimer();
        _deadlineTimer.Tick += OnDeadlineTimerTick;

        _fullscreenTimer = new DispatcherTimer {
            Interval = TimeSpan.FromMilliseconds(500)
        };
        _fullscreenTimer.Tick += OnFullscreenTimerTick;
        _fullscreenTimer.Start();

        _store.SnapshotUpdated += OnSnapshotUpdated;

        _capsuleWindow.ApplyConfig(_config);
    }

    public MonitorState State => _state;
    public MonitorConfig Config => _config;

    public void ShowMonitor() => Dispatch(new MonitorEvent(MonitorEventKind.ShowMonitorRequested));
    public void HideMonitor() => Dispatch(new MonitorEvent(MonitorEventKind.HideMonitorRequested));
    public void ToggleVisibility() => Dispatch(new MonitorEvent(MonitorEventKind.ToggleVisibility));
    public void TogglePin() => Dispatch(new MonitorEvent(MonitorEventKind.PinTogglePressed));

    public void Start(PersistedMonitorAnchor? initialAnchor = null) {
        _environment = MonitorEnvironmentProvider.GetCurrentEnvironment();
        Dispatch(new MonitorEvent(MonitorEventKind.Start, Anchor: initialAnchor));
    }

    public void UpdateConfig(
        MonitorConfig config,
        double panelOpacity = 0.85,
        double blurStrength = 1.0,
        RateUnitMode rateUnits = RateUnitMode.AutoBytes) {
        _config = config ?? throw new ArgumentNullException(nameof(config));
        _capsuleWindow.ApplyConfig(_config);
        _panelWindow.ApplyConfig(_config, panelOpacity, blurStrength, rateUnits);
        Dispatch(new MonitorEvent(MonitorEventKind.ConfigurationChanged));
    }

    public void Dispatch(MonitorEvent @event) {
        var transition = MonitorBehaviorModel.Transition(_state, @event, _environment, _config);
        _state = transition.State;

        // Manage deadlines
        if (transition.NextDeadline.HasValue) {
            _deadlineTimer.Stop();
            _deadlineTimer.Interval = transition.NextDeadline.Value;
            _deadlineTimer.Start();
        } else if (_state.Deadline == MonitorDeadline.None) {
            _deadlineTimer.Stop();
        }

        // Apply effects
        foreach (var effect in transition.Effects) {
            switch (effect.Kind) {
                case MonitorEffectKind.PersistAnchor:
                    AnchorPersisted?.Invoke(effect.Anchor);
                    break;
                case MonitorEffectKind.PersistPin:
                    PinPersisted?.Invoke(effect.Pinned);
                    break;
            }
        }

        // Apply Presentation Targets
        ApplyPresentationTargets(transition.Targets);

        StateChanged?.Invoke(_state);
    }

    private MonitorPresentationState _currentVisibility = MonitorPresentationState.CapsuleVisible;

    private void ApplyPresentationTargets(PresentationTargets targets) {
        var prevVisibility = _currentVisibility;
        _currentVisibility = targets.VisibilityState;

        // Update topmost state on windows
        ApplyTopmostState(_capsuleWindow, targets.Topmost);
        ApplyTopmostState(_panelWindow, targets.Topmost);

        switch (targets.VisibilityState) {
            case MonitorPresentationState.CapsuleVisible:
                PositionWindow(_capsuleWindow, targets.TargetBounds, isCapsule: true);
                _capsuleWindow.UpdateDockSide(targets.DockSide);
                _capsuleWindow.UpdatePinState(_state.PinPreference, _config.AutoHideEnabled);

                if (prevVisibility == MonitorPresentationState.PanelVisible) {
                    _panelWindow.AnimateDismiss(targets.DockSide, _config.ReducedMotion, () => _panelWindow.Hide());
                    _capsuleWindow.AnimateReveal(targets.DockSide, _config.ReducedMotion);
                } else {
                    _panelWindow.Hide();
                    _capsuleWindow.Show();
                }
                break;

            case MonitorPresentationState.PanelVisible:
                PositionWindow(_panelWindow, targets.TargetBounds, isCapsule: false);
                _panelWindow.UpdatePinState(_state.PinPreference, _config.AutoHideEnabled);

                if (prevVisibility != MonitorPresentationState.PanelVisible) {
                    _capsuleWindow.Hide();
                    _panelWindow.AnimateReveal(targets.DockSide, _config.ReducedMotion);
                }
                // Panel already visible: position and pin state updated above.
                // No Hide/Show cycle — that caused visible flickering on every snapshot.
                break;

            case MonitorPresentationState.TrayOnly:
                _capsuleWindow.Hide();
                _panelWindow.Hide();
                break;
        }
    }

    private static void ApplyTopmostState(Window window, bool topmost) {
        window.Topmost = topmost;
        var source = PresentationSource.FromVisual(window) as HwndSource;
        if (source != null && source.Handle != IntPtr.Zero) {
            DwmBackdropAdapter.SetTopmostState(source.Handle, topmost);
        }
    }

    private static void PositionWindow(Window window, PixelRect bounds, bool isCapsule) {
        var source = PresentationSource.FromVisual(window);
        var dpiX = source?.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
        var dpiY = source?.CompositionTarget?.TransformToDevice.M22 ?? 1.0;

        window.Left = bounds.Left / dpiX;
        window.Top = bounds.Top / dpiY;
        if (bounds.Width > 0) {
            window.Width = bounds.Width / dpiX;
        }
        if (isCapsule && bounds.Height > 0) {
            window.Height = bounds.Height / dpiY;
        }
    }

    private void OnDeadlineTimerTick(object? sender, EventArgs e) {
        _deadlineTimer.Stop();
        Dispatch(new MonitorEvent(MonitorEventKind.HoverDeadline));
    }

    private void OnFullscreenTimerTick(object? sender, EventArgs e) {
        if (_disposed || !_config.YieldFullscreen) return;

        var activeMonitor = MonitorBehaviorModel.ActiveMonitor(_environment, _state.Anchor);
        if (activeMonitor == null) return;

        var isFullscreen = MonitorEnvironmentProvider.CheckFullscreenActive(activeMonitor.Rect);
        if (isFullscreen != _lastFullscreenActive) {
            _lastFullscreenActive = isFullscreen;
            _environment = new MonitorEnvironment(_environment.Monitors, isFullscreen);
            Dispatch(new MonitorEvent(MonitorEventKind.FullscreenOcclusionChanged, IsFullscreenActive: isFullscreen));
        }
    }

    private void OnSnapshotUpdated(TelemetrySnapshot snapshot) {
        Application.Current?.Dispatcher.BeginInvoke(() => {
            if (_disposed) return;

            // Map data state into behavior model event if state changed
            if (snapshot.ConnectionState != CollectorConnectionState.Connected) {
                Dispatch(new MonitorEvent(MonitorEventKind.CollectorDisconnected));
            } else if (snapshot.DataState == TelemetryDataState.Warmup) {
                Dispatch(new MonitorEvent(MonitorEventKind.RankingWarming));
            } else if (snapshot.DataState == TelemetryDataState.Idle) {
                Dispatch(new MonitorEvent(MonitorEventKind.RankingReadyIdle));
            } else {
                var rawCount = snapshot.LatestRanking?.Applications?.Length ?? 0;
                var visibleRows = (byte)Math.Min(rawCount, _config.TopN);
                Dispatch(new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: visibleRows));
            }

            // Update visuals
            var capsuleRates = MonitorLayout.MapCapsuleRates(snapshot.DownloadBytesPerSec, snapshot.UploadBytesPerSec);
            _capsuleWindow.UpdateRates(capsuleRates, _config.ReducedMotion);
            _panelWindow.UpdateSnapshot(snapshot, _config.ReducedMotion, _config.TopN);
        });
    }

    public void Dispose() {
        if (!_disposed) {
            _disposed = true;
            _deadlineTimer.Stop();
            _fullscreenTimer.Stop();
            _store.SnapshotUpdated -= OnSnapshotUpdated;
            _capsuleWindow.Close();
            _panelWindow.Close();
        }
    }
}
