using System;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using NetworkFlowMonitor.Interop;
using NetworkFlowMonitor.Presentation;

namespace NetworkFlowMonitor.Views;

public partial class DockCapsuleWindow : Window {
    private Action<MonitorEvent>? _eventDispatcher;
    private HwndSourceHookAdapter? _hookAdapter;

    public DockCapsuleWindow() {
        InitializeComponent();
        SourceInitialized += OnSourceInitialized;
        MouseEnter += OnMouseEnter;
        MouseLeave += OnMouseLeave;
        MouseLeftButtonDown += OnMouseLeftButtonDown;
        MouseMove += OnMouseMove;
        MouseLeftButtonUp += OnMouseLeftButtonUp;
    }

    public void Initialize(Action<MonitorEvent> eventDispatcher) {
        _eventDispatcher = eventDispatcher ?? throw new ArgumentNullException(nameof(eventDispatcher));
    }

    private void OnSourceInitialized(object? sender, EventArgs e) {
        var hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd != IntPtr.Zero) {
            DwmBackdropAdapter.ApplyBackdrop(hwnd, BackdropType.Acrylic, CornerPreference.RoundSmall, darkMode: true);
            DwmBackdropAdapter.ConfigureFloatingWindowStyles(hwnd, topmost: true);
            _hookAdapter = HwndSourceHookAdapter.Attach(hwnd, ev => _eventDispatcher?.Invoke(ev));
        }
    }

    public void UpdateDockSide(DockSide side) {
        CapsulePill.HorizontalAlignment = side == DockSide.Right ? HorizontalAlignment.Right : HorizontalAlignment.Left;
    }

    public void UpdateRates(CapsulePresentation presentation) {
        var halfHeight = Math.Max(1.0, (CapsulePill.ActualHeight > 0 ? CapsulePill.ActualHeight : CapsulePill.Height) / 2.0);
        DownloadFillRect.Height = Math.Clamp(presentation.Download.Fill * halfHeight, 0, halfHeight);
        UploadFillRect.Height = Math.Clamp(presentation.Upload.Fill * halfHeight, 0, halfHeight);
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
        _hookAdapter?.Dispose();
        base.OnClosed(e);
    }
}
