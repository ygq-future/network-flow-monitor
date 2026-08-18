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
        GC.SuppressFinalize(this);
    }

    [Fact]
    public void Load_WhenFileMissing_ReturnsDefaultConfig() {
        var store = new ConfigStore(_configFilePath);
        var config = store.Load();

        Assert.NotNull(config);
        Assert.Equal(1, config.SchemaVersion);
        Assert.Equal(AppLanguage.System, config.General?.Language ?? AppLanguage.System);
        Assert.Equal(AppTheme.System, config.General?.Theme ?? AppTheme.System);
        Assert.False(config.General?.StartWithWindows ?? false);
        Assert.True(config.Monitor?.AutoHideEnabled ?? false);
        Assert.False(config.Monitor?.Pinned ?? true);
        Assert.Equal(10, config.Monitor?.CapsuleWidthDip ?? 0);
        Assert.Equal(64, config.Monitor?.CapsuleHeightDip ?? 0);
        Assert.Equal(350, config.Monitor?.HoverDelayMs ?? 0);
        Assert.Equal(500, config.Monitor?.DismissDelayMs ?? 0);
        Assert.Equal(0.85, config.Monitor?.PanelOpacity ?? 0, precision: 2);
        Assert.Equal(1.0, config.Monitor?.BlurStrength ?? 0, precision: 2);
        Assert.True(config.Monitor?.YieldFullscreen ?? false);
        Assert.Equal(RateUnitMode.AutoBytes, config.Monitor?.RateUnits ?? RateUnitMode.AutoBits);
        Assert.Null(config.Monitor?.Anchor);
        Assert.Equal((byte)5, config.Ranking?.TopN ?? (byte)0);
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
        Assert.Equal(1, loaded.SchemaVersion);
        Assert.Equal(AppLanguage.SimplifiedChinese, loaded.General?.Language);
        Assert.Equal(AppTheme.Dark, loaded.General?.Theme);
        Assert.True(loaded.General?.StartWithWindows);
        Assert.False(loaded.Monitor?.AutoHideEnabled);
        Assert.True(loaded.Monitor?.Pinned);
        Assert.Equal(14, loaded.Monitor?.CapsuleWidthDip);
        Assert.Equal(80, loaded.Monitor?.CapsuleHeightDip);
        Assert.Equal(250, loaded.Monitor?.HoverDelayMs);
        Assert.Equal(400, loaded.Monitor?.DismissDelayMs);
        Assert.Equal(0.9, loaded.Monitor?.PanelOpacity ?? 0, precision: 2);
        Assert.Equal(0.8, loaded.Monitor?.BlurStrength ?? 0, precision: 2);
        Assert.False(loaded.Monitor?.YieldFullscreen);
        Assert.Equal(RateUnitMode.MegabytesPerSec, loaded.Monitor?.RateUnits);
        Assert.NotNull(loaded.Monitor?.Anchor);
        Assert.Equal("MON_2", loaded.Monitor?.Anchor?.MonitorId);
        Assert.Equal(DockSide.Left, loaded.Monitor?.Anchor?.Side);
        Assert.Equal(120, loaded.Monitor?.Anchor?.VerticalOffsetDip);
        Assert.Equal((byte)8, loaded.Ranking?.TopN);
    }

    [Fact]
    public void Load_WhenCorrupted_PreservesDamagedFileAndReturnsDefaults() {
        const string corruptContent = "{ invalid json content !!! @#$ }";
        File.WriteAllText(_configFilePath, corruptContent);

        var store = new ConfigStore(_configFilePath);
        var loaded = store.Load();

        Assert.NotNull(loaded);
        Assert.Equal(AppConfigModel.Default.SchemaVersion, loaded.SchemaVersion);
        Assert.Equal(AppConfigModel.Default.General?.Language, loaded.General?.Language);

        // Verify .damaged file was created and contains the corrupt content
        var damagedFiles = Directory.GetFiles(_tempDirectory, "*.damaged.json");
        Assert.NotEmpty(damagedFiles);
        var savedCorrupt = File.ReadAllText(damagedFiles[0]);
        Assert.Equal(corruptContent, savedCorrupt);
    }
}
