using System;
using Microsoft.Win32;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class AutoStartServiceTests : IDisposable {
    private readonly string _testSubKey;

    public AutoStartServiceTests() {
        _testSubKey = @"Software\NfmAutoStartTests_" + Guid.NewGuid().ToString("N");
    }

    public void Dispose() {
        try {
            if (OperatingSystem.IsWindows()) {
                Registry.CurrentUser.DeleteSubKeyTree(_testSubKey, throwOnMissingSubKey: false);
            }
        } catch { }
        GC.SuppressFinalize(this);
    }

    [Fact]
    public void FormatCommandLine_WithPlainPath_EnclosesInQuotes() {
        const string exePath = @"C:\Program Files\NFM\NetworkFlowMonitor.exe";
        var result = AutoStartService.FormatCommandLine(exePath);
        Assert.Equal("\"C:\\Program Files\\NFM\\NetworkFlowMonitor.exe\"", result);
    }

    [Fact]
    public void FormatCommandLine_WithAlreadyQuotedPath_PreservesSingleLayerQuotes() {
        const string exePath = "\"C:\\Apps\\NetworkFlowMonitor.exe\"";
        var result = AutoStartService.FormatCommandLine(exePath);
        Assert.Equal("\"C:\\Apps\\NetworkFlowMonitor.exe\"", result);
    }

    [Fact]
    public void FormatCommandLine_WithNullOrWhitespace_ReturnsEmpty() {
        Assert.Equal(string.Empty, AutoStartService.FormatCommandLine(""));
        Assert.Equal(string.Empty, AutoStartService.FormatCommandLine("   "));
    }

    [Fact]
    public void IsCommandLineMatching_QuotedAndUnquoted_ReturnsTrue() {
        const string exePath = @"C:\Apps\NetworkFlowMonitor.exe";
        Assert.True(AutoStartService.IsCommandLineMatching("\"C:\\Apps\\NetworkFlowMonitor.exe\"", exePath));
        Assert.True(AutoStartService.IsCommandLineMatching(@"C:\Apps\NetworkFlowMonitor.exe", exePath));
    }

    [Fact]
    public void IsCommandLineMatching_CaseInsensitive_ReturnsTrue() {
        const string testExe = @"C:\Apps\NetworkFlowMonitor.exe";
        Assert.True(AutoStartService.IsCommandLineMatching("\"c:\\apps\\networkflowmonitor.exe\"", testExe));
        Assert.True(AutoStartService.IsCommandLineMatching("C:\\APPS\\NETWORKFLOWMONITOR.EXE", testExe));
    }

    [Fact]
    public void IsCommandLineMatching_ForwardSlashVsBackslash_ReturnsTrue() {
        const string testExe = @"C:\Apps\NetworkFlowMonitor.exe";
        Assert.True(AutoStartService.IsCommandLineMatching("\"C:/Apps/NetworkFlowMonitor.exe\"", testExe));
    }

    [Fact]
    public void IsCommandLineMatching_WithTrailingArgs_ReturnsTrue() {
        const string testExe = @"C:\Apps\NetworkFlowMonitor.exe";
        Assert.True(AutoStartService.IsCommandLineMatching("\"C:\\Apps\\NetworkFlowMonitor.exe\" --autostart", testExe));
    }

    [Fact]
    public void IsCommandLineMatching_DifferentPath_ReturnsFalse() {
        const string testExe = @"C:\Apps\NetworkFlowMonitor.exe";
        Assert.False(AutoStartService.IsCommandLineMatching("\"C:\\Other\\App.exe\"", testExe));
    }

    [Theory]
    [InlineData(null, @"C:\Apps\App.exe")]
    [InlineData("", @"C:\Apps\App.exe")]
    [InlineData("   ", @"C:\Apps\App.exe")]
    [InlineData("\"C:\\Apps\\App.exe\"", null)]
    [InlineData("\"C:\\Apps\\App.exe\"", "")]
    [InlineData("\"C:\\Apps\\App.exe\"", "   ")]
    public void IsCommandLineMatching_NullOrEmptyInputs_ReturnsFalse(string? registryValue, string? executablePath) {
        Assert.False(AutoStartService.IsCommandLineMatching(registryValue, executablePath!));
    }

    [Fact]
    public void SetEnabled_And_IsEnabled_WithCustomSubKey_SavesAndRemovesCorrectly() {
        if (!OperatingSystem.IsWindows()) {
            return;
        }

        const string fakeExePath = @"C:\Mock\NetworkFlowMonitor.exe";
        const string customValueName = "NfmTestEntry";

        // Initially should be disabled
        var initiallyEnabled = AutoStartService.IsEnabled(
            runSubKey: _testSubKey,
            valueName: customValueName,
            executablePath: fakeExePath
        );
        Assert.False(initiallyEnabled);

        // Enable auto-start
        var setSuccess = AutoStartService.SetEnabled(
            enable: true,
            runSubKey: _testSubKey,
            valueName: customValueName,
            executablePath: fakeExePath
        );
        Assert.True(setSuccess);

        // Verify IsEnabled is now true
        var isEnabledAfterSet = AutoStartService.IsEnabled(
            runSubKey: _testSubKey,
            valueName: customValueName,
            executablePath: fakeExePath
        );
        Assert.True(isEnabledAfterSet);

        // Verify the raw registry value is properly quoted
        using (var key = Registry.CurrentUser.OpenSubKey(_testSubKey, writable: false)) {
            Assert.NotNull(key);
            var rawValue = key.GetValue(customValueName) as string;
            Assert.Equal("\"C:\\Mock\\NetworkFlowMonitor.exe\"", rawValue);
        }

        // Disable auto-start
        var disableSuccess = AutoStartService.SetEnabled(
            enable: false,
            runSubKey: _testSubKey,
            valueName: customValueName,
            executablePath: fakeExePath
        );
        Assert.True(disableSuccess);

        // Verify IsEnabled is now false
        var isEnabledAfterDisable = AutoStartService.IsEnabled(
            runSubKey: _testSubKey,
            valueName: customValueName,
            executablePath: fakeExePath
        );
        Assert.False(isEnabledAfterDisable);

        // Verify the registry value has been deleted
        using (var key = Registry.CurrentUser.OpenSubKey(_testSubKey, writable: false)) {
            Assert.NotNull(key);
            var rawValue = key.GetValue(customValueName);
            Assert.Null(rawValue);
        }
    }
}
