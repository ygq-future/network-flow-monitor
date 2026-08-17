using System;
using System.Collections.Generic;
using System.Globalization;

namespace NetworkFlowMonitor.Services;

public enum AppLanguage {
    System,
    SimplifiedChinese,
    English
}

public static class LocalizationService {
    private static AppLanguage _currentLanguage = AppLanguage.System;

    public static event Action? LanguageChanged;

    public static AppLanguage CurrentLanguage {
        get => _currentLanguage;
        set {
            if (_currentLanguage != value) {
                _currentLanguage = value;
                LanguageChanged?.Invoke();
            }
        }
    }

    public static bool IsChinese =>
        _currentLanguage == AppLanguage.SimplifiedChinese ||
        (_currentLanguage == AppLanguage.System && CultureInfo.CurrentUICulture.Name.StartsWith("zh", StringComparison.OrdinalIgnoreCase));

    public static string HideMonitor => IsChinese ? "隐藏监控" : "Hide Monitor";
    public static string ShowMonitor => IsChinese ? "显示监控" : "Show Monitor";
    public static string Pin => IsChinese ? "固定显示" : "Pin";
    public static string RetryCollector => IsChinese ? "重试采集" : "Retry Collector";
    public static string RestartCollector => IsChinese ? "重启采集" : "Restart Collector";
    public static string Settings => IsChinese ? "设置" : "Settings";
    public static string Exit => IsChinese ? "退出" : "Exit";
    public static string CollectorOffline => IsChinese ? "采集器未运行" : "Collector offline";
    public static string CollectingData => IsChinese ? "正在采集数据…" : "Collecting data…";
    public static string NoNetworkActivity => IsChinese ? "无网络活动" : "No network activity";
    public static string BrandName => "Network Flow Monitor";
}
