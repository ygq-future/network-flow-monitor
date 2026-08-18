using System;
using System.Text.Json.Serialization;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Services;

namespace NetworkFlowMonitor.Store;

public enum RateUnitMode {
    AutoBytes,
    AutoBits,
    BytesPerSec,
    KilobytesPerSec,
    MegabytesPerSec
}

public enum AppTheme {
    System,
    Light,
    Dark
}

public sealed record PersistedAnchorModel(
    string MonitorId = "",
    DockSide Side = DockSide.Right,
    int VerticalOffsetDip = 0
);

public sealed record GeneralConfigModel(
    AppLanguage Language = AppLanguage.System,
    AppTheme Theme = AppTheme.System,
    bool StartWithWindows = false
);

public sealed record MonitorConfigModel(
    bool AutoHideEnabled = true,
    bool Pinned = false,
    int CapsuleWidthDip = 10,
    int CapsuleHeightDip = 64,
    int HoverDelayMs = 350,
    int DismissDelayMs = 500,
    double PanelOpacity = 0.85,
    double BlurStrength = 1.0,
    bool YieldFullscreen = true,
    RateUnitMode RateUnits = RateUnitMode.AutoBytes,
    PersistedAnchorModel? Anchor = null
);

public sealed record RankingConfigModel(
    byte TopN = 5
);

public sealed record AppConfigModel(
    [property: JsonPropertyName("schema_version")] int SchemaVersion = 1,
    [property: JsonPropertyName("general")] GeneralConfigModel? General = null,
    [property: JsonPropertyName("monitor")] MonitorConfigModel? Monitor = null,
    [property: JsonPropertyName("ranking")] RankingConfigModel? Ranking = null
) {
    public static AppConfigModel Default => new(
        SchemaVersion: 1,
        General: new GeneralConfigModel(),
        Monitor: new MonitorConfigModel(),
        Ranking: new RankingConfigModel()
    );
}
