using System;
using System.Linq;
using System.Threading.Tasks;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Store;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class TelemetryStoreTests
{
    [Fact]
    public void InitialState_IsDisconnectedAndZero()
    {
        var store = new TelemetryStore();
        var snapshot = store.CurrentSnapshot;

        Assert.Equal(CollectorConnectionState.Disconnected, snapshot.ConnectionState);
        Assert.Equal(TelemetryDataState.Disconnected, snapshot.DataState);
        Assert.Equal(0UL, snapshot.Seq);
        Assert.Equal(0UL, snapshot.DownloadBytesPerSec);
        Assert.Equal(0UL, snapshot.UploadBytesPerSec);
        Assert.Null(snapshot.CollectorInstanceId);
        Assert.Null(snapshot.LatestRanking);
        Assert.Null(snapshot.LatestStatus);
    }

    [Fact]
    public void UpdateConnection_UpdatesStateAtomically()
    {
        var store = new TelemetryStore();
        bool eventFired = false;
        store.SnapshotUpdated += snap =>
        {
            if (snap.ConnectionState == CollectorConnectionState.Connected)
            {
                eventFired = true;
            }
        };

        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_abc123", 4588);
        var snapshot = store.CurrentSnapshot;

        Assert.True(eventFired);
        Assert.Equal(CollectorConnectionState.Connected, snapshot.ConnectionState);
        Assert.Equal("cinst_abc123", snapshot.CollectorInstanceId);
        Assert.Equal(4588, snapshot.CollectorPid);
        Assert.Equal(TelemetryDataState.Warmup, snapshot.DataState);
    }

    [Fact]
    public void UpdateTrafficTick_UpdatesRatesAndNotifies()
    {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_1", 1000);

        var tick = new TrafficTickMessage(
            Seq: 100,
            MonotonicMs: 50000,
            DownloadBytesPerSec: 5242880,
            UploadBytesPerSec: 1048576
        );

        TelemetrySnapshot? updated = null;
        store.SnapshotUpdated += snap => updated = snap;

        store.UpdateTrafficTick(tick);

        Assert.NotNull(updated);
        Assert.Equal(100UL, updated.Seq);
        Assert.Equal(50000UL, updated.MonotonicMs);
        Assert.Equal(5242880UL, updated.DownloadBytesPerSec);
        Assert.Equal(1048576UL, updated.UploadBytesPerSec);
        Assert.Equal(CollectorConnectionState.Connected, updated.ConnectionState);
    }

    [Fact]
    public void UpdateTrafficTick_DiscardsOutOfOrderTicks()
    {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_1", 1000);

        store.UpdateTrafficTick(new TrafficTickMessage(10, 1000, 1000, 500));
        Assert.Equal(10UL, store.CurrentSnapshot.Seq);
        Assert.Equal(1000UL, store.CurrentSnapshot.DownloadBytesPerSec);

        // Stale tick with lower seq should be discarded
        store.UpdateTrafficTick(new TrafficTickMessage(9, 900, 500, 250));
        Assert.Equal(10UL, store.CurrentSnapshot.Seq);
        Assert.Equal(1000UL, store.CurrentSnapshot.DownloadBytesPerSec);

        // Equal seq should also be discarded
        store.UpdateTrafficTick(new TrafficTickMessage(10, 1000, 800, 400));
        Assert.Equal(1000UL, store.CurrentSnapshot.DownloadBytesPerSec);

        // Newer tick accepted
        store.UpdateTrafficTick(new TrafficTickMessage(11, 1100, 2000, 1000));
        Assert.Equal(11UL, store.CurrentSnapshot.Seq);
        Assert.Equal(2000UL, store.CurrentSnapshot.DownloadBytesPerSec);
    }

    [Fact]
    public void UpdateRanking_AtomicallyReplacesLatestRanking_AndDiscardsOutOfOrder()
    {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_1", 1000);

        var ranking1 = new RankingSnapshotMessage(
            Seq: 5,
            MonotonicMs: 5000,
            Status: "Ready",
            TotalRate: new TotalRate(1000, 500),
            Applications:
            [
                new RankingApplication("app1", "C:\\app1.exe", "App 1", "icon1", 1000, 500)
            ]
        );

        store.UpdateRanking(ranking1);
        Assert.NotNull(store.CurrentSnapshot.LatestRanking);
        Assert.Equal(5UL, store.CurrentSnapshot.LatestRanking.Seq);
        Assert.Single(store.CurrentSnapshot.LatestRanking.Applications);
        Assert.Equal("app1", store.CurrentSnapshot.LatestRanking.Applications[0].Id);

        // Stale ranking (Seq 4) should be discarded
        var staleRanking = new RankingSnapshotMessage(
            Seq: 4,
            MonotonicMs: 4000,
            Status: "Ready",
            TotalRate: new TotalRate(800, 400),
            Applications: []
        );
        store.UpdateRanking(staleRanking);
        Assert.Equal(5UL, store.CurrentSnapshot.LatestRanking.Seq);
        Assert.Single(store.CurrentSnapshot.LatestRanking.Applications);

        // Equal ranking (Seq 5) should be discarded
        var duplicateRanking = new RankingSnapshotMessage(
            Seq: 5,
            MonotonicMs: 5000,
            Status: "Ready",
            TotalRate: new TotalRate(900, 450),
            Applications: []
        );
        store.UpdateRanking(duplicateRanking);
        Assert.Equal(5UL, store.CurrentSnapshot.LatestRanking.Seq);
        Assert.Single(store.CurrentSnapshot.LatestRanking.Applications);

        // Newer ranking (Seq 6) should be accepted
        var ranking2 = new RankingSnapshotMessage(
            Seq: 6,
            MonotonicMs: 6000,
            Status: "Ready",
            TotalRate: new TotalRate(2000, 1000),
            Applications:
            [
                new RankingApplication("app2", "C:\\app2.exe", "App 2", "icon2", 2000, 1000)
            ]
        );
        store.UpdateRanking(ranking2);
        Assert.Equal(6UL, store.CurrentSnapshot.LatestRanking.Seq);
        Assert.Equal("app2", store.CurrentSnapshot.LatestRanking.Applications[0].Id);
    }

    [Fact]
    public void UpdateConnection_OnDisconnect_PurgesStaleDataAndSetsDisconnectedState()
    {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_1", 1000);
        store.UpdateTrafficTick(new TrafficTickMessage(10, 1000, 5000, 2000));
        store.UpdateRanking(new RankingSnapshotMessage(5, 5000, "Ready", new TotalRate(5000, 2000), [
            new RankingApplication("app1", "C:\\app1.exe", "App 1", "icon1", 5000, 2000)
        ]));
        store.UpdateStatus(new CollectorStatusMessage(5000, true, 0, 10));

        Assert.Equal(CollectorConnectionState.Connected, store.CurrentSnapshot.ConnectionState);
        Assert.Equal(5000UL, store.CurrentSnapshot.DownloadBytesPerSec);
        Assert.NotNull(store.CurrentSnapshot.LatestRanking);
        Assert.NotNull(store.CurrentSnapshot.LatestStatus);

        // Disconnect
        store.UpdateConnection(CollectorConnectionState.Disconnected);

        var snap = store.CurrentSnapshot;
        Assert.Equal(CollectorConnectionState.Disconnected, snap.ConnectionState);
        Assert.Equal(TelemetryDataState.Disconnected, snap.DataState);
        Assert.Equal(0UL, snap.DownloadBytesPerSec);
        Assert.Equal(0UL, snap.UploadBytesPerSec);
        Assert.Null(snap.LatestRanking);
        Assert.Null(snap.LatestStatus);
        Assert.Equal(0UL, snap.Seq);
    }

    [Fact]
    public void UpdateConnection_OnNewInstanceId_ResetsSequenceAndPurgesStaleRanking()
    {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_1", 1000);
        store.UpdateTrafficTick(new TrafficTickMessage(100, 10000, 5000, 2000));
        store.UpdateRanking(new RankingSnapshotMessage(50, 10000, "Ready", new TotalRate(5000, 2000), [
            new RankingApplication("app1", "C:\\app1.exe", "App 1", "icon1", 5000, 2000)
        ]));

        Assert.Equal("cinst_1", store.CurrentSnapshot.CollectorInstanceId);
        Assert.Equal(100UL, store.CurrentSnapshot.Seq);
        Assert.NotNull(store.CurrentSnapshot.LatestRanking);

        // Reconnect to a brand new collector instance
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_2", 2000);

        var snap = store.CurrentSnapshot;
        Assert.Equal("cinst_2", snap.CollectorInstanceId);
        Assert.Equal(2000, snap.CollectorPid);
        Assert.Equal(0UL, snap.Seq);
        Assert.Equal(0UL, snap.DownloadBytesPerSec);
        Assert.Equal(0UL, snap.UploadBytesPerSec);
        Assert.Null(snap.LatestRanking);
        Assert.Null(snap.LatestStatus);
        Assert.Equal(TelemetryDataState.Warmup, snap.DataState);

        // Can ingest new tick with lower seq (e.g. Seq 1) because sequence was reset
        store.UpdateTrafficTick(new TrafficTickMessage(1, 250, 1024, 512));
        Assert.Equal(1UL, store.CurrentSnapshot.Seq);
        Assert.Equal(1024UL, store.CurrentSnapshot.DownloadBytesPerSec);
    }

    [Fact]
    public void TelemetryDataState_ReflectsDisconnectedWarmupIdleAndActiveCorrectly()
    {
        var store = new TelemetryStore();
        Assert.Equal(TelemetryDataState.Disconnected, store.CurrentSnapshot.DataState);

        // Connected but no ranking yet => Warmup
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_1", 1000);
        Assert.Equal(TelemetryDataState.Warmup, store.CurrentSnapshot.DataState);

        // Ranking with WarmingUp status => Warmup
        store.UpdateRanking(new RankingSnapshotMessage(1, 1000, "WarmingUp", new TotalRate(0, 0), []));
        Assert.Equal(TelemetryDataState.Warmup, store.CurrentSnapshot.DataState);

        // Ranking with Ready but 0 rates and no apps => Idle
        store.UpdateRanking(new RankingSnapshotMessage(2, 2000, "Ready", new TotalRate(0, 0), []));
        store.UpdateTrafficTick(new TrafficTickMessage(1, 2000, 0, 0));
        Assert.Equal(TelemetryDataState.Idle, store.CurrentSnapshot.DataState);

        // Active traffic => Active
        store.UpdateTrafficTick(new TrafficTickMessage(2, 2250, 5000, 1000));
        store.UpdateRanking(new RankingSnapshotMessage(3, 3000, "Ready", new TotalRate(5000, 1000), [
            new RankingApplication("app1", "C:\\app1.exe", "App 1", "icon1", 5000, 1000)
        ]));
        Assert.Equal(TelemetryDataState.Active, store.CurrentSnapshot.DataState);

        // Disconnect => Disconnected
        store.UpdateConnection(CollectorConnectionState.Disconnected);
        Assert.Equal(TelemetryDataState.Disconnected, store.CurrentSnapshot.DataState);
    }

    [Fact]
    public void RateFormatter_FormatsCorrectly()
    {
        Assert.Equal("0 B/s", RateFormatter.FormatRate(0));
        Assert.Equal("500 B/s", RateFormatter.FormatRate(500));
        Assert.Equal("1023 B/s", RateFormatter.FormatRate(1023));
        Assert.Equal("1.0 KB/s", RateFormatter.FormatRate(1024));
        Assert.Equal("1.5 KB/s", RateFormatter.FormatRate(1536));
        Assert.Equal("1.0 MB/s", RateFormatter.FormatRate(1024 * 1024));
        Assert.Equal("2.5 MB/s", RateFormatter.FormatRate((ulong)(2.5 * 1024 * 1024)));
        Assert.Equal("1.00 GB/s", RateFormatter.FormatRate(1024UL * 1024 * 1024));
    }

    [Fact]
    public async Task ConcurrentUpdates_MaintainConsistency()
    {
        var store = new TelemetryStore();
        store.UpdateConnection(CollectorConnectionState.Connected, "cinst_1", 1000);
        const int iterations = 1000;

        var task1 = Task.Run(() =>
        {
            for (ulong i = 1; i <= iterations; i++)
            {
                store.UpdateTrafficTick(new TrafficTickMessage(i, i * 100, i * 1024, i * 512));
            }
        });

        var task2 = Task.Run(() =>
        {
            for (int i = 1; i <= iterations; i++)
            {
                store.UpdateStatus(new CollectorStatusMessage((ulong)i, true, 0, (uint)i));
            }
        });

        var task3 = Task.Run(() =>
        {
            for (ulong i = 1; i <= iterations; i++)
            {
                store.UpdateRanking(new RankingSnapshotMessage(i, i * 1000, "Ready", new TotalRate(i * 1024, i * 512), []));
            }
        });

        await Task.WhenAll(task1, task2, task3);

        var finalSnap = store.CurrentSnapshot;
        Assert.NotNull(finalSnap);
        Assert.True(finalSnap.Seq > 0);
        Assert.NotNull(finalSnap.LatestStatus);
        Assert.NotNull(finalSnap.LatestRanking);
    }
}
