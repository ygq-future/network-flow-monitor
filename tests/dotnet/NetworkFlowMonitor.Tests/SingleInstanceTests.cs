using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class SingleInstanceTests {
    [Fact]
    public void DefaultNaming_ReturnsValidFormattedNames() {
        var channelName = SingleInstanceChannel.GetDefaultChannelName();
        var mutexName = SingleInstanceChannel.GetDefaultMutexName();

        Assert.False(string.IsNullOrWhiteSpace(channelName));
        Assert.StartsWith("NetworkFlowMonitor_Activation_", channelName, StringComparison.Ordinal);

        Assert.False(string.IsNullOrWhiteSpace(mutexName));
        Assert.StartsWith(@"Local\NetworkFlowMonitor_SingleInstance_", mutexName, StringComparison.Ordinal);
    }

    [Fact]
    public async Task SignalExistingInstanceAsync_WithRunningServer_SendsCommandAndReceivesAck() {
        var testChannelName = "Nfm_Test_Activation_" + Guid.NewGuid().ToString("N");
        var receivedCommand = "";
        var tcs = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);

        await using var server = new SingleInstanceServer(testChannelName, cmd => {
            receivedCommand = cmd;
            tcs.TrySetResult(cmd);
        });
        server.Start();

        var pingResult = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, SingleInstanceChannel.ActivationCommandShowMonitor);
        Assert.True(pingResult);

        var completed = await Task.WhenAny(tcs.Task, Task.Delay(2000));
        Assert.Same(tcs.Task, completed);
        Assert.Equal(SingleInstanceChannel.ActivationCommandShowMonitor, await tcs.Task);
        Assert.Equal(SingleInstanceChannel.ActivationCommandShowMonitor, receivedCommand);
    }

    [Fact]
    public async Task SignalExistingInstanceAsync_WithMultipleCommandsSequentially_DeliversAllCommands() {
        var testChannelName = "Nfm_Test_Activation_" + Guid.NewGuid().ToString("N");
        var receivedCommands = new List<string>();
        var syncLock = new object();

        await using var server = new SingleInstanceServer(testChannelName, cmd => {
            lock (syncLock) {
                receivedCommands.Add(cmd);
            }
        });
        server.Start();

        var commands = new[] {
            SingleInstanceChannel.ActivationCommandShowMonitor,
            SingleInstanceChannel.ActivationCommandShowSettings,
            SingleInstanceChannel.ActivationCommandTogglePin,
            SingleInstanceChannel.ActivationCommandExit
        };

        foreach (var cmd in commands) {
            var result = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, cmd);
            Assert.True(result);
        }

        lock (syncLock) {
            Assert.Equal(commands, receivedCommands);
        }
    }

    [Fact]
    public async Task SignalExistingInstanceAsync_WithNoServerRunning_ReturnsFalseSafely() {
        var nonExistentChannel = "Nfm_NonExistent_" + Guid.NewGuid().ToString("N");
        var pingResult = await SingleInstanceClient.SignalExistingInstanceAsync(nonExistentChannel, "SHOW_MONITOR", timeoutMs: 150);
        Assert.False(pingResult);
    }

    [Theory]
    [InlineData(null, "SHOW_MONITOR")]
    [InlineData("", "SHOW_MONITOR")]
    [InlineData("   ", "SHOW_MONITOR")]
    [InlineData("test_pipe", null)]
    [InlineData("test_pipe", "")]
    [InlineData("test_pipe", "   ")]
    public async Task SignalExistingInstanceAsync_WithNullOrEmptyInputs_ReturnsFalse(string? pipeName, string? command) {
        var pingResult = await SingleInstanceClient.SignalExistingInstanceAsync(pipeName!, command!, timeoutMs: 150);
        Assert.False(pingResult);
    }

    [Fact]
    public async Task SingleInstanceServer_WhenSubscriberThrows_StillRepliesAckAndContinuesListening() {
        var testChannelName = "Nfm_Test_Activation_" + Guid.NewGuid().ToString("N");
        var receivedCount = 0;

        await using var server = new SingleInstanceServer(testChannelName, cmd => {
            Interlocked.Increment(ref receivedCount);
            if (cmd == "THROW_ERR") {
                throw new InvalidOperationException("Simulated subscriber exception");
            }
        });
        server.Start();

        var result1 = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, "THROW_ERR");
        Assert.True(result1);

        var result2 = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, "SHOW_MONITOR");
        Assert.True(result2);

        Assert.Equal(2, receivedCount);
    }

    [Fact]
    public async Task SingleInstanceServer_DisposeAndDisposeAsync_StopsListeningGracefully() {
        var testChannelName = "Nfm_Test_Activation_" + Guid.NewGuid().ToString("N");

        var server = new SingleInstanceServer(testChannelName, _ => { });
        server.Start();

        var ping1 = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, "TEST_BEFORE_STOP");
        Assert.True(ping1);

        await server.DisposeAsync();

        var ping2 = await SingleInstanceClient.SignalExistingInstanceAsync(testChannelName, "TEST_AFTER_STOP", timeoutMs: 150);
        Assert.False(ping2);
    }
}
