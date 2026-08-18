using System;
using System.Drawing;
using System.Windows.Forms;

namespace NetworkFlowMonitor.Services;

public sealed class TrayIconManager : IDisposable {
    private readonly NotifyIcon _notifyIcon;
    private readonly Action _onToggleMonitor;
    private readonly Action _onOpenSettings;
    private readonly Action _onExit;
    private readonly Action _onTogglePin;
    private readonly Action _onRestartCollector;

    private bool _isPanelVisible;
    private bool _isPinned;
    private bool _isCollectorOnline;
    private bool _isDisposed;

    public TrayIconManager(
        Action onToggleMonitor,
        Action onOpenSettings,
        Action onExit,
        Action onTogglePin,
        Action onRestartCollector,
        bool initialPanelVisible = true,
        bool initialPinned = false,
        bool initialCollectorOnline = true) {
        _onToggleMonitor = onToggleMonitor ?? throw new ArgumentNullException(nameof(onToggleMonitor));
        _onOpenSettings = onOpenSettings ?? throw new ArgumentNullException(nameof(onOpenSettings));
        _onExit = onExit ?? throw new ArgumentNullException(nameof(onExit));
        _onTogglePin = onTogglePin ?? throw new ArgumentNullException(nameof(onTogglePin));
        _onRestartCollector = onRestartCollector ?? throw new ArgumentNullException(nameof(onRestartCollector));

        _isPanelVisible = initialPanelVisible;
        _isPinned = initialPinned;
        _isCollectorOnline = initialCollectorOnline;

        Icon? icon = null;
        try {
            var iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "Resources", "app.ico");
            if (System.IO.File.Exists(iconPath)) {
                icon = new Icon(iconPath);
            } else {
                var mainModulePath = Environment.ProcessPath;
                if (!string.IsNullOrEmpty(mainModulePath) && System.IO.File.Exists(mainModulePath)) {
                    icon = Icon.ExtractAssociatedIcon(mainModulePath);
                }
            }
        } catch {
            // fallback
        }

        _notifyIcon = new NotifyIcon {
            Text = LocalizationService.BrandName,
            Icon = icon ?? SystemIcons.Application,
            Visible = true
        };

        _notifyIcon.MouseClick += OnNotifyIconMouseClick;

        UpdateContextMenu(_isPanelVisible, _isPinned, _isCollectorOnline);

        LocalizationService.LanguageChanged += OnLanguageChanged;
    }

    public bool IsPanelVisible => _isPanelVisible;
    public bool IsPinned => _isPinned;
    public bool IsCollectorOnline => _isCollectorOnline;
    public ContextMenuStrip? ContextMenuStrip => _notifyIcon.ContextMenuStrip;
    public NotifyIcon NotifyIcon => _notifyIcon;

    private void OnNotifyIconMouseClick(object? sender, MouseEventArgs e) {
        if (e.Button == MouseButtons.Left) {
            _onToggleMonitor();
        }
    }

    private void OnLanguageChanged() {
        if (_isDisposed) return;
        UpdateContextMenu(_isPanelVisible, _isPinned, _isCollectorOnline);
    }

    public void UpdateContextMenu(bool isPanelVisible, bool isPinned, bool isCollectorOnline) {
        if (_isDisposed) return;

        _isPanelVisible = isPanelVisible;
        _isPinned = isPinned;
        _isCollectorOnline = isCollectorOnline;

        var menu = new ContextMenuStrip();

        // 1. Show/Hide Monitor
        var showHideItem = new ToolStripMenuItem(
            isPanelVisible ? LocalizationService.HideMonitor : LocalizationService.ShowMonitor,
            null,
            (s, e) => _onToggleMonitor()
        );
        menu.Items.Add(showHideItem);

        // 2. Pin
        var pinItem = new ToolStripMenuItem(
            LocalizationService.Pin,
            null,
            (s, e) => _onTogglePin()
        ) {
            Checked = isPinned
        };
        menu.Items.Add(pinItem);

        // Separator
        menu.Items.Add(new ToolStripSeparator());

        // 3. Retry/Restart Collector
        var collectorItem = new ToolStripMenuItem(
            isCollectorOnline ? LocalizationService.RestartCollector : LocalizationService.RetryCollector,
            null,
            (s, e) => _onRestartCollector()
        );
        menu.Items.Add(collectorItem);

        // 4. Settings
        var settingsItem = new ToolStripMenuItem(
            LocalizationService.Settings,
            null,
            (s, e) => _onOpenSettings()
        );
        menu.Items.Add(settingsItem);

        // Separator
        menu.Items.Add(new ToolStripSeparator());

        // 5. Exit
        var exitItem = new ToolStripMenuItem(
            LocalizationService.Exit,
            null,
            (s, e) => _onExit()
        );
        menu.Items.Add(exitItem);

        var oldMenu = _notifyIcon.ContextMenuStrip;
        _notifyIcon.ContextMenuStrip = menu;
        oldMenu?.Dispose();
    }

    public void Dispose() {
        if (_isDisposed) return;
        _isDisposed = true;

        LocalizationService.LanguageChanged -= OnLanguageChanged;

        _notifyIcon.Visible = false;
        var oldMenu = _notifyIcon.ContextMenuStrip;
        _notifyIcon.ContextMenuStrip = null;
        oldMenu?.Dispose();
        _notifyIcon.Dispose();
    }
}
