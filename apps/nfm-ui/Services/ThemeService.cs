using System;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using Microsoft.Win32;
using NetworkFlowMonitor.Interop;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Services;

public static class ThemeService {
    private static AppTheme _currentConfiguredTheme = AppTheme.System;
    private static bool _isHooked;
    private static readonly object _lock = new();

    public static event Action? ThemeChanged;

    public static AppTheme CurrentTheme {
        get => _currentConfiguredTheme;
        set {
            if (_currentConfiguredTheme != value) {
                _currentConfiguredTheme = value;
                ApplyTheme(EffectiveTheme);
                ThemeChanged?.Invoke();
            }
        }
    }

    public static AppTheme EffectiveTheme => ResolveEffectiveTheme(_currentConfiguredTheme, IsSystemInDarkMode());

    public static AppTheme ResolveEffectiveTheme(AppTheme configured, bool systemIsDark) {
        return configured switch {
            AppTheme.Light => AppTheme.Light,
            AppTheme.Dark => AppTheme.Dark,
            _ => systemIsDark ? AppTheme.Dark : AppTheme.Light
        };
    }

    public static bool IsSystemInDarkMode() {
        try {
            if (!OperatingSystem.IsWindows()) {
                return true;
            }
            using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            if (key != null) {
                var val = key.GetValue("AppsUseLightTheme");
                if (val is int lightThemeInt) {
                    return lightThemeInt == 0;
                }
            }
        } catch {
            // Default to dark theme on any access exception
        }
        return true;
    }

    public static void Initialize(AppTheme initialTheme) {
        _currentConfiguredTheme = initialTheme;
        ApplyTheme(EffectiveTheme);

        if (OperatingSystem.IsWindows()) {
            lock (_lock) {
                if (!_isHooked) {
                    SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;
                    _isHooked = true;
                }
            }
        }
    }

    private static void OnUserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e) {
        if (_currentConfiguredTheme == AppTheme.System) {
            ApplyTheme(EffectiveTheme);
            ThemeChanged?.Invoke();
        }
    }

    public static void ApplyTheme(AppTheme effectiveTheme) {
        if (Application.Current == null) {
            return;
        }

        var isDark = effectiveTheme == AppTheme.Dark;
        void UpdateResources() {
            var resources = Application.Current.Resources;
            resources["CardBackgroundBrush"] = CreateBrush(isDark ? Color.FromArgb(0xE6, 0x18, 0x18, 0x1C) : Color.FromArgb(0xF2, 0xF5, 0xF5, 0xF7));
            resources["CardBorderBrush"] = CreateBrush(isDark ? Color.FromArgb(0x28, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x18, 0x00, 0x00, 0x00));
            resources["CardItemHoverBrush"] = CreateBrush(isDark ? Color.FromArgb(0x1E, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x12, 0x00, 0x00, 0x00));
            resources["CardItemActiveBrush"] = CreateBrush(isDark ? Color.FromArgb(0x2E, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x20, 0x00, 0x00, 0x00));
            resources["PrimaryTextBrush"] = CreateBrush(isDark ? Color.FromRgb(0xF4, 0xF4, 0xF5) : Color.FromRgb(0x18, 0x18, 0x1B));
            resources["SecondaryTextBrush"] = CreateBrush(isDark ? Color.FromRgb(0xA1, 0xA1, 0xAA) : Color.FromRgb(0x71, 0x71, 0x7A));
            resources["AccentTextBrush"] = CreateBrush(isDark ? Color.FromRgb(0x38, 0xBD, 0xF8) : Color.FromRgb(0x02, 0x84, 0xC7));
            resources["SettingsSidebarBrush"] = CreateBrush(isDark ? Color.FromArgb(0xF2, 0x14, 0x14, 0x18) : Color.FromArgb(0xF2, 0xEE, 0xEE, 0xF2));
            resources["InputBackgroundBrush"] = CreateBrush(isDark ? Color.FromArgb(0x22, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x15, 0x00, 0x00, 0x00));
            resources["InputBorderBrush"] = CreateBrush(isDark ? Color.FromArgb(0x38, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x25, 0x00, 0x00, 0x00));
            resources["PopupBackgroundBrush"] = CreateBrush(isDark ? Color.FromArgb(0xF0, 0x24, 0x24, 0x28) : Color.FromArgb(0xF8, 0xFA, 0xFA, 0xFC));

            foreach (Window window in Application.Current.Windows) {
                var hwnd = new WindowInteropHelper(window).Handle;
                if (hwnd != IntPtr.Zero) {
                    DwmBackdropAdapter.ApplyBackdrop(hwnd, BackdropType.Acrylic, CornerPreference.Round, darkMode: isDark);
                }
            }
        }

        if (Application.Current.Dispatcher.CheckAccess()) {
            UpdateResources();
        } else {
            Application.Current.Dispatcher.Invoke(UpdateResources);
        }
    }

    private static SolidColorBrush CreateBrush(Color color) {
        var brush = new SolidColorBrush(color);
        brush.Freeze();
        return brush;
    }
}
