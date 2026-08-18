using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using NetworkFlowMonitor.Presentation;

namespace NetworkFlowMonitor.Interop;

public static class MonitorEnvironmentProvider {
    public static MonitorEnvironment GetCurrentEnvironment(bool fullscreenOccluded = false) {
        var monitors = new List<MonitorWorkArea>();

        try {
            NativeMethods.MonitorEnumProc proc = (IntPtr hMonitor, IntPtr hdcMonitor, ref NativeMethods.RECT lprcMonitor, IntPtr dwData) => {
                var mi = new NativeMethods.MONITORINFOEX();
                mi.cbSize = Marshal.SizeOf<NativeMethods.MONITORINFOEX>();

                if (NativeMethods.GetMonitorInfo(hMonitor, ref mi)) {
                    var isPrimary = (mi.dwFlags & NativeMethods.MONITORINFOF_PRIMARY) != 0;
                    var deviceId = string.IsNullOrWhiteSpace(mi.szDevice)
                        ? (isPrimary ? "primary" : $"mon_{monitors.Count}")
                        : mi.szDevice;

                    uint dpi = 96;
                    if (NativeMethods.GetDpiForMonitor(hMonitor, NativeMethods.MDT_EFFECTIVE_DPI, out var dpiX, out var _) == 0) {
                        dpi = dpiX > 0 ? dpiX : 96;
                    }

                    var monitorRect = new PixelRect(mi.rcMonitor.Left, mi.rcMonitor.Top, mi.rcMonitor.Right, mi.rcMonitor.Bottom);
                    var workRect = new PixelRect(mi.rcWork.Left, mi.rcWork.Top, mi.rcWork.Right, mi.rcWork.Bottom);

                    var autoHideLeft = workRect.Left > monitorRect.Left && (workRect.Left - monitorRect.Left) <= 4;
                    var autoHideRight = monitorRect.Right > workRect.Right && (monitorRect.Right - workRect.Right) <= 4;

                    monitors.Add(new MonitorWorkArea(
                        Id: deviceId,
                        Rect: workRect,
                        Dpi: dpi,
                        Primary: isPrimary,
                        AutoHideRevealOnLeft: autoHideLeft,
                        AutoHideRevealOnRight: autoHideRight
                    ));
                }
                return true;
            };

            NativeMethods.EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, proc, IntPtr.Zero);
        } catch {
            // Fallback for headless/sandbox testing
        }

        if (monitors.Count == 0) {
            monitors.Add(new MonitorWorkArea(
                Id: "primary",
                Rect: new PixelRect(0, 0, 1920, 1040),
                Dpi: 96,
                Primary: true
            ));
        }

        return new MonitorEnvironment(monitors, fullscreenOccluded);
    }

    public static bool CheckFullscreenActive(PixelRect activeMonitorRect) {
        try {
            var hwnd = NativeMethods.GetForegroundWindow();
            if (hwnd == IntPtr.Zero) return false;

            var buffer = new char[256];
            var len = NativeMethods.GetClassName(hwnd, buffer, 256);
            if (len > 0) {
                var cls = new string(buffer, 0, len);
                if (cls == "Progman" || cls == "WorkerW" || cls == "Shell_TrayWnd" || cls == "Windows.UI.Core.CoreWindow") {
                    return false;
                }
            }

            if (NativeMethods.GetWindowRect(hwnd, out var lpRect)) {
                return lpRect.Left <= activeMonitorRect.Left &&
                       lpRect.Top <= activeMonitorRect.Top &&
                       lpRect.Right >= activeMonitorRect.Right &&
                       lpRect.Bottom >= activeMonitorRect.Bottom;
            }
        } catch { }
        return false;
    }
}
