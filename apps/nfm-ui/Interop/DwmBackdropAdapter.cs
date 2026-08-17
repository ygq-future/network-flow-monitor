using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace NetworkFlowMonitor.Interop;

public enum BackdropType {
    None = NativeMethods.DWMSBT_NONE,
    Mica = NativeMethods.DWMSBT_MAINWINDOW,
    Acrylic = NativeMethods.DWMSBT_TRANSIENTWINDOW,
    MicaAlt = NativeMethods.DWMSBT_TABBEDWINDOW
}

public enum CornerPreference {
    Default = NativeMethods.DWMWCP_DEFAULT,
    DoNotRound = NativeMethods.DWMWCP_DONOTROUND,
    Round = NativeMethods.DWMWCP_ROUND,
    RoundSmall = NativeMethods.DWMWCP_ROUNDSMALL
}

public static class DwmBackdropAdapter {
    public static bool ApplyBackdrop(
        Window window,
        BackdropType backdrop = BackdropType.Acrylic,
        CornerPreference corner = CornerPreference.Round,
        bool darkMode = true) {
        var hwnd = new WindowInteropHelper(window).Handle;
        if (hwnd == IntPtr.Zero) {
            return false;
        }

        return ApplyBackdrop(hwnd, backdrop, corner, darkMode);
    }

    public static bool ApplyBackdrop(
        IntPtr hwnd,
        BackdropType backdrop = BackdropType.Acrylic,
        CornerPreference corner = CornerPreference.Round,
        bool darkMode = true) {
        if (hwnd == IntPtr.Zero) {
            return false;
        }

        try {
            // Set dark/light mode
            var isDark = darkMode ? 1 : 0;
            NativeMethods.DwmSetWindowAttribute(
                hwnd,
                NativeMethods.DWMWA_USE_IMMERSIVE_DARK_MODE,
                ref isDark,
                sizeof(int)
            );

            // Remove DWM default 1px white border
            var noBorder = unchecked((int)NativeMethods.DWMWA_COLOR_NONE);
            NativeMethods.DwmSetWindowAttribute(
                hwnd,
                NativeMethods.DWMWA_BORDER_COLOR,
                ref noBorder,
                sizeof(int)
            );

            // Set system backdrop type (Windows 11 22H2+: Acrylic / Mica)
            var backdropValue = (int)backdrop;
            NativeMethods.DwmSetWindowAttribute(
                hwnd,
                NativeMethods.DWMWA_SYSTEMBACKDROP_TYPE,
                ref backdropValue,
                sizeof(int)
            );

            // Set corner preference (DWM hardware-clipped rounded corners)
            var cornerValue = (int)corner;
            NativeMethods.DwmSetWindowAttribute(
                hwnd,
                NativeMethods.DWMWA_WINDOW_CORNER_PREFERENCE,
                ref cornerValue,
                sizeof(int)
            );

            // Extend frame into client area for seamless DWM backdrop
            var margins = NativeMethods.MARGINS.FullWindow;
            NativeMethods.DwmExtendFrameIntoClientArea(hwnd, ref margins);

            return true;
        } catch {
            return false;
        }
    }

    public static bool EnableBlurBehind(IntPtr hwnd) {
        if (hwnd == IntPtr.Zero) {
            return false;
        }

        try {
            var isDark = 1;
            NativeMethods.DwmSetWindowAttribute(
                hwnd,
                NativeMethods.DWMWA_USE_IMMERSIVE_DARK_MODE,
                ref isDark,
                sizeof(int)
            );

            var bb = new NativeMethods.DWM_BLURBEHIND {
                dwFlags = NativeMethods.DWM_BB_ENABLE,
                fEnable = true,
                hRgnBlur = IntPtr.Zero,
                fTransitionOnMaximized = false
            };

            return NativeMethods.DwmEnableBlurBehindWindow(hwnd, ref bb) == 0;
        } catch {
            return false;
        }
    }

    public static void ConfigureFloatingWindowStyles(IntPtr hwnd, bool topmost = true) {
        if (hwnd == IntPtr.Zero) {
            return;
        }

        try {
            var exStyle = NativeMethods.GetWindowLong(hwnd, NativeMethods.GWL_EXSTYLE);
            exStyle |= NativeMethods.WS_EX_TOOLWINDOW | NativeMethods.WS_EX_NOACTIVATE;
            if (topmost) {
                exStyle |= NativeMethods.WS_EX_TOPMOST;
            }
            NativeMethods.SetWindowLong(hwnd, NativeMethods.GWL_EXSTYLE, exStyle);

            var insertAfter = topmost ? NativeMethods.HWND_TOPMOST : NativeMethods.HWND_NOTOPMOST;
            NativeMethods.SetWindowPos(
                hwnd,
                insertAfter,
                0, 0, 0, 0,
                NativeMethods.SWP_NOMOVE | NativeMethods.SWP_NOSIZE | NativeMethods.SWP_NOACTIVATE | NativeMethods.SWP_FRAMECHANGED
            );
        } catch {
            // Ignore styling errors on headless/mock environments
        }
    }

    public static void SetTopmostState(IntPtr hwnd, bool topmost) {
        if (hwnd == IntPtr.Zero) {
            return;
        }

        try {
            var insertAfter = topmost ? NativeMethods.HWND_TOPMOST : NativeMethods.HWND_NOTOPMOST;
            NativeMethods.SetWindowPos(
                hwnd,
                insertAfter,
                0, 0, 0, 0,
                NativeMethods.SWP_NOMOVE | NativeMethods.SWP_NOSIZE | NativeMethods.SWP_NOACTIVATE
            );
        } catch {
            // Ignore errors
        }
    }
}
