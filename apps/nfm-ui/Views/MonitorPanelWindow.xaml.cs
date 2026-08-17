using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using NetworkFlowMonitor.Interop;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Views;

public partial class MonitorPanelWindow : Window {
    private Action<MonitorEvent>? _eventDispatcher;
    private HwndSourceHookAdapter? _hookAdapter;
    private ContextMenu? _contextMenu;
    private GlobalMouseHook? _mouseHook;
    private MenuItem? _hideMenuItem;
    private MenuItem? _pinMenuItem;
    private MenuItem? _retryMenuItem;
    private MenuItem? _settingsItem;
    private MenuItem? _exitItem;

    public MonitorPanelWindow() {
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
        UpdateLocalizedTexts();
    }

    public void Initialize(Action<MonitorEvent> eventDispatcher) {
        _eventDispatcher = eventDispatcher ?? throw new ArgumentNullException(nameof(eventDispatcher));
    }

    private void OnSourceInitialized(object? sender, EventArgs e) {
        var hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd != IntPtr.Zero) {
            DwmBackdropAdapter.ApplyBackdrop(hwnd, BackdropType.Acrylic, CornerPreference.Round, darkMode: true);
            DwmBackdropAdapter.ConfigureFloatingWindowStyles(hwnd, topmost: true);
            _hookAdapter = HwndSourceHookAdapter.Attach(hwnd, ev => _eventDispatcher?.Invoke(ev));
        }
    }

    private void SetupContextMenu() {
        var menuStyle = (Style)FindResource("FluentContextMenuStyle");
        var separatorStyle = (Style)FindResource("FluentSeparatorStyle");

        _contextMenu = new ContextMenu {
            Style = menuStyle,
            StaysOpen = false,
            Placement = PlacementMode.MousePoint
        };

        // Suppress all right-clicks on context menu items: only left-click can activate menu items
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

            // Start global mouse hook to detect click outside context menu anywhere on screen
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

    public void UpdateSnapshot(TelemetrySnapshot snapshot) {
        HeaderDownloadText.Text = RateFormatter.FormatRate(snapshot.DownloadBytesPerSec);
        HeaderUploadText.Text = RateFormatter.FormatRate(snapshot.UploadBytesPerSec);

        if (snapshot.ConnectionState != CollectorConnectionState.Connected) {
            StatusRowText.Text = LocalizationService.CollectorOffline;
            StatusRow.Visibility = Visibility.Visible;
            RankingItemsControl.Visibility = Visibility.Collapsed;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RetryCollector;
        } else if (snapshot.DataState == TelemetryDataState.Warmup) {
            StatusRowText.Text = LocalizationService.CollectingData;
            StatusRow.Visibility = Visibility.Visible;
            RankingItemsControl.Visibility = Visibility.Collapsed;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RestartCollector;
        } else if (snapshot.DataState == TelemetryDataState.Idle) {
            StatusRowText.Text = LocalizationService.NoNetworkActivity;
            StatusRow.Visibility = Visibility.Visible;
            RankingItemsControl.Visibility = Visibility.Collapsed;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RestartCollector;
        } else {
            StatusRow.Visibility = Visibility.Collapsed;
            RankingItemsControl.Visibility = Visibility.Visible;
            if (_retryMenuItem != null) _retryMenuItem.Header = LocalizationService.RestartCollector;

            var items = snapshot.LatestRanking?.Applications?
                .Select(app => new RankingRowItem(
                    DisplayName: string.IsNullOrWhiteSpace(app.DisplayName) ? app.Id : app.DisplayName,
                    ExecutablePath: app.ExecutablePath,
                    DownloadRateText: RateFormatter.FormatRate(app.DownloadBytesPerSec),
                    UploadRateText: RateFormatter.FormatRate(app.UploadBytesPerSec)
                ))
                .ToList() ?? [];

            RankingItemsControl.ItemsSource = items;
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
        _mouseHook?.Dispose();
        _mouseHook = null;
        LocalizationService.LanguageChanged -= UpdateLocalizedTexts;
        _hookAdapter?.Dispose();
        base.OnClosed(e);
    }
}
