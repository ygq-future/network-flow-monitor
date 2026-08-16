using System;
using System.Globalization;

namespace NetworkFlowMonitor.Store;

public static class RateFormatter
{
    public static string FormatRate(ulong bytesPerSec)
    {
        if (bytesPerSec < 1024)
        {
            return $"{bytesPerSec} B/s";
        }
        if (bytesPerSec < 1024 * 1024)
        {
            return $"{(bytesPerSec / 1024.0):F1} KB/s";
        }
        if (bytesPerSec < 1024 * 1024 * 1024)
        {
            return $"{(bytesPerSec / (1024.0 * 1024.0)):F1} MB/s";
        }
        return $"{(bytesPerSec / (1024.0 * 1024.0 * 1024.0)):F2} GB/s";
    }
}
