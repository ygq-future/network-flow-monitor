using System;
using System.Linq;
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

public enum TelemetryDataState
{
    Disconnected,
    Warmup,
    Idle,
    Active,
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
    public TelemetryDataState DataState => CalculateDataState();

    private TelemetryDataState CalculateDataState()
    {
        if (ConnectionState != CollectorConnectionState.Connected)
        {
            return TelemetryDataState.Disconnected;
        }

        if (LatestRanking == null || string.Equals(LatestRanking.Status, "WarmingUp", StringComparison.OrdinalIgnoreCase))
        {
            return TelemetryDataState.Warmup;
        }

        if (DownloadBytesPerSec == 0 && UploadBytesPerSec == 0 &&
            (LatestRanking.Applications == null || LatestRanking.Applications.Length == 0 ||
             LatestRanking.Applications.All(a => a.DownloadBytesPerSec == 0 && a.UploadBytesPerSec == 0)))
        {
            return TelemetryDataState.Idle;
        }

        return TelemetryDataState.Active;
    }

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

    private static TelemetrySnapshot ResetTelemetrySnapshot(
        TelemetrySnapshot prev,
        CollectorConnectionState connectionState,
        string? instanceId,
        int pid)
    {
        return prev with
        {
            ConnectionState = connectionState,
            CollectorInstanceId = instanceId,
            CollectorPid = pid,
            DownloadBytesPerSec = 0,
            UploadBytesPerSec = 0,
            Seq = 0,
            MonotonicMs = 0,
            LatestRanking = null,
            LatestStatus = null,
            LastUpdated = DateTimeOffset.UtcNow,
        };
    }

    public void UpdateConnection(CollectorConnectionState connectionState, string? instanceId = null, int pid = 0)
    {
        TelemetrySnapshot prev, next;
        do
        {
            prev = _current;

            if (connectionState == CollectorConnectionState.Disconnected || connectionState == CollectorConnectionState.Offline)
            {
                var retainedInstance = connectionState == CollectorConnectionState.Offline ? (instanceId ?? prev.CollectorInstanceId) : null;
                var retainedPid = connectionState == CollectorConnectionState.Offline ? (pid != 0 ? pid : prev.CollectorPid) : 0;
                next = ResetTelemetrySnapshot(prev, connectionState, retainedInstance, retainedPid);
            }
            else if (connectionState == CollectorConnectionState.Connected)
            {
                bool isNewInstance = instanceId != null && prev.CollectorInstanceId != null &&
                                     !string.Equals(instanceId, prev.CollectorInstanceId, StringComparison.Ordinal);

                if (isNewInstance)
                {
                    next = ResetTelemetrySnapshot(prev, CollectorConnectionState.Connected, instanceId, pid != 0 ? pid : prev.CollectorPid);
                }
                else
                {
                    next = prev with
                    {
                        ConnectionState = CollectorConnectionState.Connected,
                        CollectorInstanceId = instanceId ?? prev.CollectorInstanceId,
                        CollectorPid = pid != 0 ? pid : prev.CollectorPid,
                        LastUpdated = DateTimeOffset.UtcNow,
                    };
                }
            }
            else
            {
                next = prev with
                {
                    ConnectionState = connectionState,
                    LastUpdated = DateTimeOffset.UtcNow,
                };
            }
        } while (Interlocked.CompareExchange(ref _current, next, prev) != prev);

        SnapshotUpdated?.Invoke(next);
    }

    public void UpdateTrafficTick(TrafficTickMessage tick)
    {
        TelemetrySnapshot prev, next;
        do
        {
            prev = _current;

            // Discard out-of-order or duplicate ticks for the current instance
            if (prev.Seq > 0 && tick.Seq <= prev.Seq)
            {
                return;
            }

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

            // Discard out-of-order or duplicate ranking snapshots
            if (prev.LatestRanking != null && ranking.Seq <= prev.LatestRanking.Seq)
            {
                return;
            }

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
