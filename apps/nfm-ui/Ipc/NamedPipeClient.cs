using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Security.Principal;
using System.Threading;
using System.Threading.Tasks;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Ipc;

public class NamedPipeClient : IAsyncDisposable
{
    private readonly string _pipeName;
    private readonly TelemetryStore _store;
    private readonly CancellationTokenSource _cts = new();
    private Task? _workerTask;
    private NamedPipeClientStream? _pipeStream;

    public NamedPipeClient(TelemetryStore store, string? pipeName = null)
    {
        _store = store ?? throw new ArgumentNullException(nameof(store));
        _pipeName = pipeName ?? GetDefaultPipeName();
    }

    public static string GetDefaultPipeName()
    {
        var sid = WindowsIdentity.GetCurrent().User?.Value ?? "S-1-5-18";
        return $"NetworkFlowMonitor.portable.{sid}";
    }

    public void Start()
    {
        if (_workerTask != null) return;
        _workerTask = Task.Run(() => WorkerLoopAsync(_cts.Token));
    }

    public async Task StopAsync()
    {
        _cts.Cancel();
        if (_pipeStream != null && _pipeStream.IsConnected)
        {
            try
            {
                var shutdownBytes = ProtocolV1Codec.EncodeMessage(MessageType.ShutdownRequest, new ShutdownRequestMessage("ui_exit"));
                await _pipeStream.WriteAsync(shutdownBytes).ConfigureAwait(false);
                await _pipeStream.FlushAsync().ConfigureAwait(false);
            }
            catch
            {
                // Ignored during shutdown
            }
        }

        if (_workerTask != null)
        {
            try
            {
                await _workerTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // Expected
            }
        }

        _store.UpdateConnection(CollectorConnectionState.Disconnected);
    }

    private async Task WorkerLoopAsync(CancellationToken ct)
    {
        var retryDelay = TimeSpan.FromMilliseconds(250);
        var maxRetryDelay = TimeSpan.FromSeconds(5);

        while (!ct.IsCancellationRequested)
        {
            try
            {
                _store.UpdateConnection(CollectorConnectionState.Connecting);

                var pipe = new NamedPipeClientStream(
                    ".",
                    _pipeName,
                    PipeDirection.InOut,
                    PipeOptions.Asynchronous | PipeOptions.WriteThrough);

                _pipeStream = pipe;

                await pipe.ConnectAsync(3000, ct).ConfigureAwait(false);

                // Handshake: Send Hello
                var hello = new HelloMessage(
                    ClientPid: Process.GetCurrentProcess().Id,
                    ProtocolVersion: ProtocolConstants.CurrentProtocolVersion,
                    ClientName: "NetworkFlowMonitor.Ui"
                );
                var helloBytes = ProtocolV1Codec.EncodeMessage(MessageType.Hello, hello);
                await pipe.WriteAsync(helloBytes, ct).ConfigureAwait(false);
                await pipe.FlushAsync(ct).ConfigureAwait(false);

                // Read Loop
                var headerBuffer = new byte[ProtocolConstants.HeaderSize];
                var payloadBuffer = new byte[ProtocolConstants.MaxPayloadLength];

                retryDelay = TimeSpan.FromMilliseconds(250); // Reset on successful connect

                while (!ct.IsCancellationRequested && pipe.IsConnected)
                {
                    await pipe.ReadExactlyAsync(headerBuffer.AsMemory(0, ProtocolConstants.HeaderSize), ct).ConfigureAwait(false);

                    if (!ProtocolV1Codec.TryDecodeHeader(headerBuffer, out var header, out var errorCode) || errorCode != ProtocolErrorCode.None)
                    {
                        break;
                    }

                    if (header.PayloadLength > ProtocolConstants.MaxPayloadLength)
                    {
                        break;
                    }

                    var payloadMemory = payloadBuffer.AsMemory(0, (int)header.PayloadLength);
                    if (header.PayloadLength > 0)
                    {
                        await pipe.ReadExactlyAsync(payloadMemory, ct).ConfigureAwait(false);
                    }

                    switch (header.MessageType)
                    {
                        case MessageType.HelloAck:
                            var ack = ProtocolV1Codec.DeserializePayload<HelloAckMessage>(payloadMemory.Span);
                            if (ack != null)
                            {
                                _store.UpdateConnection(CollectorConnectionState.Connected, ack.CollectorInstanceId, ack.CollectorPid);
                            }
                            break;

                        case MessageType.TrafficTick:
                            var tick = ProtocolV1Codec.DeserializePayload<TrafficTickMessage>(payloadMemory.Span);
                            if (tick != null)
                            {
                                _store.UpdateTrafficTick(tick);
                            }
                            break;

                        case MessageType.RankingSnapshot:
                            var ranking = ProtocolV1Codec.DeserializePayload<RankingSnapshotMessage>(payloadMemory.Span);
                            if (ranking != null)
                            {
                                _store.UpdateRanking(ranking);
                            }
                            break;

                        case MessageType.CollectorStatus:
                            var status = ProtocolV1Codec.DeserializePayload<CollectorStatusMessage>(payloadMemory.Span);
                            if (status != null)
                            {
                                _store.UpdateStatus(status);
                            }
                            break;
                    }
                }
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                break;
            }
            catch
            {
                _store.UpdateConnection(CollectorConnectionState.Offline);
            }
            finally
            {
                if (_pipeStream != null)
                {
                    try { await _pipeStream.DisposeAsync().ConfigureAwait(false); } catch { }
                    _pipeStream = null;
                }
            }

            if (!ct.IsCancellationRequested)
            {
                try
                {
                    await Task.Delay(retryDelay, ct).ConfigureAwait(false);
                    retryDelay = TimeSpan.FromMilliseconds(Math.Min(retryDelay.TotalMilliseconds * 2, maxRetryDelay.TotalMilliseconds));
                }
                catch (OperationCanceledException)
                {
                    break;
                }
            }
        }

        _store.UpdateConnection(CollectorConnectionState.Disconnected);
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
        _cts.Dispose();
        GC.SuppressFinalize(this);
    }
}
