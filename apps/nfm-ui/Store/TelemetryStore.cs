using System;
using System.Threading;
using NetworkFlowMonitor.Ipc;

namespace NetworkFlowMonitor.Store;

public enum CollectorConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Offline,
}

public sealed record TelemetrySnapshot(
    ulong Seq,
    ulong MonotonicMs,
    ulong DownloadBytesPerSec,
    ulong UploadBytesPerSec,
    DateTimeOffset LastUpdated,
    CollectorConnectionState ConnectionState,
    string? CollectorInstanceId,
    int CollectorPid,
    RankingSnapshotMessage? LatestRanking,
    CollectorStatusMessage? LatestStatus
)
{
    public static TelemetrySnapshot Initial => new(
        Seq: 0,
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
}

public class TelemetryStore
{
    private TelemetrySnapshot _current = TelemetrySnapshot.Initial;

    public TelemetrySnapshot CurrentSnapshot => Volatile.Read(ref _current);

    public event Action<TelemetrySnapshot>? SnapshotUpdated;

    public void UpdateConnection(CollectorConnectionState connectionState, string? instanceId = null, int pid = 0)
    {
        TelemetrySnapshot prev, next;
        do
        {
            prev = _current;
            next = prev with
            {
                ConnectionState = connectionState,
                CollectorInstanceId = instanceId ?? (connectionState == CollectorConnectionState.Connected ? prev.CollectorInstanceId : null),
                CollectorPid = pid != 0 ? pid : (connectionState == CollectorConnectionState.Connected ? prev.CollectorPid : 0),
                LastUpdated = DateTimeOffset.UtcNow,
            };
        } while (Interlocked.CompareExchange(ref _current, next, prev) != prev);

        SnapshotUpdated?.Invoke(next);
    }

    public void UpdateTrafficTick(TrafficTickMessage tick)
    {
        TelemetrySnapshot prev, next;
        do
        {
            prev = _current;
            next = prev with
            {
                Seq = tick.Seq,
                MonotonicMs = tick.MonotonicMs,
                DownloadBytesPerSec = tick.DownloadBytesPerSec,
                UploadBytesPerSec = tick.UploadBytesPerSec,
                LastUpdated = DateTimeOffset.UtcNow,
                ConnectionState = CollectorConnectionState.Connected,
            };
        } while (Interlocked.CompareExchange(ref _current, next, prev) != prev);

        SnapshotUpdated?.Invoke(next);
    }

    public void UpdateRanking(RankingSnapshotMessage ranking)
    {
        TelemetrySnapshot prev, next;
        do
        {
            prev = _current;
            next = prev with
            {
                LatestRanking = ranking,
                LastUpdated = DateTimeOffset.UtcNow,
                ConnectionState = CollectorConnectionState.Connected,
            };
        } while (Interlocked.CompareExchange(ref _current, next, prev) != prev);

        SnapshotUpdated?.Invoke(next);
    }

    public void UpdateStatus(CollectorStatusMessage status)
    {
        TelemetrySnapshot prev, next;
        do
        {
            prev = _current;
            next = prev with
            {
                LatestStatus = status,
                LastUpdated = DateTimeOffset.UtcNow,
            };
        } while (Interlocked.CompareExchange(ref _current, next, prev) != prev);

        SnapshotUpdated?.Invoke(next);
    }
}
