using System;
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

    public static string BrandName => "Network Flow Monitor";

    // Shared Menu & Actions
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
    public static string CopyDiagnostics => IsChinese ? "复制诊断信息" : "Copy Diagnostics";
    public static string CopiedToClipboard => IsChinese ? "已复制到剪贴板" : "Copied to clipboard";

    // Settings Navigation Tabs
    public static string GeneralTab => IsChinese ? "常规" : "General";
    public static string MonitorTab => IsChinese ? "监控" : "Monitor";
    public static string RankingTab => IsChinese ? "排行" : "Ranking";
    public static string CollectorTab => IsChinese ? "采集器" : "Collector";
    public static string DiagnosticsTab => IsChinese ? "诊断" : "Diagnostics";

    // General Tab
    public static string LanguageLabel => IsChinese ? "语言" : "Language";
    public static string ThemeLabel => IsChinese ? "主题" : "Theme";
    public static string StartWithWindowsLabel => IsChinese ? "开机自动启动" : "Start with Windows";
    public static string LanguageSystem => IsChinese ? "跟随系统" : "System";
    public static string LanguageEnglish => "English";
    public static string LanguageChinese => "简体中文";
    public static string ThemeSystem => IsChinese ? "跟随系统" : "System";
    public static string ThemeLight => IsChinese ? "浅色" : "Light";
    public static string ThemeDark => IsChinese ? "深色" : "Dark";

    // Monitor Tab
    public static string AutoHideLabel => IsChinese ? "鼠标离开自动收起" : "Auto-Hide on Pointer Leave";
    public static string PinnedLabel => IsChinese ? "默认固定面板" : "Pin Panel by Default";
    public static string CapsuleWidthLabel => IsChinese ? "胶囊宽度 (DIP)" : "Capsule Width (DIP)";
    public static string CapsuleHeightLabel => IsChinese ? "胶囊高度 (DIP)" : "Capsule Height (DIP)";
    public static string HoverDelayLabel => IsChinese ? "展开悬停延迟 (ms)" : "Hover Reveal Delay (ms)";
    public static string DismissDelayLabel => IsChinese ? "收起延迟 (ms)" : "Dismiss Delay (ms)";
    public static string PanelOpacityLabel => IsChinese ? "面板背景不透明度" : "Panel Background Opacity";
    public static string BlurStrengthLabel => IsChinese ? "亚克力模糊强度" : "Acrylic Blur Strength";
    public static string TopmostYieldLabel => IsChinese ? "全屏应用时让出置顶" : "Yield Topmost on Fullscreen";
    public static string RateUnitsLabel => IsChinese ? "速率显示单位" : "Rate Display Units";

    // Ranking Tab
    public static string TopNLabel => IsChinese ? "显示应用数量 (Top N)" : "Top N Application Count";

    // Collector & Diagnostics Tab
    public static string ConnectionStateLabel => IsChinese ? "连接状态" : "Connection State";
    public static string CollectorPidLabel => IsChinese ? "采集器 PID" : "Collector PID";
    public static string InstanceIdLabel => IsChinese ? "实例 ID" : "Instance ID";
    public static string ElevationStatusLabel => IsChinese ? "权限状态" : "Elevation Status";
    public static string EtwHealthLabel => IsChinese ? "ETW 会话状态" : "ETW Session Health";
    public static string DroppedPacketsLabel => IsChinese ? "丢包数" : "Dropped Packets";
    public static string ActivePidsLabel => IsChinese ? "活跃进程数" : "Active Process Count";
    public static string AppVersionLabel => IsChinese ? "应用版本" : "App Version";
    public static string CollectorVersionLabel => IsChinese ? "采集器版本" : "Collector Version";
    public static string ProtocolVersionLabel => IsChinese ? "IPC 协议版本" : "Protocol Version";
    public static string ConfigPathLabel => IsChinese ? "配置文件路径" : "Config File Path";
}
