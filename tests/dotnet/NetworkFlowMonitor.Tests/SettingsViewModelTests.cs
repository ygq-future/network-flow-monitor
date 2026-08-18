using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.ViewModels;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class SettingsViewModelTests {
    [Fact]
    public void InitialLoading_LoadsConfigValuesIntoProperties_WithoutTriggeringSave() {
        var initial = new AppConfigModel(
            SchemaVersion: 1,
            General: new GeneralConfigModel(
                Language: AppLanguage.SimplifiedChinese,
                Theme: AppTheme.Light,
                StartWithWindows: true
            ),
            Monitor: new MonitorConfigModel(
                AutoHideEnabled: false,
                Pinned: true,
                CapsuleWidthDip: 18,
                CapsuleHeightDip: 120,
                HoverDelayMs: 450,
                DismissDelayMs: 650,
                PanelOpacity: 0.75,
                BlurStrength: 0.5,
                YieldFullscreen: false,
                RateUnits: RateUnitMode.KilobytesPerSec,
                Anchor: new PersistedAnchorModel("MON-1", DockSide.Left, 200)
            ),
            Ranking: new RankingConfigModel(
                TopN: 10
            )
        );

        var saveCount = 0;
        using var vm = new SettingsViewModel(
            initialConfig: initial,
            onSaveConfig: _ => saveCount++
        );

        Assert.Equal(0, saveCount);
        Assert.Equal(AppLanguage.SimplifiedChinese, vm.SelectedLanguage);
        Assert.Equal(AppTheme.Light, vm.SelectedTheme);
        Assert.True(vm.StartWithWindows);
        Assert.False(vm.AutoHideEnabled);
        Assert.True(vm.Pinned);
        Assert.Equal(18, vm.CapsuleWidthDip);
        Assert.Equal(120, vm.CapsuleHeightDip);
        Assert.Equal(450, vm.HoverDelayMs);
        Assert.Equal(650, vm.DismissDelayMs);
        Assert.Equal(0.75, vm.PanelOpacity, 3);
        Assert.Equal(0.5, vm.BlurStrength, 3);
        Assert.False(vm.YieldFullscreen);
        Assert.Equal(RateUnitMode.KilobytesPerSec, vm.RateUnits);
        Assert.Equal(10, vm.TopN);
    }

    [Fact]
    public void PropertyChanges_UpdatePropertiesAndTriggerOnSaveConfig() {
        var savedConfigs = new List<AppConfigModel>();
        using var vm = new SettingsViewModel(
            initialConfig: AppConfigModel.Default,
            onSaveConfig: cfg => savedConfigs.Add(cfg)
        );

        // General
        vm.SelectedLanguage = AppLanguage.English;
        Assert.Equal(AppLanguage.English, vm.SelectedLanguage);
        Assert.Equal(AppLanguage.English, savedConfigs[^1].General!.Language);

        vm.SelectedTheme = AppTheme.Dark;
        Assert.Equal(AppTheme.Dark, vm.SelectedTheme);
        Assert.Equal(AppTheme.Dark, savedConfigs[^1].General!.Theme);

        vm.StartWithWindows = true;
        Assert.True(vm.StartWithWindows);
        Assert.True(savedConfigs[^1].General!.StartWithWindows);

        // Monitor
        vm.AutoHideEnabled = false;
        Assert.False(vm.AutoHideEnabled);
        Assert.False(savedConfigs[^1].Monitor!.AutoHideEnabled);

        vm.Pinned = true;
        Assert.True(vm.Pinned);
        Assert.True(savedConfigs[^1].Monitor!.Pinned);

        vm.CapsuleWidthDip = 20;
        Assert.Equal(20, vm.CapsuleWidthDip);
        Assert.Equal(20, savedConfigs[^1].Monitor!.CapsuleWidthDip);

        vm.CapsuleHeightDip = 100;
        Assert.Equal(100, vm.CapsuleHeightDip);
        Assert.Equal(100, savedConfigs[^1].Monitor!.CapsuleHeightDip);

        vm.HoverDelayMs = 500;
        Assert.Equal(500, vm.HoverDelayMs);
        Assert.Equal(500, savedConfigs[^1].Monitor!.HoverDelayMs);

        vm.DismissDelayMs = 800;
        Assert.Equal(800, vm.DismissDelayMs);
        Assert.Equal(800, savedConfigs[^1].Monitor!.DismissDelayMs);

        vm.PanelOpacity = 0.60;
        Assert.Equal(0.60, vm.PanelOpacity, 3);
        Assert.Equal(0.60, savedConfigs[^1].Monitor!.PanelOpacity, 3);

        vm.BlurStrength = 0.80;
        Assert.Equal(0.80, vm.BlurStrength, 3);
        Assert.Equal(0.80, savedConfigs[^1].Monitor!.BlurStrength, 3);

        vm.YieldFullscreen = false;
        Assert.False(vm.YieldFullscreen);
        Assert.False(savedConfigs[^1].Monitor!.YieldFullscreen);

        vm.RateUnits = RateUnitMode.MegabytesPerSec;
        Assert.Equal(RateUnitMode.MegabytesPerSec, vm.RateUnits);
        Assert.Equal(RateUnitMode.MegabytesPerSec, savedConfigs[^1].Monitor!.RateUnits);

        // Ranking
        vm.TopN = 15;
        Assert.Equal(15, vm.TopN);
        Assert.Equal(15, savedConfigs[^1].Ranking!.TopN);

        Assert.Equal(14, savedConfigs.Count);
    }

    [Theory]
    [InlineData(4, 8)]
    [InlineData(8, 8)]
    [InlineData(15, 15)]
    [InlineData(24, 24)]
    [InlineData(30, 24)]
    public void CapsuleWidthDip_ClampsCorrectly(int input, int expected) {
        using var vm = new SettingsViewModel();
        vm.CapsuleWidthDip = input;
        Assert.Equal(expected, vm.CapsuleWidthDip);
    }

    [Theory]
    [InlineData(20, 40)]
    [InlineData(40, 40)]
    [InlineData(80, 80)]
    [InlineData(160, 160)]
    [InlineData(200, 160)]
    public void CapsuleHeightDip_ClampsCorrectly(int input, int expected) {
        using var vm = new SettingsViewModel();
        vm.CapsuleHeightDip = input;
        Assert.Equal(expected, vm.CapsuleHeightDip);
    }

    [Theory]
    [InlineData(50, 100)]
    [InlineData(100, 100)]
    [InlineData(500, 500)]
    [InlineData(2000, 2000)]
    [InlineData(2500, 2000)]
    public void Delays_ClampsCorrectly(int input, int expected) {
        using var vm = new SettingsViewModel();
        vm.HoverDelayMs = input;
        vm.DismissDelayMs = input;
        Assert.Equal(expected, vm.HoverDelayMs);
        Assert.Equal(expected, vm.DismissDelayMs);
    }

    [Theory]
    [InlineData(0.05, 0.1)]
    [InlineData(0.1, 0.1)]
    [InlineData(0.5, 0.5)]
    [InlineData(1.0, 1.0)]
    [InlineData(1.5, 1.0)]
    public void PanelOpacity_ClampsCorrectly(double input, double expected) {
        using var vm = new SettingsViewModel();
        vm.PanelOpacity = input;
        Assert.Equal(expected, vm.PanelOpacity, 3);
    }

    [Theory]
    [InlineData(-0.5, 0.0)]
    [InlineData(0.0, 0.0)]
    [InlineData(0.5, 0.5)]
    [InlineData(1.0, 1.0)]
    [InlineData(1.2, 1.0)]
    public void BlurStrength_ClampsCorrectly(double input, double expected) {
        using var vm = new SettingsViewModel();
        vm.BlurStrength = input;
        Assert.Equal(expected, vm.BlurStrength, 3);
    }

    [Theory]
    [InlineData(0, 1)]
    [InlineData(1, 1)]
    [InlineData(10, 10)]
    [InlineData(20, 20)]
    [InlineData(25, 20)]
    public void TopN_ClampsCorrectly(int input, int expected) {
        using var vm = new SettingsViewModel();
        vm.TopN = input;
        Assert.Equal(expected, vm.TopN);
    }

    [Fact]
    public void GenerateDiagnosticsText_ContainsBrandVersionPidInstanceIdDroppedPacketsActivePids() {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "test-instance-789", 4321);
        store.UpdateStatus(new CollectorStatusMessage(
            MonotonicMs: 12345,
            EtwSessionActive: true,
            DroppedPackets: 128,
            ActivePids: 17
        ));

        using var vm = new SettingsViewModel(
            initialConfig: AppConfigModel.Default,
            telemetryStore: store,
            configPath: @"C:\TestPath\NetworkFlowMonitor.json"
        );

        var text = vm.GenerateDiagnosticsText();

        Assert.Contains(LocalizationService.BrandName, text);
        Assert.Contains("App Version:", text);
        Assert.Contains("Protocol Version:", text);
        Assert.Contains("4321", text); // PID
        Assert.Contains("test-instance-789", text); // Instance ID
        Assert.Contains("128", text); // Dropped packets
        Assert.Contains("17", text); // Active PIDs
        Assert.Contains(@"C:\TestPath\NetworkFlowMonitor.json", text);
    }

    [Fact]
    public void CopyDiagnosticsToClipboard_CopiesFormattedTextAndSetsNotice() {
        string? copiedContent = null;
        using var vm = new SettingsViewModel(
            initialConfig: AppConfigModel.Default,
            clipboardSetter: text => copiedContent = text
        );

        Assert.Empty(vm.CopyStatusText);

        vm.CopyDiagnosticsCommand.Execute(null);

        Assert.NotNull(copiedContent);
        Assert.Contains(LocalizationService.BrandName, copiedContent);
        Assert.Contains("=== Network Flow Monitor Diagnostics ===", copiedContent);
        Assert.Contains("--- Configuration ---", copiedContent);
        Assert.Equal(LocalizationService.CopiedToClipboard, vm.CopyStatusText);
    }

    [Fact]
    public void TelemetryStore_LiveSnapshotUpdate_UpdatesPropertiesInRealTime() {
        WpfTestRunner.Run(() => {
            var store = new TelemetryStore();
            using var vm = new SettingsViewModel(telemetryStore: store);

            Assert.Equal(CollectorConnectionState.Disconnected, vm.ConnectionState);
            Assert.Equal(0, vm.CollectorPid);
            Assert.Empty(vm.CollectorInstanceId);
            Assert.False(vm.EtwSessionActive);
            Assert.Equal(0UL, vm.DroppedPackets);
            Assert.Equal(0U, vm.ActivePids);

            // Update connection
            store.UpdateConnection(CollectorConnectionState.Connected, "inst-live-42", 9876);

            Assert.Equal(CollectorConnectionState.Connected, vm.ConnectionState);
            Assert.Equal(9876, vm.CollectorPid);
            Assert.Equal("inst-live-42", vm.CollectorInstanceId);

            // Update collector status
            store.UpdateStatus(new CollectorStatusMessage(
                MonotonicMs: 9999,
                EtwSessionActive: true,
                DroppedPackets: 55,
                ActivePids: 23
            ));

            Assert.True(vm.EtwSessionActive);
            Assert.Equal(55UL, vm.DroppedPackets);
            Assert.Equal(23U, vm.ActivePids);
        });
    }

    [Fact]
    public void RetryAndRestartCommands_InvokeAssignedCallbacks() {
        var retryCalled = 0;
        var restartCalled = 0;

        using var vm = new SettingsViewModel(
            retryCollectorAction: () => retryCalled++,
            restartCollectorAction: () => restartCalled++
        );

        vm.RetryCollectorCommand.Execute(null);
        Assert.Equal(1, retryCalled);

        vm.RestartCollectorCommand.Execute(null);
        Assert.Equal(1, restartCalled);
    }

    [Fact]
    public void LocalizationAndThemeChanges_UpdateViewModelDynamically() {
        LocalizationService.CurrentLanguage = AppLanguage.System;
        ThemeService.CurrentTheme = AppTheme.System;

        using var vm = new SettingsViewModel();

        LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
        Assert.Equal(AppLanguage.SimplifiedChinese, vm.SelectedLanguage);
        Assert.Equal("常规", vm.GeneralTabHeader);

        LocalizationService.CurrentLanguage = AppLanguage.English;
        Assert.Equal(AppLanguage.English, vm.SelectedLanguage);
        Assert.Equal("General", vm.GeneralTabHeader);

        ThemeService.CurrentTheme = AppTheme.Dark;
        Assert.Equal(AppTheme.Dark, vm.SelectedTheme);

        ThemeService.CurrentTheme = AppTheme.Light;
        Assert.Equal(AppTheme.Light, vm.SelectedTheme);
    }

    [Fact]
    public void SelectedTabIndex_WhenUpdated_RaisesPropertyChanged() {
        using var vm = new SettingsViewModel();
        var changedProps = new List<string?>();
        vm.PropertyChanged += (_, e) => changedProps.Add(e.PropertyName);

        Assert.Equal(0, vm.SelectedTabIndex);

        vm.SelectedTabIndex = 2;
        Assert.Equal(2, vm.SelectedTabIndex);
        Assert.Contains(nameof(vm.SelectedTabIndex), changedProps);
    }

    [Fact]
    public void SettingsWindow_InstantiatesAndBindsDataContext() {
        WpfTestRunner.Run(() => {
            var window = new SettingsWindow();
            Assert.NotNull(window.ViewModel);
            Assert.Equal(0, window.ViewModel.SelectedTabIndex);
            Assert.Equal("Network Flow Monitor - Settings", window.Title);
            window.Close();
        });
    }

    [Fact]
    public void SettingsWindow_WithCustomViewModel_BindsProvidedViewModel() {
        WpfTestRunner.Run(() => {
            var initial = new AppConfigModel(
                SchemaVersion: 1,
                General: new GeneralConfigModel(Language: AppLanguage.English, Theme: AppTheme.Dark),
                Monitor: new MonitorConfigModel(YieldFullscreen: false, RateUnits: RateUnitMode.MegabytesPerSec),
                Ranking: new RankingConfigModel(TopN: 8)
            );
            using var vm = new SettingsViewModel(initial);
            var window = new SettingsWindow(vm);
            Assert.Same(vm, window.ViewModel);
            Assert.Equal(AppLanguage.English, window.ViewModel.SelectedLanguage);
            Assert.Equal(8, window.ViewModel.TopN);
            window.Close();
        });
    }
}
