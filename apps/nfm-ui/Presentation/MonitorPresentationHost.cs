using System;
using System.Windows;
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

    private MonitorState _state = new();
    private MonitorConfig _config = new();
    private MonitorEnvironment _environment;
    private bool _disposed;

    public event Action<PersistedMonitorAnchor>? AnchorPersisted;
    public event Action<bool>? PinPersisted;

    public MonitorPresentationHost(
        TelemetryStore store,
        DockCapsuleWindow capsuleWindow,
        MonitorPanelWindow panelWindow,
        MonitorConfig? config = null) {
        _store = store ?? throw new ArgumentNullException(nameof(store));
        _capsuleWindow = capsuleWindow ?? throw new ArgumentNullException(nameof(capsuleWindow));
        _panelWindow = panelWindow ?? throw new ArgumentNullException(nameof(panelWindow));
        _config = config ?? new MonitorConfig();

        _environment = MonitorEnvironmentProvider.GetCurrentEnvironment();

        _capsuleWindow.Initialize(Dispatch);
        _panelWindow.Initialize(Dispatch);

        _deadlineTimer = new DispatcherTimer();
        _deadlineTimer.Tick += OnDeadlineTimerTick;

        _store.SnapshotUpdated += OnSnapshotUpdated;
    }

    public MonitorState State => _state;
    public MonitorConfig Config => _config;

    public void Start(PersistedMonitorAnchor? initialAnchor = null) {
        _environment = MonitorEnvironmentProvider.GetCurrentEnvironment();
        Dispatch(new MonitorEvent(MonitorEventKind.Start, Anchor: initialAnchor));
    }

    public void UpdateConfig(MonitorConfig config) {
        _config = config ?? throw new ArgumentNullException(nameof(config));
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
    }

    private MonitorPresentationState _currentVisibility = MonitorPresentationState.CapsuleVisible;

    private void ApplyPresentationTargets(PresentationTargets targets) {
        var prevVisibility = _currentVisibility;
        _currentVisibility = targets.VisibilityState;

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
                } else {
                    _capsuleWindow.Hide();
                    _panelWindow.Show();
                }
                break;

            case MonitorPresentationState.TrayOnly:
                _capsuleWindow.Hide();
                _panelWindow.Hide();
                break;
        }
    }

    private static void PositionWindow(Window window, PixelRect bounds, bool isCapsule) {
        // Convert screen pixel bounds to WPF DIP coordinates
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
                var visibleRows = (byte)(snapshot.LatestRanking?.Applications?.Length ?? 0);
                Dispatch(new MonitorEvent(MonitorEventKind.RankingReadyRows, VisibleRows: visibleRows));
            }

            // Update visuals
            var capsuleRates = MonitorLayout.MapCapsuleRates(snapshot.DownloadBytesPerSec, snapshot.UploadBytesPerSec);
            _capsuleWindow.UpdateRates(capsuleRates, _config.ReducedMotion);
            _panelWindow.UpdateSnapshot(snapshot, _config.ReducedMotion);
        });
    }

    public void Dispose() {
        if (!_disposed) {
            _disposed = true;
            _deadlineTimer.Stop();
            _store.SnapshotUpdated -= OnSnapshotUpdated;
            _capsuleWindow.Close();
            _panelWindow.Close();
        }
    }
}
