# Settings Control Center, Portable Companion Lifecycle & Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the complete Fluent Settings control center, atomic executable-adjacent `ConfigStore`, Windows user auto-start, session-scoped single-instance mutex and activation channel, system tray integration, portable companion collector process management, unified automated quality gate, and self-contained release packaging for Network Flow Monitor.

**Architecture:**
- **Executable-Adjacent JSON `ConfigStore`**: Atomic write via temporary file swap, robust schema validation, automatic `.damaged` preservation on malformed JSON, restoring window anchor, pin preference, opacity, and full user configuration across restarts.
- **Modern Fluent `SettingsWindow`**: Left-nav sidebar with responsive top-selector fallback on narrow widths, opaque readable Fluent backdrop (isolated from Monitor Panel opacity), dynamic System/Light/Dark theme sync, and dynamic EN/ZH-CN live bilingual localization.
- **Single Interactive Instance & Activation Channel**: Session-scoped mutex preventing duplicate processes; secondary launch signals primary instance over a dedicated named pipe activation channel to bring it to foreground and exits immediately.
- **Portable Companion Lifecycle & Tray**: Manages elevated `nfm-collector.exe` with UAC launch, retry/restart actions, shutdown request fast-path, and full system tray context menu integration.
- **Unified Quality Gate & Packaging**: Single entry `tools/quality.ps1` compiling C++20 and .NET 10, running cross-language protocol tests, unit tests, and producing `dist/NetworkFlowMonitor-<version>-win-x64-portable.zip`.

**Tech Stack:** C# 14 / .NET 10 WPF, Win32 Registry / Named Pipe Interop, GDI / DWM Interop, xUnit.

---

### Task 1: Executable-Adjacent Atomic ConfigStore with Corrupt Backup

**Files:**
- Create: `apps/nfm-ui/Services/ConfigStore.cs`
- Create: `apps/nfm-ui/Store/AppConfigModel.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/ConfigStoreTests.cs`

- [ ] **Step 1: Create `AppConfigModel.cs` data models**

```csharp
// apps/nfm-ui/Store/AppConfigModel.cs
using System;
using System.Text.Json.Serialization;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;

namespace NetworkFlowMonitor.Store;

public enum RateUnitMode {
    AutoBytes,
    AutoBits,
    BytesPerSec,
    KilobytesPerSec,
    MegabytesPerSec
}

public enum AppTheme {
    System,
    Light,
    Dark
}

public sealed record PersistedAnchorModel(
    string MonitorId = "",
    DockSide Side = DockSide.Right,
    int VerticalOffsetDip = 0
);

public sealed record GeneralConfigModel(
    AppLanguage Language = AppLanguage.System,
    AppTheme Theme = AppTheme.System,
    bool StartWithWindows = false
);

public sealed record MonitorConfigModel(
    bool AutoHideEnabled = true,
    bool Pinned = false,
    int CapsuleWidthDip = 10,
    int CapsuleHeightDip = 64,
    int HoverDelayMs = 350,
    int DismissDelayMs = 500,
    double PanelOpacity = 0.85,
    double BlurStrength = 1.0,
    bool YieldFullscreen = true,
    RateUnitMode RateUnits = RateUnitMode.AutoBytes,
    PersistedAnchorModel? Anchor = null
);

public sealed record RankingConfigModel(
    byte TopN = 5
);

public sealed record AppConfigModel(
    [property: JsonPropertyName("schema_version")] int SchemaVersion = 1,
    [property: JsonPropertyName("general")] GeneralConfigModel? General = null,
    [property: JsonPropertyName("monitor")] MonitorConfigModel? Monitor = null,
    [property: JsonPropertyName("ranking")] RankingConfigModel? Ranking = null
) {
    public static AppConfigModel Default => new(
        SchemaVersion: 1,
        General: new GeneralConfigModel(),
        Monitor: new MonitorConfigModel(),
        Ranking: new RankingConfigModel()
    );
}
```

