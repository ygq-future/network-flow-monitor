using System.Globalization;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class LocalizationServiceTests {
    [Fact]
    public void BrandName_RemainsUntranslatedAcrossAllLanguages() {
        LocalizationService.CurrentLanguage = AppLanguage.English;
        Assert.Equal("Network Flow Monitor", LocalizationService.BrandName);

        LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
        Assert.Equal("Network Flow Monitor", LocalizationService.BrandName);

        LocalizationService.CurrentLanguage = AppLanguage.System;
        Assert.Equal("Network Flow Monitor", LocalizationService.BrandName);
    }

    [Fact]
    public void SettingsStrings_SwitchDynamicallyWithoutRestart() {
        // Switch to English
        LocalizationService.CurrentLanguage = AppLanguage.English;
        Assert.False(LocalizationService.IsChinese);

        Assert.Equal("Settings", LocalizationService.Settings);
        Assert.Equal("General", LocalizationService.GeneralTab);
        Assert.Equal("Monitor", LocalizationService.MonitorTab);
        Assert.Equal("Ranking", LocalizationService.RankingTab);
        Assert.Equal("Collector", LocalizationService.CollectorTab);
        Assert.Equal("Diagnostics", LocalizationService.DiagnosticsTab);

        Assert.Equal("Language", LocalizationService.LanguageLabel);
        Assert.Equal("Theme", LocalizationService.ThemeLabel);
        Assert.Equal("Start with Windows", LocalizationService.StartWithWindowsLabel);
        Assert.Equal("System", LocalizationService.LanguageSystem);
        Assert.Equal("English", LocalizationService.LanguageEnglish);
        Assert.Equal("简体中文", LocalizationService.LanguageChinese);
        Assert.Equal("System", LocalizationService.ThemeSystem);
        Assert.Equal("Light", LocalizationService.ThemeLight);
        Assert.Equal("Dark", LocalizationService.ThemeDark);

        Assert.Equal("Auto-Hide on Pointer Leave", LocalizationService.AutoHideLabel);
        Assert.Equal("Pin Panel by Default", LocalizationService.PinnedLabel);
        Assert.Equal("Capsule Width (DIP)", LocalizationService.CapsuleWidthLabel);
        Assert.Equal("Capsule Height (DIP)", LocalizationService.CapsuleHeightLabel);
        Assert.Equal("Hover Reveal Delay (ms)", LocalizationService.HoverDelayLabel);
        Assert.Equal("Dismiss Delay (ms)", LocalizationService.DismissDelayLabel);
        Assert.Equal("Panel Background Opacity", LocalizationService.PanelOpacityLabel);
        Assert.Equal("Acrylic Blur Strength", LocalizationService.BlurStrengthLabel);
        Assert.Equal("Yield Topmost on Fullscreen", LocalizationService.TopmostYieldLabel);
        Assert.Equal("Rate Display Units", LocalizationService.RateUnitsLabel);

        Assert.Equal("Top N Application Count", LocalizationService.TopNLabel);

        Assert.Equal("Connection State", LocalizationService.ConnectionStateLabel);
        Assert.Equal("Collector PID", LocalizationService.CollectorPidLabel);
        Assert.Equal("Instance ID", LocalizationService.InstanceIdLabel);
        Assert.Equal("Elevation Status", LocalizationService.ElevationStatusLabel);
        Assert.Equal("ETW Session Health", LocalizationService.EtwHealthLabel);
        Assert.Equal("Dropped Packets", LocalizationService.DroppedPacketsLabel);
        Assert.Equal("Active Process Count", LocalizationService.ActivePidsLabel);
        Assert.Equal("App Version", LocalizationService.AppVersionLabel);
        Assert.Equal("Collector Version", LocalizationService.CollectorVersionLabel);
        Assert.Equal("Protocol Version", LocalizationService.ProtocolVersionLabel);
        Assert.Equal("Config File Path", LocalizationService.ConfigPathLabel);

        Assert.Equal("Hide Monitor", LocalizationService.HideMonitor);
        Assert.Equal("Show Monitor", LocalizationService.ShowMonitor);
        Assert.Equal("Pin", LocalizationService.Pin);
        Assert.Equal("Retry Collector", LocalizationService.RetryCollector);
        Assert.Equal("Restart Collector", LocalizationService.RestartCollector);
        Assert.Equal("Exit", LocalizationService.Exit);
        Assert.Equal("Collector offline", LocalizationService.CollectorOffline);
        Assert.Equal("Collecting data…", LocalizationService.CollectingData);
        Assert.Equal("No network activity", LocalizationService.NoNetworkActivity);
        Assert.Equal("Copy Diagnostics", LocalizationService.CopyDiagnostics);
        Assert.Equal("Copied to clipboard", LocalizationService.CopiedToClipboard);

        // Switch to Simplified Chinese
        LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
        Assert.True(LocalizationService.IsChinese);

        Assert.Equal("设置", LocalizationService.Settings);
        Assert.Equal("常规", LocalizationService.GeneralTab);
        Assert.Equal("监控", LocalizationService.MonitorTab);
        Assert.Equal("排行", LocalizationService.RankingTab);
        Assert.Equal("采集器", LocalizationService.CollectorTab);
        Assert.Equal("诊断", LocalizationService.DiagnosticsTab);

        Assert.Equal("语言", LocalizationService.LanguageLabel);
        Assert.Equal("主题", LocalizationService.ThemeLabel);
        Assert.Equal("开机自动启动", LocalizationService.StartWithWindowsLabel);
        Assert.Equal("跟随系统", LocalizationService.LanguageSystem);
        Assert.Equal("English", LocalizationService.LanguageEnglish);
        Assert.Equal("简体中文", LocalizationService.LanguageChinese);
        Assert.Equal("跟随系统", LocalizationService.ThemeSystem);
        Assert.Equal("浅色", LocalizationService.ThemeLight);
        Assert.Equal("深色", LocalizationService.ThemeDark);

        Assert.Equal("鼠标离开自动收起", LocalizationService.AutoHideLabel);
        Assert.Equal("默认固定面板", LocalizationService.PinnedLabel);
        Assert.Equal("胶囊宽度 (DIP)", LocalizationService.CapsuleWidthLabel);
        Assert.Equal("胶囊高度 (DIP)", LocalizationService.CapsuleHeightLabel);
        Assert.Equal("展开悬停延迟 (ms)", LocalizationService.HoverDelayLabel);
        Assert.Equal("收起延迟 (ms)", LocalizationService.DismissDelayLabel);
        Assert.Equal("面板背景不透明度", LocalizationService.PanelOpacityLabel);
        Assert.Equal("亚克力模糊强度", LocalizationService.BlurStrengthLabel);
        Assert.Equal("全屏应用时让出置顶", LocalizationService.TopmostYieldLabel);
        Assert.Equal("速率显示单位", LocalizationService.RateUnitsLabel);

        Assert.Equal("显示应用数量 (Top N)", LocalizationService.TopNLabel);

        Assert.Equal("连接状态", LocalizationService.ConnectionStateLabel);
        Assert.Equal("采集器 PID", LocalizationService.CollectorPidLabel);
        Assert.Equal("实例 ID", LocalizationService.InstanceIdLabel);
        Assert.Equal("权限状态", LocalizationService.ElevationStatusLabel);
        Assert.Equal("ETW 会话状态", LocalizationService.EtwHealthLabel);
        Assert.Equal("丢包数", LocalizationService.DroppedPacketsLabel);
        Assert.Equal("活跃进程数", LocalizationService.ActivePidsLabel);
        Assert.Equal("应用版本", LocalizationService.AppVersionLabel);
        Assert.Equal("采集器版本", LocalizationService.CollectorVersionLabel);
        Assert.Equal("IPC 协议版本", LocalizationService.ProtocolVersionLabel);
        Assert.Equal("配置文件路径", LocalizationService.ConfigPathLabel);

        Assert.Equal("隐藏监控", LocalizationService.HideMonitor);
        Assert.Equal("显示监控", LocalizationService.ShowMonitor);
        Assert.Equal("固定显示", LocalizationService.Pin);
        Assert.Equal("重试采集", LocalizationService.RetryCollector);
        Assert.Equal("重启采集", LocalizationService.RestartCollector);
        Assert.Equal("退出", LocalizationService.Exit);
        Assert.Equal("采集器未运行", LocalizationService.CollectorOffline);
        Assert.Equal("正在采集数据…", LocalizationService.CollectingData);
        Assert.Equal("无网络活动", LocalizationService.NoNetworkActivity);
        Assert.Equal("复制诊断信息", LocalizationService.CopyDiagnostics);
        Assert.Equal("已复制到剪贴板", LocalizationService.CopiedToClipboard);
    }

    [Fact]
    public void LanguageChanged_Event_FiresOnLanguageChange() {
        LocalizationService.CurrentLanguage = AppLanguage.English;

        var eventCount = 0;
        void Handler() => eventCount++;

        LocalizationService.LanguageChanged += Handler;

        try {
            // Change language -> should fire
            LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
            Assert.Equal(1, eventCount);

            // Same language -> should not fire
            LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
            Assert.Equal(1, eventCount);

            // Change language again -> should fire
            LocalizationService.CurrentLanguage = AppLanguage.System;
            Assert.Equal(2, eventCount);
        } finally {
            LocalizationService.LanguageChanged -= Handler;
        }
    }

    [Fact]
    public void IsChinese_EvaluatesSystemCultureProperly() {
        var originalCulture = CultureInfo.CurrentUICulture;
        try {
            LocalizationService.CurrentLanguage = AppLanguage.System;

            CultureInfo.CurrentUICulture = new CultureInfo("zh-CN");
            Assert.True(LocalizationService.IsChinese);

            CultureInfo.CurrentUICulture = new CultureInfo("en-US");
            Assert.False(LocalizationService.IsChinese);
        } finally {
            CultureInfo.CurrentUICulture = originalCulture;
            LocalizationService.CurrentLanguage = AppLanguage.System;
        }
    }
}
