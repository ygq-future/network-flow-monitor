using System;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using NetworkFlowMonitor.Interop;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.ViewModels;

namespace NetworkFlowMonitor.Views;

public partial class SettingsWindow : Window {
    private IntPtr _hwnd;
    public SettingsViewModel ViewModel => (SettingsViewModel)DataContext;

    public SettingsWindow() : this(new SettingsViewModel()) {
    }

    public SettingsWindow(SettingsViewModel viewModel) {
        InitializeComponent();
        DataContext = viewModel ?? throw new ArgumentNullException(nameof(viewModel));

        LoadAppIcon();

        SourceInitialized += OnSourceInitialized;
        SizeChanged += OnWindowSizeChanged;
        Closed += OnWindowClosed;
        Loaded += OnWindowLoaded;
    }

    private void LoadAppIcon() {
        try {
            var iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "Resources", "app.ico");
            if (System.IO.File.Exists(iconPath)) {
                var frame = System.Windows.Media.Imaging.BitmapFrame.Create(new Uri(iconPath));
                Icon = frame;
                if (AppHeaderIcon != null) AppHeaderIcon.Source = frame;
                return;
            }
        } catch { }

        if (AppHeaderIcon != null) {
            AppHeaderIcon.Source = AppIconCache.FallbackIcon;
        }
    }

    private void OnSourceInitialized(object? sender, EventArgs e) {
        _hwnd = new WindowInteropHelper(this).Handle;
        if (_hwnd != IntPtr.Zero) {
            var isDark = ThemeService.EffectiveTheme == AppTheme.Dark;
            DwmBackdropAdapter.ApplyBackdrop(_hwnd, BackdropType.Acrylic, CornerPreference.Round, darkMode: isDark);
        }
    }

    private void OnWindowLoaded(object sender, RoutedEventArgs e) {
        UpdateActiveNavButton();
    }

    private void OnTitleBarMouseLeftButtonDown(object sender, MouseButtonEventArgs e) {
        if (e.ButtonState == MouseButtonState.Pressed) {
            DragMove();
        }
    }

    private void OnMinimizeClicked(object sender, RoutedEventArgs e) {
        WindowState = WindowState.Minimized;
    }

    private void OnCloseClicked(object sender, RoutedEventArgs e) {
        Close();
    }

    private void OnWindowSizeChanged(object sender, SizeChangedEventArgs e) {
        var isNarrow = e.NewSize.Width < 560;
        ViewModel.IsNarrowMode = isNarrow;

        if (isNarrow) {
            SidebarColumn.Width = new GridLength(54);
            SidebarFooterText.Visibility = Visibility.Collapsed;
            NavGeneralLabel.Visibility = Visibility.Collapsed;
            NavMonitorLabel.Visibility = Visibility.Collapsed;
            NavCollectorLabel.Visibility = Visibility.Collapsed;
            NavDiagnosticsLabel.Visibility = Visibility.Collapsed;
        } else {
            SidebarColumn.Width = new GridLength(180);
            SidebarFooterText.Visibility = Visibility.Visible;
            NavGeneralLabel.Visibility = Visibility.Visible;
            NavMonitorLabel.Visibility = Visibility.Visible;
            NavCollectorLabel.Visibility = Visibility.Visible;
            NavDiagnosticsLabel.Visibility = Visibility.Visible;
        }
    }

    private void UpdateActiveNavButton() {
        var index = ViewModel.SelectedTabIndex;
        NavGeneralButton.IsChecked = index == 0;
        NavMonitorButton.IsChecked = index == 1;
        NavCollectorButton.IsChecked = index == 2;
        NavDiagnosticsButton.IsChecked = index == 3;
    }

    private void OnNavGeneralClick(object sender, RoutedEventArgs e) {
        ViewModel.SelectedTabIndex = 0;
        UpdateActiveNavButton();
    }

    private void OnNavMonitorClick(object sender, RoutedEventArgs e) {
        ViewModel.SelectedTabIndex = 1;
        UpdateActiveNavButton();
    }

    private void OnNavCollectorClick(object sender, RoutedEventArgs e) {
        ViewModel.SelectedTabIndex = 2;
        UpdateActiveNavButton();
    }

    private void OnNavDiagnosticsClick(object sender, RoutedEventArgs e) {
        ViewModel.SelectedTabIndex = 3;
        UpdateActiveNavButton();
    }

    private void OnWindowClosed(object? sender, EventArgs e) {
        if (DataContext is IDisposable disposable) {
            disposable.Dispose();
        }
    }
}
