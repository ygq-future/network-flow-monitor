using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.ViewModels;
using NetworkFlowMonitor.Views;

namespace NetworkFlowMonitor;

public partial class App : Application {
    private Mutex? _mutex;
    private ConfigStore? _configStore;
    private AppConfigModel _currentConfig = AppConfigModel.Default;
    private TelemetryStore? _store;
    private NamedPipeClient? _client;
    private CollectorProcessManager? _collectorManager;
    private DockCapsuleWindow? _capsuleWindow;
    private MonitorPanelWindow? _panelWindow;
    private MonitorPresentationHost? _host;
    private TrayIconManager? _trayIconManager;
    private SingleInstanceServer? _singleInstanceServer;
    private SettingsWindow? _settingsWindow;

    public AppConfigModel CurrentConfig => _currentConfig;
    public MonitorPresentationHost? Host => _host;
    public TelemetryStore? TelemetryStore => _store;
    public TrayIconManager? TrayIconManager => _trayIconManager;
    public SettingsWindow? CurrentSettingsWindow => _settingsWindow;

    protected override void OnStartup(StartupEventArgs e) {
        base.OnStartup(e);

        if (e.Args.Contains("--surface-spike", StringComparer.OrdinalIgnoreCase) ||
            e.Args.Contains("--spike", StringComparer.OrdinalIgnoreCase)) {
            var spikeWindow = new WpfSurfaceSpikeWindow();
            spikeWindow.Show();
            return;
        }

        if (e.Args.Contains("--tracer", StringComparer.OrdinalIgnoreCase) ||
            e.Args.Contains("--tracer-bullet", StringComparer.OrdinalIgnoreCase)) {
            var mainWindow = new MainWindow();
            mainWindow.Show();
            return;
        }

        // Single-instance check
        var mutexName = SingleInstanceChannel.GetDefaultMutexName();
        bool createdNew;
        try {
            _mutex = new Mutex(true, mutexName, out createdNew);
        } catch {
            createdNew = false;
        }

        if (!createdNew) {
            var command = ResolveActivationCommand(e.Args);
            try {
                SingleInstanceClient.SignalExistingInstanceAsync(command, timeoutMs: 1500).GetAwaiter().GetResult();
            } catch {
                // Ignore signal errors
            }

            Shutdown(0);
            return;
        }

        // Explicit shutdown so application stays alive in tray when windows are hidden
        ShutdownMode = ShutdownMode.OnExplicitShutdown;

        // Load configuration
        _configStore = new ConfigStore();
        _currentConfig = _configStore.Load();

        // Initialize Theme and Language
        ThemeService.Initialize(_currentConfig.General?.Theme ?? AppTheme.System);
        LocalizationService.CurrentLanguage = _currentConfig.General?.Language ?? AppLanguage.System;

        // AutoStart state synchronization
        if (OperatingSystem.IsWindows() && _currentConfig.General != null) {
            var autoStartConfigured = _currentConfig.General.StartWithWindows;
            var autoStartActual = AutoStartService.IsEnabled();
            if (autoStartConfigured != autoStartActual) {
                AutoStartService.SetEnabled(autoStartConfigured);
            }
        }

        // Build initial monitor config and anchor
        var monitorConfig = CreateMonitorConfig(_currentConfig);
        var initialAnchor = CreateInitialAnchor(_currentConfig);

        // Core data stores and IPC clients
        _store = new TelemetryStore();
        _client = new NamedPipeClient(_store);
        _collectorManager = new CollectorProcessManager(_client, _store);

        // Windows and Presentation Host
        _capsuleWindow = new DockCapsuleWindow();
        _panelWindow = new MonitorPanelWindow();

        _host = new MonitorPresentationHost(
            _store,
            _capsuleWindow,
            _panelWindow,
            monitorConfig,
            onOpenSettings: OpenSettingsWindow,
            onRestartCollector: () => _ = _collectorManager.RestartCollectorAsync()
        );

        // Hook up persistence and state change callbacks
        _host.AnchorPersisted += OnAnchorPersisted;
        _host.PinPersisted += OnPinPersisted;
        _host.StateChanged += _ => UpdateTrayState();

        _store.SnapshotUpdated += _ => Dispatcher.BeginInvoke(UpdateTrayState);

        // Setup System Tray Icon Manager
        var initialPanelVisible = monitorConfig.Pinned || !monitorConfig.AutoHideEnabled;
        var initialPinned = monitorConfig.Pinned;
        var initialCollectorOnline = false;

        _trayIconManager = new TrayIconManager(
            onToggleMonitor: () => _host.ToggleVisibility(),
            onOpenSettings: OpenSettingsWindow,
            onExit: () => Shutdown(),
            onTogglePin: () => _host.TogglePin(),
            onRestartCollector: () => _ = _collectorManager.RestartCollectorAsync(),
            initialPanelVisible: initialPanelVisible,
            initialPinned: initialPinned,
            initialCollectorOnline: initialCollectorOnline
        );

        // Setup Single-Instance Activation Server
        _singleInstanceServer = new SingleInstanceServer(OnSingleInstanceCommandReceived);
        _singleInstanceServer.Start();

        // Start host, IPC client and collector
        _host.Start(initialAnchor);
        _client.Start();
        _collectorManager.TryLaunchCollector();

        if (e.Args.Contains("--settings", StringComparer.OrdinalIgnoreCase)) {
            OpenSettingsWindow();
        }
    }

