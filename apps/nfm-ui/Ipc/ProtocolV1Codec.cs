using System;
using System.Buffers.Binary;
using System.Text.Json;

namespace NetworkFlowMonitor.Ipc;

public static class ProtocolV1Codec {
    private static readonly JsonSerializerOptions JsonOptions = new() {
        PropertyNamingPolicy = null, // uses JsonPropertyName attributes
    };

    public static void EncodeHeader(MessageType type, uint payloadLength, Span<byte> destination) {
        if (destination.Length < ProtocolConstants.HeaderSize) {
            throw new ArgumentException("Destination buffer is too small for header", nameof(destination));
        }

        ProtocolConstants.MagicBytes.CopyTo(destination[..4]);
        BinaryPrimitives.WriteUInt16LittleEndian(destination.Slice(4, 2), ProtocolConstants.CurrentProtocolVersion);
        BinaryPrimitives.WriteUInt16LittleEndian(destination.Slice(6, 2), (ushort)type);
        BinaryPrimitives.WriteUInt32LittleEndian(destination.Slice(8, 4), payloadLength);
    }

    public static bool TryDecodeHeader(ReadOnlySpan<byte> headerBytes, out FrameHeader header, out ProtocolErrorCode errorCode) {
        header = default;
        errorCode = ProtocolErrorCode.None;

        if (headerBytes.Length < ProtocolConstants.HeaderSize) {
            return false;
        }

        if (!headerBytes[..4].SequenceEqual(ProtocolConstants.MagicBytes)) {
            errorCode = ProtocolErrorCode.MalformedHeader;
            return false;
        }

        var version = BinaryPrimitives.ReadUInt16LittleEndian(headerBytes.Slice(4, 2));
        if (version == 0 || version != ProtocolConstants.CurrentProtocolVersion) {
            errorCode = ProtocolErrorCode.VersionMismatch;
            return false;
        }

        var messageTypeValue = BinaryPrimitives.ReadUInt16LittleEndian(headerBytes.Slice(6, 2));
        if (messageTypeValue < (ushort)MessageType.Hello || messageTypeValue > (ushort)MessageType.ShutdownRequest) {
            errorCode = ProtocolErrorCode.UnknownMessageType;
            return false;
        }

        var payloadLength = BinaryPrimitives.ReadUInt32LittleEndian(headerBytes.Slice(8, 4));
        if (payloadLength > ProtocolConstants.MaxPayloadLength) {
            errorCode = ProtocolErrorCode.PayloadTooLarge;
            return false;
        }

        header = new FrameHeader(version, (MessageType)messageTypeValue, payloadLength);
        return true;
    }

    public static FrameParseStatus TryDecodeFrame(
        ReadOnlyMemory<byte> buffer,
        out FrameHeader header,
        out ReadOnlyMemory<byte> payload,
        out int requiredBytes,
        out ProtocolErrorCode errorCode) {
        header = default;
        payload = default;
        errorCode = ProtocolErrorCode.None;

        if (buffer.Length < ProtocolConstants.HeaderSize) {
            requiredBytes = ProtocolConstants.HeaderSize;
            return FrameParseStatus.NeedMoreData;
        }

        if (!TryDecodeHeader(buffer.Span[..ProtocolConstants.HeaderSize], out header, out errorCode)) {
            requiredBytes = ProtocolConstants.HeaderSize;
            return FrameParseStatus.Rejected;
        }

        var totalRequired = ProtocolConstants.HeaderSize + (int)header.PayloadLength;
        if (buffer.Length < totalRequired) {
            requiredBytes = totalRequired;
            return FrameParseStatus.NeedMoreData;
        }

        payload = buffer.Slice(ProtocolConstants.HeaderSize, (int)header.PayloadLength);
        requiredBytes = totalRequired;
        return FrameParseStatus.Decoded;
    }

    public static byte[] EncodeMessage<T>(MessageType type, T message) {
        var payloadBytes = JsonSerializer.SerializeToUtf8Bytes(message, JsonOptions);
        if (payloadBytes.Length > ProtocolConstants.MaxPayloadLength) {
            throw new InvalidOperationException($"Payload length {payloadBytes.Length} exceeds 256 KiB maximum");
        }

        var result = new byte[ProtocolConstants.HeaderSize + payloadBytes.Length];
        EncodeHeader(type, (uint)payloadBytes.Length, result);
        payloadBytes.CopyTo(result, ProtocolConstants.HeaderSize);
        return result;
    }

    public static T? DeserializePayload<T>(ReadOnlySpan<byte> payloadBytes) {
        return JsonSerializer.Deserialize<T>(payloadBytes, JsonOptions);
    }
}
