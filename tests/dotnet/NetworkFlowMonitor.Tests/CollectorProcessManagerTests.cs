using System;
using System.IO;
using System.Threading.Tasks;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class CollectorProcessManagerTests {
    [Fact]
    public void ResolveCollectorExecutablePath_DefaultDirectory_ReturnsValidExePath() {
        var path = CollectorProcessManager.ResolveCollectorExecutablePath();
        Assert.NotNull(path);
        Assert.EndsWith(".exe", path, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void ResolveCollectorExecutablePath_WhenNfmCollectorExists_ReturnsNfmCollectorPath() {
        var tempDir = Path.Combine(Path.GetTempPath(), "Nfm_CollectorTest_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        try {
            var nfmCollectorPath = Path.Combine(tempDir, "nfm-collector.exe");
            File.WriteAllText(nfmCollectorPath, "dummy-binary");

            var resolved = CollectorProcessManager.ResolveCollectorExecutablePath(tempDir);
            Assert.Equal(nfmCollectorPath, resolved);
        } finally {
            if (Directory.Exists(tempDir)) {
                Directory.Delete(tempDir, recursive: true);
            }
        }
    }

    [Fact]
    public void ResolveCollectorExecutablePath_WhenOnlyNetworkFlowMonitorExists_ReturnsNetworkFlowMonitorPath() {
        var tempDir = Path.Combine(Path.GetTempPath(), "Nfm_CollectorTest_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        try {
            var expectedPath = Path.Combine(tempDir, "NetworkFlowMonitor.exe");
            var resolved = CollectorProcessManager.ResolveCollectorExecutablePath(tempDir);
            Assert.Equal(expectedPath, resolved);
        } finally {
            if (Directory.Exists(tempDir)) {
                Directory.Delete(tempDir, recursive: true);
            }
        }
    }

    [Fact]
    public void TryLaunchCollector_NonExistentFile_ReturnsFalse() {
        var store = new TelemetryStore();
        var client = new NamedPipeClient(store);
        var manager = new CollectorProcessManager(store, client);

        var nonExistentPath = Path.Combine(Path.GetTempPath(), "non_existent_folder_xyz", "fake_collector.exe");
        var result = manager.TryLaunchCollector(nonExistentPath);

        Assert.False(result);
    }

    [Fact]
    public async Task RestartCollectorAsync_ExecutesStopLaunchAndStartInOrder() {
        var store = new TelemetryStore();
        var client = new NamedPipeClient(store);

        var stopCalled = false;
        var launchCalled = false;
        var startCalled = false;
        var sequence = 0;

        var stopOrder = 0;
        var launchOrder = 0;
        var startOrder = 0;

        var manager = new CollectorProcessManager(
            client: client,
            store: store,
            launchSeam: () => {
                launchCalled = true;
                launchOrder = ++sequence;
                return true;
            },
            stopSeam: () => {
                stopCalled = true;
                stopOrder = ++sequence;
                return Task.CompletedTask;
            },
            startSeam: () => {
                startCalled = true;
                startOrder = ++sequence;
            }
        );

        await manager.RestartCollectorAsync(delayMs: 1);

        Assert.True(stopCalled);
        Assert.True(launchCalled);
        Assert.True(startCalled);

        Assert.Equal(1, stopOrder);
        Assert.Equal(2, launchOrder);
        Assert.Equal(3, startOrder);
    }

    [Fact]
    public void Constructor_NullClient_ThrowsArgumentNullException() {
        Assert.Throws<ArgumentNullException>(() => new CollectorProcessManager(null!));
    }
}
