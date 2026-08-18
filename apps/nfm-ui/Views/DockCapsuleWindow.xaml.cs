using System;
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

namespace NetworkFlowMonitor.Views;

public partial class DockCapsuleWindow : Window {
    private Action<MonitorEvent>? _eventDispatcher;
    private HwndSourceHookAdapter? _hookAdapter;
    private ContextMenu? _contextMenu;
    private GlobalMouseHook? _mouseHook;
    private MenuItem? _hideMenuItem;
    private MenuItem? _pinMenuItem;
    private MenuItem? _retryMenuItem;
    private MenuItem? _settingsItem;
    private MenuItem? _exitItem;

    private float _currentDownloadFill;
    private float _targetDownloadFill;
    private float _currentUploadFill;
    private float _targetUploadFill;

    public DockCapsuleWindow() {
        InitializeComponent();
        SourceInitialized += OnSourceInitialized;
        MouseEnter += OnMouseEnter;
        MouseLeave += OnMouseLeave;
        MouseLeftButtonDown += OnMouseLeftButtonDown;
        MouseMove += OnMouseMove;
        MouseLeftButtonUp += OnMouseLeftButtonUp;
        MouseRightButtonUp += OnMouseRightButtonUp;

        SetupContextMenu();
        LocalizationService.LanguageChanged += UpdateLocalizedTexts;
        Closed += (_, _) => {
            LocalizationService.LanguageChanged -= UpdateLocalizedTexts;
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

    public void ApplyConfig(MonitorConfig config) {
        if (config == null) return;
        Width = config.CapsuleWidthDip;
        Height = config.CapsuleHeightDip;
        CapsulePill.Width = config.CapsuleWidthDip;
        CapsulePill.Height = config.CapsuleHeightDip;
        CapsulePill.CornerRadius = new CornerRadius(config.CapsuleWidthDip / 2.0);

        var halfHeight = config.CapsuleHeightDip / 2.0;
        if (CapsulePill.Child is Grid grid && grid.RowDefinitions.Count >= 2) {
            grid.RowDefinitions[0].Height = new GridLength(halfHeight);
            grid.RowDefinitions[1].Height = new GridLength(halfHeight);
        }
    }

    private void OnSourceInitialized(object? sender, EventArgs e) {
        var hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd != IntPtr.Zero) {
            DwmBackdropAdapter.ConfigureFloatingWindowStyles(hwnd, topmost: true);
            _hookAdapter = HwndSourceHookAdapter.Attach(hwnd, ev => _eventDispatcher?.Invoke(ev));
        }
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

    public void AnimateReveal(DockSide side, bool reducedMotion) {
        Show();
        if (reducedMotion) {
            CapsulePill.Opacity = 1.0;
            CapsulePillTranslate.X = 0;
            CapsulePillScale.ScaleX = 1.0;
            CapsulePillScale.ScaleY = 1.0;
            return;
        }

        var startX = side == DockSide.Right ? 6.0 : -6.0;
        CapsulePillTranslate.BeginAnimation(TranslateTransform.XProperty, null);
        CapsulePillScale.BeginAnimation(ScaleTransform.ScaleXProperty, null);
        CapsulePillScale.BeginAnimation(ScaleTransform.ScaleYProperty, null);
        CapsulePill.BeginAnimation(UIElement.OpacityProperty, null);

        CapsulePillTranslate.X = startX;
        CapsulePillScale.ScaleX = 0.5;
        CapsulePillScale.ScaleY = 0.5;
        CapsulePill.Opacity = 0.0;

        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        var dur = TimeSpan.FromMilliseconds(180);

        var slideAnim = new DoubleAnimation(0.0, dur) { EasingFunction = ease };
        var scaleAnim = new DoubleAnimation(1.0, dur) { EasingFunction = ease };
        var opacityAnim = new DoubleAnimation(1.0, dur) { EasingFunction = ease };

        CapsulePillTranslate.BeginAnimation(TranslateTransform.XProperty, slideAnim, HandoffBehavior.SnapshotAndReplace);
        CapsulePillScale.BeginAnimation(ScaleTransform.ScaleXProperty, scaleAnim, HandoffBehavior.SnapshotAndReplace);
        CapsulePillScale.BeginAnimation(ScaleTransform.ScaleYProperty, scaleAnim, HandoffBehavior.SnapshotAndReplace);
        CapsulePill.BeginAnimation(UIElement.OpacityProperty, opacityAnim, HandoffBehavior.SnapshotAndReplace);
    }

    public void AnimateDismiss(DockSide side, bool reducedMotion, Action? onCompleted = null) {
        if (reducedMotion) {
            CapsulePill.Opacity = 0.0;
            onCompleted?.Invoke();
            return;
        }

        var targetX = side == DockSide.Right ? 6.0 : -6.0;
        var ease = new QuadraticEase { EasingMode = EasingMode.EaseIn };
        var dur = TimeSpan.FromMilliseconds(150);

        var slideAnim = new DoubleAnimation(targetX, dur) { EasingFunction = ease };
        var scaleAnim = new DoubleAnimation(0.5, dur) { EasingFunction = ease };
        var opacityAnim = new DoubleAnimation(0.0, dur) { EasingFunction = ease };

        if (onCompleted != null) {
            opacityAnim.Completed += (_, _) => onCompleted();
        }

        CapsulePillTranslate.BeginAnimation(TranslateTransform.XProperty, slideAnim, HandoffBehavior.SnapshotAndReplace);
        CapsulePillScale.BeginAnimation(ScaleTransform.ScaleXProperty, scaleAnim, HandoffBehavior.SnapshotAndReplace);
        CapsulePillScale.BeginAnimation(ScaleTransform.ScaleYProperty, scaleAnim, HandoffBehavior.SnapshotAndReplace);
        CapsulePill.BeginAnimation(UIElement.OpacityProperty, opacityAnim, HandoffBehavior.SnapshotAndReplace);
    }

    public void UpdateDockSide(DockSide side) {
        CapsulePill.HorizontalAlignment = side == DockSide.Right ? HorizontalAlignment.Right : HorizontalAlignment.Left;
    }

    public void UpdateRates(CapsulePresentation presentation, bool reducedMotion = false) {
        AnimateDownloadTrack(presentation.Download.Fill, presentation.Download.ColorRgb, reducedMotion);
        AnimateUploadTrack(presentation.Upload.Fill, presentation.Upload.ColorRgb, reducedMotion);
    }

    private void AnimateDownloadTrack(float targetFill, uint targetColorRgb, bool reducedMotion) {
        if (Math.Abs(_targetDownloadFill - targetFill) < 0.001f &&
            Math.Abs(_currentDownloadFill - targetFill) < 0.001f) {
            return;
        }

        _targetDownloadFill = targetFill;
        var ballistics = MonitorLayout.GetBallistics(_currentDownloadFill, targetFill, reducedMotion);
        var targetHeight = Math.Clamp(targetFill * 32.0, 0.0, 32.0);

        if (ballistics.Duration == TimeSpan.Zero) {
            DownloadFillRect.BeginAnimation(HeightProperty, null);
            DlStop2.BeginAnimation(GradientStop.ColorProperty, null);
            DownloadFillRect.Height = targetHeight;
            _currentDownloadFill = targetFill;
            DlStop2.Color = ColorFromRgb(targetColorRgb);
            return;
        }

        var heightAnim = new DoubleAnimation {
            To = targetHeight,
            Duration = ballistics.Duration,
            EasingFunction = ballistics.EasingFunction
        };
        heightAnim.Completed += (_, _) => _currentDownloadFill = targetFill;
        DownloadFillRect.BeginAnimation(HeightProperty, heightAnim, HandoffBehavior.SnapshotAndReplace);

        var targetColor = ColorFromRgb(targetColorRgb);
        var colorAnim = new ColorAnimation {
            To = targetColor,
            Duration = ballistics.Duration,
            EasingFunction = ballistics.EasingFunction
        };
        DlStop2.BeginAnimation(GradientStop.ColorProperty, colorAnim, HandoffBehavior.SnapshotAndReplace);
    }

    private void AnimateUploadTrack(float targetFill, uint targetColorRgb, bool reducedMotion) {
        if (Math.Abs(_targetUploadFill - targetFill) < 0.001f &&
            Math.Abs(_currentUploadFill - targetFill) < 0.001f) {
            return;
        }

        _targetUploadFill = targetFill;
        var ballistics = MonitorLayout.GetBallistics(_currentUploadFill, targetFill, reducedMotion);
        var targetHeight = Math.Clamp(targetFill * 32.0, 0.0, 32.0);

        if (ballistics.Duration == TimeSpan.Zero) {
            UploadFillRect.BeginAnimation(HeightProperty, null);
            UlStop2.BeginAnimation(GradientStop.ColorProperty, null);
            UploadFillRect.Height = targetHeight;
            _currentUploadFill = targetFill;
            UlStop2.Color = ColorFromRgb(targetColorRgb);
            return;
        }

        var heightAnim = new DoubleAnimation {
            To = targetHeight,
            Duration = ballistics.Duration,
            EasingFunction = ballistics.EasingFunction
        };
        heightAnim.Completed += (_, _) => _currentUploadFill = targetFill;
        UploadFillRect.BeginAnimation(HeightProperty, heightAnim, HandoffBehavior.SnapshotAndReplace);

        var targetColor = ColorFromRgb(targetColorRgb);
        var colorAnim = new ColorAnimation {
            To = targetColor,
            Duration = ballistics.Duration,
            EasingFunction = ballistics.EasingFunction
        };
        UlStop2.BeginAnimation(GradientStop.ColorProperty, colorAnim, HandoffBehavior.SnapshotAndReplace);
    }

    private static Color ColorFromRgb(uint rgb) {
        return Color.FromRgb(
            (byte)((rgb >> 16) & 0xFF),
            (byte)((rgb >> 8) & 0xFF),
            (byte)(rgb & 0xFF)
        );
    }

    private void OnMouseEnter(object sender, MouseEventArgs e) {
        _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.CapsulePointerEnter));
    }

    private void OnMouseLeave(object sender, MouseEventArgs e) {
        _eventDispatcher?.Invoke(new MonitorEvent(MonitorEventKind.CapsulePointerLeave));
    }

    private void OnMouseLeftButtonDown(object sender, MouseButtonEventArgs e) {
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
        _mouseHook?.Dispose();
        _mouseHook = null;
        LocalizationService.LanguageChanged -= UpdateLocalizedTexts;
        _hookAdapter?.Dispose();
        base.OnClosed(e);
    }
}
