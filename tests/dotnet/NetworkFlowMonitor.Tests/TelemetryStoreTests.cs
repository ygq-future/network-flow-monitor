using System;
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
        Assert.Equal(0UL, snapshot.Seq);
        Assert.Equal(0UL, snapshot.DownloadBytesPerSec);
        Assert.Equal(0UL, snapshot.UploadBytesPerSec);
        Assert.Null(snapshot.CollectorInstanceId);
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
    }

    [Fact]
    public void UpdateTrafficTick_UpdatesRatesAndNotifies()
    {
        var store = new TelemetryStore();
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
    public async Task ConcurrentUpdates_MaintainConsistency()
    {
        var store = new TelemetryStore();
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

        await Task.WhenAll(task1, task2);

        var finalSnap = store.CurrentSnapshot;
        Assert.NotNull(finalSnap);
        Assert.True(finalSnap.Seq > 0);
        Assert.NotNull(finalSnap.LatestStatus);
    }
}
