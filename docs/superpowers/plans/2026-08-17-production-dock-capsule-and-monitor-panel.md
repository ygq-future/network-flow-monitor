# Production Dock Capsule & Monitor Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the complete, production-grade visual experience and interaction fidelity for the dock capsule and single monitor panel in .NET 10 WPF.

**Architecture:** 
- Dock capsule renders rate dynamics strictly via layout-free `RenderTransform` (`ScaleTransform.ScaleY`) and dynamic `ColorAnimation` on fixed gradient topologies with asymmetric ballistics (250 ms Fluent Cubic Ease-Out attack / 450 ms Quadratic Ease-Out decay) and retargeting from in-flight visual values.
- Single monitor panel renders an invariant brand header, right-aligned tabular rate columns, virtualized scrollable ranking rows with smooth Y translation / entrant fade / exit fade transitions, and pinned header.
- Asynchronous application icon extraction (`SHGetFileInfo`) with managed `HICON` ownership and immediate `DestroyIcon` GDI cleanup.
- Shared native context menu with dynamic Show/Hide items, Pin toggle retention, auto-hide dismiss pausing, and settings activation.

**Tech Stack:** C# 14 / .NET 10 WPF, Windows 11 DWM / Acrylic interop, Shell32 / GDI interop, xUnit.

---

### Task 1: Rate Math, Color Interpolation & Ballistics Pure Functions

**Files:**
- Modify: `apps/nfm-ui/Presentation/MonitorLayout.cs`
- Modify: `apps/nfm-ui/Presentation/PresentationTypes.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/RateMathAndColorTests.cs`

- [ ] **Step 1: Write the failing tests for RateToFill, ColorMorph, and Ballistics**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/RateMathAndColorTests.cs
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~RateMathAndColorTests"`
Expected: FAIL due to `GetBallistics` not defined.

- [ ] **Step 3: Implement minimal code in `MonitorLayout.cs` and `PresentationTypes.cs`**

Add `AnimationBallistics` record to `PresentationTypes.cs` and `GetBallistics` to `MonitorLayout.cs`:

```csharp
// In apps/nfm-ui/Presentation/PresentationTypes.cs:
public sealed record AnimationBallistics(
    TimeSpan Duration,
    IEasingFunction? EasingFunction
);

// In apps/nfm-ui/Presentation/MonitorLayout.cs:
public static AnimationBallistics GetBallistics(float current, float target, bool reducedMotion) {
    if (reducedMotion) {
        return new AnimationBallistics(TimeSpan.Zero, null);
    }

    if (target > current) {
        // Attack: 250 ms Fluent Cubic Ease-Out: 1 - (1 - t)^3
        return new AnimationBallistics(
            TimeSpan.FromMilliseconds(250),
            new CubicEase { EasingMode = EasingMode.EaseOut }
        );
    } else {
        // Decay: 450 ms natural momentum decay: 1 - (1 - t)^2
        return new AnimationBallistics(
            TimeSpan.FromMilliseconds(450),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }
        );
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~RateMathAndColorTests"`
Expected: PASS

---

### Task 2: Asynchronous App Icon Extraction Service with Managed GDI Cleanup

**Files:**
- Create: `apps/nfm-ui/Services/AppIconCache.cs`
- Modify: `apps/nfm-ui/Interop/NativeMethods.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/AppIconCacheTests.cs`

- [ ] **Step 1: Add Shell and GDI P/Invoke definitions to `NativeMethods.cs`**

```csharp
// In apps/nfm-ui/Interop/NativeMethods.cs:
public const uint SHGFI_ICON = 0x000000100;
public const uint SHGFI_SMALLICON = 0x000000001;
public const uint SHGFI_USEFILEATTRIBUTES = 0x000000010;
public const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
public struct SHFILEINFO {
    public IntPtr hIcon;
    public int iIcon;
    public uint dwAttributes;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
    public string szDisplayName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)]
    public string szTypeName;
}

[DllImport("shell32.dll", CharSet = CharSet.Auto)]
public static extern IntPtr SHGetFileInfo(
    string pszPath,
    uint dwFileAttributes,
    ref SHFILEINFO psfi,
    uint cbFileInfo,
    uint uFlags
);

[DllImport("user32.dll", SetLastError = true)]
[return: MarshalAs(UnmanagedType.Bool)]
public static extern bool DestroyIcon(IntPtr hIcon);
```

- [ ] **Step 2: Write failing unit test for `AppIconCache`**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/AppIconCacheTests.cs
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
}
```

- [ ] **Step 3: Implement `AppIconCache` in `apps/nfm-ui/Services/AppIconCache.cs`**

```csharp
using System;
using System.Collections.Concurrent;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using NetworkFlowMonitor.Interop;

