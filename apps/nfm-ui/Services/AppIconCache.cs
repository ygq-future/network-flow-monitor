using System;
using System.Collections.Concurrent;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using NetworkFlowMonitor.Interop;

namespace NetworkFlowMonitor.Services;

public static class AppIconCache {
    private static readonly ConcurrentDictionary<string, ImageSource> _cache = new(StringComparer.OrdinalIgnoreCase);
    private static readonly ImageSource _fallbackIcon = CreateFallbackIcon();

    public static ImageSource FallbackIcon => _fallbackIcon;

    private static RenderTargetBitmap CreateFallbackIcon() {
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen()) {
            var bgBrush = new SolidColorBrush(Color.FromArgb(0x40, 0x80, 0x80, 0x80));
            bgBrush.Freeze();
            var borderPen = new Pen(new SolidColorBrush(Color.FromArgb(0x60, 0xFF, 0xFF, 0xFF)), 1.0);
            borderPen.Freeze();
            dc.DrawRoundedRectangle(bgBrush, borderPen, new Rect(0.5, 0.5, 15, 15), 3, 3);

            var glyphBrush = new SolidColorBrush(Color.FromArgb(0xCC, 0xE4, 0xE4, 0xE7));
            glyphBrush.Freeze();
            var glyphPen = new Pen(glyphBrush, 1.2);
            glyphPen.Freeze();
            dc.DrawRectangle(null, glyphPen, new Rect(3.5, 3.5, 9, 9));
            dc.DrawLine(glyphPen, new Point(3.5, 6.5), new Point(12.5, 6.5));
            dc.DrawLine(glyphPen, new Point(7.5, 6.5), new Point(7.5, 12.5));
        }

        var rtb = new RenderTargetBitmap(16, 16, 96, 96, PixelFormats.Pbgra32);
        rtb.Render(dv);
        rtb.Freeze();
        return rtb;
    }

    public static Task<ImageSource> GetIconAsync(string? executablePath, string? iconKey = null) {
        if (string.IsNullOrWhiteSpace(executablePath) && string.IsNullOrWhiteSpace(iconKey)) {
            return Task.FromResult(_fallbackIcon);
        }

        var key = !string.IsNullOrWhiteSpace(iconKey) ? iconKey : executablePath!;
        if (_cache.TryGetValue(key, out var cached)) {
            return Task.FromResult(cached);
        }

        return Task.Run(() => ExtractAndCache(executablePath, key));
    }

    private static ImageSource ExtractAndCache(string? executablePath, string key) {
        if (_cache.TryGetValue(key, out var cached)) {
            return cached;
        }

        var resolvedPath = ResolvePath(executablePath ?? key);

        // Attempt 1: SHGetFileInfoW with real file or attributes
        if (!string.IsNullOrWhiteSpace(resolvedPath)) {
            var icon = TryExtractWithShell(resolvedPath);
            if (icon != null) {
                _cache[key] = icon;
                return icon;
            }

            // Attempt 2: ExtractIconExW
            if (File.Exists(resolvedPath)) {
                var iconEx = TryExtractWithIconEx(resolvedPath);
                if (iconEx != null) {
                    _cache[key] = iconEx;
                    return iconEx;
                }
            }
        }

        // Attempt 3: Generic .exe shell association
        var genericExe = TryExtractWithShell(".exe", useAttributes: true);
        if (genericExe != null) {
            _cache[key] = genericExe;
            return genericExe;
        }

        _cache[key] = _fallbackIcon;
        return _fallbackIcon;
    }

    private static string? ResolvePath(string rawPath) {
        if (string.IsNullOrWhiteSpace(rawPath)) {
            return null;
        }

        // Handle system pseudo-identities
        if (rawPath.Equals("System", StringComparison.OrdinalIgnoreCase) ||
            rawPath.Equals("System Idle Process", StringComparison.OrdinalIgnoreCase) ||
            rawPath.Equals("Registry", StringComparison.OrdinalIgnoreCase)) {
            var ntoskrnl = Path.Combine(Environment.SystemDirectory, "ntoskrnl.exe");
            if (File.Exists(ntoskrnl)) return ntoskrnl;
            var shell32 = Path.Combine(Environment.SystemDirectory, "shell32.dll");
            if (File.Exists(shell32)) return shell32;
        }

        if (File.Exists(rawPath)) {
            return rawPath;
        }

        // If it's a bare filename or relative path, check system folders
        var fileName = Path.GetFileName(rawPath);
        if (!string.IsNullOrEmpty(fileName)) {
            var sysPath = Path.Combine(Environment.SystemDirectory, fileName);
            if (File.Exists(sysPath)) return sysPath;

            var winPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), fileName);
            if (File.Exists(winPath)) return winPath;
        }

        return rawPath;
    }

    private static BitmapSource? TryExtractWithShell(string path, bool useAttributes = false) {
        try {
            var psfi = new NativeMethods.SHFILEINFO();
            var flags = NativeMethods.SHGFI_ICON | NativeMethods.SHGFI_SMALLICON;
            if (useAttributes || !File.Exists(path)) {
                flags |= NativeMethods.SHGFI_USEFILEATTRIBUTES;
            }

            var res = NativeMethods.SHGetFileInfo(
                path,
                NativeMethods.FILE_ATTRIBUTE_NORMAL,
                ref psfi,
                (uint)Marshal.SizeOf<NativeMethods.SHFILEINFO>(),
                flags
            );

            if (res != IntPtr.Zero && psfi.hIcon != IntPtr.Zero) {
                try {
                    var bitmapSource = Imaging.CreateBitmapSourceFromHIcon(
                        psfi.hIcon,
                        Int32Rect.Empty,
                        BitmapSizeOptions.FromEmptyOptions()
                    );
                    bitmapSource.Freeze();
                    return bitmapSource;
                } finally {
                    NativeMethods.DestroyIcon(psfi.hIcon);
                }
            }
        } catch {
            // Ignore shell extraction error
        }

        return null;
    }

    private static BitmapSource? TryExtractWithIconEx(string path) {
        try {
            var smallIcons = new IntPtr[1];
            var count = NativeMethods.ExtractIconEx(path, 0, null, smallIcons, 1);
            if (count > 0 && smallIcons[0] != IntPtr.Zero) {
                try {
                    var bitmapSource = Imaging.CreateBitmapSourceFromHIcon(
                        smallIcons[0],
                        Int32Rect.Empty,
                        BitmapSizeOptions.FromEmptyOptions()
                    );
                    bitmapSource.Freeze();
                    return bitmapSource;
                } finally {
                    NativeMethods.DestroyIcon(smallIcons[0]);
                }
            }
        } catch {
            // Ignore
        }

        return null;
    }
}