    public static string ResolveActivationCommand(string[] args) {
        if (args.Contains("--settings", StringComparer.OrdinalIgnoreCase)) {
            return SingleInstanceChannel.ActivationCommandShowSettings;
        }
        if (args.Contains("--toggle-pin", StringComparer.OrdinalIgnoreCase) ||
            args.Contains("--pin", StringComparer.OrdinalIgnoreCase)) {
            return SingleInstanceChannel.ActivationCommandTogglePin;
        }
        if (args.Contains("--exit", StringComparer.OrdinalIgnoreCase) ||
            args.Contains("--quit", StringComparer.OrdinalIgnoreCase)) {
            return SingleInstanceChannel.ActivationCommandExit;
        }
        return SingleInstanceChannel.ActivationCommandShowMonitor;
    }

    public static MonitorConfig CreateMonitorConfig(AppConfigModel config) {
        var monitor = config.Monitor ?? new MonitorConfigModel();
        var ranking = config.Ranking ?? new RankingConfigModel();
        return new MonitorConfig(
            HoverDelay: TimeSpan.FromMilliseconds(monitor.HoverDelayMs),
            AutoHideDelay: TimeSpan.FromMilliseconds(monitor.DismissDelayMs),
            CapsuleWidthDip: monitor.CapsuleWidthDip,
            CapsuleHeightDip: monitor.CapsuleHeightDip,
            AutoHideEnabled: monitor.AutoHideEnabled,
            Pinned: monitor.Pinned,
            TopN: ranking.TopN,
            YieldFullscreen: monitor.YieldFullscreen
        );
    }

    public static PersistedMonitorAnchor? CreateInitialAnchor(AppConfigModel config) {
        var anchor = config.Monitor?.Anchor;
        if (anchor == null) {
            return null;
        }

        return new PersistedMonitorAnchor(
            anchor.MonitorId ?? string.Empty,
            anchor.Side,
            anchor.VerticalOffsetDip
        );
    }

    public void OpenSettingsWindow() {
        if (_settingsWindow != null && _settingsWindow.IsLoaded) {
            if (_settingsWindow.WindowState == WindowState.Minimized) {
                _settingsWindow.WindowState = WindowState.Normal;
            }
            _settingsWindow.Activate();
            _settingsWindow.Focus();
            return;
        }

        var vm = new SettingsViewModel(
            initialConfig: _currentConfig,
            onSaveConfig: OnConfigSavedFromSettings,
            telemetryStore: _store,
            retryCollectorAction: () => _ = _collectorManager?.RetryCollectorAsync(),
            restartCollectorAction: () => _ = _collectorManager?.RestartCollectorAsync(500),
            configPath: _configStore?.ConfigPath
        );

        _settingsWindow = new SettingsWindow(vm);
        _settingsWindow.Closed += (_, _) => {
            _settingsWindow = null;
        };
        _settingsWindow.Show();
        _settingsWindow.Activate();
    }

    public void HandleSingleInstanceCommand(string command) {
        switch (command) {
            case SingleInstanceChannel.ActivationCommandShowSettings:
                OpenSettingsWindow();
                break;
            case SingleInstanceChannel.ActivationCommandShowMonitor:
                _host?.ShowMonitor();
                break;
            case SingleInstanceChannel.ActivationCommandTogglePin:
                _host?.TogglePin();
                break;
            case SingleInstanceChannel.ActivationCommandExit:
                Shutdown();
                break;
        }
    }

    private void OnSingleInstanceCommandReceived(string command) {
        Dispatcher.BeginInvoke(() => HandleSingleInstanceCommand(command));
    }

    private void OnConfigSavedFromSettings(AppConfigModel updatedConfig) {
        _currentConfig = updatedConfig;
        _configStore?.Save(updatedConfig);

        if (_host != null) {
            var newConfig = CreateMonitorConfig(updatedConfig);
            var m = updatedConfig.Monitor ?? new MonitorConfigModel();
            _host.UpdateConfig(newConfig, m.PanelOpacity, m.BlurStrength, m.RateUnits);
        }

        UpdateTrayState();
    }

    private void OnAnchorPersisted(PersistedMonitorAnchor anchor) {
        var currentMonitor = _currentConfig.Monitor ?? new MonitorConfigModel();
        var updatedAnchor = new PersistedAnchorModel(anchor.MonitorId, anchor.Side, anchor.VerticalOffsetDip);
        if (currentMonitor.Anchor != updatedAnchor) {
            _currentConfig = _currentConfig with {
                Monitor = currentMonitor with { Anchor = updatedAnchor }
            };
            _configStore?.Save(_currentConfig);
        }
    }

    private void OnPinPersisted(bool pinned) {
        var currentMonitor = _currentConfig.Monitor ?? new MonitorConfigModel();
        if (currentMonitor.Pinned != pinned) {
            _currentConfig = _currentConfig with {
                Monitor = currentMonitor with { Pinned = pinned }
            };
            _configStore?.Save(_currentConfig);
        }
        UpdateTrayState();
    }

    private void UpdateTrayState() {
        if (_trayIconManager == null || _host == null || _store == null) {
            return;
        }

        var isVisible = _host.State.State != MonitorPresentationState.TrayOnly;
        var isPinned = _host.State.PinPreference;
        var isCollectorOnline = _store.CurrentSnapshot.ConnectionState == CollectorConnectionState.Connected;

        _trayIconManager.UpdateContextMenu(isVisible, isPinned, isCollectorOnline);
    }

    protected override async void OnExit(ExitEventArgs e) {
        if (_singleInstanceServer != null) {
            await _singleInstanceServer.DisposeAsync();
            _singleInstanceServer = null;
        }

        _trayIconManager?.Dispose();
        _trayIconManager = null;

        _host?.Dispose();
        _host = null;

        if (_client != null) {
            await _client.DisposeAsync();
            _client = null;
        }

        _collectorManager?.Dispose();
        _collectorManager = null;

        if (_mutex != null) {
            try {
                _mutex.ReleaseMutex();
            } catch { }
            _mutex.Dispose();
            _mutex = null;
        }

        base.OnExit(e);
    }
}