namespace NetworkFlowMonitor.Services;

public static class AppIconCache {
    private static readonly ConcurrentDictionary<string, ImageSource> _cache = new(StringComparer.OrdinalIgnoreCase);
    private static readonly ImageSource _fallbackIcon = CreateFallbackIcon();

    public static ImageSource FallbackIcon => _fallbackIcon;

    private static ImageSource CreateFallbackIcon() {
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen()) {
            // Draw a clean 16x16 subtle modern Fluent application tile
            var bgBrush = new SolidColorBrush(Color.FromArgb(0x40, 0x80, 0x80, 0x80));
            bgBrush.Freeze();
            var borderPen = new Pen(new SolidColorBrush(Color.FromArgb(0x60, 0xFF, 0xFF, 0xFF)), 1.0);
            borderPen.Freeze();
            dc.DrawRoundedRectangle(bgBrush, borderPen, new Rect(0.5, 0.5, 15, 15), 3, 3);

            var glyphBrush = new SolidColorBrush(Color.FromArgb(0xCC, 0xE4, 0xE4, 0xE7));
            glyphBrush.Freeze();
            var glyphPen = new Pen(glyphBrush, 1.2);
            glyphPen.Freeze();
            // Subtle mini window/app grid glyph
            dc.DrawRectangle(null, glyphPen, new Rect(3.5, 3.5, 9, 9));
            dc.DrawLine(glyphPen, new Point(3.5, 6.5), new Point(12.5, 6.5));
            dc.DrawLine(glyphPen, new Point(7.5, 6.5), new Point(7.5, 12.5));
        }

