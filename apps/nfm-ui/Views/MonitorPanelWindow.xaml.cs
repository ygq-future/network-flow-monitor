using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using NetworkFlowMonitor.Interop;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Views;

public sealed class RankingRowItemModel : INotifyPropertyChanged {
    private ImageSource _icon;
    private double _translateY;
    private double _opacity = 1.0;
    private string _displayName;
    private string _downloadRateText;
    private string _uploadRateText;

    public string Id { get; }
    public string? ExecutablePath { get; }

    public string DisplayName {
        get => _displayName;
        set {
            if (_displayName != value) {
                _displayName = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(DisplayName)));
            }
        }
    }

    public string DownloadRateText {
        get => _downloadRateText;
        set {
            if (_downloadRateText != value) {
                _downloadRateText = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(DownloadRateText)));
            }
        }
    }

    public string UploadRateText {
        get => _uploadRateText;
        set {
            if (_uploadRateText != value) {
                _uploadRateText = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(UploadRateText)));
            }
        }
    }

    public ImageSource Icon {
        get => _icon;
        set {
            if (_icon != value) {
                _icon = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Icon)));
            }
        }
    }

    public double TranslateY {
        get => _translateY;
        set {
            if (Math.Abs(_translateY - value) > 0.001) {
                _translateY = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(TranslateY)));
            }
        }
    }

    public double Opacity {
        get => _opacity;
        set {
            if (Math.Abs(_opacity - value) > 0.001) {
                _opacity = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Opacity)));
            }
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public RankingRowItemModel(
        string id,
        string displayName,
        string? executablePath,
        string downloadRateText,
        string uploadRateText,
        ImageSource initialIcon) {
        Id = id;
        _displayName = displayName;
        ExecutablePath = executablePath;
        _downloadRateText = downloadRateText;
        _uploadRateText = uploadRateText;
        _icon = initialIcon;
    }
}

public partial class MonitorPanelWindow : Window {
    private Action<MonitorEvent>? _eventDispatcher;
    private HwndSourceHookAdapter? _hookAdapter;
    private ContextMenu? _contextMenu;
    private GlobalMouseHook? _mouseHook;
    private GlobalMouseHook? _outsideClickHook;
    private MenuItem? _hideMenuItem;
    private MenuItem? _pinMenuItem;
    private MenuItem? _retryMenuItem;
    private MenuItem? _settingsItem;
    private MenuItem? _exitItem;

    private readonly Dictionary<string, RankingRowItemModel> _existingRows = new(StringComparer.OrdinalIgnoreCase);
    private readonly ObservableCollection<RankingRowItemModel> _rowItems = [];

    private readonly List<RowTransition> _activeTransitions = [];
    private bool _isRenderingHooked;

    private sealed class RowTransition(
        RankingRowItemModel row,
        double startY,
        double targetY,
        double startOpacity,
        double targetOpacity,
        DateTime startTime,
        TimeSpan duration) {
        public RankingRowItemModel Row { get; } = row;
        public double StartY { get; } = startY;
        public double TargetY { get; } = targetY;
        public double StartOpacity { get; } = startOpacity;
        public double TargetOpacity { get; } = targetOpacity;
        public DateTime StartTime { get; } = startTime;
        public TimeSpan Duration { get; } = duration;
    }

    public MonitorPanelWindow() {
        InitializeComponent();
        SourceInitialized += OnSourceInitialized;
        MouseEnter += OnMouseEnter;
        MouseLeave += OnMouseLeave;
        MouseLeftButtonDown += OnMouseLeftButtonDown;
        MouseMove += OnMouseMove;
        MouseLeftButtonUp += OnMouseLeftButtonUp;
        MouseRightButtonUp += OnMouseRightButtonUp;

        RankingItemsControl.ItemsSource = _rowItems;

        SetupContextMenu();
        LocalizationService.LanguageChanged += UpdateLocalizedTexts;
        Closed += (_, _) => {
            LocalizationService.LanguageChanged -= UpdateLocalizedTexts;
            _outsideClickHook?.Dispose();
            _outsideClickHook = null;
            _mouseHook?.Dispose();
            _mouseHook = null;
            _hookAdapter?.Dispose();
            _hookAdapter = null;
        };
        UpdateLocalizedTexts();
    }

