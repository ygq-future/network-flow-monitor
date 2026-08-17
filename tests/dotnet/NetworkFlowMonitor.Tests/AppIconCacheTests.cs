using System;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Media;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class AppIconCacheTests {
    [Fact]
    public async Task GetIconAsync_ReturnsFallbackIcon_ForNullOrEmptyPath() {
        var icon1 = await AppIconCache.GetIconAsync(null);
        var icon2 = await AppIconCache.GetIconAsync("");
        var icon3 = await AppIconCache.GetIconAsync("   ");

        Assert.NotNull(icon1);
        Assert.NotNull(icon2);
        Assert.NotNull(icon3);
        Assert.Same(AppIconCache.FallbackIcon, icon1);
    }

    [Fact]
    public async Task GetIconAsync_CachesIconsInMemory() {
        var icon1 = await AppIconCache.GetIconAsync("nonexistent_app.exe");
        var icon2 = await AppIconCache.GetIconAsync("nonexistent_app.exe");

        Assert.NotNull(icon1);
        Assert.Same(icon1, icon2);
    }

    [Fact]
    public void FallbackIcon_IsFrozenAndValidImageSource() {
        var fallback = AppIconCache.FallbackIcon;
        Assert.NotNull(fallback);
        Assert.True(fallback.IsFrozen);
        Assert.True(fallback.Width > 0);
        Assert.True(fallback.Height > 0);
    }

    [Fact]
    public async Task GetIconAsync_ExtractsRealIcon_ForSystemExecutable() {
        var explorerPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe");
        if (File.Exists(explorerPath)) {
            var icon = await AppIconCache.GetIconAsync(explorerPath);
            Assert.NotNull(icon);
            Assert.NotSame(AppIconCache.FallbackIcon, icon);
            Assert.True(icon.IsFrozen);
            Assert.True(icon.Width >= 16.0);
            Assert.True(icon.Height >= 16.0);
        }
    }
}