        var rtb = new RenderTargetBitmap(16, 16, 96, 96, PixelFormats.Pbgra32);
        rtb.Render(dv);
        rtb.Freeze();
        return rtb;
    }

    public static Task<ImageSource> GetIconAsync(string? executablePath, string? iconKey = null) {
        if (string.IsNullOrWhiteSpace(executablePath)) {
            return Task.FromResult(_fallbackIcon);
        }

        var key = iconKey ?? executablePath;
        if (_cache.TryGetValue(key, out var cached)) {
            return Task.FromResult(cached);
        }

        return Task.Run(() => ExtractAndCache(executablePath, key));
    }

    private static ImageSource ExtractAndCache(string executablePath, string key) {
        if (_cache.TryGetValue(key, out var cached)) {
            return cached;
        }

        try {
            var psfi = new NativeMethods.SHFILEINFO();
            var flags = NativeMethods.SHGFI_ICON | NativeMethods.SHGFI_SMALLICON;
            if (!File.Exists(executablePath)) {
                flags |= NativeMethods.SHGFI_USEFILEATTRIBUTES;
            }

            var res = NativeMethods.SHGetFileInfo(
                executablePath,
                NativeMethods.FILE_ATTRIBUTE_NORMAL,
                ref psfi,
                (uint)Marshal.SizeOf(typeof(NativeMethods.SHFILEINFO)),
                flags
            );

            if (res != IntPtr.Zero && psfi.hIcon != IntPtr.Zero) {
                try {
                    var bitmapSource = Imaging.CreateBitmapSourceFromHIcon(
                        psfi.hIcon,
                        Int32Rect.Empty,
                        BitmapSizeOptions.FromEmptyOptions()
                    );
                    bitmapSource.Freeze();
                    _cache[key] = bitmapSource;
                    return bitmapSource;
                } finally {
                    NativeMethods.DestroyIcon(psfi.hIcon);
                }
            }
        } catch {
            // Inaccessible, security sandbox, or missing file -> fallback
        }

        _cache[key] = _fallbackIcon;
        return _fallbackIcon;
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~AppIconCacheTests"`
Expected: PASS

---

### Task 3: Zero-Layout-Thrashing Dock Capsule Window with Asymmetric Ballistics & Dynamic Gradient Morphing

**Files:**
- Modify: `apps/nfm-ui/Views/DockCapsuleWindow.xaml`
- Modify: `apps/nfm-ui/Views/DockCapsuleWindow.xaml.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/DockCapsuleVisualTests.cs`

- [ ] **Step 1: Update `DockCapsuleWindow.xaml` for layout-free `RenderTransform` (`ScaleTransform`) and Gradient Stops**

```xml
<Window x:Class="NetworkFlowMonitor.Views.DockCapsuleWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="NetworkFlowMonitor Capsule"
        WindowStyle="None"
        AllowsTransparency="False"
        Background="Transparent"
        ShowInTaskbar="False"
        ResizeMode="NoResize"
        Topmost="True"
        Width="16"
        Height="64">
    <WindowChrome.WindowChrome>
        <WindowChrome CaptionHeight="0"
                      CornerRadius="0"
                      GlassFrameThickness="-1"
                      UseAeroCaptionButtons="False"
                      NonClientFrameEdges="None"
                      ResizeBorderThickness="0" />
    </WindowChrome.WindowChrome>
    <Grid x:Name="RootGrid" Background="Transparent">
        <!-- Visible Capsule Pill: Fixed Layout Size (10 x 64 DIP) -->
        <Border x:Name="CapsulePill"
                Width="10"
                Height="64"
                HorizontalAlignment="Right"
                VerticalAlignment="Stretch"
                CornerRadius="5"
                Background="#D918181B"
                BorderBrush="#40FFFFFF"
                BorderThickness="1"
                ClipToBounds="True">
            <Grid>
                <Grid.RowDefinitions>
                    <RowDefinition Height="32" />
                    <RowDefinition Height="32" />
                </Grid.RowDefinitions>

                <!-- Download Track (Top half: Fixed Height 32, ScaleTransform downwards from Y=0) -->
                <Grid Grid.Row="0" VerticalAlignment="Top" HorizontalAlignment="Stretch" Height="32" ClipToBounds="True">
                    <Rectangle x:Name="DownloadFillRect"
                               Height="32"
                               VerticalAlignment="Top"
                               HorizontalAlignment="Stretch"
                               RenderTransformOrigin="0.5,0">
                        <Rectangle.RenderTransform>
                            <ScaleTransform x:Name="DownloadScaleTransform" ScaleY="0" ScaleX="1" />
                        </Rectangle.RenderTransform>
                        <Rectangle.Fill>
                            <LinearGradientBrush x:Name="DownloadGradientBrush" StartPoint="0,0" EndPoint="0,1">
                                <GradientStop x:Name="DlStop0" Color="#19D8E6" Offset="0.0" />
                                <GradientStop x:Name="DlStop1" Color="#2878D0" Offset="0.5" />
                                <GradientStop x:Name="DlStop2" Color="#8146B8" Offset="1.0" />
                            </LinearGradientBrush>
                        </Rectangle.Fill>
                    </Rectangle>
                </Grid>

                <!-- Upload Track (Bottom half: Fixed Height 32, ScaleTransform upwards from Y=1) -->
                <Grid Grid.Row="1" VerticalAlignment="Bottom" HorizontalAlignment="Stretch" Height="32" ClipToBounds="True">
                    <Rectangle x:Name="UploadFillRect"
                               Height="32"
                               VerticalAlignment="Bottom"
                               HorizontalAlignment="Stretch"
                               RenderTransformOrigin="0.5,1">
                        <Rectangle.RenderTransform>
                            <ScaleTransform x:Name="UploadScaleTransform" ScaleY="0" ScaleX="1" />
                        </Rectangle.RenderTransform>
                        <Rectangle.Fill>
                            <LinearGradientBrush x:Name="UploadGradientBrush" StartPoint="0,1" EndPoint="0,0">
                                <GradientStop x:Name="UlStop0" Color="#F4D03F" Offset="0.0" />
                                <GradientStop x:Name="UlStop1" Color="#F39C32" Offset="0.5" />
                                <GradientStop x:Name="UlStop2" Color="#D93636" Offset="1.0" />
                            </LinearGradientBrush>
                        </Rectangle.Fill>
                    </Rectangle>
                </Grid>
            </Grid>
        </Border>
    </Grid>
</Window>
```

- [ ] **Step 2: Update `DockCapsuleWindow.xaml.cs` with Ballistics, Dynamic Retargeting & Color Morphing**

```csharp
// In apps/nfm-ui/Views/DockCapsuleWindow.xaml.cs:
private float _currentDownloadFill = 0.0f;
private float _targetDownloadFill = 0.0f;
private float _currentUploadFill = 0.0f;
private float _targetUploadFill = 0.0f;

public void UpdateRates(CapsulePresentation presentation, bool reducedMotion = false) {
    AnimateDownloadTrack(presentation.Download.Fill, presentation.Download.ColorRgb, reducedMotion);
    AnimateUploadTrack(presentation.Upload.Fill, presentation.Upload.ColorRgb, reducedMotion);
}

private void AnimateDownloadTrack(float targetFill, uint targetColorRgb, bool reducedMotion) {
    if (Math.Abs(_targetDownloadFill - targetFill) < 0.001f &&
        Math.Abs(_currentDownloadFill - targetFill) < 0.001f) {
        return; // Stale or identical target -> avoid redundant animation
    }

    _targetDownloadFill = targetFill;
    var ballistics = MonitorLayout.GetBallistics(_currentDownloadFill, targetFill, reducedMotion);

    if (ballistics.Duration == TimeSpan.Zero) {
        DownloadScaleTransform.ScaleY = targetFill;
        _currentDownloadFill = targetFill;
        DlStop2.Color = ColorFromRgb(targetColorRgb);
        return;
    }

    var scaleAnim = new DoubleAnimation {
        To = targetFill,
        Duration = ballistics.Duration,
        EasingFunction = ballistics.EasingFunction
    };
    scaleAnim.Completed += (_, _) => _currentDownloadFill = targetFill;
    DownloadScaleTransform.BeginAnimation(ScaleTransform.ScaleYProperty, scaleAnim, HandoffBehavior.SnapshotAndReplace);

    var targetColor = ColorFromRgb(targetColorRgb);
    var colorAnim = new ColorAnimation {
        To = targetColor,
        Duration = ballistics.Duration,
        EasingFunction = ballistics.EasingFunction
    };
    DlStop2.BeginAnimation(GradientStop.ColorProperty, colorAnim, HandoffBehavior.SnapshotAndReplace);
}

private void AnimateUploadTrack(float targetFill, uint targetColorRgb, bool reducedMotion) {
    if (Math.Abs(_targetUploadFill - targetFill) < 0.001f &&
        Math.Abs(_currentUploadFill - targetFill) < 0.001f) {
        return;
    }

    _targetUploadFill = targetFill;
    var ballistics = MonitorLayout.GetBallistics(_currentUploadFill, targetFill, reducedMotion);

    if (ballistics.Duration == TimeSpan.Zero) {
        UploadScaleTransform.ScaleY = targetFill;
        _currentUploadFill = targetFill;
        UlStop2.Color = ColorFromRgb(targetColorRgb);
        return;
    }

    var scaleAnim = new DoubleAnimation {
        To = targetFill,
        Duration = ballistics.Duration,
        EasingFunction = ballistics.EasingFunction
    };
    scaleAnim.Completed += (_, _) => _currentUploadFill = targetFill;
    UploadScaleTransform.BeginAnimation(ScaleTransform.ScaleYProperty, scaleAnim, HandoffBehavior.SnapshotAndReplace);

    var targetColor = ColorFromRgb(targetColorRgb);
    var colorAnim = new ColorAnimation {
        To = targetColor,
        Duration = ballistics.Duration,
        EasingFunction = ballistics.EasingFunction
    };
    UlStop2.BeginAnimation(GradientStop.ColorProperty, colorAnim, HandoffBehavior.SnapshotAndReplace);
}

private static Color ColorFromRgb(uint rgb) {
    return Color.FromRgb(
        (byte)((rgb >> 16) & 0xFF),
        (byte)((rgb >> 8) & 0xFF),
        (byte)(rgb & 0xFF)
    );
}
```

- [ ] **Step 3: Write tests verifying capsule transforms and ballistics**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/DockCapsuleVisualTests.cs
using System.Threading;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class DockCapsuleVisualTests {
    [Fact]
    public void CapsuleWindow_HasZeroLayoutMutationOnRateUpdate() {
        var tcs = new TaskCompletionSource<bool>();
        var thread = new Thread(() => {
            var window = new DockCapsuleWindow();
            var presentation = MonitorLayout.MapCapsuleRates(5242880, 1048576);
            window.UpdateRates(presentation, reducedMotion: true);

            // Invariant: Width and Height of layout elements remain fixed
            Assert.Equal(16.0, window.Width);
            Assert.Equal(64.0, window.Height);
            Assert.Equal(10.0, window.CapsulePill.Width);
            Assert.Equal(64.0, window.CapsulePill.Height);
            Assert.Equal(32.0, window.DownloadFillRect.Height);
            Assert.Equal(32.0, window.UploadFillRect.Height);

            // Scale transform reflects calculated fills
            Assert.Equal(presentation.Download.Fill, (float)window.DownloadScaleTransform.ScaleY, precision: 3);
            Assert.Equal(presentation.Upload.Fill, (float)window.UploadScaleTransform.ScaleY, precision: 3);

            window.Close();
            tcs.SetResult(true);
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();

        Assert.True(tcs.Task.Result);
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~DockCapsuleVisualTests"`
Expected: PASS

---

### Task 4: Production-Grade Single Monitor Panel with Pinned Header, Virtualized Scrolling & Smooth Reordering

**Files:**
- Modify: `apps/nfm-ui/Views/MonitorPanelWindow.xaml`
- Modify: `apps/nfm-ui/Views/MonitorPanelWindow.xaml.cs`
- Modify: `apps/nfm-ui/Resources/FluentStyles.xaml`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/MonitorPanelVisualTests.cs`

- [ ] **Step 1: Update `FluentStyles.xaml` for ScrollViewer, ScrollBar, and Ranking Row Transitions**

Add modern Fluent scroll bar style to `FluentStyles.xaml`:
- Thin 4px thumb that expands to 6px on hover.
- Snaps to device pixels.
- Dark / transparent track.

- [ ] **Step 2: Update `MonitorPanelWindow.xaml` with fixed 340 DIP card, pinned header, and scrollable ranking rows**

```xml
<Window x:Class="NetworkFlowMonitor.Views.MonitorPanelWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Network Flow Monitor"
        WindowStyle="None"
        AllowsTransparency="False"
        Background="Transparent"
        ShowInTaskbar="False"
        ResizeMode="NoResize"
        Topmost="True"
        Width="340"
        SizeToContent="Height"
        MaxHeight="520">
    <WindowChrome.WindowChrome>
        <WindowChrome CaptionHeight="0"
                      CornerRadius="0"
                      GlassFrameThickness="-1"
                      UseAeroCaptionButtons="False"
                      NonClientFrameEdges="None"
                      ResizeBorderThickness="0" />
    </WindowChrome.WindowChrome>
    <Window.Resources>
        <Style x:Key="HeaderRateTextStyle" TargetType="TextBlock">
            <Setter Property="FontFamily" Value="Segoe UI Variable, Segoe UI, sans-serif" />
            <Setter Property="FontSize" Value="12" />
            <Setter Property="FontWeight" Value="SemiBold" />
            <Setter Property="Foreground" Value="#F4F4F5" />
            <Setter Property="VerticalAlignment" Value="Center" />
            <Setter Property="Margin" Value="4,0,0,0" />
        </Style>
    </Window.Resources>

    <Border x:Name="PanelCard"
            CornerRadius="8"
            Background="#D918181C"
            BorderBrush="#33FFFFFF"
            BorderThickness="1"
            Padding="6,4"
            SnapsToDevicePixels="True">
        <Grid>
            <Grid.RowDefinitions>
                <RowDefinition Height="36" />
                <RowDefinition Height="*" />
            </Grid.RowDefinitions>

            <!-- Pinned Header Row (36 DIP) -->
            <Grid Grid.Row="0" Margin="4,0,4,0" SnapsToDevicePixels="True">
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="*" />
                    <ColumnDefinition Width="75" />
                    <ColumnDefinition Width="75" />
                </Grid.ColumnDefinitions>

                <!-- Brand: Untranslated "Network Flow Monitor" -->
                <StackPanel Grid.Column="0" Orientation="Horizontal" VerticalAlignment="Center">
                    <Path Width="14" Height="14"
                          Data="M2,7 L5,2 L9,12 L12,7 L14,7"
                          Stroke="#38BDF8" StrokeThickness="1.5"
                          VerticalAlignment="Center" Margin="0,0,6,0" />
                    <TextBlock Text="Network Flow Monitor"
                               FontFamily="Segoe UI Variable, Segoe UI, sans-serif"
                               FontSize="12" FontWeight="SemiBold"
                               Foreground="#E4E4E7" VerticalAlignment="Center"
                               TextTrimming="CharacterEllipsis" />
                </StackPanel>

                <!-- Total Download Rate -->
                <StackPanel Grid.Column="1" Orientation="Horizontal" VerticalAlignment="Center" HorizontalAlignment="Right">
                    <Path Width="10" Height="10"
                          Data="M5,1 L5,8 M2,5 L5,8 L8,5"
                          Stroke="#38BDF8" StrokeThickness="1.5"
                          VerticalAlignment="Center" />
                    <TextBlock x:Name="HeaderDownloadText" Text="0 B/s"
                               Style="{StaticResource HeaderRateTextStyle}" />
                </StackPanel>

                <!-- Total Upload Rate -->
                <StackPanel Grid.Column="2" Orientation="Horizontal" VerticalAlignment="Center" HorizontalAlignment="Right">
                    <Path Width="10" Height="10"
                          Data="M5,8 L5,1 M2,4 L5,1 L8,4"
                          Stroke="#F59E0B" StrokeThickness="1.5"
                          VerticalAlignment="Center" />
                    <TextBlock x:Name="HeaderUploadText" Text="0 B/s"
                               Style="{StaticResource HeaderRateTextStyle}" />
                </StackPanel>
            </Grid>

            <!-- Content Area: Status Row or Virtualized Ranking List -->
            <Grid Grid.Row="1" x:Name="ContentArea">
                <!-- Status Row -->
                <Border x:Name="StatusRow"
                        Height="28"
                        Visibility="Visible"
                        Padding="8,0"
                        VerticalAlignment="Center">
                    <TextBlock x:Name="StatusRowText"
                               Text="正在采集数据…"
                               FontFamily="Segoe UI Variable, Segoe UI, sans-serif"
                               FontSize="12"
                               Foreground="#A1A1AA"
                               VerticalAlignment="Center"
                               HorizontalAlignment="Left" />
                </Border>

                <!-- Ranking Rows Viewport (Scrollable with pinned header) -->
                <ScrollViewer x:Name="RankingScrollViewer"
                              Visibility="Collapsed"
                              VerticalScrollBarVisibility="Auto"
                              HorizontalScrollBarVisibility="Disabled"
                              Focusable="True"
                              MaxHeight="460">
                    <ItemsControl x:Name="RankingItemsControl">
                        <ItemsControl.ItemTemplate>
                            <DataTemplate>
                                <Grid Height="28" Margin="4,0" SnapsToDevicePixels="True">
                                    <Grid.ColumnDefinitions>
                                        <ColumnDefinition Width="22" />
                                        <ColumnDefinition Width="*" />
                                        <ColumnDefinition Width="75" />
                                        <ColumnDefinition Width="75" />
                                    </Grid.ColumnDefinitions>

                                    <!-- Async Extracted / Fallback Icon (Fixed 16x16 geometry box) -->
                                    <Image Grid.Column="0"
                                           Source="{Binding Icon}"
                                           Width="16" Height="16"
                                           VerticalAlignment="Center"
                                           HorizontalAlignment="Left"
                                           RenderOptions.BitmapScalingMode="HighQuality" />

                                    <!-- Application Name (Ellipsized) -->
                                    <TextBlock Grid.Column="1"
                                               Text="{Binding DisplayName}"
                                               FontFamily="Segoe UI Variable, Segoe UI, sans-serif"
                                               FontSize="12"
                                               Foreground="#F4F4F5"
                                               VerticalAlignment="Center"
                                               TextTrimming="CharacterEllipsis"
                                               Margin="2,0,8,0" />

                                    <!-- Download Rate -->
                                    <TextBlock Grid.Column="2"
                                               Text="{Binding DownloadRateText}"
                                               FontFamily="Segoe UI Variable, Segoe UI, sans-serif"
                                               FontSize="12"
                                               Foreground="#38BDF8"
                                               HorizontalAlignment="Right"
                                               VerticalAlignment="Center" />

                                    <!-- Upload Rate -->
                                    <TextBlock Grid.Column="3"
                                               Text="{Binding UploadRateText}"
                                               FontFamily="Segoe UI Variable, Segoe UI, sans-serif"
                                               FontSize="12"
                                               Foreground="#F59E0B"
                                               HorizontalAlignment="Right"
                                               VerticalAlignment="Center" />
                                </Grid>
                            </DataTemplate>
                        </ItemsControl.ItemTemplate>
                    </ItemsControl>
                </ScrollViewer>
            </Grid>
        </Grid>
    </Border>
</Window>
```

- [ ] **Step 3: Update `MonitorPanelWindow.xaml.cs` with Observable Ranking Model and Async Icon Resolution**

```csharp
// In apps/nfm-ui/Views/MonitorPanelWindow.xaml.cs:
public sealed class RankingRowItemModel : System.ComponentModel.INotifyPropertyChanged {
    private ImageSource _icon;
    public string Id { get; }
    public string DisplayName { get; }
    public string? ExecutablePath { get; }
    public string DownloadRateText { get; }
    public string UploadRateText { get; }

    public ImageSource Icon {
        get => _icon;
        set {
            if (_icon != value) {
                _icon = value;
                PropertyChanged?.Invoke(this, new System.ComponentModel.PropertyChangedEventArgs(nameof(Icon)));
            }
        }
    }

    public event System.ComponentModel.PropertyChangedEventHandler? PropertyChanged;

    public RankingRowItemModel(string id, string displayName, string? executablePath, string downloadRateText, string uploadRateText, ImageSource initialIcon) {
        Id = id;
        DisplayName = displayName;
        ExecutablePath = executablePath;
        DownloadRateText = downloadRateText;
        UploadRateText = uploadRateText;
        _icon = initialIcon;
    }
}
```

- [ ] **Step 4: Connect async icon loading into `UpdateSnapshot`**

In `MonitorPanelWindow.xaml.cs`, resolve icons asynchronously and update items:

```csharp
var rowModels = new List<RankingRowItemModel>();
if (snapshot.LatestRanking?.Applications != null) {
    foreach (var app in snapshot.LatestRanking.Applications) {
        var name = string.IsNullOrWhiteSpace(app.DisplayName) ? app.Id : app.DisplayName;
        var row = new RankingRowItemModel(
            app.Id,
            name,
            app.ExecutablePath,
            RateFormatter.FormatRate(app.DownloadBytesPerSec),
            RateFormatter.FormatRate(app.UploadBytesPerSec),
            AppIconCache.FallbackIcon
        );
        rowModels.Add(row);

        // Async resolve real icon
        if (!string.IsNullOrWhiteSpace(app.ExecutablePath)) {
            AppIconCache.GetIconAsync(app.ExecutablePath, app.IconKey).ContinueWith(t => {
                if (t.IsCompletedSuccessfully && t.Result != null) {
                    Dispatcher.BeginInvoke(() => row.Icon = t.Result);
                }
            });
        }
    }
}
RankingItemsControl.ItemsSource = rowModels;
```

- [ ] **Step 5: Write unit test for MonitorPanelWindow layout & snapshot updates**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/MonitorPanelVisualTests.cs
using System.Threading;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class MonitorPanelVisualTests {
    [Fact]
    public void MonitorPanelWindow_RendersHeaderAndTabularRates() {
        var tcs = new TaskCompletionSource<bool>();
        var thread = new Thread(() => {
            var window = new MonitorPanelWindow();
            var snapshot = new TelemetrySnapshot(
                Seq: 100,
                TimestampMs: 12345,
                DownloadBytesPerSec: 10485760, // 10 MB/s
                UploadBytesPerSec: 1048576,   // 1 MB/s
                ConnectionState: CollectorConnectionState.Connected,
                DataState: TelemetryDataState.Active,
                LatestRanking: new RankingSnapshotPayload(
                    Seq: 100,
                    MonotonicMs: 12345,
                    Status: "Ready",
                    TotalRate: new RatePair(10485760, 1048576),
                    Applications: [
                        new ApplicationTraffic(
                            Id: "app1",
                            ExecutablePath: @"C:\Windows\explorer.exe",
                            DisplayName: "Windows Explorer",
                            IconKey: null,
                            DownloadBytesPerSec: 5242880,
                            UploadBytesPerSec: 524288
                        )
                    ]
                )
            );

            window.UpdateSnapshot(snapshot);

            Assert.Equal("10.0 MB/s", window.HeaderDownloadText.Text);
            Assert.Equal("1.0 MB/s", window.HeaderUploadText.Text);
            Assert.Equal(System.Windows.Visibility.Collapsed, window.StatusRow.Visibility);
            Assert.Equal(System.Windows.Visibility.Visible, window.RankingScrollViewer.Visibility);

            window.Close();
            tcs.SetResult(true);
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();

        Assert.True(tcs.Task.Result);
    }
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~MonitorPanelVisualTests"`
Expected: PASS

---

### Task 5: Context Menu Dynamic State & Presentation Host Integration

**Files:**
- Modify: `apps/nfm-ui/Views/MonitorPanelWindow.xaml.cs`
- Modify: `apps/nfm-ui/Views/DockCapsuleWindow.xaml.cs`
- Modify: `apps/nfm-ui/Presentation/MonitorPresentationHost.cs`
- Modify: `apps/nfm-ui/Presentation/MonitorBehaviorModel.cs`
- Test: `tests/dotnet/NetworkFlowMonitor.Tests/ContextMenuAndBehaviorTests.cs`

- [ ] **Step 1: Write tests for Context Menu Dynamic States (Show/Hide, Pin, Collector State)**

```csharp
// tests/dotnet/NetworkFlowMonitor.Tests/ContextMenuAndBehaviorTests.cs
using NetworkFlowMonitor.Presentation;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class ContextMenuAndBehaviorTests {
    [Fact]
    public void ContextMenu_ShowHideItem_ReflectsTrayOnlyVersusVisible() {
        var env = new MonitorEnvironment([
            new MonitorWorkArea("mon1", new PixelRect(0, 0, 1920, 1080), 96, false, false)
        ], "mon1");
        var config = new MonitorConfig(AutoHideEnabled: true);

        var state = new MonitorState(VisibilityState: MonitorPresentationState.CapsuleVisible);
        var t1 = MonitorBehaviorModel.Transition(state, new MonitorEvent(MonitorEventKind.HideMonitorRequested), env, config);
        Assert.Equal(MonitorPresentationState.TrayOnly, t1.State.VisibilityState);

        var t2 = MonitorBehaviorModel.Transition(t1.State, new MonitorEvent(MonitorEventKind.ShowMonitorRequested), env, config);
        Assert.Equal(MonitorPresentationState.CapsuleVisible, t2.State.VisibilityState);
    }

    [Fact]
    public void ContextMenu_OpeningPausesDismiss_ClosingReconcilesPointer() {
        var env = new MonitorEnvironment([
            new MonitorWorkArea("mon1", new PixelRect(0, 0, 1920, 1080), 96, false, false)
        ], "mon1");
        var config = new MonitorConfig(AutoHideEnabled: true, DismissDelayMs: 500);

        // Panel is visible, pointer leaves panel -> dismiss deadline set
        var state = new MonitorState(VisibilityState: MonitorPresentationState.PanelVisible);
        var t1 = MonitorBehaviorModel.Transition(state, new MonitorEvent(MonitorEventKind.PanelPointerLeave), env, config);
        Assert.Equal(MonitorDeadline.Dismiss, t1.State.Deadline);

        // Context menu opened -> dismiss deadline paused
        var t2 = MonitorBehaviorModel.Transition(t1.State, new MonitorEvent(MonitorEventKind.ContextMenuOpened), env, config);
        Assert.Equal(MonitorDeadline.None, t2.State.Deadline);
        Assert.True(t2.State.ContextMenuOpen);

        // Context menu closed with pointer outside -> dismiss delay resumed
        var t3 = MonitorBehaviorModel.Transition(t2.State, new MonitorEvent(
            MonitorEventKind.ContextMenuClosed,
            Point: new PixelPoint(500, 500) // outside panel
        ), env, config);
        Assert.Equal(MonitorDeadline.Dismiss, t3.State.Deadline);
    }
}
```

- [ ] **Step 2: Run test to verify it passes or update BehaviorModel if needed**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj --filter "FullyQualifiedName~ContextMenuAndBehaviorTests"`
Expected: PASS

- [ ] **Step 3: Wire right-click context menu to `DockCapsuleWindow`**

In `DockCapsuleWindow.xaml.cs`, add context menu opening with shared styling and events matching `MonitorPanelWindow`.

- [ ] **Step 4: Run full dotnet tests**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj`
Expected: PASS with all tests passing.

---

### Task 6: Headless Automated Test Suite & Multi-Language Quality Verification

**Files:**
- Check & Verify: `tests/dotnet/NetworkFlowMonitor.Tests/`
- Check & Verify: `tools/quality.ps1`

- [ ] **Step 1: Run .NET test suite**

Run: `dotnet test tests/dotnet/NetworkFlowMonitor.Tests/NetworkFlowMonitor.Tests.csproj -v minimal`
Expected: 100% tests pass headlessly without any UI popups, cursor moves, or timeouts.

- [ ] **Step 2: Run `tools/quality.ps1 -Mode Fast`**

Run: `powershell -ExecutionPolicy Bypass -File tools/quality.ps1 -Mode Fast`
Expected: All phases (Format check, C++ Debug, CTest, .NET test, clang-tidy) pass.

- [ ] **Step 3: Verification of Ticket 04 Checklist and Deliverables**

Confirm all Ticket 04 deliverables:
1. Logarithmic Rate-to-Fill math $F(R) = \text{clamp}(\log_2(R)/30.0, 0.0, 1.0)$.
2. Fixed capsule geometry with layout-free `ScaleTransform.ScaleY` transforms.
3. Asymmetric ballistics (250ms Fluent Cubic Ease-Out attack / 450ms Quadratic Ease-Out decay).
4. Continuous RGB gradient tip color morphing without snapping.
5. In-flight retargeting without animation restarts on identical targets.
6. Pinned header, 340 DIP compact card, right-aligned tabular rate columns.
7. Virtualized scrollable ranking rows with character ellipsizing.
8. Asynchronous `AppIconCache` with managed `DestroyIcon` GDI cleanup and 16x16 fallback.
9. Shared native context menu with Show/Hide, Pin, Retry/Restart Collector, Settings, and Exit.
10. Headless automated quality gate passes 100%.

---
