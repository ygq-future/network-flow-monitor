using System;

namespace NetworkFlowMonitor.Presentation;

public static class MonitorLayout
{
    public const int WidthDip = 340;
    public const int IconLeftDip = 12;
    public const int IconSizeDip = 16;
    public const int IconTextGapDip = 8;
    public const int HeaderRowDip = 36;
    public const int RowDip = 28;
    public const int VerticalPaddingDip = 5;
    public const int MinimumInwardHitTargetDip = 16;
    public const int AutoHideRevealPixels = 2;

    public static double DpiScale(uint dpi) => (dpi > 0 ? dpi : 96) / 96.0;

    public static int DipToPixels(double dip, uint dpi)
    {
        var scaled = dip * DpiScale(dpi);
        if (double.IsNaN(scaled) || double.IsInfinity(scaled))
        {
            return 0;
        }
        return (int)Math.Round(scaled, MidpointRounding.AwayFromZero);
    }

    public static double PixelsToDip(int pixels, uint dpi) => pixels / DpiScale(dpi);

    public static int PanelRowCount(MonitorConfig config, RankingPresentation presentation, byte visibleRows)
    {
        if (presentation != RankingPresentation.ReadyWithRows)
        {
            return 1;
        }
        var ranked = Math.Max(1, (int)visibleRows);
        return Math.Max(1, Math.Min((int)config.TopN, ranked));
    }

    public static int PanelHeightDip(MonitorConfig config, RankingPresentation presentation, byte visibleRows)
    {
        return (2 * VerticalPaddingDip) + HeaderRowDip +
               (PanelRowCount(config, presentation, visibleRows) * RowDip);
    }

    public static PixelRect BoundsFor(
        PersistedMonitorAnchor anchor,
        MonitorWorkArea monitor,
        MonitorConfig config,
        MonitorPresentationState surface,
        RankingPresentation presentation,
        byte visibleRows)
    {
        var dpi = monitor.Dpi > 0 ? monitor.Dpi : 96;
        var desiredWidth = surface == MonitorPresentationState.CapsuleVisible
            ? DipToPixels(config.CapsuleWidthDip, dpi)
            : DipToPixels(WidthDip, dpi);
        var desiredHeight = surface == MonitorPresentationState.CapsuleVisible
            ? DipToPixels(config.CapsuleHeightDip, dpi)
            : DipToPixels(PanelHeightDip(config, presentation, visibleRows), dpi);

        var reserveReveal =
            (anchor.Side == DockSide.Left && monitor.AutoHideRevealOnLeft) ||
            (anchor.Side == DockSide.Right && monitor.AutoHideRevealOnRight);

        var availableWidth = Math.Max(1, monitor.Rect.Width - (reserveReveal ? AutoHideRevealPixels : 0));
        var width = Math.Max(1, Math.Min(desiredWidth, availableWidth));
        var height = Math.Max(1, Math.Min(desiredHeight, monitor.Rect.Height));
        var maximumTop = Math.Max(monitor.Rect.Top, monitor.Rect.Bottom - height);
        var top = Math.Clamp(monitor.Rect.Top + DipToPixels(anchor.VerticalOffsetDip, dpi),
            monitor.Rect.Top, maximumTop);
        var left = anchor.Side == DockSide.Left ? monitor.Rect.Left : monitor.Rect.Right - width;

        if (anchor.Side == DockSide.Left && monitor.AutoHideRevealOnLeft)
        {
            left += Math.Min(AutoHideRevealPixels, Math.Max(0, monitor.Rect.Width - width));
        }
        else if (anchor.Side == DockSide.Right && monitor.AutoHideRevealOnRight)
        {
            left -= Math.Min(AutoHideRevealPixels, Math.Max(0, monitor.Rect.Width - width));
        }

        return new PixelRect(left, top, left + width, top + height);
    }

