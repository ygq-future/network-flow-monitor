using System;
using System.IO;
using System.IO.Pipes;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace NetworkFlowMonitor.Services;

public static class SingleInstanceChannel {
    public const string ActivationCommandShowMonitor = "SHOW_MONITOR";
    public const string ActivationCommandShowSettings = "SHOW_SETTINGS";
    public const string ActivationCommandTogglePin = "TOGGLE_PIN";
    public const string ActivationCommandExit = "EXIT";

    public static string GetUserSidOrFallback() {
        try {
            if (OperatingSystem.IsWindows()) {
                var sid = WindowsIdentity.GetCurrent().User?.Value;
                if (!string.IsNullOrWhiteSpace(sid)) {
                    return sid;
                }
            }
        } catch { }

        return Environment.UserName.Replace('\\', '_');
    }

    public static string GetDefaultChannelName() {
        var userSid = GetUserSidOrFallback();
        return $"NetworkFlowMonitor_Activation_{userSid}";
    }

    public static string GetDefaultMutexName() {
        var userSid = GetUserSidOrFallback();
        return $@"Local\NetworkFlowMonitor_SingleInstance_{userSid}";
    }
}

public sealed class SingleInstanceServer : IDisposable, IAsyncDisposable {
    private readonly string _pipeName;
    private readonly Action<string> _onCommandReceived;
    private readonly CancellationTokenSource _cts = new();
    private Task? _serverTask;
    private NamedPipeServerStream? _activePipe;
    private bool _disposed;

    public SingleInstanceServer(string pipeName, Action<string> onCommandReceived) {
        _pipeName = pipeName ?? throw new ArgumentNullException(nameof(pipeName));
        _onCommandReceived = onCommandReceived ?? throw new ArgumentNullException(nameof(onCommandReceived));
    }

    public SingleInstanceServer(Action<string> onCommandReceived)
        : this(SingleInstanceChannel.GetDefaultChannelName(), onCommandReceived) {
    }

    public void Start() {
        if (_serverTask != null) {
            return;
        }

        _serverTask = Task.Run(() => ServerLoopAsync(_cts.Token));
    }

    public async Task StopAsync() {
        try {
            _cts.Cancel();
        } catch { }

        try {
            _activePipe?.Dispose();
        } catch { }

        if (_serverTask != null) {
            try {
                await _serverTask.ConfigureAwait(false);
            } catch (OperationCanceledException) {
                // Expected during cancellation
            } catch {
                // Ignore during shutdown
            }
        }
    }

    private async Task ServerLoopAsync(CancellationToken ct) {
        var buffer = new byte[512];
        var ackBytes = Encoding.UTF8.GetBytes("ACK\n");

        while (!ct.IsCancellationRequested) {
            NamedPipeServerStream? pipe = null;
            try {
                pipe = new NamedPipeServerStream(
                    _pipeName,
                    PipeDirection.InOut,
                    NamedPipeServerStream.MaxAllowedServerInstances,
                    PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous
                );
                _activePipe = pipe;

                await pipe.WaitForConnectionAsync(ct).ConfigureAwait(false);

                var bytesRead = await pipe.ReadAsync(buffer.AsMemory(0, buffer.Length), ct).ConfigureAwait(false);
                if (bytesRead > 0) {
                    var command = Encoding.UTF8.GetString(buffer, 0, bytesRead).Trim('\r', '\n', ' ');
                    if (!string.IsNullOrEmpty(command)) {
                        try {
                            _onCommandReceived(command);
                        } catch {
                            // Protect the server loop from unhandled subscriber exceptions
                        }

                        await pipe.WriteAsync(ackBytes.AsMemory(0, ackBytes.Length), ct).ConfigureAwait(false);
                        await pipe.FlushAsync(ct).ConfigureAwait(false);
                    }
                }
            } catch (OperationCanceledException) when (ct.IsCancellationRequested) {
                break;
            } catch (ObjectDisposedException) when (ct.IsCancellationRequested) {
                break;
            } catch {
                if (ct.IsCancellationRequested) {
                    break;
                }
                // Ignore transient pipe/connection errors and keep listening
            } finally {
                _activePipe = null;
                if (pipe != null) {
                    try {
                        if (pipe.IsConnected) {
                            pipe.Disconnect();
                        }
                    } catch { }

                    try {
                        await pipe.DisposeAsync().ConfigureAwait(false);
                    } catch { }
                }
            }
        }
    }

    public void Dispose() {
        if (_disposed) {
            return;
        }

        _disposed = true;

        try {
            _cts.Cancel();
        } catch { }

        try {
            _activePipe?.Dispose();
        } catch { }

        try {
            _cts.Dispose();
        } catch { }

        GC.SuppressFinalize(this);
    }

    public async ValueTask DisposeAsync() {
        if (_disposed) {
            return;
        }

        _disposed = true;

        await StopAsync().ConfigureAwait(false);

        try {
            _cts.Dispose();
        } catch { }

        GC.SuppressFinalize(this);
    }
}

public static class SingleInstanceClient {
    public static async Task<bool> SignalExistingInstanceAsync(string pipeName, string command, int timeoutMs = 1500) {
        if (string.IsNullOrWhiteSpace(pipeName) || string.IsNullOrWhiteSpace(command)) {
            return false;
        }

        try {
            using var cts = new CancellationTokenSource(timeoutMs);
            using var pipe = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous);

            await pipe.ConnectAsync(timeoutMs, cts.Token).ConfigureAwait(false);

            var sendBytes = Encoding.UTF8.GetBytes(command.Trim() + "\n");
            await pipe.WriteAsync(sendBytes.AsMemory(0, sendBytes.Length), cts.Token).ConfigureAwait(false);
            await pipe.FlushAsync(cts.Token).ConfigureAwait(false);

            var buffer = new byte[64];
            var bytesRead = await pipe.ReadAsync(buffer.AsMemory(0, buffer.Length), cts.Token).ConfigureAwait(false);
            if (bytesRead <= 0) {
                return false;
            }

            var response = Encoding.UTF8.GetString(buffer, 0, bytesRead).Trim('\r', '\n', ' ');
            return string.Equals(response, "ACK", StringComparison.Ordinal);
        } catch {
            return false;
        }
    }

    public static Task<bool> SignalExistingInstanceAsync(string command, int timeoutMs = 1500) {
        return SignalExistingInstanceAsync(SingleInstanceChannel.GetDefaultChannelName(), command, timeoutMs);
    }
}
