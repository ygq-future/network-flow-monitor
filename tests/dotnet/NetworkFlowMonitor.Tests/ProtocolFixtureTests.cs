using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using NetworkFlowMonitor.Ipc;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class ProtocolFixtureTests {
    private static string FindFixturesDirectory() {
        var current = AppContext.BaseDirectory;
        for (int i = 0; i < 12; i++) {
            var candidate = Path.Combine(current, "tests", "fixtures");
            if (Directory.Exists(candidate)) {
                return candidate;
            }
            var parent = Directory.GetParent(current);
            if (parent == null) break;
            current = parent.FullName;
        }
        throw new DirectoryNotFoundException("Could not find tests/fixtures directory");
    }

    [Fact]
    public void FramingVectors_AllVectors_PassValidation() {
        var fixturesDir = FindFixturesDirectory();
        var vectorsFile = Path.Combine(fixturesDir, "framing_vectors.json");
        Assert.True(File.Exists(vectorsFile), "framing_vectors.json exists");

        var json = File.ReadAllText(vectorsFile);
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.Equal("NFM1", root.GetProperty("magic_bytes_ascii").GetString());
        Assert.Equal(12, root.GetProperty("header_size_bytes").GetInt32());
        Assert.Equal(262144, root.GetProperty("max_payload_length_bytes").GetInt32());
        Assert.Equal(1, root.GetProperty("current_protocol_version").GetInt32());

        foreach (var vector in root.GetProperty("vectors").EnumerateArray()) {
            var name = vector.GetProperty("name").GetString()!;
            var isValid = vector.GetProperty("valid").GetBoolean();
            var hexWire = vector.GetProperty("hex_encoded_wire").GetString()!;
            var wireBytes = Convert.FromHexString(hexWire);

            var parseResult = ProtocolV1Codec.TryDecodeFrame(wireBytes, out var header, out var payload, out var requiredBytes, out var errorCode);

            if (isValid) {
                Assert.True(parseResult == FrameParseStatus.Decoded, $"Vector '{name}' failed with status={parseResult}, error={errorCode}, required={requiredBytes}, wireLen={wireBytes.Length}");
                Assert.Equal(ProtocolErrorCode.None, errorCode);

                var expectedHeader = vector.GetProperty("header");
                Assert.Equal(expectedHeader.GetProperty("version").GetUInt16(), header.Version);
                Assert.Equal((MessageType)expectedHeader.GetProperty("message_type").GetUInt16(), header.MessageType);
                Assert.Equal(expectedHeader.GetProperty("payload_length").GetUInt32(), header.PayloadLength);

                if (header.PayloadLength > 0) {
                    var expectedJson = vector.GetProperty("payload_json").GetString();
                    var actualJson = System.Text.Encoding.UTF8.GetString(payload.Span);
                    // Verify both represent equivalent JSON
                    using var expectedDoc = JsonDocument.Parse(expectedJson!);
                    using var actualDoc = JsonDocument.Parse(actualJson);
                    Assert.Equal(expectedDoc.RootElement.ToString(), actualDoc.RootElement.ToString());
                }
            } else {
                Assert.True(parseResult == FrameParseStatus.Rejected, $"Vector '{name}' should be rejected");
                var expectedError = vector.GetProperty("expected_error").GetString();
                Assert.Equal(expectedError, errorCode.ToString());
            }
        }
    }

    [Fact]
    public void MessageFixtures_Hello_RoundTrips() {
        var fixturesDir = FindFixturesDirectory();
        var helloFile = Path.Combine(fixturesDir, "messages", "hello.json");
        var json = File.ReadAllText(helloFile);

        var msg = ProtocolV1Codec.DeserializePayload<HelloMessage>(System.Text.Encoding.UTF8.GetBytes(json));
        Assert.NotNull(msg);
        Assert.Equal(12344, msg.ClientPid);
        Assert.Equal(1, msg.ProtocolVersion);
        Assert.Equal("NetworkFlowMonitor.Ui", msg.ClientName);

        var encoded = ProtocolV1Codec.EncodeMessage(MessageType.Hello, msg);
        Assert.True(encoded.Length > 12);
        var reDecoded = ProtocolV1Codec.DeserializePayload<HelloMessage>(encoded.AsSpan(12));
        Assert.NotNull(reDecoded);
        Assert.Equal(msg, reDecoded);
    }

    [Fact]
    public void MessageFixtures_HelloAck_RoundTrips() {
        var fixturesDir = FindFixturesDirectory();
        var file = Path.Combine(fixturesDir, "messages", "hello_ack.json");
        var json = File.ReadAllText(file);

        var msg = ProtocolV1Codec.DeserializePayload<HelloAckMessage>(System.Text.Encoding.UTF8.GetBytes(json));
        Assert.NotNull(msg);
        Assert.Equal(1, msg.ProtocolVersion);
        Assert.Equal("cinst_9f8a3c2e1b", msg.CollectorInstanceId);
        Assert.Equal(4588, msg.CollectorPid);
        Assert.Equal(3, msg.Capabilities.Length);
        Assert.Contains("traffic_tick_4hz", msg.Capabilities);
    }

    [Fact]
    public void MessageFixtures_TrafficTick_RoundTrips() {
        var fixturesDir = FindFixturesDirectory();
        var file = Path.Combine(fixturesDir, "messages", "traffic_tick.json");
        var json = File.ReadAllText(file);

        var msg = ProtocolV1Codec.DeserializePayload<TrafficTickMessage>(System.Text.Encoding.UTF8.GetBytes(json));
        Assert.NotNull(msg);
        Assert.Equal(4096UL, msg.Seq);
        Assert.Equal(83928471UL, msg.MonotonicMs);
        Assert.Equal(5242880UL, msg.DownloadBytesPerSec);
        Assert.Equal(1048576UL, msg.UploadBytesPerSec);
    }

    [Fact]
    public void MessageFixtures_RankingSnapshot_RoundTrips() {
        var fixturesDir = FindFixturesDirectory();
        var file = Path.Combine(fixturesDir, "messages", "ranking_snapshot.json");
        var json = File.ReadAllText(file);

        var msg = ProtocolV1Codec.DeserializePayload<RankingSnapshotMessage>(System.Text.Encoding.UTF8.GetBytes(json));
        Assert.NotNull(msg);
        Assert.Equal(1024UL, msg.Seq);
        Assert.Equal("Ready", msg.Status);
        Assert.Equal(5242880UL, msg.TotalRate.DownloadBytesPerSec);
        Assert.Single(msg.Applications);
        Assert.Equal("app_chrome", msg.Applications[0].Id);
        Assert.Equal("Google Chrome", msg.Applications[0].DisplayName);
    }

    [Fact]
    public void MessageFixtures_CollectorStatus_RoundTrips() {
        var fixturesDir = FindFixturesDirectory();
        var file = Path.Combine(fixturesDir, "messages", "collector_status.json");
        var json = File.ReadAllText(file);

        var msg = ProtocolV1Codec.DeserializePayload<CollectorStatusMessage>(System.Text.Encoding.UTF8.GetBytes(json));
        Assert.NotNull(msg);
        Assert.True(msg.EtwSessionActive);
        Assert.Equal(42U, msg.ActivePids);
    }

    [Fact]
    public void MessageFixtures_ShutdownRequest_RoundTrips() {
        var fixturesDir = FindFixturesDirectory();
        var file = Path.Combine(fixturesDir, "messages", "shutdown_request.json");
        var json = File.ReadAllText(file);

        var msg = ProtocolV1Codec.DeserializePayload<ShutdownRequestMessage>(System.Text.Encoding.UTF8.GetBytes(json));
        Assert.NotNull(msg);
        Assert.Equal("ui_exit", msg.Reason);
    }
}