    public void Initialize(Action<MonitorEvent> eventDispatcher, Action? onOpenSettings = null, Action? onRestartCollector = null) {
        _eventDispatcher = eventDispatcher ?? throw new ArgumentNullException(nameof(eventDispatcher));

        if (_settingsItem != null && onOpenSettings != null) {
            _settingsItem.Click += (_, _) => {
                if (_contextMenu != null) _contextMenu.IsOpen = false;
                onOpenSettings();
            };
        }

        if (_retryMenuItem != null && onRestartCollector != null) {
            _retryMenuItem.Click += (_, _) => {
                if (_contextMenu != null) _contextMenu.IsOpen = false;
                onRestartCollector();
            };
        }
    }

    private RateUnitMode _rateUnitMode = RateUnitMode.AutoBytes;

    public void ApplyConfig(
        MonitorConfig config,
        double panelOpacity = 0.85,
        double blurStrength = 1.0,
        RateUnitMode rateUnits = RateUnitMode.AutoBytes) {
        _rateUnitMode = rateUnits;
        var alpha = (byte)Math.Clamp((int)(panelOpacity * 255), 25, 255);
        PanelCard.Background = new SolidColorBrush(Color.FromArgb(alpha, 16, 16, 20));

        var hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd != IntPtr.Zero) {
            if (blurStrength > 0.05) {
                DwmBackdropAdapter.ApplyBackdrop(hwnd, BackdropType.Acrylic, CornerPreference.Round, darkMode: true);
            } else {
                DwmBackdropAdapter.ApplyBackdrop(hwnd, BackdropType.None, CornerPreference.Round, darkMode: true);
            }
        }
    }

    private void OnSourceInitialized(object? sender, EventArgs e) {
        var hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd != IntPtr.Zero) {
            DwmBackdropAdapter.EnableBlurBehind(hwnd);
            DwmBackdropAdapter.ConfigureFloatingWindowStyles(hwnd, topmost: true);
            _hookAdapter = HwndSourceHookAdapter.Attach(hwnd, ev => _eventDispatcher?.Invoke(ev));
        }
    }

    private double _currentTargetHeight = 74.0;

    public void AnimateReveal(DockSide side, bool reducedMotion) {
        Show();
        StartOutsideClickHook();

        if (reducedMotion) {
            PanelCard.Opacity = 1.0;
            PanelCardScale.ScaleX = 1.0;
            PanelCardScale.ScaleY = 1.0;
            return;
        }

        PanelCard.RenderTransformOrigin = side == DockSide.Right ? new Point(1.0, 0.0) : new Point(0.0, 0.0);

        PanelCardScale.BeginAnimation(ScaleTransform.ScaleXProperty, null);
        PanelCardScale.BeginAnimation(ScaleTransform.ScaleYProperty, null);
        PanelCard.BeginAnimation(UIElement.OpacityProperty, null);

        PanelCardScale.ScaleX = 0.05;
        PanelCardScale.ScaleY = 0.05;
        PanelCard.Opacity = 0.0;

        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        var dur = TimeSpan.FromMilliseconds(220);

        var scaleXAnim = new DoubleAnimation(1.0, dur) { EasingFunction = ease };
        var scaleYAnim = new DoubleAnimation(1.0, dur) { EasingFunction = ease };
        var opacityAnim = new DoubleAnimation(1.0, TimeSpan.FromMilliseconds(180)) { EasingFunction = ease };

        PanelCardScale.BeginAnimation(ScaleTransform.ScaleXProperty, scaleXAnim, HandoffBehavior.SnapshotAndReplace);
        PanelCardScale.BeginAnimation(ScaleTransform.ScaleYProperty, scaleYAnim, HandoffBehavior.SnapshotAndReplace);
        PanelCard.BeginAnimation(UIElement.OpacityProperty, opacityAnim, HandoffBehavior.SnapshotAndReplace);
    }

    public void AnimateDismiss(DockSide side, bool reducedMotion, Action? onCompleted = null) {
        StopOutsideClickHook();

        if (reducedMotion) {
            PanelCard.Opacity = 0.0;
            onCompleted?.Invoke();
            return;
        }

        PanelCard.RenderTransformOrigin = side == DockSide.Right ? new Point(1.0, 0.0) : new Point(0.0, 0.0);
        var ease = new QuadraticEase { EasingMode = EasingMode.EaseIn };
        var dur = TimeSpan.FromMilliseconds(180);

        var scaleXAnim = new DoubleAnimation(0.05, dur) { EasingFunction = ease };
        var scaleYAnim = new DoubleAnimation(0.05, dur) { EasingFunction = ease };
        var opacityAnim = new DoubleAnimation(0.0, dur) { EasingFunction = ease };

        if (onCompleted != null) {
            opacityAnim.Completed += (_, _) => onCompleted();
        }

        PanelCardScale.BeginAnimation(ScaleTransform.ScaleXProperty, scaleXAnim, HandoffBehavior.SnapshotAndReplace);
        PanelCardScale.BeginAnimation(ScaleTransform.ScaleYProperty, scaleYAnim, HandoffBehavior.SnapshotAndReplace);
        PanelCard.BeginAnimation(UIElement.OpacityProperty, opacityAnim, HandoffBehavior.SnapshotAndReplace);
    }

    private void StartOutsideClickHook() {
        _outsideClickHook?.Dispose();
        _outsideClickHook = new GlobalMouseHook(screenPt => {
            Dispatcher.BeginInvoke(() => {
                if (_contextMenu != null && _contextMenu.IsOpen) {
                    return;
                }

                if (IsVisible) {
                    var source = PresentationSource.FromVisual(this) as HwndSource;
                    if (source != null && source.Handle != IntPtr.Zero &&
                        NativeMethods.GetWindowRect(source.Handle, out var lpRect)) {
                        var rect = new PixelRect(lpRect.Left, lpRect.Top, lpRect.Right, lpRect.Bottom);
                        if (!rect.Contains(screenPt)) {
                            // Instantly dismiss when clicking outside the panel
                            _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.DismissDeadline));
                        }
                    }
                }
            });
        });
    }

    private void StopOutsideClickHook() {
        _outsideClickHook?.Dispose();
        _outsideClickHook = null;
    }

    private void AnimateHeightTo(double targetHeight, bool reducedMotion) {
        if (Math.Abs(_currentTargetHeight - targetHeight) < 0.5) {
            return;
        }

        _currentTargetHeight = targetHeight;

        if (reducedMotion || ActualHeight <= 0) {
            BeginAnimation(HeightProperty, null);
            Height = targetHeight;
            return;
        }

        var anim = new DoubleAnimation {
            To = targetHeight,
            Duration = TimeSpan.FromMilliseconds(220),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        BeginAnimation(HeightProperty, anim, HandoffBehavior.SnapshotAndReplace);
    }

    private void SetupContextMenu() {
        var menuStyle = TryFindResource("FluentContextMenuStyle") as Style;
        var separatorStyle = TryFindResource("FluentSeparatorStyle") as Style;

        if (menuStyle == null) {
            try {
                var dict = new ResourceDictionary {
                    Source = new Uri("/NetworkFlowMonitor;component/Resources/FluentStyles.xaml", UriKind.RelativeOrAbsolute)
                };
                Resources.MergedDictionaries.Add(dict);
                menuStyle = dict["FluentContextMenuStyle"] as Style;
                separatorStyle = dict["FluentSeparatorStyle"] as Style;
            } catch {
                // Fallback to default
            }
        }

        _contextMenu = new ContextMenu {
            Style = menuStyle,
            StaysOpen = false,
            Placement = PlacementMode.MousePoint
        };

        _contextMenu.PreviewMouseRightButtonDown += (_, e) => e.Handled = true;
        _contextMenu.PreviewMouseRightButtonUp += (_, e) => e.Handled = true;

        _hideMenuItem = new MenuItem();
        _hideMenuItem.Click += (_, _) => {
            _contextMenu.IsOpen = false;
            _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.HideMonitorRequested));
        };
        _contextMenu.Items.Add(_hideMenuItem);

        _pinMenuItem = new MenuItem {
            IsCheckable = true
        };
        _pinMenuItem.Click += (_, _) => {
            _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.PinTogglePressed));
        };
        _contextMenu.Items.Add(_pinMenuItem);

        _contextMenu.Items.Add(new Separator { Style = separatorStyle });

        _retryMenuItem = new MenuItem();
        _contextMenu.Items.Add(_retryMenuItem);

        _settingsItem = new MenuItem();
        _contextMenu.Items.Add(_settingsItem);

        _contextMenu.Items.Add(new Separator { Style = separatorStyle });

        _exitItem = new MenuItem();
        _exitItem.Click += (_, _) => {
            _contextMenu.IsOpen = false;
            Application.Current.Shutdown();
        };
        _contextMenu.Items.Add(_exitItem);

        _contextMenu.Opened += (_, _) => {
            _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.ContextMenuOpened));

            _mouseHook?.Dispose();
            _mouseHook = new GlobalMouseHook(screenPt => {
                Dispatcher.BeginInvoke(() => {
                    if (_contextMenu != null && _contextMenu.IsOpen) {
                        var source = PresentationSource.FromVisual(_contextMenu) as HwndSource;
                        if (source != null && source.Handle != IntPtr.Zero &&
                            NativeMethods.GetWindowRect(source.Handle, out var lpRect)) {
                            var rect = new PixelRect(lpRect.Left, lpRect.Top, lpRect.Right, lpRect.Bottom);
                            if (!rect.Contains(screenPt)) {
                                _contextMenu.IsOpen = false;
                            }
                        }
                    }
                });
            });
        };

        _contextMenu.Closed += (_, _) => {
            _mouseHook?.Dispose();
            _mouseHook = null;

            var p = Mouse.GetPosition(this);
            var screenP = PointToScreen(p);
            _eventDispatcher?.Invoke(new MonitorEvent(
                MonitorEventKind.ContextMenuClosed,
                Point: new PixelPoint((int)screenP.X, (int)screenP.Y)
            ));
        };

        ContextMenu = _contextMenu;
    }

    private void OnMouseRightButtonUp(object sender, MouseButtonEventArgs e) {
        if (_contextMenu != null) {
            _contextMenu.PlacementTarget = this;
            _contextMenu.Placement = PlacementMode.MousePoint;
            _contextMenu.IsOpen = true;
            e.Handled = true;
        }
    }

    private void UpdateLocalizedTexts() {
        if (!Dispatcher.CheckAccess()) {
            Dispatcher.BeginInvoke(UpdateLocalizedTexts);
            return;
        }

        if (_hideMenuItem != null) _hideMenuItem.Header = LocalizationService.HideMonitor;
        if (_pinMenuItem != null) _pinMenuItem.Header = LocalizationService.Pin;
        if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RestartCollector;
        if (_settingsItem != null) _settingsItem.Header = LocalizationService.Settings;
        if (_exitItem != null) _exitItem.Header = LocalizationService.Exit;
    }

    public void UpdatePinState(bool pinned, bool autoHideEnabled) {
        if (_pinMenuItem != null) {
            _pinMenuItem.IsChecked = pinned;
            _pinMenuItem.IsEnabled = autoHideEnabled;
        }
    }

    public void UpdateSnapshot(TelemetrySnapshot snapshot, bool reducedMotion = false, int topN = int.MaxValue) {
        HeaderDownloadText.Text = RateFormatter.FormatRate(snapshot.DownloadBytesPerSec, _rateUnitMode);
        HeaderUploadText.Text = RateFormatter.FormatRate(snapshot.UploadBytesPerSec, _rateUnitMode);

        if (snapshot.ConnectionState != CollectorConnectionState.Connected) {
            StatusRowText.Text = LocalizationService.CollectorOffline;
            StatusRow.Visibility = Visibility.Visible;
            RankingScrollViewer.Visibility = Visibility.Collapsed;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RetryCollector;
            _rowItems.Clear();
            _existingRows.Clear();
            _activeTransitions.Clear();
            StopRenderingHook();
            AnimateHeightTo(74.0, reducedMotion);
        } else if (snapshot.DataState == TelemetryDataState.Warmup) {
            StatusRowText.Text = LocalizationService.CollectingData;
            StatusRow.Visibility = Visibility.Visible;
            RankingScrollViewer.Visibility = Visibility.Collapsed;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RestartCollector;
            _rowItems.Clear();
            _existingRows.Clear();
            _activeTransitions.Clear();
            StopRenderingHook();
            AnimateHeightTo(74.0, reducedMotion);
        } else if (snapshot.DataState == TelemetryDataState.Idle) {
            StatusRowText.Text = LocalizationService.NoNetworkActivity;
            StatusRow.Visibility = Visibility.Visible;
            RankingScrollViewer.Visibility = Visibility.Collapsed;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RestartCollector;
            _rowItems.Clear();
            _existingRows.Clear();
            _activeTransitions.Clear();
            StopRenderingHook();
            AnimateHeightTo(74.0, reducedMotion);
        } else {
            StatusRow.Visibility = Visibility.Collapsed;
            RankingScrollViewer.Visibility = Visibility.Visible;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RestartCollector;

            var apps = snapshot.LatestRanking?.Applications ?? [];
            if (topN > 0 && topN < apps.Length) {
                apps = apps[..topN];
            }
            var targetCount = Math.Max(1, apps.Length);
            var targetHeight = Math.Min(520.0, 36.0 + (targetCount * 28.0) + 10.0);
            AnimateHeightTo(targetHeight, reducedMotion);

            var oldIndexMap = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; i < _rowItems.Count; i++) {
                oldIndexMap[_rowItems[i].Id] = i;
            }

            var newRowList = new List<RankingRowItemModel>();
            var activeKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var now = DateTime.UtcNow;
            var animDuration = TimeSpan.FromMilliseconds(250);

            for (int newIndex = 0; newIndex < apps.Length; newIndex++) {
                var app = apps[newIndex];
                activeKeys.Add(app.Id);
                var dlText = RateFormatter.FormatRate(app.DownloadBytesPerSec, _rateUnitMode);
                var ulText = RateFormatter.FormatRate(app.UploadBytesPerSec, _rateUnitMode);
                var name = string.IsNullOrWhiteSpace(app.DisplayName) ? app.Id : app.DisplayName;

                if (_existingRows.TryGetValue(app.Id, out var existingRow)) {
                    existingRow.DisplayName = name;
                    existingRow.DownloadRateText = dlText;
                    existingRow.UploadRateText = ulText;

                    if (oldIndexMap.TryGetValue(app.Id, out var oldIndex) && oldIndex != newIndex && !reducedMotion) {
                        var deltaY = (oldIndex - newIndex) * 28.0;
                        StartRowTransition(existingRow, existingRow.TranslateY + deltaY, 0.0, existingRow.Opacity, 1.0, now, animDuration);
                    } else {
                        existingRow.TranslateY = 0.0;
                        existingRow.Opacity = 1.0;
                    }
                    newRowList.Add(existingRow);
                } else {
                    var newRow = new RankingRowItemModel(
                        app.Id,
                        name,
                        app.ExecutablePath,
                        dlText,
                        ulText,
                        AppIconCache.FallbackIcon
                    );
                    _existingRows[app.Id] = newRow;

                    if (!reducedMotion) {
                        newRow.Opacity = 0.0;
                        newRow.TranslateY = 14.0;
                        StartRowTransition(newRow, 14.0, 0.0, 0.0, 1.0, now, animDuration);
                    } else {
                        newRow.Opacity = 1.0;
                        newRow.TranslateY = 0.0;
                    }

                    newRowList.Add(newRow);

                    if (!string.IsNullOrWhiteSpace(app.ExecutablePath)) {
                        AppIconCache.GetIconAsync(app.ExecutablePath, app.IconKey).ContinueWith(t => {
                            if (t.IsCompletedSuccessfully && t.Result != null) {
                                Dispatcher.BeginInvoke(() => newRow.Icon = t.Result);
                            }
                        });
                    }
                }
            }

            var toRemove = _existingRows.Keys.Where(k => !activeKeys.Contains(k)).ToList();
            foreach (var k in toRemove) {
                _existingRows.Remove(k);
            }

            // In-place sync into ObservableCollection to keep visual containers alive
            for (int i = 0; i < newRowList.Count; i++) {
                if (i < _rowItems.Count) {
                    if (!ReferenceEquals(_rowItems[i], newRowList[i])) {
                        _rowItems[i] = newRowList[i];
                    }
                } else {
                    _rowItems.Add(newRowList[i]);
                }
            }
            while (_rowItems.Count > newRowList.Count) {
                _rowItems.RemoveAt(_rowItems.Count - 1);
            }
        }
    }

    private void StartRowTransition(
        RankingRowItemModel row,
        double startY,
        double targetY,
        double startOpacity,
        double targetOpacity,
        DateTime startTime,
        TimeSpan duration) {
        row.TranslateY = startY;
        row.Opacity = startOpacity;
        _activeTransitions.RemoveAll(t => t.Row.Id == row.Id);
        _activeTransitions.Add(new RowTransition(row, startY, targetY, startOpacity, targetOpacity, startTime, duration));
        EnsureRenderingHook();
    }

    private void EnsureRenderingHook() {
        if (!_isRenderingHooked && _activeTransitions.Count > 0) {
            _isRenderingHooked = true;
            CompositionTarget.Rendering += OnCompositionRendering;
        }
    }

    private void StopRenderingHook() {
        if (_isRenderingHooked) {
            _isRenderingHooked = false;
            CompositionTarget.Rendering -= OnCompositionRendering;
        }
    }

    private void OnCompositionRendering(object? sender, EventArgs e) {
        if (_activeTransitions.Count == 0) {
            StopRenderingHook();
            return;
        }

        var now = DateTime.UtcNow;
        for (int i = _activeTransitions.Count - 1; i >= 0; i--) {
            var t = _activeTransitions[i];
            var elapsed = (now - t.StartTime).TotalMilliseconds;
            var progress = Math.Clamp(elapsed / t.Duration.TotalMilliseconds, 0.0, 1.0);
            // Fluent Cubic Ease-Out: 1 - (1 - progress)^3
            var ease = 1.0 - Math.Pow(1.0 - progress, 3);

            t.Row.TranslateY = t.StartY + ((t.TargetY - t.StartY) * ease);
            t.Row.Opacity = t.StartOpacity + ((t.TargetOpacity - t.StartOpacity) * ease);

            if (progress >= 1.0) {
                t.Row.TranslateY = t.TargetY;
                t.Row.Opacity = t.TargetOpacity;
                _activeTransitions.RemoveAt(i);
            }
        }

        if (_activeTransitions.Count == 0) {
            StopRenderingHook();
        }
    }

    private void OnMouseEnter(object sender, MouseEventArgs e) {
        _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.PanelPointerEnter));
    }

    private void OnMouseLeave(object sender, MouseEventArgs e) {
        _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.PanelPointerLeave));
    }

    private void OnMouseLeftButtonDown(object sender, MouseButtonEventArgs e) {
        if (_contextMenu != null && _contextMenu.IsOpen) {
            _contextMenu.IsOpen = false;
        }

        CaptureMouse();
        var screenPoint = PointToScreen(e.GetPosition(this));
        _eventDispatcher?.Invoke(new MonitorEvent(
            MonitorEventKind.PanelPress,
            Point: new PixelPoint((int)screenPoint.X, (int)screenPoint.Y)
        ));
        e.Handled = true;
    }

    private void OnMouseMove(object sender, MouseEventArgs e) {
        if (IsMouseCaptured) {
            var screenPoint = PointToScreen(e.GetPosition(this));
            _eventDispatcher?.Invoke(new MonitorEvent(
                MonitorEventKind.PointerMove,
                Point: new PixelPoint((int)screenPoint.X, (int)screenPoint.Y)
            ));
            e.Handled = true;
        }
    }

    private void OnMouseLeftButtonUp(object sender, MouseButtonEventArgs e) {
        if (IsMouseCaptured) {
            ReleaseMouseCapture();
            var screenPoint = PointToScreen(e.GetPosition(this));
            _eventDispatcher?.Invoke(new MonitorEvent(
                MonitorEventKind.PanelRelease,
                Point: new PixelPoint((int)screenPoint.X, (int)screenPoint.Y)
            ));
            e.Handled = true;
        }
    }

    protected override void OnClosed(EventArgs e) {
        StopRenderingHook();
        StopOutsideClickHook();
        _mouseHook?.Dispose();
        _mouseHook = null;
        LocalizationService.LanguageChanged -= UpdateLocalizedTexts;
        _hookAdapter?.Dispose();
        base.OnClosed(e);
    }
}
