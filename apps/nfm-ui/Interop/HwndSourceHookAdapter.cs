using System;
using System.Windows.Interop;
using NetworkFlowMonitor.Presentation;

namespace NetworkFlowMonitor.Interop;

public sealed class HwndSourceHookAdapter : IDisposable {
    private readonly HwndSource _source;
    private readonly Action<MonitorEvent>? _onEvent;
    private bool _disposed;

    public HwndSourceHookAdapter(HwndSource source, Action<MonitorEvent>? onEvent = null) {
        _source = source ?? throw new ArgumentNullException(nameof(source));
        _onEvent = onEvent;
        _source.AddHook(HwndHook);
    }

    public static HwndSourceHookAdapter? Attach(IntPtr hwnd, Action<MonitorEvent>? onEvent = null) {
        if (hwnd == IntPtr.Zero) {
            return null;
        }

        var source = HwndSource.FromHwnd(hwnd);
        return source != null ? new HwndSourceHookAdapter(source, onEvent) : null;
    }

    private IntPtr HwndHook(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled) {
        switch (msg) {
            case NativeMethods.WM_DPICHANGED:
            case NativeMethods.WM_DISPLAYCHANGE:
            case NativeMethods.WM_SETTINGCHANGE:
                _onEvent?.Invoke(new MonitorEvent(MonitorEventKind.EnvironmentChanged));
                break;
        }

        return IntPtr.Zero;
    }

    public void Dispose() {
        if (!_disposed) {
            _disposed = true;
            _source.RemoveHook(HwndHook);
        }
    }
}
