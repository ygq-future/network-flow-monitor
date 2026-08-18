using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Interop;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class AppLifecycleAndHostWiringTests {
    [Theory]
    [InlineData(new[] { "--settings" }, SingleInstanceChannel.ActivationCommandShowSettings)]
    [InlineData(new[] { "--SETTINGS" }, SingleInstanceChannel.ActivationCommandShowSettings)]
    [InlineData(new[] { "--toggle-pin" }, SingleInstanceChannel.ActivationCommandTogglePin)]
    [InlineData(new[] { "--pin" }, SingleInstanceChannel.ActivationCommandTogglePin)]
    [InlineData(new[] { "--exit" }, SingleInstanceChannel.ActivationCommandExit)]
    [InlineData(new[] { "--quit" }, SingleInstanceChannel.ActivationCommandExit)]
    [InlineData(new string[0], SingleInstanceChannel.ActivationCommandShowMonitor)]
    [InlineData(new[] { "--other" }, SingleInstanceChannel.ActivationCommandShowMonitor)]
    public void ResolveActivationCommand_WithVariousArgs_ReturnsExpectedCommand(string[] args, string expectedCommand) {
        var command = App.ResolveActivationCommand(args);
        Assert.Equal(expectedCommand, command);
    }

    [Fact]
    public void CreateMonitorConfig_FromAppConfigModel_MapsPropertiesAccurately() {
        var appConfig = new AppConfigModel(
            SchemaVersion: 1,
            General: new GeneralConfigModel(Language: AppLanguage.English, Theme: AppTheme.Dark, StartWithWindows: true),
            Monitor: new MonitorConfigModel(
                AutoHideEnabled: false,
                Pinned: true,
                CapsuleWidthDip: 16,
                CapsuleHeightDip: 96,
                HoverDelayMs: 400,
                DismissDelayMs: 600,
                PanelOpacity: 0.9,
                BlurStrength: 0.8,
                YieldFullscreen: false,
                RateUnits: RateUnitMode.AutoBits,
                Anchor: new PersistedAnchorModel("MON_1", DockSide.Left, 120)
            ),
            Ranking: new RankingConfigModel(TopN: 10)
        );

        var monitorConfig = App.CreateMonitorConfig(appConfig);

        Assert.False(monitorConfig.AutoHideEnabled);
        Assert.True(monitorConfig.Pinned);
        Assert.Equal(16, monitorConfig.CapsuleWidthDip);
        Assert.Equal(96, monitorConfig.CapsuleHeightDip);
        Assert.Equal(TimeSpan.FromMilliseconds(400), monitorConfig.HoverDelay);
        Assert.Equal(TimeSpan.FromMilliseconds(600), monitorConfig.AutoHideDelay);
        Assert.Equal(10, monitorConfig.TopN);
        Assert.False(monitorConfig.YieldFullscreen);
    }

    [Fact]
    public void CreateInitialAnchor_WithConfigAnchor_MapsAccurately() {
        var configWithAnchor = new AppConfigModel(
            Monitor: new MonitorConfigModel(Anchor: new PersistedAnchorModel("DISPLAY_2", DockSide.Left, 250))
        );

        var anchor = App.CreateInitialAnchor(configWithAnchor);

        Assert.NotNull(anchor);
        Assert.Equal("DISPLAY_2", anchor.Value.MonitorId);
        Assert.Equal(DockSide.Left, anchor.Value.Side);
        Assert.Equal(250, anchor.Value.VerticalOffsetDip);

        var configWithoutAnchor = new AppConfigModel(Monitor: new MonitorConfigModel(Anchor: null));
        var nullAnchor = App.CreateInitialAnchor(configWithoutAnchor);
        Assert.Null(nullAnchor);
    }

    [Fact]
    public void MonitorPresentationHost_HelperMethods_DispatchExpectedTransitions() {
        WpfTestRunner.Run(() => {
            var store = new TelemetryStore();
            var capsule = new DockCapsuleWindow();
            var panel = new MonitorPanelWindow();
            new WindowInteropHelper(capsule).EnsureHandle();
            new WindowInteropHelper(panel).EnsureHandle();

            var config = new MonitorConfig(AutoHideEnabled: true, Pinned: false);
            using var host = new MonitorPresentationHost(store, capsule, panel, config);

            host.Start(new PersistedMonitorAnchor("MON_PRIMARY", DockSide.Right, 50));
            Assert.Equal(MonitorPresentationState.CapsuleVisible, host.State.State);

            // 1. Hide Monitor
            host.HideMonitor();
            Assert.Equal(MonitorPresentationState.TrayOnly, host.State.State);

            // 2. Show Monitor
            host.ShowMonitor();
            Assert.Equal(MonitorPresentationState.CapsuleVisible, host.State.State);

            // 3. Toggle Visibility
            host.ToggleVisibility();
            Assert.Equal(MonitorPresentationState.TrayOnly, host.State.State);

            host.ToggleVisibility();
            Assert.Equal(MonitorPresentationState.CapsuleVisible, host.State.State);

            // 4. Toggle Pin
            Assert.False(host.State.PinPreference);
            host.TogglePin();
            Assert.True(host.State.PinPreference);

            capsule.Close();
            panel.Close();
        });
    }

    [Fact]
    public void MonitorPresentationHost_AnchorAndPinPersistence_IntegratesWithConfigStore() {
        var tempFile = Path.Combine(Path.GetTempPath(), $"nfm_test_config_{Guid.NewGuid():N}.json");
        try {
            var configStore = new ConfigStore(tempFile);
            var initialConfig = AppConfigModel.Default;
            configStore.Save(initialConfig);

            PersistedMonitorAnchor? savedAnchor = null;
            bool? savedPin = null;

            WpfTestRunner.Run(() => {
                var store = new TelemetryStore();
                var capsule = new DockCapsuleWindow();
                var panel = new MonitorPanelWindow();
                new WindowInteropHelper(capsule).EnsureHandle();
                new WindowInteropHelper(panel).EnsureHandle();

                var config = new MonitorConfig(AutoHideEnabled: true, Pinned: false);
                using var host = new MonitorPresentationHost(store, capsule, panel, config);

                host.AnchorPersisted += anchor => {
                    savedAnchor = anchor;
                    var current = configStore.Load();
                    var updatedMonitor = (current.Monitor ?? new MonitorConfigModel()) with {
                        Anchor = new PersistedAnchorModel(anchor.MonitorId, anchor.Side, anchor.VerticalOffsetDip)
                    };
                    configStore.Save(current with { Monitor = updatedMonitor });
                };

                host.PinPersisted += pin => {
                    savedPin = pin;
                    var current = configStore.Load();
                    var updatedMonitor = (current.Monitor ?? new MonitorConfigModel()) with {
                        Pinned = pin
                    };
                    configStore.Save(current with { Monitor = updatedMonitor });
                };

                host.Start();

                // Trigger pin toggle
                host.TogglePin();

                // Trigger anchor change event directly
                host.Dispatch(new MonitorEvent(MonitorEventKind.EnvironmentChanged));

                capsule.Close();
                panel.Close();
            });

            Assert.True(savedPin);
            var reloaded = configStore.Load();
            Assert.True(reloaded.Monitor?.Pinned);
        } finally {
            if (File.Exists(tempFile)) {
                try { File.Delete(tempFile); } catch { }
            }
        }
    }

    [Fact]
    public async Task SingleInstanceChannel_EndToEnd_DispatchesToAppHandler() {
        var testChannelName = "Nfm_AppTest_Channel_" + Guid.NewGuid().ToString("N");
        string? handledCommand = null;
        var tcs = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);

        await using var server = new SingleInstanceServer(testChannelName, cmd => {
            handledCommand = cmd;
            tcs.TrySetResult(cmd);
        });
        server.Start();

        var success = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, SingleInstanceChannel.ActivationCommandShowSettings);
        Assert.True(success);

        var completed = await Task.WhenAny(tcs.Task, Task.Delay(2000));
        Assert.Same(tcs.Task, completed);
        Assert.Equal(SingleInstanceChannel.ActivationCommandShowSettings, handledCommand);
    }

    [Fact]
    public void Windows_InitializeWithActionCallbacks_AttachesWithoutError() {
        WpfTestRunner.Run(() => {
            var capsule = new DockCapsuleWindow();
            var panel = new MonitorPanelWindow();

            bool settingsOpened = false;
            bool collectorRestarted = false;

            capsule.Initialize(
                _ => { },
                onOpenSettings: () => settingsOpened = true,
                onRestartCollector: () => collectorRestarted = true
            );

            panel.Initialize(
                _ => { },
                onOpenSettings: () => settingsOpened = true,
                onRestartCollector: () => collectorRestarted = true
            );

            Assert.False(settingsOpened);
            Assert.False(collectorRestarted);

            capsule.Close();
            panel.Close();
        });
    }
}