- [ ] **Step 2: Write failing tests for `ConfigStore`**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/ConfigStoreTests.cs
using System;
using System.IO;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class ConfigStoreTests : IDisposable {
    private readonly string _tempDirectory;
    private readonly string _configFilePath;

    public ConfigStoreTests() {
        _tempDirectory = Path.Combine(Path.GetTempPath(), "NfmConfigTests_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDirectory);
        _configFilePath = Path.Combine(_tempDirectory, "NetworkFlowMonitor.json");
    }

    public void Dispose() {
        try {
            if (Directory.Exists(_tempDirectory)) {
                Directory.Delete(_tempDirectory, true);
            }
        } catch { }
    }

    [Fact]
    public void Load_WhenFileMissing_ReturnsDefaultConfig() {
        var store = new ConfigStore(_configFilePath);
        var config = store.Load();

        Assert.NotNull(config);
        Assert.Equal(1, config.SchemaVersion);
        Assert.Equal(AppLanguage.System, config.General?.Language ?? AppLanguage.System);
        Assert.Equal(10, config.Monitor?.CapsuleWidthDip ?? 10);
    }

    [Fact]
    public void SaveAndLoad_RoundTripsAccurately() {
        var store = new ConfigStore(_configFilePath);
        var customConfig = new AppConfigModel(
            SchemaVersion: 1,
            General: new GeneralConfigModel(
                Language: AppLanguage.SimplifiedChinese,
                Theme: AppTheme.Dark,
                StartWithWindows: true
            ),
            Monitor: new MonitorConfigModel(
                AutoHideEnabled: false,
                Pinned: true,
                CapsuleWidthDip: 14,
                CapsuleHeightDip: 80,
                HoverDelayMs: 250,
                DismissDelayMs: 400,
                PanelOpacity: 0.9,
                BlurStrength: 0.8,
                YieldFullscreen: false,
                RateUnits: RateUnitMode.MegabytesPerSec,
                Anchor: new PersistedAnchorModel("MON_2", DockSide.Left, 120)
            ),
            Ranking: new RankingConfigModel(TopN: 8)
        );

        var saved = store.Save(customConfig);
        Assert.True(saved);
        Assert.True(File.Exists(_configFilePath));

        var loaded = store.Load();
        Assert.NotNull(loaded);
        Assert.Equal(AppLanguage.SimplifiedChinese, loaded.General?.Language);
        Assert.Equal(AppTheme.Dark, loaded.General?.Theme);
        Assert.True(loaded.General?.StartWithWindows);
        Assert.False(loaded.Monitor?.AutoHideEnabled);
        Assert.True(loaded.Monitor?.Pinned);
        Assert.Equal(14, loaded.Monitor?.CapsuleWidthDip);
        Assert.Equal(80, loaded.Monitor?.CapsuleHeightDip);
        Assert.Equal("MON_2", loaded.Monitor?.Anchor?.MonitorId);
        Assert.Equal(DockSide.Left, loaded.Monitor?.Anchor?.Side);
        Assert.Equal(120, loaded.Monitor?.Anchor?.VerticalOffsetDip);
        Assert.Equal(8, loaded.Ranking?.TopN);
    }

    [Fact]
    public void Load_WhenCorrupted_PreservesDamagedFileAndReturnsDefaults() {
        File.WriteAllText(_configFilePath, "{ invalid json content !!! @#$ }");

        var store = new ConfigStore(_configFilePath);
        var loaded = store.Load();

        Assert.NotNull(loaded);
        Assert.Equal(AppConfigModel.Default.SchemaVersion, loaded.SchemaVersion);

        // Verify .damaged file was created
        var damagedFiles = Directory.GetFiles(_tempDirectory, "*.damaged*");
        Assert.NotEmpty(damagedFiles);
    }
}
```

- [ ] **Step 3: Run test to verify failure**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~ConfigStoreTests"`
Expected: FAIL due to missing `ConfigStore`.

- [ ] **Step 4: Implement `ConfigStore.cs`**

```csharp
// apps/nfm-ui/Services/ConfigStore.cs
using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Services;

public class ConfigStore {
    private readonly string _configPath;
    private static readonly JsonSerializerOptions _jsonOptions = new() {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = {
            new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower)
        }
    };

    public ConfigStore(string? configPath = null) {
        _configPath = configPath ?? GetDefaultConfigPath();
    }

    public static string GetDefaultConfigPath() {
        var baseDir = AppContext.BaseDirectory;
        return Path.Combine(baseDir, "NetworkFlowMonitor.json");
    }

    public AppConfigModel Load() {
        if (!File.Exists(_configPath)) {
            return AppConfigModel.Default;
        }

        try {
            var json = File.ReadAllText(_configPath);
            var model = JsonSerializer.Deserialize<AppConfigModel>(json, _jsonOptions);
            return model ?? AppConfigModel.Default;
        } catch (Exception ex) {
            PreserveDamagedFile(_configPath, ex);
            return AppConfigModel.Default;
        }
    }

    public bool Save(AppConfigModel config) {
        try {
            var tempPath = _configPath + ".tmp." + Guid.NewGuid().ToString("N");
            var json = JsonSerializer.Serialize(config, _jsonOptions);

            File.WriteAllText(tempPath, json);

            // Atomic file replace
            if (File.Exists(_configPath)) {
                File.Move(tempPath, _configPath, overwrite: true);
            } else {
                File.Move(tempPath, _configPath);
            }
            return true;
        } catch {
            return false;
        }
    }

    private static void PreserveDamagedFile(string originalPath, Exception ex) {
        try {
            var dir = Path.GetDirectoryName(originalPath) ?? AppContext.BaseDirectory;
            var timestamp = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss");
            var damagedPath = Path.Combine(dir, $"{Path.GetFileNameWithoutExtension(originalPath)}.{timestamp}.damaged.json");
            if (File.Exists(originalPath)) {
                File.Copy(originalPath, damagedPath, overwrite: true);
            }
        } catch {
            // Best effort preservation
        }
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~ConfigStoreTests"`
Expected: PASS

---

### Task 2: Windows User Login Auto-Start & Registry Service

**Files:**
- Create: `apps/nfm-ui/Services/AutoStartService.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/AutoStartServiceTests.cs`

- [ ] **Step 1: Write unit tests for `AutoStartService` with custom key seam**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/AutoStartServiceTests.cs
using System;
using System.IO;
using Microsoft.Win32;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class AutoStartServiceTests {
    [Fact]
    public void FormatCommandLine_EnclosesPathInQuotes() {
        var exePath = @"C:\Program Files\NFM\NetworkFlowMonitor.exe";
        var cmd = AutoStartService.FormatCommandLine(exePath);
        Assert.Equal("\"C:\\Program Files\\NFM\\NetworkFlowMonitor.exe\"", cmd);
    }

    [Fact]
    public void IsAutoStartEnabled_DetectsMatchingEntry() {
        var testExe = @"C:\Apps\NetworkFlowMonitor.exe";
        var isEnabled = AutoStartService.IsCommandLineMatching("\"C:\\Apps\\NetworkFlowMonitor.exe\"", testExe);
        Assert.True(isEnabled);

        var isNotMatching = AutoStartService.IsCommandLineMatching("\"C:\\Other\\App.exe\"", testExe);
        Assert.False(isNotMatching);
    }
}
```

- [ ] **Step 2: Implement `AutoStartService.cs`**

```csharp
// apps/nfm-ui/Services/AutoStartService.cs
using System;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace NetworkFlowMonitor.Services;

public static class AutoStartService {
    private const string RunRegistrySubKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ApplicationName = "NetworkFlowMonitor";

    public static string FormatCommandLine(string executablePath) {
        return $"\"{executablePath}\"";
    }

    public static bool IsCommandLineMatching(string? registryValue, string executablePath) {
        if (string.IsNullOrWhiteSpace(registryValue)) return false;
        var trimmed = registryValue.Trim().Trim('"');
        return string.Equals(trimmed, executablePath, StringComparison.OrdinalIgnoreCase);
    }

    public static bool IsEnabled() {
        try {
            if (!OperatingSystem.IsWindows()) return false;
            var exePath = Environment.ProcessPath;
            if (string.IsNullOrEmpty(exePath)) return false;

            using var key = Registry.CurrentUser.OpenSubKey(RunRegistrySubKey, writable: false);
            if (key == null) return false;

            var val = key.GetValue(ApplicationName) as string;
            return IsCommandLineMatching(val, exePath);
        } catch {
            return false;
        }
    }

    public static bool SetEnabled(bool enable) {
        try {
            if (!OperatingSystem.IsWindows()) return false;
            var exePath = Environment.ProcessPath;
            if (string.IsNullOrEmpty(exePath)) return false;

            using var key = Registry.CurrentUser.OpenSubKey(RunRegistrySubKey, writable: true);
            if (key == null) return false;

            if (enable) {
                key.SetValue(ApplicationName, FormatCommandLine(exePath));
            } else {
                key.DeleteValue(ApplicationName, throwOnMissingValue: false);
            }
            return true;
        } catch {
            return false;
        }
    }
}
```

- [ ] **Step 3: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~AutoStartServiceTests"`
Expected: PASS

---

### Task 3: Single Instance Mutex & Named Pipe Activation Channel

**Files:**
- Create: `apps/nfm-ui/Services/SingleInstanceChannel.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/SingleInstanceTests.cs`

- [ ] **Step 1: Write tests for `SingleInstanceChannel`**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/SingleInstanceTests.cs
using System;
using System.IO;
using System.IO.Pipes;
using System.Threading;
using System.Threading.Tasks;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class SingleInstanceTests {
    [Fact]
    public async Task SecondaryInstance_PingsPrimaryActivationChannelSuccessfully() {
        var testChannelName = "Nfm_Test_Activation_" + Guid.NewGuid().ToString("N");
        var receivedCommand = "";
        var tcs = new TaskCompletionSource<string>();

        using var server = new SingleInstanceServer(testChannelName, cmd => {
            receivedCommand = cmd;
            tcs.TrySetResult(cmd);
        });
        server.Start();

        var pingResult = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, "SHOW_MONITOR");
        Assert.True(pingResult);

        var completed = await Task.WhenAny(tcs.Task, Task.Delay(2000));
        Assert.Same(tcs.Task, completed);
        Assert.Equal("SHOW_MONITOR", await tcs.Task);
    }
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~SingleInstanceTests"`
Expected: FAIL due to missing `SingleInstanceChannel`.

- [ ] **Step 3: Implement `SingleInstanceChannel.cs`**

```csharp
// apps/nfm-ui/Services/SingleInstanceChannel.cs
using System;
using System.IO;
using System.IO.Pipes;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace NetworkFlowMonitor.Services;

public static class SingleInstanceChannel {
    public static string GetDefaultChannelName() {
        var userSid = WindowsIdentity.GetCurrent().User?.Value ?? "Default";
        return $"NetworkFlowMonitor_Activation_{userSid}";
    }

    public static string GetDefaultMutexName() {
        var userSid = WindowsIdentity.GetCurrent().User?.Value ?? "Default";
        return $@"Local\NetworkFlowMonitor_SingleInstance_{userSid}";
    }
}

public sealed class SingleInstanceServer : IDisposable {
    private readonly string _pipeName;
    private readonly Action<string> _onCommandReceived;
    private readonly CancellationTokenSource _cts = new();
    private Task? _serverTask;

    public SingleInstanceServer(string pipeName, Action<string> onCommandReceived) {
        _pipeName = pipeName;
        _onCommandReceived = onCommandReceived;
    }

    public void Start() {
        _serverTask = Task.Run(() => ServerLoopAsync(_cts.Token));
    }

    private async Task ServerLoopAsync(CancellationToken ct) {
        while (!ct.IsCancellationRequested) {
            try {
                using var pipe = new NamedPipeServerStream(
                    _pipeName,
                    PipeDirection.InOut,
                    1,
                    PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous
                );

                await pipe.WaitForConnectionAsync(ct).ConfigureAwait(false);

                using var reader = new StreamReader(pipe, Encoding.UTF8);
                using var writer = new StreamWriter(pipe, Encoding.UTF8) { AutoFlush = true };

                var command = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                if (!string.IsNullOrEmpty(command)) {
                    _onCommandReceived(command);
                    await writer.WriteLineAsync("ACK").ConfigureAwait(false);
                }
            } catch (OperationCanceledException) when (ct.IsCancellationRequested) {
                break;
            } catch {
                // Ignore and keep listening
            }
        }
    }

    public void Dispose() {
        _cts.Cancel();
        _cts.Dispose();
    }
}

public static class SingleInstanceClient {
    public static async Task<bool> SignalExistingInstanceAsync(string pipeName, string command, int timeoutMs = 1500) {
        try {
            using var pipe = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
            using var cts = new CancellationTokenSource(timeoutMs);

            await pipe.ConnectAsync(timeoutMs, cts.Token).ConfigureAwait(false);

            using var writer = new StreamWriter(pipe, Encoding.UTF8) { AutoFlush = true };
            using var reader = new StreamReader(pipe, Encoding.UTF8);

            await writer.WriteLineAsync(command).ConfigureAwait(false);
            var response = await reader.ReadLineAsync(cts.Token).ConfigureAwait(false);

            return response == "ACK";
        } catch {
            return false;
        }
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~SingleInstanceTests"`
Expected: PASS

---

### Task 4: Theme Service & Live Windows 11 Synchronization

**Files:**
- Create: `apps/nfm-ui/Services/ThemeService.cs`
- Modify: `apps/nfm-ui/Resources/FluentStyles.xaml`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/ThemeServiceTests.cs`

- [ ] **Step 1: Write tests for `ThemeService`**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/ThemeServiceTests.cs
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class ThemeServiceTests {
    [Fact]
    public void ResolveEffectiveTheme_ExplicitThemeOverridesSystem() {
        Assert.Equal(AppTheme.Light, ThemeService.ResolveEffectiveTheme(AppTheme.Light, systemIsDark: true));
        Assert.Equal(AppTheme.Dark, ThemeService.ResolveEffectiveTheme(AppTheme.Dark, systemIsDark: false));
    }

    [Fact]
    public void ResolveEffectiveTheme_SystemThemeFollowsSystemMode() {
        Assert.Equal(AppTheme.Dark, ThemeService.ResolveEffectiveTheme(AppTheme.System, systemIsDark: true));
        Assert.Equal(AppTheme.Light, ThemeService.ResolveEffectiveTheme(AppTheme.System, systemIsDark: false));
    }
}
```

- [ ] **Step 2: Implement `ThemeService.cs`**

```csharp
// apps/nfm-ui/Services/ThemeService.cs
using System;
using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Services;

public static class ThemeService {
    private static AppTheme _currentConfiguredTheme = AppTheme.System;
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
            if (!OperatingSystem.IsWindows()) return true;
            using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            if (key != null) {
                var val = key.GetValue("AppsUseLightTheme");
                if (val is int lightThemeInt) {
                    return lightThemeInt == 0;
                }
            }
        } catch { }
        return true; // default dark
    }

    public static void Initialize(AppTheme initialTheme) {
        _currentConfiguredTheme = initialTheme;
        ApplyTheme(EffectiveTheme);

        if (OperatingSystem.IsWindows()) {
            SystemEvents.UserPreferenceChanged += (_, _) => {
                if (_currentConfiguredTheme == AppTheme.System) {
                    ApplyTheme(EffectiveTheme);
                    ThemeChanged?.Invoke();
                }
            };
        }
    }

    public static void ApplyTheme(AppTheme effectiveTheme) {
        if (Application.Current == null) return;

        Application.Current.Dispatcher.Invoke(() => {
            var isDark = effectiveTheme == AppTheme.Dark;
            var resources = Application.Current.Resources;

            resources["CardBackgroundBrush"] = new SolidColorBrush(isDark ? Color.FromArgb(0xE6, 0x1F, 0x1F, 0x23) : Color.FromArgb(0xF0, 0xF9, 0xF9, 0xFB));
            resources["CardBorderBrush"] = new SolidColorBrush(isDark ? Color.FromArgb(0x33, 0xFF, 0xFF, 0xFF) : Color.FromArgb(0x20, 0x00, 0x00, 0x00));
            resources["PrimaryTextBrush"] = new SolidColorBrush(isDark ? Color.FromRgb(0xF4, 0xF4, 0xF5) : Color.FromRgb(0x18, 0x18, 0x1B));
            resources["SecondaryTextBrush"] = new SolidColorBrush(isDark ? Color.FromRgb(0xA1, 0xA1, 0xAA) : Color.FromRgb(0x71, 0x71, 0x7A));
            resources["AccentTextBrush"] = new SolidColorBrush(isDark ? Color.FromRgb(0x38, 0xBD, 0xF8) : Color.FromRgb(0x02, 0x84, 0xC7));
            resources["SettingsSidebarBrush"] = new SolidColorBrush(isDark ? Color.FromArgb(0xF2, 0x18, 0x18, 0x1B) : Color.FromArgb(0xF2, 0xEE, 0xEE, 0xF2));
        });
    }
}
```

- [ ] **Step 3: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~ThemeServiceTests"`
Expected: PASS

---

### Task 5: Dynamic Localization Expansion & Comprehensive Bilingual Dictionary

**Files:**
- Modify: `apps/nfm-ui/Services/LocalizationService.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/LocalizationServiceTests.cs`

- [ ] **Step 1: Write tests for `LocalizationService`**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/LocalizationServiceTests.cs
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class LocalizationServiceTests {
    [Fact]
    public void BrandName_RemainsUntranslatedAcrossAllLanguages() {
        LocalizationService.CurrentLanguage = AppLanguage.English;
        Assert.Equal("Network Flow Monitor", LocalizationService.BrandName);

        LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
        Assert.Equal("Network Flow Monitor", LocalizationService.BrandName);
    }

    [Fact]
    public void SettingsStrings_SwitchDynamicallyWithoutRestart() {
        LocalizationService.CurrentLanguage = AppLanguage.English;
        Assert.Equal("Settings", LocalizationService.Settings);
        Assert.Equal("General", LocalizationService.GeneralTab);
        Assert.Equal("Monitor", LocalizationService.MonitorTab);
        Assert.Equal("Collector", LocalizationService.CollectorTab);

        LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
        Assert.Equal("设置", LocalizationService.Settings);
        Assert.Equal("常规", LocalizationService.GeneralTab);
        Assert.Equal("监控", LocalizationService.MonitorTab);
        Assert.Equal("采集器", LocalizationService.CollectorTab);
    }
}
```

- [ ] **Step 2: Update `LocalizationService.cs` with complete Settings & Diagnostics keys**

```csharp
// apps/nfm-ui/Services/LocalizationService.cs
using System;
using System.Globalization;

namespace NetworkFlowMonitor.Services;

public enum AppLanguage {
    System,
    SimplifiedChinese,
    English
}

public static class LocalizationService {
    private static AppLanguage _currentLanguage = AppLanguage.System;

    public static event Action? LanguageChanged;

    public static AppLanguage CurrentLanguage {
        get => _currentLanguage;
        set {
            if (_currentLanguage != value) {
                _currentLanguage = value;
                LanguageChanged?.Invoke();
            }
        }
    }

    public static bool IsChinese =>
        _currentLanguage == AppLanguage.SimplifiedChinese ||
        (_currentLanguage == AppLanguage.System && CultureInfo.CurrentUICulture.Name.StartsWith("zh", StringComparison.OrdinalIgnoreCase));

    public static string BrandName => "Network Flow Monitor";

    // Shared Menu & Actions
    public static string HideMonitor => IsChinese ? "隐藏监控" : "Hide Monitor";
    public static string ShowMonitor => IsChinese ? "显示监控" : "Show Monitor";
    public static string Pin => IsChinese ? "固定显示" : "Pin";
    public static string RetryCollector => IsChinese ? "重试采集" : "Retry Collector";
    public static string RestartCollector => IsChinese ? "重启采集" : "Restart Collector";
    public static string Settings => IsChinese ? "设置" : "Settings";
    public static string Exit => IsChinese ? "退出" : "Exit";
    public static string CollectorOffline => IsChinese ? "采集器未运行" : "Collector offline";
    public static string CollectingData => IsChinese ? "正在采集数据…" : "Collecting data…";
    public static string NoNetworkActivity => IsChinese ? "无网络活动" : "No network activity";
    public static string CopyDiagnostics => IsChinese ? "复制诊断信息" : "Copy Diagnostics";
    public static string CopiedToClipboard => IsChinese ? "已复制到剪贴板" : "Copied to clipboard";

    // Settings Navigation Tabs
    public static string GeneralTab => IsChinese ? "常规" : "General";
    public static string MonitorTab => IsChinese ? "监控" : "Monitor";
    public static string RankingTab => IsChinese ? "排行" : "Ranking";
    public static string CollectorTab => IsChinese ? "采集器" : "Collector";
    public static string DiagnosticsTab => IsChinese ? "诊断" : "Diagnostics";

    // General Tab
    public static string LanguageLabel => IsChinese ? "语言" : "Language";
    public static string ThemeLabel => IsChinese ? "主题" : "Theme";
    public static string StartWithWindowsLabel => IsChinese ? "开机自动启动" : "Start with Windows";
    public static string LanguageSystem => IsChinese ? "跟随系统" : "System";
    public static string LanguageEnglish => "English";
    public static string LanguageChinese => "简体中文";
    public static string ThemeSystem => IsChinese ? "跟随系统" : "System";
    public static string ThemeLight => IsChinese ? "浅色" : "Light";
    public static string ThemeDark => IsChinese ? "深色" : "Dark";

    // Monitor Tab
    public static string AutoHideLabel => IsChinese ? "鼠标离开自动收起" : "Auto-Hide on Pointer Leave";
    public static string PinnedLabel => IsChinese ? "默认固定面板" : "Pin Panel by Default";
    public static string CapsuleWidthLabel => IsChinese ? "胶囊宽度 (DIP)" : "Capsule Width (DIP)";
    public static string CapsuleHeightLabel => IsChinese ? "胶囊高度 (DIP)" : "Capsule Height (DIP)";
    public static string HoverDelayLabel => IsChinese ? "展开悬停延迟 (ms)" : "Hover Reveal Delay (ms)";
    public static string DismissDelayLabel => IsChinese ? "收起延迟 (ms)" : "Dismiss Delay (ms)";
    public static string PanelOpacityLabel => IsChinese ? "面板背景不透明度" : "Panel Background Opacity";
    public static string BlurStrengthLabel => IsChinese ? "亚克力模糊强度" : "Acrylic Blur Strength";
    public static string TopmostYieldLabel => IsChinese ? "全屏应用时让出置顶" : "Yield Topmost on Fullscreen";
    public static string RateUnitsLabel => IsChinese ? "速率显示单位" : "Rate Display Units";

    // Ranking Tab
    public static string TopNLabel => IsChinese ? "显示应用数量 (Top N)" : "Top N Application Count";

    // Collector & Diagnostics Tab
    public static string ConnectionStateLabel => IsChinese ? "连接状态" : "Connection State";
    public static string CollectorPidLabel => IsChinese ? "采集器 PID" : "Collector PID";
    public static string InstanceIdLabel => IsChinese ? "实例 ID" : "Instance ID";
    public static string ElevationStatusLabel => IsChinese ? "权限状态" : "Elevation Status";
    public static string EtwHealthLabel => IsChinese ? "ETW 会话状态" : "ETW Session Health";
    public static string DroppedPacketsLabel => IsChinese ? "丢包数" : "Dropped Packets";
    public static string ActivePidsLabel => IsChinese ? "活跃进程数" : "Active Process Count";
    public static string AppVersionLabel => IsChinese ? "应用版本" : "App Version";
    public static string CollectorVersionLabel => IsChinese ? "采集器版本" : "Collector Version";
    public static string ProtocolVersionLabel => IsChinese ? "IPC 协议版本" : "Protocol Version";
    public static string ConfigPathLabel => IsChinese ? "配置文件路径" : "Config File Path";
}
```

- [ ] **Step 3: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~LocalizationServiceTests"`
Expected: PASS

---

### Task 6: Modern Fluent Settings Control Center View & ViewModel

**Files:**
- Create: `apps/nfm-ui/ViewModels/SettingsViewModel.cs`
- Create: `apps/nfm-ui/Views/SettingsWindow.xaml`
- Create: `apps/nfm-ui/Views/SettingsWindow.xaml.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/SettingsViewModelTests.cs`

- [ ] **Step 1: Write unit tests for `SettingsViewModel`**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/SettingsViewModelTests.cs
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.ViewModels;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class SettingsViewModelTests {
    [Fact]
    public void SettingsViewModel_LoadsAndAppliesConfig() {
        var store = new TelemetryStore();
        var config = new AppConfigModel(
            SchemaVersion: 1,
            General: new GeneralConfigModel(Language: AppLanguage.English, Theme: AppTheme.Dark, StartWithWindows: false),
            Monitor: new MonitorConfigModel(CapsuleWidthDip: 12, CapsuleHeightDip: 72, TopN: 6),
            Ranking: new RankingConfigModel(TopN: 6)
        );

        var vm = new SettingsViewModel(store, config, onSaveConfig: null);

        Assert.Equal(AppLanguage.English, vm.SelectedLanguage);
        Assert.Equal(AppTheme.Dark, vm.SelectedTheme);
        Assert.Equal(12, vm.CapsuleWidthDip);
        Assert.Equal(72, vm.CapsuleHeightDip);
        Assert.Equal(6, vm.TopN);
    }

    [Fact]
    public void SettingsViewModel_GeneratesDiagnosticDump() {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_123", 4567);
        var config = AppConfigModel.Default;

        var vm = new SettingsViewModel(store, config, onSaveConfig: null);
        var dump = vm.GenerateDiagnosticsText();

        Assert.Contains("Network Flow Monitor", dump);
        Assert.Contains("cinst_123", dump);
        Assert.Contains("4567", dump);
    }
}
```

- [ ] **Step 2: Implement `SettingsViewModel.cs`**

```csharp
// apps/nfm-ui/ViewModels/SettingsViewModel.cs
using System;
using System.ComponentModel;
using System.IO;
using System.Reflection;
using System.Text;
using System.Windows;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.ViewModels;

public sealed class SettingsViewModel : INotifyPropertyChanged {
    private readonly TelemetryStore _telemetryStore;
    private readonly Action<AppConfigModel>? _onSaveConfig;

    private int _selectedTabIndex = 0;
    private AppLanguage _selectedLanguage;
    private AppTheme _selectedTheme;
    private bool _startWithWindows;
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
    private byte _topN;
    private string _statusMessage = "";

    public event PropertyChangedEventHandler? PropertyChanged;

    public SettingsViewModel(TelemetryStore telemetryStore, AppConfigModel initialConfig, Action<AppConfigModel>? onSaveConfig) {
        _telemetryStore = telemetryStore;
        _onSaveConfig = onSaveConfig;

        var g = initialConfig.General ?? new GeneralConfigModel();
        var m = initialConfig.Monitor ?? new MonitorConfigModel();
        var r = initialConfig.Ranking ?? new RankingConfigModel();

        _selectedLanguage = g.Language;
        _selectedTheme = g.Theme;
        _startWithWindows = g.StartWithWindows;

        _autoHideEnabled = m.AutoHideEnabled;
        _pinned = m.Pinned;
        _capsuleWidthDip = m.CapsuleWidthDip;
        _capsuleHeightDip = m.CapsuleHeightDip;
        _hoverDelayMs = m.HoverDelayMs;
        _dismissDelayMs = m.DismissDelayMs;
        _panelOpacity = m.PanelOpacity;
        _blurStrength = m.BlurStrength;
        _yieldFullscreen = m.YieldFullscreen;
        _rateUnits = m.RateUnits;
        _topN = r.TopN;

        _telemetryStore.SnapshotUpdated += _ => OnPropertyChanged(nameof(CollectorSnapshot));
        LocalizationService.LanguageChanged += () => OnPropertyChanged(string.Empty);
    }

    public TelemetrySnapshot CollectorSnapshot => _telemetryStore.CurrentSnapshot;

    public int SelectedTabIndex {
        get => _selectedTabIndex;
        set { _selectedTabIndex = value; OnPropertyChanged(nameof(SelectedTabIndex)); }
    }

    public AppLanguage SelectedLanguage {
        get => _selectedLanguage;
        set {
            if (_selectedLanguage != value) {
                _selectedLanguage = value;
                LocalizationService.CurrentLanguage = value;
                OnPropertyChanged(nameof(SelectedLanguage));
                SaveCurrentConfig();
            }
        }
    }

    public AppTheme SelectedTheme {
        get => _selectedTheme;
        set {
            if (_selectedTheme != value) {
                _selectedTheme = value;
                ThemeService.CurrentTheme = value;
                OnPropertyChanged(nameof(SelectedTheme));
                SaveCurrentConfig();
            }
        }
    }

    public bool StartWithWindows {
        get => _startWithWindows;
        set {
            if (_startWithWindows != value) {
                _startWithWindows = value;
                AutoStartService.SetEnabled(value);
                OnPropertyChanged(nameof(StartWithWindows));
                SaveCurrentConfig();
            }
        }
    }

    public bool AutoHideEnabled {
        get => _autoHideEnabled;
        set {
            if (_autoHideEnabled != value) {
                _autoHideEnabled = value;
                OnPropertyChanged(nameof(AutoHideEnabled));
                SaveCurrentConfig();
            }
        }
    }

    public bool Pinned {
        get => _pinned;
        set {
            if (_pinned != value) {
                _pinned = value;
                OnPropertyChanged(nameof(Pinned));
                SaveCurrentConfig();
            }
        }
    }

    public int CapsuleWidthDip {
        get => _capsuleWidthDip;
        set {
            if (_capsuleWidthDip != value) {
                _capsuleWidthDip = value;
                OnPropertyChanged(nameof(CapsuleWidthDip));
                SaveCurrentConfig();
            }
        }
    }

    public int CapsuleHeightDip {
        get => _capsuleHeightDip;
        set {
            if (_capsuleHeightDip != value) {
                _capsuleHeightDip = value;
                OnPropertyChanged(nameof(CapsuleHeightDip));
                SaveCurrentConfig();
            }
        }
    }

    public int HoverDelayMs {
        get => _hoverDelayMs;
        set {
            if (_hoverDelayMs != value) {
                _hoverDelayMs = value;
                OnPropertyChanged(nameof(HoverDelayMs));
                SaveCurrentConfig();
            }
        }
    }

    public int DismissDelayMs {
        get => _dismissDelayMs;
        set {
            if (_dismissDelayMs != value) {
                _dismissDelayMs = value;
                OnPropertyChanged(nameof(DismissDelayMs));
                SaveCurrentConfig();
            }
        }
    }

    public double PanelOpacity {
        get => _panelOpacity;
        set {
            if (Math.Abs(_panelOpacity - value) > 0.001) {
                _panelOpacity = value;
                OnPropertyChanged(nameof(PanelOpacity));
                SaveCurrentConfig();
            }
        }
    }

    public double BlurStrength {
        get => _blurStrength;
        set {
            if (Math.Abs(_blurStrength - value) > 0.001) {
                _blurStrength = value;
                OnPropertyChanged(nameof(BlurStrength));
                SaveCurrentConfig();
            }
        }
    }

    public bool YieldFullscreen {
        get => _yieldFullscreen;
        set {
            if (_yieldFullscreen != value) {
                _yieldFullscreen = value;
                OnPropertyChanged(nameof(YieldFullscreen));
                SaveCurrentConfig();
            }
        }
    }

    public RateUnitMode RateUnits {
        get => _rateUnits;
        set {
            if (_rateUnits != value) {
                _rateUnits = value;
                OnPropertyChanged(nameof(RateUnits));
                SaveCurrentConfig();
            }
        }
    }

    public byte TopN {
        get => _topN;
        set {
            if (_topN != value) {
                _topN = value;
                OnPropertyChanged(nameof(TopN));
                SaveCurrentConfig();
            }
        }
    }

    public string StatusMessage {
        get => _statusMessage;
        set { _statusMessage = value; OnPropertyChanged(nameof(StatusMessage)); }
    }

    public void SaveCurrentConfig() {
        var current = new AppConfigModel(
            SchemaVersion: 1,
            General: new GeneralConfigModel(SelectedLanguage, SelectedTheme, StartWithWindows),
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
                RateUnits: RateUnits
            ),
            Ranking: new RankingConfigModel(TopN: TopN)
        );
        _onSaveConfig?.Invoke(current);
    }

    public string GenerateDiagnosticsText() {
        var sb = new StringBuilder();
        var snapshot = _telemetryStore.CurrentSnapshot;
        var version = Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "0.1.0";

        sb.AppendLine($"=== {LocalizationService.BrandName} Diagnostics ===");
        sb.AppendLine($"Timestamp (UTC): {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss}");
        sb.AppendLine($"App Version: {version}");
        sb.AppendLine($"Config Path: {ConfigStore.GetDefaultConfigPath()}");
        sb.AppendLine($"Protocol Version: {ProtocolConstants.CurrentProtocolVersion}");
        sb.AppendLine($"Connection State: {snapshot.ConnectionState}");
        sb.AppendLine($"Collector Instance ID: {snapshot.CollectorInstanceId ?? "None"}");
        sb.AppendLine($"Collector PID: {snapshot.CollectorPid}");
        sb.AppendLine($"ETW Session Active: {snapshot.LatestStatus?.EtwSessionActive ?? false}");
        sb.AppendLine($"Dropped Packets: {snapshot.LatestStatus?.DroppedPackets ?? 0}");
        sb.AppendLine($"Active PIDs: {snapshot.LatestStatus?.ActivePids ?? 0}");
        sb.AppendLine($"Download Rate: {RateFormatter.FormatRate(snapshot.DownloadBytesPerSec)}");
        sb.AppendLine($"Upload Rate: {RateFormatter.FormatRate(snapshot.UploadBytesPerSec)}");

        return sb.ToString();
    }

    public void CopyDiagnosticsToClipboard() {
        try {
            Clipboard.SetText(GenerateDiagnosticsText());
            StatusMessage = LocalizationService.CopiedToClipboard;
        } catch { }
    }

    private void OnPropertyChanged(string propertyName) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
```

- [ ] **Step 3: Create `SettingsWindow.xaml` and `SettingsWindow.xaml.cs`**

```xml
<!-- apps/nfm-ui/Views/SettingsWindow.xaml -->
<Window x:Class="NetworkFlowMonitor.Views.SettingsWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Network Flow Monitor - Settings"
        Width="680" Height="500"
        MinWidth="520" MinHeight="420"
        WindowStartupLocation="CenterScreen"
        Background="#18181B"
        Foreground="#F4F4F5"
        FontFamily="Segoe UI Variable, Segoe UI, sans-serif">
    <Grid>
        <Grid.ColumnDefinitions>
            <ColumnDefinition Width="180" MinWidth="140" MaxWidth="220" />
            <ColumnDefinition Width="*" />
        </Grid.ColumnDefinitions>

        <!-- Left Navigation Sidebar -->
        <Border Grid.Column="0" Background="#1F1F23" BorderBrush="#26FFFFFF" BorderThickness="0,0,1,0" Padding="8,16">
            <StackPanel>
                <TextBlock Text="Settings" FontSize="18" FontWeight="SemiBold" Foreground="#F4F4F5" Margin="8,0,0,16" />
                <ListBox x:Name="NavListBox" SelectedIndex="0" Background="Transparent" BorderThickness="0">
                    <ListBoxItem x:Name="NavGeneral" Content="General / 常规" Height="34" />
                    <ListBoxItem x:Name="NavMonitor" Content="Monitor / 监控" Height="34" />
                    <ListBoxItem x:Name="NavRanking" Content="Ranking / 排行" Height="34" />
                    <ListBoxItem x:Name="NavCollector" Content="Collector / 采集器" Height="34" />
                    <ListBoxItem x:Name="NavDiagnostics" Content="Diagnostics / 诊断" Height="34" />
                </ListBox>
            </StackPanel>
        </Border>

        <!-- Right Content Cards Area -->
        <ScrollViewer Grid.Column="1" VerticalScrollBarVisibility="Auto" Padding="20">
            <StackPanel x:Name="ContentPanel">
                <!-- Dynamic Content Bound to Selected Nav Item -->
                <TextBlock x:Name="SectionTitle" FontSize="20" FontWeight="SemiBold" Margin="0,0,0,16" Text="General" />
                
                <!-- General Section -->
                <StackPanel x:Name="GeneralSection">
                    <TextBlock Text="Language / 语言" FontWeight="SemiBold" Margin="0,8,0,4" />
                    <ComboBox x:Name="LanguageCombo" Width="200" HorizontalAlignment="Left" Margin="0,0,0,16" />

                    <TextBlock Text="Theme / 主题" FontWeight="SemiBold" Margin="0,8,0,4" />
                    <ComboBox x:Name="ThemeCombo" Width="200" HorizontalAlignment="Left" Margin="0,0,0,16" />

                    <CheckBox x:Name="AutoStartCheck" Content="Start with Windows / 开机自动启动" Margin="0,8,0,16" />
                </StackPanel>

                <!-- Monitor Section -->
                <StackPanel x:Name="MonitorSection" Visibility="Collapsed">
                    <CheckBox x:Name="AutoHideCheck" Content="Auto-Hide on Pointer Leave" Margin="0,8,0,12" />
                    <CheckBox x:Name="PinCheck" Content="Pin Panel by Default" Margin="0,0,0,16" />

                    <TextBlock Text="Capsule Width (8-24 DIP)" Margin="0,8,0,4" />
                    <Slider x:Name="CapsuleWidthSlider" Minimum="8" Maximum="24" SmallChange="1" Margin="0,0,0,12" />

                    <TextBlock Text="Capsule Height (40-160 DIP)" Margin="0,8,0,4" />
                    <Slider x:Name="CapsuleHeightSlider" Minimum="40" Maximum="160" SmallChange="4" Margin="0,0,0,12" />

                    <TextBlock Text="Panel Opacity (10% - 100%)" Margin="0,8,0,4" />
                    <Slider x:Name="PanelOpacitySlider" Minimum="0.1" Maximum="1.0" SmallChange="0.05" Margin="0,0,0,12" />

                    <CheckBox x:Name="TopmostYieldCheck" Content="Yield Topmost on Fullscreen" Margin="0,8,0,16" />
                </StackPanel>

                <!-- Ranking Section -->
                <StackPanel x:Name="RankingSection" Visibility="Collapsed">
                    <TextBlock Text="Top N Applications (1-20)" Margin="0,8,0,4" />
                    <Slider x:Name="TopNSlider" Minimum="1" Maximum="20" SmallChange="1" Margin="0,0,0,16" />
                </StackPanel>

                <!-- Collector Section -->
                <StackPanel x:Name="CollectorSection" Visibility="Collapsed">
                    <TextBlock x:Name="CollectorStatusText" Text="Collector Status: Connected" Margin="0,0,0,16" />
                    <StackPanel Orientation="Horizontal" Margin="0,8,0,16">
                        <Button x:Name="RetryCollectorBtn" Content="Retry Collector" Padding="12,6" Margin="0,0,8,0" />
                        <Button x:Name="RestartCollectorBtn" Content="Restart Collector" Padding="12,6" Margin="0,0,8,0" />
                    </StackPanel>
                </StackPanel>

                <!-- Diagnostics Section -->
                <StackPanel x:Name="DiagnosticsSection" Visibility="Collapsed">
                    <TextBox x:Name="DiagnosticsLogBox" Height="180" IsReadOnly="True" TextWrapping="Wrap" Margin="0,0,0,12" />
                    <Button x:Name="CopyDiagnosticsBtn" Content="Copy Diagnostics" Padding="12,6" HorizontalAlignment="Left" />
                </StackPanel>
            </StackPanel>
        </ScrollViewer>
    </Grid>
</Window>
```

- [ ] **Step 4: Implement `SettingsWindow.xaml.cs`**

```csharp
// apps/nfm-ui/Views/SettingsWindow.xaml.cs
using System;
using System.Windows;
using System.Windows.Controls;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.ViewModels;

namespace NetworkFlowMonitor.Views;

public partial class SettingsWindow : Window {
    private readonly SettingsViewModel _viewModel;

    public SettingsWindow(SettingsViewModel viewModel) {
        InitializeComponent();
        _viewModel = viewModel;
        DataContext = _viewModel;

        NavListBox.SelectionChanged += OnNavSelectionChanged;
        CopyDiagnosticsBtn.Click += (_, _) => _viewModel.CopyDiagnosticsToClipboard();
    }

    private void OnNavSelectionChanged(object sender, SelectionChangedEventArgs e) {
        GeneralSection.Visibility = Visibility.Collapsed;
        MonitorSection.Visibility = Visibility.Collapsed;
        RankingSection.Visibility = Visibility.Collapsed;
        CollectorSection.Visibility = Visibility.Collapsed;
        DiagnosticsSection.Visibility = Visibility.Collapsed;

        switch (NavListBox.SelectedIndex) {
            case 0:
                SectionTitle.Text = LocalizationService.GeneralTab;
                GeneralSection.Visibility = Visibility.Visible;
                break;
            case 1:
                SectionTitle.Text = LocalizationService.MonitorTab;
                MonitorSection.Visibility = Visibility.Visible;
                break;
            case 2:
                SectionTitle.Text = LocalizationService.RankingTab;
                RankingSection.Visibility = Visibility.Visible;
                break;
            case 3:
                SectionTitle.Text = LocalizationService.CollectorTab;
                CollectorSection.Visibility = Visibility.Visible;
                break;
            case 4:
                SectionTitle.Text = LocalizationService.DiagnosticsTab;
                DiagnosticsSection.Visibility = Visibility.Visible;
                DiagnosticsLogBox.Text = _viewModel.GenerateDiagnosticsText();
                break;
        }
    }
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~SettingsViewModelTests"`
Expected: PASS

---

### Task 7: Portable Companion Lifecycle Manager & System Tray Integration

**Files:**
- Create: `apps/nfm-ui/Services/CollectorProcessManager.cs`
- Create: `apps/nfm-ui/Services/TrayIconManager.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/CollectorProcessManagerTests.cs`

- [ ] **Step 1: Write tests for `CollectorProcessManager`**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/CollectorProcessManagerTests.cs
using System.IO;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class CollectorProcessManagerTests {
    [Fact]
    public void ResolveCollectorExecutable_ReturnsAdjacentExecutable() {
        var path = CollectorProcessManager.ResolveCollectorExecutablePath();
        Assert.NotNull(path);
        Assert.True(path.EndsWith(".exe", System.StringComparison.OrdinalIgnoreCase));
    }
}
```

- [ ] **Step 2: Implement `CollectorProcessManager.cs`**

```csharp
// apps/nfm-ui/Services/CollectorProcessManager.cs
using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Services;

public class CollectorProcessManager {
    private readonly TelemetryStore _store;
    private readonly NamedPipeClient _client;

    public CollectorProcessManager(TelemetryStore store, NamedPipeClient client) {
        _store = store;
        _client = client;
    }

    public static string ResolveCollectorExecutablePath() {
        var baseDir = AppContext.BaseDirectory;
        var collectorCandidate = Path.Combine(baseDir, "nfm-collector.exe");
        if (File.Exists(collectorCandidate)) {
            return collectorCandidate;
        }
        var appCandidate = Path.Combine(baseDir, "NetworkFlowMonitor.exe");
        return appCandidate;
    }

    public bool TryLaunchCollector() {
        try {
            var exePath = ResolveCollectorExecutablePath();
            if (!File.Exists(exePath)) return false;

            var startInfo = new ProcessStartInfo {
                FileName = exePath,
                Arguments = "--collector",
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            };

            Process.Start(startInfo);
            return true;
        } catch {
            return false;
        }
    }

    public async Task RestartCollectorAsync() {
        try {
            await _client.StopAsync();
            await Task.Delay(500);
            TryLaunchCollector();
            _client.Start();
        } catch { }
    }
}
```

- [ ] **Step 3: Implement `TrayIconManager.cs`**

```csharp
// apps/nfm-ui/Services/TrayIconManager.cs
using System;
using System.Drawing;
using System.Windows.Forms;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Services;

public sealed class TrayIconManager : IDisposable {
    private readonly NotifyIcon _notifyIcon;
    private readonly Action _onToggleMonitor;
    private readonly Action _onOpenSettings;
    private readonly Action _onExit;
    private readonly Action _onTogglePin;
    private readonly Action _onRestartCollector;

    public TrayIconManager(
        Action onToggleMonitor,
        Action onOpenSettings,
        Action onExit,
        Action onTogglePin,
        Action onRestartCollector) {
        _onToggleMonitor = onToggleMonitor;
        _onOpenSettings = onOpenSettings;
        _onExit = onExit;
        _onTogglePin = onTogglePin;
        _onRestartCollector = onRestartCollector;

        _notifyIcon = new NotifyIcon {
            Text = LocalizationService.BrandName,
            Icon = SystemIcons.Application,
            Visible = true
        };

        _notifyIcon.MouseClick += (s, e) => {
            if (e.Button == MouseButtons.Left) {
                _onToggleMonitor();
            }
        };

        UpdateContextMenu(isPanelVisible: true, isPinned: false, isCollectorOnline: true);
        LocalizationService.LanguageChanged += () => UpdateContextMenu(true, false, true);
    }

    public void UpdateContextMenu(bool isPanelVisible, bool isPinned, bool isCollectorOnline) {
        var menu = new ContextMenuStrip();

        var showHide = new ToolStripMenuItem(
            isPanelVisible ? LocalizationService.HideMonitor : LocalizationService.ShowMonitor,
            null,
            (s, e) => _onToggleMonitor()
        );
        menu.Items.Add(showHide);

        var pinItem = new ToolStripMenuItem(
            LocalizationService.Pin,
            null,
            (s, e) => _onTogglePin()
        ) {
            Checked = isPinned
        };
        menu.Items.Add(pinItem);

        menu.Items.Add(new ToolStripSeparator());

        var collectorItem = new ToolStripMenuItem(
            isCollectorOnline ? LocalizationService.RestartCollector : LocalizationService.RetryCollector,
            null,
            (s, e) => _onRestartCollector()
        );
        menu.Items.Add(collectorItem);

        var settingsItem = new ToolStripMenuItem(
            LocalizationService.Settings,
            null,
            (s, e) => _onOpenSettings()
        );
        menu.Items.Add(settingsItem);

        menu.Items.Add(new ToolStripSeparator());

        var exitItem = new ToolStripMenuItem(
            LocalizationService.Exit,
            null,
            (s, e) => _onExit()
        );
        menu.Items.Add(exitItem);

        _notifyIcon.ContextMenuStrip = menu;
    }

    public void Dispose() {
        _notifyIcon.Visible = false;
        _notifyIcon.Dispose();
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~CollectorProcessManagerTests"`
Expected: PASS

---

### Task 8: Production App Lifecycle, Single-Instance Entry & Host Wiring

**Files:**
- Modify: `apps/nfm-ui/App.xaml.cs`
- Modify: `apps/nfm-ui/Presentation/MonitorPresentationHost.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/WindowInteractionTests.cs`

- [ ] **Step 1: Wire all services into `App.xaml.cs`**

```csharp
// apps/nfm-ui/App.xaml.cs
using System;
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
    private Mutex? _singleInstanceMutex;
    private SingleInstanceServer? _activationServer;
    private ConfigStore? _configStore;
    private AppConfigModel? _config;
    private TelemetryStore? _store;
    private NamedPipeClient? _client;
    private CollectorProcessManager? _collectorManager;
    private DockCapsuleWindow? _capsuleWindow;
    private MonitorPanelWindow? _panelWindow;
    private SettingsWindow? _settingsWindow;
    private MonitorPresentationHost? _host;
    private TrayIconManager? _trayManager;

    protected override async void OnStartup(StartupEventArgs e) {
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

        // 1. Single Instance Check
        var mutexName = SingleInstanceChannel.GetDefaultMutexName();
        _singleInstanceMutex = new Mutex(true, mutexName, out bool isNewInstance);
        if (!isNewInstance) {
            var pipeName = SingleInstanceChannel.GetDefaultChannelName();
            var cmd = e.Args.Contains("--settings", StringComparer.OrdinalIgnoreCase) ? "SHOW_SETTINGS" : "SHOW_MONITOR";
            await SingleInstanceClient.SignalExistingInstanceAsync(pipeName, cmd);
            Shutdown(0);
            return;
        }

        // 2. Load Configuration
        _configStore = new ConfigStore();
        _config = _configStore.Load();

        // 3. Initialize Themes & Localization
        ThemeService.Initialize(_config.General?.Theme ?? AppTheme.System);
        LocalizationService.CurrentLanguage = _config.General?.Language ?? AppLanguage.System;

        // 4. Core Telemetry & IPC
        _store = new TelemetryStore();
        _client = new NamedPipeClient(_store);
        _collectorManager = new CollectorProcessManager(_store, _client);

        // 5. Windows
        _capsuleWindow = new DockCapsuleWindow();
        _panelWindow = new MonitorPanelWindow();

        // 6. Presentation Host
        _host = new MonitorPresentationHost(_store, _capsuleWindow, _panelWindow);
        _host.Start();
        _client.Start();

        // 7. System Tray
        _trayManager = new TrayIconManager(
            onToggleMonitor: () => _host.ToggleVisibility(),
            onOpenSettings: OpenSettingsWindow,
            onExit: () => Shutdown(0),
            onTogglePin: () => _host.TogglePin(),
            onRestartCollector: () => _ = _collectorManager.RestartCollectorAsync()
        );

        // 8. Activation Channel Server
        _activationServer = new SingleInstanceServer(SingleInstanceChannel.GetDefaultChannelName(), cmd => {
            Dispatcher.Invoke(() => {
                if (cmd == "SHOW_SETTINGS") {
                    OpenSettingsWindow();
                } else {
                    _host.ShowMonitor();
                }
            });
        });
        _activationServer.Start();
    }

    private void OpenSettingsWindow() {
        if (_settingsWindow != null && _settingsWindow.IsLoaded) {
            _settingsWindow.Activate();
            return;
        }

        var vm = new SettingsViewModel(_store!, _config!, saved => {
            _config = saved;
            _configStore?.Save(saved);
        });

        _settingsWindow = new SettingsWindow(vm);
        _settingsWindow.Show();
    }

    protected override async void OnExit(ExitEventArgs e) {
        _trayManager?.Dispose();
        _activationServer?.Dispose();
        _host?.Dispose();
        if (_client != null) {
            await _client.DisposeAsync();
        }
        _singleInstanceMutex?.Dispose();
        base.OnExit(e);
    }
}
```

- [ ] **Step 2: Run all .NET tests**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj`
Expected: 100% PASS

---

### Task 9: Unified Quality Gate & Release Packaging

**Files:**
- Modify: `tools/quality.ps1`
- Test: Run `tools/quality.ps1 -Mode Fast` and `tools/quality.ps1 -Mode Full`

- [ ] **Step 1: Update `tools/quality.ps1` to publish .NET 10 self-contained portable package into `dist/`**

Add .NET release publish and portable zip packing step to `tools/quality.ps1`:
- Publishes `NetworkFlowMonitor.csproj` in Release mode with `-r win-x64 --self-contained true /p:PublishSingleFile=true`.
- Bundles `NetworkFlowMonitor.exe`, `nfm-collector.exe` (or C++ collector), `README.md` into `dist/NetworkFlowMonitor-<version>-win-x64-portable.zip`.

- [ ] **Step 2: Run `tools/quality.ps1 -Mode Fast`**

Run: `powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Fast`
Expected: Format check, C++ debug build, C++ CTest, .NET xUnit tests, clang-tidy all PASS (100% headless).

- [ ] **Step 3: Run `tools/quality.ps1 -Mode Full`**

Run: `powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Full`
Expected: Fast gate + MSVC /analyze + CPack/Release packaging into `dist/` all PASS.

---

### Task 10: Legacy C++ UI Source Retirement Certification

**Files:**
- Clean up: `src/ui/` legacy source upon confirmation
- Update: `src/CMakeLists.txt` to remove `nfm_ui`
- Verify: `tools/quality.ps1 -Mode Fast` and `tools/quality.ps1 -Mode Full`

- [ ] **Step 1: Check gating criteria 1–5 for legacy UI removal**
- [ ] **Step 2: Safely remove `src/ui/` and update `src/CMakeLists.txt`**
- [ ] **Step 3: Re-run Full quality gate to certify clean build**

---
