using System;
using System.Text.Json.Serialization;

namespace NetworkFlowMonitor.Ipc;

public static class ProtocolConstants
{
    public static ReadOnlySpan<byte> MagicBytes => "NFM1"u8;
    public const int HeaderSize = 12;
    public const ushort CurrentProtocolVersion = 1;
    public const uint MaxPayloadLength = 262_144; // 256 KiB
}

public enum MessageType : ushort
{
    Hello = 1,
    HelloAck = 2,
    TrafficTick = 3,
    RankingSnapshot = 4,
    CollectorStatus = 5,
    ShutdownRequest = 6,
}

public enum ProtocolErrorCode
{
    None = 0,
    MalformedHeader = 1,
    UnsupportedHeader = 2,
    VersionMismatch = 3,
    UnknownMessageType = 4,
    PayloadTooLarge = 5,
    MalformedPayload = 6,
    InvalidUtf8 = 7,
    Unauthorized = 8,
    UnexpectedMessage = 9,
    ServerBusy = 10,
    InternalFailure = 11,
}

public enum FrameParseStatus
{
    NeedMoreData,
    Decoded,
    Rejected,
}

public readonly record struct FrameHeader(
    ushort Version,
    MessageType MessageType,
    uint PayloadLength
);

public sealed record HelloMessage(
    [property: JsonPropertyName("client_pid")] int ClientPid,
    [property: JsonPropertyName("protocol_version")] int ProtocolVersion,
    [property: JsonPropertyName("client_name")] string ClientName
);

public sealed record HelloAckMessage(
    [property: JsonPropertyName("protocol_version")] int ProtocolVersion,
    [property: JsonPropertyName("collector_instance_id")] string CollectorInstanceId,
    [property: JsonPropertyName("collector_pid")] int CollectorPid,
    [property: JsonPropertyName("capabilities")] string[] Capabilities
);

public sealed record TrafficTickMessage(
    [property: JsonPropertyName("seq")] ulong Seq,
    [property: JsonPropertyName("monotonic_ms")] ulong MonotonicMs,
    [property: JsonPropertyName("download_bytes_per_sec")] ulong DownloadBytesPerSec,
    [property: JsonPropertyName("upload_bytes_per_sec")] ulong UploadBytesPerSec
);

public sealed record TotalRate(
    [property: JsonPropertyName("download_bytes_per_sec")] ulong DownloadBytesPerSec,
    [property: JsonPropertyName("upload_bytes_per_sec")] ulong UploadBytesPerSec
);

public sealed record RankingApplication(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("executable_path")] string ExecutablePath,
    [property: JsonPropertyName("display_name")] string DisplayName,
    [property: JsonPropertyName("icon_key")] string IconKey,
    [property: JsonPropertyName("download_bytes_per_sec")] ulong DownloadBytesPerSec,
    [property: JsonPropertyName("upload_bytes_per_sec")] ulong UploadBytesPerSec
);

public sealed record RankingSnapshotMessage(
    [property: JsonPropertyName("seq")] ulong Seq,
    [property: JsonPropertyName("monotonic_ms")] ulong MonotonicMs,
    [property: JsonPropertyName("status")] string Status,
    [property: JsonPropertyName("total_rate")] TotalRate TotalRate,
    [property: JsonPropertyName("applications")] RankingApplication[] Applications
);

public sealed record CollectorStatusMessage(
    [property: JsonPropertyName("monotonic_ms")] ulong MonotonicMs,
    [property: JsonPropertyName("etw_session_active")] bool EtwSessionActive,
    [property: JsonPropertyName("dropped_packets")] ulong DroppedPackets,
    [property: JsonPropertyName("active_pids")] uint ActivePids
);

public sealed record ShutdownRequestMessage(
    [property: JsonPropertyName("reason")] string Reason
);
