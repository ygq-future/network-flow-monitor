using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Media;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class DockCapsuleVisualTests {
    [Fact]
    public async Task CapsuleWindow_HasZeroLayoutMutationOnRateUpdate() {
        var tcs = new TaskCompletionSource<bool>();
        var thread = new Thread(() => {
            var window = new DockCapsuleWindow();
            var presentation = MonitorLayout.MapCapsuleRates(5242880, 1048576);
            window.UpdateRates(presentation, reducedMotion: true);

            // Invariant: Width and Height of layout elements remain fixed
            Assert.Equal(10.0, window.Width);
            Assert.Equal(64.0, window.Height);
            Assert.Equal(10.0, window.CapsulePill.Width);
            Assert.Equal(64.0, window.CapsulePill.Height);

            // Fill heights reflect calculated fills
            Assert.Equal(presentation.Download.Fill * 32.0, window.DownloadFillRect.Height, precision: 2);
            Assert.Equal(presentation.Upload.Fill * 32.0, window.UploadFillRect.Height, precision: 2);

            window.Close();
            tcs.SetResult(true);
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();

        var result = await tcs.Task;
        Assert.True(result);
    }

    [Fact]
    public async Task CapsuleWindow_MorphsGradientStopsCorrectly() {
        var tcs = new TaskCompletionSource<bool>();
        var thread = new Thread(() => {
            var window = new DockCapsuleWindow();
            // 1 GiB/s -> full saturation (Purple #8146B8 for dl, Red #D93636 for ul)
            var highPresentation = MonitorLayout.MapCapsuleRates(1073741824UL, 1073741824UL);
            window.UpdateRates(highPresentation, reducedMotion: true);

            Assert.Equal(Color.FromRgb(0x81, 0x46, 0xB8), window.DlStop2.Color);
            Assert.Equal(Color.FromRgb(0xD9, 0x36, 0x36), window.UlStop2.Color);

            window.Close();
            tcs.SetResult(true);
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();

        var result = await tcs.Task;
        Assert.True(result);
    }
}
