using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Services;

public class CollectorProcessManager : IDisposable {
    private readonly TelemetryStore? _store;
    private readonly NamedPipeClient _client;
    private readonly Func<bool>? _launchSeam;
    private readonly Func<Task>? _stopSeam;
    private readonly Action? _startSeam;

    private readonly string _stopEventName;
    private readonly EventWaitHandle? _stopEvent;
    private bool _isDisposed;

    public CollectorProcessManager(NamedPipeClient client, TelemetryStore? store = null) {
        _client = client ?? throw new ArgumentNullException(nameof(client));
        _store = store;

        _stopEventName = $"Local\\NetworkFlowMonitor.Stop.{Guid.NewGuid():N}";
        try {
            _stopEvent = new EventWaitHandle(false, EventResetMode.ManualReset, _stopEventName);
        } catch {
            // Ignore event creation failure on unsupported platforms
        }
    }

    public CollectorProcessManager(TelemetryStore store, NamedPipeClient client)
        : this(client, store) {
    }

    internal CollectorProcessManager(
        NamedPipeClient client,
        TelemetryStore? store,
        Func<bool>? launchSeam,
        Func<Task>? stopSeam,
        Action? startSeam) {
        _client = client;
        _store = store;
        _launchSeam = launchSeam;
        _stopSeam = stopSeam;
        _startSeam = startSeam;
        _stopEventName = $"Local\\NetworkFlowMonitor.Stop.{Guid.NewGuid():N}";
    }

    public string StopEventName => _stopEventName;

    public static string ResolveCollectorExecutablePath(string? baseDirectory = null) {
        var baseDir = baseDirectory ?? AppContext.BaseDirectory;
        var collectorCandidate = Path.Combine(baseDir, "nfm-collector.exe");
        if (File.Exists(collectorCandidate)) {
            return collectorCandidate;
        }

        var appCandidate = Path.Combine(baseDir, "NetworkFlowMonitor.exe");
        return appCandidate;
    }

    public bool TryLaunchCollector(string? customExecutablePath = null) {
        if (_launchSeam != null) {
            return _launchSeam();
        }

        try {
            var exePath = customExecutablePath ?? ResolveCollectorExecutablePath();
            if (!File.Exists(exePath)) {
                return false;
            }

            var startInfo = new ProcessStartInfo {
                FileName = exePath,
                Arguments = $"--collector --stop-event {_stopEventName} --parent-pid {Environment.ProcessId}",
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            };

            var proc = Process.Start(startInfo);
            return proc != null;
        } catch {
            return false;
        }
    }

    public async Task RetryCollectorAsync() {
        if (_store != null && _store.CurrentSnapshot.ConnectionState == CollectorConnectionState.Connected) {
            return;
        }

        TryLaunchCollector();
        if (_startSeam != null) {
            _startSeam();
        } else {
            _client.Start();
        }
        await Task.CompletedTask;
    }

    public async Task RestartCollectorAsync(int delayMs = 500) {
        try {
            if (_stopSeam != null) {
                await _stopSeam().ConfigureAwait(false);
            } else {
                await _client.StopAsync().ConfigureAwait(false);
            }

            if (delayMs > 0) {
                await Task.Delay(delayMs).ConfigureAwait(false);
            }

            TryLaunchCollector();

            if (_startSeam != null) {
                _startSeam();
            } else {
                _client.Start();
            }
        } catch {
            // Best effort restart
        }
    }

    public void Dispose() {
        if (!_isDisposed) {
            _isDisposed = true;
            try {
                _stopEvent?.Set();
                _stopEvent?.Dispose();
            } catch { }
        }
        GC.SuppressFinalize(this);
    }
}