    public static PersistedMonitorAnchor DockAt(
        MonitorWorkArea monitor,
        PixelPoint release,
        PixelPoint pressOffset,
        DockSide previousSide,
        MonitorConfig config,
        MonitorPresentationState surface,
        RankingPresentation presentation,
        byte visibleRows)
    {
        var leftDistance = (long)release.X - monitor.Rect.Left;
        var rightDistance = (long)monitor.Rect.Right - release.X;
        var side = previousSide;
        if (leftDistance < rightDistance)
        {
            side = DockSide.Left;
        }
        else if (rightDistance < leftDistance)
        {
            side = DockSide.Right;
        }

        var dpi = monitor.Dpi > 0 ? monitor.Dpi : 96;
        var desiredHeight = surface == MonitorPresentationState.CapsuleVisible
            ? DipToPixels(config.CapsuleHeightDip, dpi)
            : DipToPixels(PanelHeightDip(config, presentation, visibleRows), dpi);

        var height = Math.Max(1, Math.Min(desiredHeight, monitor.Rect.Height));
        var top = Math.Clamp(release.Y - pressOffset.Y, monitor.Rect.Top,
            Math.Max(monitor.Rect.Top, monitor.Rect.Bottom - height));

        var verticalOffsetDip = (int)Math.Round(PixelsToDip(top - monitor.Rect.Top, dpi), MidpointRounding.AwayFromZero);
        return new PersistedMonitorAnchor(monitor.Id, side, verticalOffsetDip);
    }

    public static PixelRect HitBoundsFor(
        PixelRect bounds,
        DockSide side,
        MonitorWorkArea monitor,
        MonitorConfig config,
        MonitorPresentationState surface)
    {
        if (surface != MonitorPresentationState.CapsuleVisible)
        {
            return bounds;
        }

        var dpi = monitor.Dpi > 0 ? monitor.Dpi : 96;
        var minHitPixels = DipToPixels(MinimumInwardHitTargetDip, dpi);

        if (side == DockSide.Right)
        {
            var hitLeft = Math.Min(bounds.Left, bounds.Right - minHitPixels);
            return new PixelRect(hitLeft, bounds.Top, bounds.Right, bounds.Bottom);
        }
        else
        {
            var hitRight = Math.Max(bounds.Right, bounds.Left + minHitPixels);
            return new PixelRect(bounds.Left, bounds.Top, hitRight, bounds.Bottom);
        }
    }

    public static float RateToFill(ulong bytesPerSec)
    {
        if (bytesPerSec == 0)
        {
            return 0.0f;
        }
        var log2 = Math.Log2(bytesPerSec);
        return (float)Math.Clamp(log2 / 30.0, 0.0, 1.0);
    }

    public static uint InterpolateColor(uint fromRgb, uint toRgb, float progress)
    {
        var p = Math.Clamp(progress, 0.0f, 1.0f);
        var r1 = (float)((fromRgb >> 16) & 0xFF);
        var g1 = (float)((fromRgb >> 8) & 0xFF);
        var b1 = (float)(fromRgb & 0xFF);

        var r2 = (float)((toRgb >> 16) & 0xFF);
        var g2 = (float)((toRgb >> 8) & 0xFF);
        var b2 = (float)(toRgb & 0xFF);

        var r = (byte)Math.Round(r1 + ((r2 - r1) * p), MidpointRounding.AwayFromZero);
        var g = (byte)Math.Round(g1 + ((g2 - g1) * p), MidpointRounding.AwayFromZero);
        var b = (byte)Math.Round(b1 + ((b2 - b1) * p), MidpointRounding.AwayFromZero);

        return ((uint)r << 16) | ((uint)g << 8) | b;
    }

    public static CapsuleTrack Track(ulong bytes, bool download)
    {
        var fill = RateToFill(bytes);
        var low = download ? 0x19D8E6u : 0xF4D03Fu;
        var mid = download ? 0x2878D0u : 0xF39C32u;
        var high = download ? 0x8146B8u : 0xD93636u;
        var color = fill <= 0.5f
            ? InterpolateColor(low, mid, fill * 2.0f)
            : InterpolateColor(mid, high, (fill - 0.5f) * 2.0f);
        return new CapsuleTrack(fill, color);
    }

    public static CapsulePresentation MapCapsuleRates(ulong downloadBytesPerSec, ulong uploadBytesPerSec)
    {
        return new CapsulePresentation(
            Track(downloadBytesPerSec, download: true),
            Track(uploadBytesPerSec, download: false)
        );
    }
}
