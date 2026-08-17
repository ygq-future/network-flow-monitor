using System;
using System.IO.Pipes;
using System.Security.Principal;
using System.Threading;
using System.Threading.Tasks;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Store;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class HybridE2ETests {
    [Fact]
    public async Task HybridPipeline_EndToEnd_ConnectsHandshakesStreamsAndDisconnects() {
        var sid = WindowsIdentity.GetCurrent().User?.Value ?? "S-1-5-18";
        var pipeName = $"NetworkFlowMonitor.test.{Guid.NewGuid():N}";

        var store = new TelemetryStore();
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(10));

        // Start synthetic collector Named Pipe server
        var serverTask = Task.Run(async () => {
            var pipeServer = new NamedPipeServerStream(
                pipeName,
                PipeDirection.InOut,
                1,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous | PipeOptions.WriteThrough);

            await using (pipeServer.ConfigureAwait(false)) {
                await pipeServer.WaitForConnectionAsync(cts.Token).ConfigureAwait(false);

                // Step 1: Read Hello frame from Client
                var headerBuf = new byte[ProtocolConstants.HeaderSize];
                await pipeServer.ReadExactlyAsync(headerBuf, cts.Token).ConfigureAwait(false);

                var parseRes = ProtocolV1Codec.TryDecodeFrame(headerBuf, out var header, out _, out _, out var error);
                Assert.Equal(FrameParseStatus.NeedMoreData, parseRes);
                Assert.Equal(MessageType.Hello, header.MessageType);

                var payloadBuf = new byte[(int)header.PayloadLength];
                await pipeServer.ReadExactlyAsync(payloadBuf, cts.Token).ConfigureAwait(false);
                var hello = ProtocolV1Codec.DeserializePayload<HelloMessage>(payloadBuf);
                Assert.NotNull(hello);
                Assert.Equal(1, hello.ProtocolVersion);
                Assert.Equal("NetworkFlowMonitor.Ui", hello.ClientName);

                // Step 2: Send HelloAck
                var ack = new HelloAckMessage(
                    ProtocolVersion: 1,
                    CollectorInstanceId: "cinst_hybrid_tracer_01",
                    CollectorPid: 9876,
                    Capabilities: ["etw_live_counters", "traffic_tick_4hz"]
                );
                var ackBytes = ProtocolV1Codec.EncodeMessage(MessageType.HelloAck, ack);
                await pipeServer.WriteAsync(ackBytes, cts.Token).ConfigureAwait(false);
                await pipeServer.FlushAsync(cts.Token).ConfigureAwait(false);

                // Step 3: Stream 4 Hz ticks and 1 Hz ranking snapshot
                for (ulong seq = 1; seq <= 3; seq++) {
                    var tick = new TrafficTickMessage(
                        Seq: seq,
                        MonotonicMs: seq * 250,
                        DownloadBytesPerSec: seq * 5_242_880,
                        UploadBytesPerSec: seq * 1_048_576
                    );
                    var tickBytes = ProtocolV1Codec.EncodeMessage(MessageType.TrafficTick, tick);
                    await pipeServer.WriteAsync(tickBytes, cts.Token).ConfigureAwait(false);
                    await pipeServer.FlushAsync(cts.Token).ConfigureAwait(false);

                    if (seq == 2) {
                        var ranking = new RankingSnapshotMessage(
                            Seq: 1,
                            MonotonicMs: 500,
                            Status: "Ready",
                            TotalRate: new TotalRate(10_485_760, 2_097_152),
                            Applications:
                            [
                                new RankingApplication("app_chrome", "C:\\chrome.exe", "Google Chrome", "chrome_icon", 8_388_608, 1_048_576)
                            ]
                        );
                        var rankBytes = ProtocolV1Codec.EncodeMessage(MessageType.RankingSnapshot, ranking);
                        await pipeServer.WriteAsync(rankBytes, cts.Token).ConfigureAwait(false);
                        await pipeServer.FlushAsync(cts.Token).ConfigureAwait(false);

                        var status = new CollectorStatusMessage(500, true, 0, 15);
                        var statusBytes = ProtocolV1Codec.EncodeMessage(MessageType.CollectorStatus, status);
                        await pipeServer.WriteAsync(statusBytes, cts.Token).ConfigureAwait(false);
                        await pipeServer.FlushAsync(cts.Token).ConfigureAwait(false);
                    }

                    await Task.Delay(50, cts.Token).ConfigureAwait(false);
                }

                // Step 4: Wait for Client ShutdownRequest
                try {
                    await pipeServer.ReadExactlyAsync(headerBuf, cts.Token).ConfigureAwait(false);
                    ProtocolV1Codec.TryDecodeFrame(headerBuf, out var shutHeader, out _, out _, out _);
                    if (shutHeader.PayloadLength > 0) {
                        var shutPayload = new byte[(int)shutHeader.PayloadLength];
                        await pipeServer.ReadExactlyAsync(shutPayload, cts.Token).ConfigureAwait(false);
                    }
                } catch {
                    // Clean disconnect
                }
            }
        }, cts.Token);

        // Start C# Client
        var client = new NamedPipeClient(store, pipeName);
        client.Start();

        // Wait until store receives all 3 ticks
        var tcs = new TaskCompletionSource<TelemetrySnapshot>();
        store.SnapshotUpdated += snap => {
            if (snap.Seq >= 3 && snap.ConnectionState == CollectorConnectionState.Connected) {
                tcs.TrySetResult(snap);
            }
        };

        var completedTask = await Task.WhenAny(tcs.Task, Task.Delay(5000, cts.Token));
        Assert.Equal(tcs.Task, completedTask);

        var finalSnap = await tcs.Task;
        Assert.Equal(3UL, finalSnap.Seq);
        Assert.Equal("cinst_hybrid_tracer_01", finalSnap.CollectorInstanceId);
        Assert.Equal(9876, finalSnap.CollectorPid);
        Assert.Equal(3 * 5_242_880UL, finalSnap.DownloadBytesPerSec);
        Assert.Equal(3 * 1_048_576UL, finalSnap.UploadBytesPerSec);
        Assert.Equal(CollectorConnectionState.Connected, finalSnap.ConnectionState);
        Assert.Equal(TelemetryDataState.Active, finalSnap.DataState);
        Assert.NotNull(finalSnap.LatestRanking);
        Assert.Single(finalSnap.LatestRanking.Applications);
        Assert.Equal("app_chrome", finalSnap.LatestRanking.Applications[0].Id);
        Assert.NotNull(finalSnap.LatestStatus);
        Assert.True(finalSnap.LatestStatus.EtwSessionActive);
        Assert.Equal(15U, finalSnap.LatestStatus.ActivePids);

        // Stop Client and verify clean shutdown
        await client.DisposeAsync();
        await serverTask;

        Assert.Equal(CollectorConnectionState.Disconnected, store.CurrentSnapshot.ConnectionState);
    }
}
