using System;
using System.IO;
using Microsoft.Win32;

namespace NetworkFlowMonitor.Services;

public static class AutoStartService {
    public const string DefaultRunSubKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    public const string DefaultValueName = "NetworkFlowMonitor";

    public static string FormatCommandLine(string executablePath) {
        if (string.IsNullOrWhiteSpace(executablePath)) {
            return string.Empty;
        }

        var clean = executablePath.Trim().Trim('"').Trim();
        return $"\"{clean}\"";
    }

    public static bool IsCommandLineMatching(string? registryValue, string executablePath) {
        if (string.IsNullOrWhiteSpace(registryValue) || string.IsNullOrWhiteSpace(executablePath)) {
            return false;
        }

        var normalizedValue = ExtractAndNormalizePath(registryValue);
        var normalizedExe = ExtractAndNormalizePath(executablePath);

        return string.Equals(normalizedValue, normalizedExe, StringComparison.OrdinalIgnoreCase);
    }

    public static bool IsEnabled(
        string? runSubKey = null,
        string? valueName = null,
        string? executablePath = null,
        RegistryKey? rootKey = null) {
        if (!OperatingSystem.IsWindows()) {
            return false;
        }

        try {
            var exePath = executablePath ?? GetDefaultExecutablePath();
            if (string.IsNullOrWhiteSpace(exePath)) {
                return false;
            }

            var subKey = runSubKey ?? DefaultRunSubKey;
            var name = valueName ?? DefaultValueName;
            var hive = rootKey ?? Registry.CurrentUser;

            using var key = hive.OpenSubKey(subKey, writable: false);
            if (key == null) {
                return false;
            }

            var val = key.GetValue(name) as string;
            return IsCommandLineMatching(val, exePath);
        } catch {
            return false;
        }
    }

    public static bool SetEnabled(
        bool enable,
        string? runSubKey = null,
        string? valueName = null,
        string? executablePath = null,
        RegistryKey? rootKey = null) {
        if (!OperatingSystem.IsWindows()) {
            return false;
        }

        try {
            var exePath = executablePath ?? GetDefaultExecutablePath();
            if (string.IsNullOrWhiteSpace(exePath)) {
                return false;
            }

            var subKey = runSubKey ?? DefaultRunSubKey;
            var name = valueName ?? DefaultValueName;
            var hive = rootKey ?? Registry.CurrentUser;

            if (enable) {
                using var key = hive.CreateSubKey(subKey, writable: true);
                if (key == null) {
                    return false;
                }

                key.SetValue(name, FormatCommandLine(exePath), RegistryValueKind.String);
                return true;
            } else {
                using var key = hive.OpenSubKey(subKey, writable: true);
                if (key == null) {
                    return true;
                }

                key.DeleteValue(name, throwOnMissingValue: false);
                return true;
            }
        } catch {
            return false;
        }
    }

    public static string GetDefaultExecutablePath() {
        return Environment.ProcessPath
            ?? System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName
            ?? Path.Combine(AppContext.BaseDirectory, "NetworkFlowMonitor.exe");
    }

    private static string ExtractAndNormalizePath(string raw) {
        var trimmed = raw.Trim();
        if (trimmed.StartsWith('"')) {
            var secondQuote = trimmed.IndexOf('"', 1);
            if (secondQuote > 1) {
                trimmed = trimmed.Substring(1, secondQuote - 1).Trim();
            } else {
                trimmed = trimmed.Trim('"').Trim();
            }
        } else {
            trimmed = trimmed.Trim('"').Trim();
        }

        return trimmed.Replace('/', '\\');
    }
}
