using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Input;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.ViewModels;

public sealed record OptionItem<T>(T Value, string Label);

[SuppressMessage("Performance", "CA1822:Mark members as static", Justification = "WPF data binding targets")]
public sealed class SettingsViewModel : INotifyPropertyChanged, IDisposable {
    private readonly Action<AppConfigModel>? _onSaveConfig;
    private readonly TelemetryStore? _telemetryStore;
    private readonly Action? _retryCollectorAction;
    private readonly Action? _restartCollectorAction;
    private readonly Action<string> _clipboardSetter;
    private readonly string _configPath;
    private readonly int _schemaVersion;
    private readonly PersistedAnchorModel? _persistedAnchor;

    private bool _isInitializing = true;
    private AppConfigModel _currentConfig;

    // Navigation
    private int _selectedTabIndex;
    private bool _isNarrowMode;

    // General Tab
    private AppLanguage _selectedLanguage;
    private AppTheme _selectedTheme;
    private bool _startWithWindows;

    // Monitor Tab
    private bool _autoHideEnabled;
    private bool _pinned;
    private int _capsuleWidthDip;
    private int _capsuleHeightDip;
    private int _hoverDelayMs;
    private int _dismissDelayMs;
    private double _panelOpacity;
    private double _blurStrength;
    private bool _yieldFullscreen;
    private RateUnitMode _rateUnits;

    // Ranking Tab
    private int _topN;

    // Collector Tab Live State
    private CollectorConnectionState _connectionState = CollectorConnectionState.Disconnected;
    private int _collectorPid;
    private string _collectorInstanceId = string.Empty;
    private bool _etwSessionActive;
    private ulong _droppedPackets;
    private uint _activePids;
    private string _copyStatusText = string.Empty;

    public event PropertyChangedEventHandler? PropertyChanged;

    public SettingsViewModel(
        AppConfigModel? initialConfig = null,
        Action<AppConfigModel>? onSaveConfig = null,
        TelemetryStore? telemetryStore = null,
        Action? retryCollectorAction = null,
        Action? restartCollectorAction = null,
        string? configPath = null,
        Action<string>? clipboardSetter = null) {
        _isInitializing = true;
        _onSaveConfig = onSaveConfig;
        _telemetryStore = telemetryStore;
        _retryCollectorAction = retryCollectorAction;
        _restartCollectorAction = restartCollectorAction;
        _configPath = configPath ?? ConfigStore.GetDefaultConfigPath();
        _clipboardSetter = clipboardSetter ?? DefaultClipboardSetter;

        var config = initialConfig ?? AppConfigModel.Default;
        _currentConfig = config;

        _schemaVersion = config.SchemaVersion;
        var general = config.General ?? new GeneralConfigModel();
        var monitor = config.Monitor ?? new MonitorConfigModel();
        var ranking = config.Ranking ?? new RankingConfigModel();

        _persistedAnchor = monitor.Anchor;

        _selectedLanguage = general.Language;
        _selectedTheme = general.Theme;
        _startWithWindows = general.StartWithWindows;

        _autoHideEnabled = monitor.AutoHideEnabled;
        _pinned = monitor.Pinned;
        _capsuleWidthDip = Math.Clamp(monitor.CapsuleWidthDip, 8, 24);
        _capsuleHeightDip = Math.Clamp(monitor.CapsuleHeightDip, 40, 160);
        _hoverDelayMs = Math.Clamp(monitor.HoverDelayMs, 100, 2000);
        _dismissDelayMs = Math.Clamp(monitor.DismissDelayMs, 100, 2000);
        _panelOpacity = Math.Clamp(monitor.PanelOpacity, 0.1, 1.0);
        _blurStrength = Math.Clamp(monitor.BlurStrength, 0.0, 1.0);
        _yieldFullscreen = monitor.YieldFullscreen;
        _rateUnits = monitor.RateUnits;

        _topN = Math.Clamp((int)ranking.TopN, 1, 20);

        CopyDiagnosticsCommand = new RelayCommand(CopyDiagnosticsToClipboard);
        RetryCollectorCommand = new RelayCommand(() => {
            CopyStatusText = LocalizationService.IsChinese ? "正在重试连接采集器..." : "Retrying collector...";
            _retryCollectorAction?.Invoke();
        });
        RestartCollectorCommand = new RelayCommand(() => {
            CopyStatusText = LocalizationService.IsChinese ? "正在重启采集器进程..." : "Restarting collector...";
            _restartCollectorAction?.Invoke();
        });

        if (_telemetryStore != null) {
            _telemetryStore.SnapshotUpdated += OnTelemetrySnapshotUpdated;
            ApplySnapshot(_telemetryStore.CurrentSnapshot);
        }

        LocalizationService.LanguageChanged += OnLocalizationChanged;
        ThemeService.ThemeChanged += OnThemeChanged;

        _isInitializing = false;
    }

