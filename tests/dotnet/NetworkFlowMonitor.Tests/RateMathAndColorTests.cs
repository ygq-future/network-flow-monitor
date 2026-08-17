using System;
using System.Windows.Media.Animation;
using NetworkFlowMonitor.Presentation;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class RateMathAndColorTests {
    [Theory]
    [InlineData(0UL, 0.0f)]
    [InlineData(1UL, 0.0f)]
    [InlineData(1024UL, 0.33333334f)] // 1 KiB/s -> 10/30
    [InlineData(1048576UL, 0.6666667f)] // 1 MiB/s -> 20/30
    [InlineData(1073741824UL, 1.0f)] // 1 GiB/s -> 30/30
    [InlineData(2147483648UL, 1.0f)] // 2 GiB/s -> clamped to 1.0
    public void RateToFill_CalculatesLogarithmicFillCorrectly(ulong bytesPerSec, float expectedFill) {
        var fill = MonitorLayout.RateToFill(bytesPerSec);
        Assert.Equal(expectedFill, fill, precision: 4);
    }

    [Fact]
    public void RateToFill_IsMonotonicallyIncreasing() {
        float previous = -1.0f;
        for (ulong r = 1; r <= 2_000_000_000UL; r *= 2) {
            var current = MonitorLayout.RateToFill(r);
            Assert.True(current >= previous);
            Assert.InRange(current, 0.0f, 1.0f);
            previous = current;
        }
    }

    [Fact]
    public void InterpolateColor_InterpolatesAcrossRgbChannelsLinearly() {
        // Red (0xFF0000) to Blue (0x0000FF) at 50%
        var mid = MonitorLayout.InterpolateColor(0xFF0000, 0x0000FF, 0.5f);
        Assert.Equal(0x800080u, mid); // Half red, half blue -> purple
    }

    [Fact]
    public void Track_CalculatesFixedGradientColorsCorrectly() {
        var dlLow = MonitorLayout.Track(1, download: true);
        Assert.Equal(0x19D8E6u, dlLow.ColorRgb);

        var dlHigh = MonitorLayout.Track(1073741824UL, download: true);
        Assert.Equal(0x8146B8u, dlHigh.ColorRgb);

        var ulLow = MonitorLayout.Track(1, download: false);
        Assert.Equal(0xF4D03Fu, ulLow.ColorRgb);

        var ulHigh = MonitorLayout.Track(1073741824UL, download: false);
        Assert.Equal(0xD93636u, ulHigh.ColorRgb);
    }

    [Fact]
    public void GetBallistics_ReturnsAttackAndDecayEasingAndDurations() {
        var attack = MonitorLayout.GetBallistics(current: 0.2f, target: 0.8f, reducedMotion: false);
        Assert.Equal(TimeSpan.FromMilliseconds(250), attack.Duration);
        Assert.IsType<CubicEase>(attack.EasingFunction);
        Assert.Equal(EasingMode.EaseOut, ((CubicEase)attack.EasingFunction).EasingMode);

        var decay = MonitorLayout.GetBallistics(current: 0.8f, target: 0.2f, reducedMotion: false);
        Assert.Equal(TimeSpan.FromMilliseconds(450), decay.Duration);
        Assert.IsType<QuadraticEase>(decay.EasingFunction);
        Assert.Equal(EasingMode.EaseOut, ((QuadraticEase)decay.EasingFunction).EasingMode);

        var reduced = MonitorLayout.GetBallistics(current: 0.2f, target: 0.8f, reducedMotion: true);
        Assert.Equal(TimeSpan.Zero, reduced.Duration);
    }
}
