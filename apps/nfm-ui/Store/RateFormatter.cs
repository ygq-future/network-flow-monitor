using System;
using System.Globalization;

namespace NetworkFlowMonitor.Store;

public static class RateFormatter {
    public static string FormatRate(ulong bytesPerSec, RateUnitMode mode = RateUnitMode.AutoBytes) {
        return mode switch {
            RateUnitMode.AutoBits => FormatAutoBits(bytesPerSec),
            RateUnitMode.BytesPerSec => $"{bytesPerSec} B/s",
            RateUnitMode.KilobytesPerSec => $"{(bytesPerSec / 1024.0):F1} KB/s",
            RateUnitMode.MegabytesPerSec => $"{(bytesPerSec / 1048576.0):F2} MB/s",
            _ => FormatAutoBytes(bytesPerSec)
        };
    }

    private static string FormatAutoBytes(ulong bytesPerSec) {
        if (bytesPerSec < 1024) {
            return $"{bytesPerSec} B/s";
        }
        if (bytesPerSec < 1024 * 1024) {
            return $"{(bytesPerSec / 1024.0):F1} KB/s";
        }
        if (bytesPerSec < 1024 * 1024 * 1024) {
            return $"{(bytesPerSec / (1024.0 * 1024.0)):F1} MB/s";
        }
        return $"{(bytesPerSec / (1024.0 * 1024.0 * 1024.0)):F2} GB/s";
    }

    private static string FormatAutoBits(ulong bytesPerSec) {
        var bitsPerSec = bytesPerSec * 8;
        if (bitsPerSec < 1000) {
            return $"{bitsPerSec} bps";
        }
        if (bitsPerSec < 1000 * 1000) {
            return $"{(bitsPerSec / 1000.0):F1} Kbps";
        }
        if (bitsPerSec < 1000 * 1000 * 1000) {
            return $"{(bitsPerSec / (1000.0 * 1000.0)):F1} Mbps";
        }
        return $"{(bitsPerSec / (1000.0 * 1000.0 * 1000.0)):F2} Gbps";
    }
}