    private static void DefaultClipboardSetter(string text) {
        try {
            if (Application.Current != null && !Application.Current.Dispatcher.CheckAccess()) {
                Application.Current.Dispatcher.Invoke(() => Clipboard.SetText(text));
            } else {
                Clipboard.SetText(text);
            }
        } catch {
            // Ignore clipboard errors
        }
    }

    // --- Navigation ---
    public int SelectedTabIndex {
        get => _selectedTabIndex;
        set {
            if (_selectedTabIndex != value) {
                _selectedTabIndex = value;
                OnPropertyChanged();
            }
        }
    }

    public bool IsNarrowMode {
        get => _isNarrowMode;
        set {
            if (_isNarrowMode != value) {
                _isNarrowMode = value;
                OnPropertyChanged();
            }
        }
    }

    // --- General Tab Properties ---
    public AppLanguage SelectedLanguage {
        get => _selectedLanguage;
        set {
            if (_selectedLanguage != value) {
                _selectedLanguage = value;
                LocalizationService.CurrentLanguage = value;
                OnPropertyChanged();
                SaveConfig();
                RefreshLocalizedProperties();
            }
        }
    }

    public AppTheme SelectedTheme {
        get => _selectedTheme;
        set {
            if (_selectedTheme != value) {
                _selectedTheme = value;
                ThemeService.CurrentTheme = value;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public bool StartWithWindows {
        get => _startWithWindows;
        set {
            if (_startWithWindows != value) {
                _startWithWindows = value;
                AutoStartService.SetEnabled(value);
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    // --- Monitor Tab Properties ---
    public bool AutoHideEnabled {
        get => _autoHideEnabled;
        set {
            if (_autoHideEnabled != value) {
                _autoHideEnabled = value;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public bool Pinned {
        get => _pinned;
        set {
            if (_pinned != value) {
                _pinned = value;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public int CapsuleWidthDip {
        get => _capsuleWidthDip;
        set {
            var clamped = Math.Clamp(value, 8, 24);
            if (_capsuleWidthDip != clamped) {
                _capsuleWidthDip = clamped;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public int CapsuleHeightDip {
        get => _capsuleHeightDip;
        set {
            var clamped = Math.Clamp(value, 40, 160);
            if (_capsuleHeightDip != clamped) {
                _capsuleHeightDip = clamped;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public int HoverDelayMs {
        get => _hoverDelayMs;
        set {
            var clamped = Math.Clamp(value, 100, 2000);
            if (_hoverDelayMs != clamped) {
                _hoverDelayMs = clamped;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public int DismissDelayMs {
        get => _dismissDelayMs;
        set {
            var clamped = Math.Clamp(value, 100, 2000);
            if (_dismissDelayMs != clamped) {
                _dismissDelayMs = clamped;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public double PanelOpacity {
        get => _panelOpacity;
        set {
            var clamped = Math.Clamp(value, 0.1, 1.0);
            if (Math.Abs(_panelOpacity - clamped) > 0.001) {
                _panelOpacity = clamped;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public double BlurStrength {
        get => _blurStrength;
        set {
            var clamped = Math.Clamp(value, 0.0, 1.0);
            if (Math.Abs(_blurStrength - clamped) > 0.001) {
                _blurStrength = clamped;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public bool YieldFullscreen {
        get => _yieldFullscreen;
        set {
            if (_yieldFullscreen != value) {
                _yieldFullscreen = value;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    public RateUnitMode RateUnits {
        get => _rateUnits;
        set {
            if (_rateUnits != value) {
                _rateUnits = value;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    // --- Ranking Tab Properties ---
    public int TopN {
        get => _topN;
        set {
            var clamped = Math.Clamp(value, 1, 20);
            if (_topN != clamped) {
                _topN = clamped;
                OnPropertyChanged();
                SaveConfig();
            }
        }
    }

    // --- Collector Tab Live Telemetry Properties ---
    public CollectorConnectionState ConnectionState {
        get => _connectionState;
        private set {
            if (_connectionState != value) {
                _connectionState = value;
                OnPropertyChanged();
                OnPropertyChanged(nameof(ConnectionStateText));
            }
        }
    }

    public int CollectorPid {
        get => _collectorPid;
        private set {
            if (_collectorPid != value) {
                _collectorPid = value;
                OnPropertyChanged();
            }
        }
    }

    public string CollectorInstanceId {
        get => _collectorInstanceId;
        private set {
            if (_collectorInstanceId != value) {
                _collectorInstanceId = value;
                OnPropertyChanged();
            }
        }
    }

    public bool EtwSessionActive {
        get => _etwSessionActive;
        private set {
            if (_etwSessionActive != value) {
                _etwSessionActive = value;
                OnPropertyChanged();
                OnPropertyChanged(nameof(EtwSessionActiveText));
            }
        }
    }

    public ulong DroppedPackets {
        get => _droppedPackets;
        private set {
            if (_droppedPackets != value) {
                _droppedPackets = value;
                OnPropertyChanged();
            }
        }
    }

    public uint ActivePids {
        get => _activePids;
        private set {
            if (_activePids != value) {
                _activePids = value;
                OnPropertyChanged();
            }
        }
    }

    public string CopyStatusText {
        get => _copyStatusText;
        private set {
            if (_copyStatusText != value) {
                _copyStatusText = value;
                OnPropertyChanged();
            }
        }
    }

    // --- Static / Diagnostics Information ---
    public string ConfigFilePath => _configPath;

    public string AppVersion {
        get {
            var assembly = Assembly.GetExecutingAssembly();
            var version = assembly.GetName().Version;
            return version != null ? $"{version.Major}.{version.Minor}.{version.Build}" : "1.0.0";
        }
    }

    public string CollectorVersion => AppVersion;

    public string ProtocolVersion => ProtocolConstants.CurrentProtocolVersion.ToString();

    public string ConnectionStateText => ConnectionState switch {
        CollectorConnectionState.Connected => LocalizationService.IsChinese ? "已连接" : "Connected",
        CollectorConnectionState.Connecting => LocalizationService.IsChinese ? "正在连接" : "Connecting",
        CollectorConnectionState.Offline => LocalizationService.IsChinese ? "已离线" : "Offline",
        _ => LocalizationService.IsChinese ? "未连接" : "Disconnected"
    };

    public string EtwSessionActiveText => EtwSessionActive
        ? (LocalizationService.IsChinese ? "正常运行" : "Active")
        : (LocalizationService.IsChinese ? "未激活" : "Inactive");

    public string ElevationStatusText => LocalizationService.IsChinese ? "普通用户 / 服务进程" : "Standard / Service Process";

    // --- Options for ComboBoxes ---
    public IReadOnlyList<OptionItem<AppLanguage>> LanguageOptions => [
        new(AppLanguage.System, LocalizationService.LanguageSystem),
        new(AppLanguage.SimplifiedChinese, LocalizationService.LanguageChinese),
        new(AppLanguage.English, LocalizationService.LanguageEnglish)
    ];

    public IReadOnlyList<OptionItem<AppTheme>> ThemeOptions => [
        new(AppTheme.System, LocalizationService.ThemeSystem),
        new(AppTheme.Light, LocalizationService.ThemeLight),
        new(AppTheme.Dark, LocalizationService.ThemeDark)
    ];

    public IReadOnlyList<OptionItem<RateUnitMode>> RateUnitOptions => [
        new(RateUnitMode.AutoBytes, LocalizationService.IsChinese ? "自动 (字节: B/s, KB/s, MB/s)" : "Auto (Bytes: B/s, KB/s, MB/s)"),
        new(RateUnitMode.AutoBits, LocalizationService.IsChinese ? "自动 (比特: bps, Kbps, Mbps)" : "Auto (Bits: bps, Kbps, Mbps)"),
        new(RateUnitMode.BytesPerSec, "B/s"),
        new(RateUnitMode.KilobytesPerSec, "KB/s"),
        new(RateUnitMode.MegabytesPerSec, "MB/s")
    ];

    // --- Localized Labels ---
    public string GeneralTabHeader => LocalizationService.GeneralTab;
    public string MonitorTabHeader => LocalizationService.MonitorTab;
    public string RankingTabHeader => LocalizationService.RankingTab;
    public string CollectorTabHeader => LocalizationService.CollectorTab;
    public string DiagnosticsTabHeader => LocalizationService.DiagnosticsTab;

    public string LanguageLabel => LocalizationService.LanguageLabel;
    public string ThemeLabel => LocalizationService.ThemeLabel;
    public string StartWithWindowsLabel => LocalizationService.StartWithWindowsLabel;

    public string AutoHideLabel => LocalizationService.AutoHideLabel;
    public string PinnedLabel => LocalizationService.PinnedLabel;
    public string CapsuleWidthLabel => LocalizationService.CapsuleWidthLabel;
    public string CapsuleHeightLabel => LocalizationService.CapsuleHeightLabel;
    public string HoverDelayLabel => LocalizationService.HoverDelayLabel;
    public string DismissDelayLabel => LocalizationService.DismissDelayLabel;
    public string PanelOpacityLabel => LocalizationService.PanelOpacityLabel;
    public string BlurStrengthLabel => LocalizationService.BlurStrengthLabel;
    public string TopmostYieldLabel => LocalizationService.TopmostYieldLabel;
    public string RateUnitsLabel => LocalizationService.RateUnitsLabel;

    public string TopNLabel => LocalizationService.TopNLabel;

    public string ConnectionStateLabel => LocalizationService.ConnectionStateLabel;
    public string CollectorPidLabel => LocalizationService.CollectorPidLabel;
    public string InstanceIdLabel => LocalizationService.InstanceIdLabel;
    public string ElevationStatusLabel => LocalizationService.ElevationStatusLabel;
    public string EtwHealthLabel => LocalizationService.EtwHealthLabel;
    public string DroppedPacketsLabel => LocalizationService.DroppedPacketsLabel;
    public string ActivePidsLabel => LocalizationService.ActivePidsLabel;
    public string AppVersionLabel => LocalizationService.AppVersionLabel;
    public string CollectorVersionLabel => LocalizationService.CollectorVersionLabel;
    public string ProtocolVersionLabel => LocalizationService.ProtocolVersionLabel;
    public string ConfigPathLabel => LocalizationService.ConfigPathLabel;

    public string RetryCollectorLabel => LocalizationService.RetryCollector;
    public string RestartCollectorLabel => LocalizationService.RestartCollector;
    public string CopyDiagnosticsLabel => LocalizationService.CopyDiagnostics;
    public string SettingsTitle => LocalizationService.Settings;
    public string BrandName => LocalizationService.BrandName;

    // --- Commands ---
    public ICommand CopyDiagnosticsCommand { get; }
    public ICommand RetryCollectorCommand { get; }
    public ICommand RestartCollectorCommand { get; }

    public AppConfigModel CurrentConfig => _currentConfig;

    public void CopyDiagnosticsToClipboard() {
        var text = GenerateDiagnosticsText();
        _clipboardSetter(text);
        CopyStatusText = LocalizationService.CopiedToClipboard;
    }

    public string GenerateDiagnosticsText() {
        var sb = new StringBuilder();
        sb.AppendLine($"=== {LocalizationService.BrandName} Diagnostics ===");
        sb.AppendLine($"App Version: {AppVersion}");
        sb.AppendLine($"Protocol Version: {ProtocolConstants.CurrentProtocolVersion}");
        sb.AppendLine($"OS Version: {Environment.OSVersion}");
        sb.AppendLine($"Process Architecture: {RuntimeInformation.ProcessArchitecture}");
        sb.AppendLine($"Timestamp UTC: {DateTimeOffset.UtcNow:yyyy-MM-dd HH:mm:ss}Z");
        sb.AppendLine();
        sb.AppendLine("--- Collector Status ---");
        sb.AppendLine($"Connection State: {ConnectionState}");
        sb.AppendLine($"Collector PID: {CollectorPid}");
        sb.AppendLine($"Collector Instance ID: {CollectorInstanceId}");
        sb.AppendLine($"ETW Session Active: {EtwSessionActive}");
        sb.AppendLine($"Dropped Packets: {DroppedPackets}");
        sb.AppendLine($"Active PIDs: {ActivePids}");
        sb.AppendLine();
        sb.AppendLine("--- Configuration ---");
        sb.AppendLine($"Config File Path: {ConfigFilePath}");
        sb.AppendLine($"Language: {SelectedLanguage}");
        sb.AppendLine($"Theme: {SelectedTheme}");
        sb.AppendLine($"Start With Windows: {StartWithWindows}");
        sb.AppendLine($"Auto-Hide Enabled: {AutoHideEnabled}");
        sb.AppendLine($"Pinned: {Pinned}");
        sb.AppendLine($"Capsule Size: {CapsuleWidthDip}x{CapsuleHeightDip}");
        sb.AppendLine($"Hover / Dismiss Delays: {HoverDelayMs}ms / {DismissDelayMs}ms");
        sb.AppendLine($"Panel Opacity / Blur: {PanelOpacity:F2} / {BlurStrength:F2}");
        sb.AppendLine($"Yield Fullscreen: {YieldFullscreen}");
        sb.AppendLine($"Rate Units: {RateUnits}");
        sb.AppendLine($"Ranking Top N: {TopN}");
        return sb.ToString();
    }

    private void SaveConfig() {
        if (_isInitializing) {
            return;
        }

        var config = new AppConfigModel(
            SchemaVersion: _schemaVersion,
            General: new GeneralConfigModel(
                Language: SelectedLanguage,
                Theme: SelectedTheme,
                StartWithWindows: StartWithWindows
            ),
            Monitor: new MonitorConfigModel(
                AutoHideEnabled: AutoHideEnabled,
                Pinned: Pinned,
                CapsuleWidthDip: CapsuleWidthDip,
                CapsuleHeightDip: CapsuleHeightDip,
                HoverDelayMs: HoverDelayMs,
                DismissDelayMs: DismissDelayMs,
                PanelOpacity: PanelOpacity,
                BlurStrength: BlurStrength,
                YieldFullscreen: YieldFullscreen,
                RateUnits: RateUnits,
                Anchor: _persistedAnchor
            ),
            Ranking: new RankingConfigModel(
                TopN: (byte)TopN
            )
        );

        _currentConfig = config;
        _onSaveConfig?.Invoke(config);
    }

    private void OnTelemetrySnapshotUpdated(TelemetrySnapshot snapshot) {
        if (Application.Current != null && !Application.Current.Dispatcher.CheckAccess()) {
            Application.Current.Dispatcher.BeginInvoke(() => ApplySnapshot(snapshot));
        } else {
            ApplySnapshot(snapshot);
        }
    }

    private void ApplySnapshot(TelemetrySnapshot snapshot) {
        ConnectionState = snapshot.ConnectionState;
        CollectorPid = snapshot.CollectorPid;
        CollectorInstanceId = snapshot.CollectorInstanceId ?? string.Empty;
        EtwSessionActive = snapshot.LatestStatus?.EtwSessionActive ?? false;
        DroppedPackets = snapshot.LatestStatus?.DroppedPackets ?? 0;
        ActivePids = snapshot.LatestStatus?.ActivePids ?? 0;
    }

    private void OnLocalizationChanged() {
        if (_selectedLanguage != LocalizationService.CurrentLanguage) {
            _selectedLanguage = LocalizationService.CurrentLanguage;
            OnPropertyChanged(nameof(SelectedLanguage));
        }
        RefreshLocalizedProperties();
    }

    private void OnThemeChanged() {
        if (_selectedTheme != ThemeService.CurrentTheme) {
            _selectedTheme = ThemeService.CurrentTheme;
            OnPropertyChanged(nameof(SelectedTheme));
        }
    }

    private void RefreshLocalizedProperties() {
        OnPropertyChanged(string.Empty);
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    public void Dispose() {
        if (_telemetryStore != null) {
            _telemetryStore.SnapshotUpdated -= OnTelemetrySnapshotUpdated;
        }
        LocalizationService.LanguageChanged -= OnLocalizationChanged;
        ThemeService.ThemeChanged -= OnThemeChanged;
    }
}
