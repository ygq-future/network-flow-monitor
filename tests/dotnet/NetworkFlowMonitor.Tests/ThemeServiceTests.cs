using NetworkFlowMonitor.Services;
using NetworkFlowMonitor.Store;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class ThemeServiceTests {
    [Theory]
    [InlineData(AppTheme.Light, true, AppTheme.Light)]
    [InlineData(AppTheme.Light, false, AppTheme.Light)]
    [InlineData(AppTheme.Dark, true, AppTheme.Dark)]
    [InlineData(AppTheme.Dark, false, AppTheme.Dark)]
    [InlineData(AppTheme.System, true, AppTheme.Dark)]
    [InlineData(AppTheme.System, false, AppTheme.Light)]
    public void ResolveEffectiveTheme_AllCombinations_ResolvesCorrectly(AppTheme configured, bool systemIsDark, AppTheme expected) {
        var effective = ThemeService.ResolveEffectiveTheme(configured, systemIsDark);
        Assert.Equal(expected, effective);
    }

    [Fact]
    public void CurrentTheme_WhenChanged_FiresThemeChangedEvent() {
        var eventFiredCount = 0;
        void Handler() => eventFiredCount++;

        ThemeService.ThemeChanged += Handler;
        try {
            ThemeService.CurrentTheme = AppTheme.Light;
            var previousCount = eventFiredCount;

            ThemeService.CurrentTheme = AppTheme.Dark;
            Assert.Equal(previousCount + 1, eventFiredCount);

            // Setting to same value should not fire event
            ThemeService.CurrentTheme = AppTheme.Dark;
            Assert.Equal(previousCount + 1, eventFiredCount);
        } finally {
            ThemeService.ThemeChanged -= Handler;
        }
    }

    [Fact]
    public void IsSystemInDarkMode_DoesNotThrow() {
        var exception = Record.Exception(() => {
            var isDark = ThemeService.IsSystemInDarkMode();
            Assert.True(isDark || !isDark);
        });
        Assert.Null(exception);
    }

    [Fact]
    public void Initialize_SetsConfiguredTheme_DoesNotThrow() {
        try {
            var exception = Record.Exception(() => {
                ThemeService.Initialize(AppTheme.Dark);
                Assert.Equal(AppTheme.Dark, ThemeService.CurrentTheme);
                Assert.Equal(AppTheme.Dark, ThemeService.EffectiveTheme);
            });
            Assert.Null(exception);
        } finally {
            ThemeService.CurrentTheme = AppTheme.System;
        }
    }

    [Fact]
    public void ApplyTheme_WithoutApplicationCurrent_DoesNotThrow() {
        var exception = Record.Exception(() => {
            ThemeService.ApplyTheme(AppTheme.Light);
            ThemeService.ApplyTheme(AppTheme.Dark);
        });
        Assert.Null(exception);
    }
}
