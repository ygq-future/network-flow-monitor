using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class MonitorPanelVisualTests {
    [Fact]
    public void MonitorPanelWindow_RendersHeaderAndTabularRates() {
        WpfTestRunner.Run(() => {
            var window = new MonitorPanelWindow();
            var snapshot = new TelemetrySnapshot(
                Seq: 100,
                MonotonicMs: 12345,
                DownloadBytesPerSec: 10485760, // 10 MB/s
                UploadBytesPerSec: 1048576,   // 1 MB/s
                LastUpdated: DateTimeOffset.UtcNow,
                ConnectionState: CollectorConnectionState.Connected,
                CollectorInstanceId: "inst1",
                CollectorPid: 1234,
                LatestRanking: new RankingSnapshotMessage(
                    Seq: 100,
                    MonotonicMs: 12345,
                    Status: "Ready",
                    TotalRate: new TotalRate(10485760, 1048576),
                    Applications: [
                        new RankingApplication(
                            Id: "app1",
                            ExecutablePath: @"C:\Windows\explorer.exe",
                            DisplayName: "Windows Explorer",
                            IconKey: @"C:\Windows\explorer.exe",
                            DownloadBytesPerSec: 5242880,
                            UploadBytesPerSec: 524288
                        )
                    ]
                ),
                LatestStatus: null
            );

            window.UpdateSnapshot(snapshot);

            Assert.Equal(340.0, window.Width);
            Assert.Equal("10.0 MB/s", window.HeaderDownloadText.Text);
            Assert.Equal("1.0 MB/s", window.HeaderUploadText.Text);
            Assert.Equal(Visibility.Collapsed, window.StatusRow.Visibility);
            Assert.Equal(Visibility.Visible, window.RankingScrollViewer.Visibility);

            var items = window.RankingItemsControl.ItemsSource as IEnumerable<RankingRowItemModel>;
            Assert.NotNull(items);
            var list = items.ToList();
            Assert.Single(list);
            Assert.Equal("Windows Explorer", list[0].DisplayName);
            Assert.Equal("5.0 MB/s", list[0].DownloadRateText);
            Assert.Equal("512.0 KB/s", list[0].UploadRateText);
            Assert.Same(AppIconCache.FallbackIcon, list[0].Icon);

            window.Close();
        });
    }

    [Fact]
    public void MonitorPanelWindow_RendersLocalizedStatus_WhenOfflineOrWarmupOrIdle() {
        WpfTestRunner.Run(() => {
            var window = new MonitorPanelWindow();

            // Offline
            var offline = new TelemetrySnapshot(
                Seq: 1,
                MonotonicMs: 0,
                DownloadBytesPerSec: 0,
                UploadBytesPerSec: 0,
                LastUpdated: DateTimeOffset.UtcNow,
                ConnectionState: CollectorConnectionState.Disconnected,
                CollectorInstanceId: null,
                CollectorPid: 0,
                LatestRanking: null,
                LatestStatus: null
            );
            window.UpdateSnapshot(offline);
            Assert.Equal(Visibility.Visible, window.StatusRow.Visibility);
            Assert.Equal(Visibility.Collapsed, window.RankingScrollViewer.Visibility);
            Assert.Equal(LocalizationService.CollectorOffline, window.StatusRowText.Text);

            // Warmup
            var warmup = new TelemetrySnapshot(
                Seq: 2,
                MonotonicMs: 100,
                DownloadBytesPerSec: 0,
                UploadBytesPerSec: 0,
                LastUpdated: DateTimeOffset.UtcNow,
                ConnectionState: CollectorConnectionState.Connected,
                CollectorInstanceId: "inst1",
                CollectorPid: 1234,
                LatestRanking: new RankingSnapshotMessage(2, 100, "WarmingUp", new TotalRate(0, 0), []),
                LatestStatus: null
            );
            window.UpdateSnapshot(warmup);
            Assert.Equal(Visibility.Visible, window.StatusRow.Visibility);
            Assert.Equal(Visibility.Collapsed, window.RankingScrollViewer.Visibility);
            Assert.Equal(LocalizationService.CollectingData, window.StatusRowText.Text);

            // Idle
            var idle = new TelemetrySnapshot(
                Seq: 3,
                MonotonicMs: 200,
                DownloadBytesPerSec: 0,
                UploadBytesPerSec: 0,
                LastUpdated: DateTimeOffset.UtcNow,
                ConnectionState: CollectorConnectionState.Connected,
                CollectorInstanceId: "inst1",
                CollectorPid: 1234,
                LatestRanking: new RankingSnapshotMessage(3, 200, "Ready", new TotalRate(0, 0), []),
                LatestStatus: null
            );
            window.UpdateSnapshot(idle);
            Assert.Equal(Visibility.Visible, window.StatusRow.Visibility);
            Assert.Equal(Visibility.Collapsed, window.RankingScrollViewer.Visibility);
            Assert.Equal(LocalizationService.NoNetworkActivity, window.StatusRowText.Text);

            window.Close();
        });
    }

    [Fact]
    public void MonitorPanelWindow_AnimatesRowReordering_AndEntrantFades() {
        WpfTestRunner.Run(() => {
            var window = new MonitorPanelWindow();

            // Snapshot 1: App A (idx 0), App B (idx 1)
            var snap1 = new TelemetrySnapshot(
                Seq: 1,
                MonotonicMs: 1000,
                DownloadBytesPerSec: 1000,
                UploadBytesPerSec: 1000,
                LastUpdated: DateTimeOffset.UtcNow,
                ConnectionState: CollectorConnectionState.Connected,
                CollectorInstanceId: "inst1",
                CollectorPid: 1234,
                LatestRanking: new RankingSnapshotMessage(
                    1, 1000, "Ready", new TotalRate(1000, 1000),
                    [
                        new RankingApplication("appA", "pathA", "App A", "", 800, 800),
                        new RankingApplication("appB", "pathB", "App B", "", 200, 200)
                    ]
                ),
                LatestStatus: null
            );
            window.UpdateSnapshot(snap1, reducedMotion: true);

            var items1 = (window.RankingItemsControl.ItemsSource as IEnumerable<RankingRowItemModel>)!.ToList();
            Assert.Equal("appA", items1[0].Id);
            Assert.Equal("appB", items1[1].Id);

            // Snapshot 2: App B (now idx 0) and App A (now idx 1) -> Swap! Plus App C enters!
            var snap2 = new TelemetrySnapshot(
                Seq: 2,
                MonotonicMs: 2000,
                DownloadBytesPerSec: 2000,
                UploadBytesPerSec: 2000,
                LastUpdated: DateTimeOffset.UtcNow,
                ConnectionState: CollectorConnectionState.Connected,
                CollectorInstanceId: "inst1",
                CollectorPid: 1234,
                LatestRanking: new RankingSnapshotMessage(
                    2, 2000, "Ready", new TotalRate(2000, 2000),
                    [
                        new RankingApplication("appB", "pathB", "App B", "", 1200, 1200),
                        new RankingApplication("appA", "pathA", "App A", "", 500, 500),
                        new RankingApplication("appC", "pathC", "App C", "", 300, 300)
                    ]
                ),
                LatestStatus: null
            );
            // Update without reduced motion to start row transitions
            window.UpdateSnapshot(snap2, reducedMotion: false);

            var items2 = (window.RankingItemsControl.ItemsSource as IEnumerable<RankingRowItemModel>)!.ToList();
            Assert.Equal(3, items2.Count);
            Assert.Equal("appB", items2[0].Id);
            Assert.Equal("appA", items2[1].Id);
            Assert.Equal("appC", items2[2].Id);

            // App B was at idx 1, now at idx 0 -> DeltaY was (1 - 0) * 28 = +28.0
            Assert.Equal(28.0, items2[0].TranslateY, precision: 1);
            // App A was at idx 0, now at idx 1 -> DeltaY was (0 - 1) * 28 = -28.0
            Assert.Equal(-28.0, items2[1].TranslateY, precision: 1);
            // App C is new entrant -> initial TranslateY = 14.0, Opacity = 0.0
            Assert.Equal(14.0, items2[2].TranslateY, precision: 1);
            Assert.Equal(0.0, items2[2].Opacity, precision: 1);

            window.Close();
        });
    }

    [Fact]
    public void MonitorPanelWindow_TopN_FiltersApplicationsToConfiguredLimit() {
        WpfTestRunner.Run(() => {
            var window = new MonitorPanelWindow();

            // Build a snapshot with 10 applications
            var apps = Enumerable.Range(1, 10).Select(i =>
                new RankingApplication(
                    Id: $"app{i}",
                    ExecutablePath: $@"C:\fake\app{i}.exe",
                    DisplayName: $"Application {i}",
                    IconKey: "",
                    DownloadBytesPerSec: (ulong)(10000 - (i * 100)),
                    UploadBytesPerSec: (ulong)(5000 - (i * 50))
                )).ToArray();

            var snapshot = new TelemetrySnapshot(
                Seq: 100,
                MonotonicMs: 12345,
                DownloadBytesPerSec: 50000,
                UploadBytesPerSec: 25000,
                LastUpdated: DateTimeOffset.UtcNow,
                ConnectionState: CollectorConnectionState.Connected,
                CollectorInstanceId: "inst1",
                CollectorPid: 1234,
                LatestRanking: new RankingSnapshotMessage(
                    Seq: 100,
                    MonotonicMs: 12345,
                    Status: "Ready",
                    TotalRate: new TotalRate(50000, 25000),
                    Applications: apps
                ),
                LatestStatus: null
            );

            // Act: UpdateSnapshot with topN = 3
            window.UpdateSnapshot(snapshot, reducedMotion: true, topN: 3);

            // Assert: Only 3 rows rendered, not 10
            var items = (window.RankingItemsControl.ItemsSource as IEnumerable<RankingRowItemModel>)!.ToList();
            Assert.Equal(3, items.Count);
            Assert.Equal("app1", items[0].Id);
            Assert.Equal("app2", items[1].Id);
            Assert.Equal("app3", items[2].Id);

            window.Close();
        });
    }
}
