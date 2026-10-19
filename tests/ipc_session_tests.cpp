#include "network_flow_monitor/ipc/ipc_client.h"
#include "network_flow_monitor/ipc/ipc_server.h"
#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include <windows.h>

#include "support/current_logon_sid.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// === ipc_server_suite ===
namespace ipc_server_suite {
namespace {

using namespace std::chrono_literals;
using nfm::ipc::ClientSessionProtocol;
using nfm::ipc::ClientSessionState;
using nfm::ipc::DecodeHelloAck;
using nfm::ipc::EncodeHello;
using nfm::ipc::EncodeShutdownRequest;
using nfm::ipc::EncodeTrafficTick;
using nfm::ipc::FrameParseResult;
using nfm::ipc::FrameView;
using nfm::ipc::MessageType;
using nfm::ipc::TrafficTickMessage;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

FrameView View(const std::vector<std::byte>& bytes, FrameParseResult& parsed) {
    parsed = nfm::ipc::TryDecodeFrame(bytes);
    if (!parsed.frame.has_value()) {
        return {};
    }
    return {.header = parsed.frame->header, .payload = parsed.frame->payload};
}

void CheckHandshakeAndStreaming(TestContext& context) {
    static_assert(nfm::ipc::kMaximumIpcClients == 4U);
    static_assert(nfm::ipc::kHandshakeTimeout == 3s);
    static_assert(nfm::ipc::kWriteTimeout == 5s);

    ClientSessionProtocol session{7U, "cinst_test1"};
    FrameParseResult parsed;

    // Reject message before Hello
    const auto tick = EncodeTrafficTick({.seq = 1U, .monotonic_ms = 100U});
    context.Check(session.Receive(View(tick.bytes, parsed), true, 12344U).error ==
                      nfm::ipc::ClientSessionError::Protocol,
                  "tick before Hello is rejected");
    context.Check(session.state() == ClientSessionState::Closing,
                  "protocol failure closes candidate session");

    // Unauthorized client
    ClientSessionProtocol unauthorized{8U, "cinst_test2"};
    const auto hello =
        EncodeHello({.client_pid = 12344U, .protocol_version = 1U, .client_name = "TestUi"});
    context.Check(unauthorized.Receive(View(hello.bytes, parsed), false, 12344U).error ==
                      nfm::ipc::ClientSessionError::Unauthorized,
                  "unauthorized client is rejected");

    // PID mismatch rejection
    ClientSessionProtocol pid_mismatch{9U, "cinst_test3"};
    context.Check(pid_mismatch.Receive(View(hello.bytes, parsed), true, 99999U).error ==
                      nfm::ipc::ClientSessionError::Unauthorized,
                  "claimed PID mismatch with OS verified PID is rejected");

    // Version mismatch
    ClientSessionProtocol ver_mismatch{10U, "cinst_test4"};
    const auto bad_ver_hello =
        EncodeHello({.client_pid = 12344U, .protocol_version = 2U, .client_name = "TestUi"});
    context.Check(ver_mismatch.Receive(View(bad_ver_hello.bytes, parsed), true, 12344U).error ==
                      nfm::ipc::ClientSessionError::Protocol,
                  "version mismatch is rejected");

    // Valid handshake
    ClientSessionProtocol valid{11U, "cinst_valid"};
    const auto handshake_res = valid.Receive(View(hello.bytes, parsed), true, 12344U);
    context.Check(handshake_res.ok(), "valid Hello handshake succeeds");
    context.Check(valid.state() == ClientSessionState::Streaming,
                  "session transitions to Streaming");

    // Server should have queued HelloAck
    const auto* ack_frame = valid.StartNextFrame();
    context.Check(ack_frame != nullptr && ack_frame->header.message_type == MessageType::HelloAck,
                  "HelloAck is queued in response to Hello");
    if (ack_frame != nullptr) {
        const auto decoded_ack = DecodeHelloAck(ack_frame->payload);
        context.Check(decoded_ack.ok() && decoded_ack.message.has_value() &&
                          decoded_ack.message->collector_instance_id == "cinst_valid" &&
                          decoded_ack.message->protocol_version == 1U,
                      "HelloAck payload fields match");
        context.Check(valid.CompleteActiveFrame().ok(), "HelloAck completes");
    }

    // Streaming ticks
    const TrafficTickMessage tick_msg{
        .seq = 100U,
        .monotonic_ms = 5000U,
        .download_bytes_per_sec = 1024U,
        .upload_bytes_per_sec = 512U,
    };
    context.Check(valid.QueueTrafficTick(tick_msg).ok(), "TrafficTick queues in streaming state");
    const auto* tick_frame = valid.StartNextFrame();
    context.Check(tick_frame != nullptr &&
                      tick_frame->header.message_type == MessageType::TrafficTick,
                  "TrafficTick is popped from mailbox");
    if (tick_frame != nullptr) {
        const auto decoded_tick = nfm::ipc::DecodeTrafficTick(tick_frame->payload);
        context.Check(decoded_tick.ok() && decoded_tick.message.has_value() &&
                          *decoded_tick.message == tick_msg,
                      "TrafficTick decoded values match exactly");
        context.Check(valid.CompleteActiveFrame().ok(), "TrafficTick completes");
    }

    // Shutdown request
    const auto shutdown = EncodeShutdownRequest({.reason = "ui_exit"});
    context.Check(valid.Receive(View(shutdown.bytes, parsed), true, 12344U).ok(),
                  "ShutdownRequest received smoothly");
    context.Check(valid.state() == ClientSessionState::Closing,
                  "ShutdownRequest transitions session to Closing");
}

} // namespace

int Run() {
    TestContext context;
    CheckHandshakeAndStreaming(context);
    if (context.failures == 0) {
        std::cout << "ipc server tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
} // namespace ipc_server_suite

// === ipc_server_integration_suite ===
namespace ipc_server_integration_suite {
namespace {

using namespace std::chrono_literals;
using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::DecodeHelloAck;
using nfm::ipc::DecodeTrafficTick;
using nfm::ipc::EncodeHello;
using nfm::ipc::IpcServer;
using nfm::ipc::MessageType;
using nfm::ipc::NamedPipeConnection;
using nfm::ipc::PipeIoResult;
using nfm::ipc::TrafficTickMessage;
using nfm::ipc::WindowsPipeTransportFactory;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

std::vector<std::byte> CopySid(PSID sid) {
    if (sid == nullptr || IsValidSid(sid) == FALSE) {
        return {};
    }
    const auto length = GetLengthSid(sid);
    std::vector<std::byte> result(length);
    if (::CopySid(length, result.data(), sid) == FALSE) {
        return {};
    }
    return result;
}

AuthorizedPrincipal PortablePrincipal() {
    const auto sid = nfm::test::CurrentLogonSid();
    return {
        .mode = CollectorEndpointMode::Portable, .authorized_sid = sid, .server_logon_sid = sid};
}

template <typename Completion>
bool WaitOperation(HANDLE completion_event, DWORD timeout, Completion&& complete,
                   PipeIoResult& result) {
    if (WaitForSingleObject(completion_event, timeout) != WAIT_OBJECT_0) {
        return false;
    }
    result = std::forward<Completion>(complete)();
    return result.ok();
}

bool WriteWire(NamedPipeConnection& connection, std::span<const std::byte> wire) {
    auto result = connection.BeginWriteAll(wire);
    if (!result.ok() && !result.pending) {
        return false;
    }
    if (result.pending) {
        if (!WaitOperation(
                connection.write_event(), 5000U,
                [&connection] { return connection.ContinueWriteAll(); }, result)) {
            static_cast<void>(connection.CancelAndDrain());
            return false;
        }
    }
    return result.ok();
}

bool ReadWire(NamedPipeConnection& connection, std::vector<std::byte>& wire) {
    std::array<std::byte, nfm::ipc::kFrameHeaderSize> header{};
    auto result = connection.BeginReadExact(header);
    if (!result.ok() && !result.pending) {
        return false;
    }
    if (result.pending && !WaitOperation(
                              connection.read_event(), 5000U,
                              [&connection] { return connection.ContinueReadExact(); }, result)) {
        return false;
    }
    const auto parsed_header = nfm::ipc::TryDecodeFrame(header);
    if (parsed_header.status != nfm::ipc::FrameParseStatus::NeedMoreData ||
        parsed_header.required > nfm::ipc::kFrameHeaderSize + nfm::ipc::kMaximumFramePayload) {
        return false;
    }
    wire.assign(parsed_header.required, std::byte{});
    std::ranges::copy(header, wire.begin());
    const auto payload_len = parsed_header.required - nfm::ipc::kFrameHeaderSize;
    if (payload_len > 0U) {
        auto payload = std::span{wire}.subspan(nfm::ipc::kFrameHeaderSize);
        result = connection.BeginReadExact(payload);
        if (!result.ok() && !result.pending) {
            return false;
        }
        if (result.pending &&
            !WaitOperation(
                connection.read_event(), 5000U,
                [&connection] { return connection.ContinueReadExact(); }, result)) {
            return false;
        }
    }
    return nfm::ipc::TryDecodeFrame(wire).status == nfm::ipc::FrameParseStatus::Decoded;
}

void CheckIntegration(TestContext& context) {
    const auto principal = PortablePrincipal();
    context.Check(!principal.authorized_sid.empty(), "portable principal SID is available");
    if (principal.authorized_sid.empty()) {
        return;
    }
    WindowsPipeTransportFactory transport;
    IpcServer server{"cinst_integ", principal, transport};
    context.Check(server.Start().ok(), "real IPC server starts");
    auto client_result = transport.CreateClient(principal);
    context.Check(client_result.ok(), "real IPC client opens owner-scoped pipe");
    if (!client_result.ok()) {
        static_cast<void>(server.Stop());
        return;
    }
    auto& client = *client_result.connection;
    const auto hello = EncodeHello({.client_pid = static_cast<std::uint32_t>(GetCurrentProcessId()),
                                    .protocol_version = 1U,
                                    .client_name = "IntegrationClient"});
    context.Check(WriteWire(client, hello.bytes), "client sends Hello");

    std::vector<std::byte> wire;
    const auto first_read = ReadWire(client, wire);
    context.Check(first_read, "client receives HelloAck");
    auto parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::HelloAck,
                      "real HelloAck message type is valid");
        const auto ack = DecodeHelloAck(parsed.frame->payload);
        context.Check(ack.ok() && ack.message.has_value() &&
                          ack.message->collector_instance_id == "cinst_integ",
                      "HelloAck collector instance id matches");
    }

    const TrafficTickMessage tick{
        .seq = 4096U,
        .monotonic_ms = 123456U,
        .download_bytes_per_sec = 5242880U,
        .upload_bytes_per_sec = 1048576U,
    };
    context.Check(server.PublishTrafficTick(tick).ok(), "server publishes TrafficTick");
    const auto tick_read = ReadWire(client, wire);
    context.Check(tick_read, "client receives TrafficTick");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::TrafficTick,
                      "real server sends TrafficTick");
        const auto decoded_tick = DecodeTrafficTick(parsed.frame->payload);
        context.Check(decoded_tick.ok() && decoded_tick.message.has_value() &&
                          *decoded_tick.message == tick,
                      "TrafficTick matches published values");
    }

    const auto health = server.health();
    context.Check(health.accepted_connections >= 1U && health.frames_received >= 1U &&
                      health.frames_sent >= 2U,
                  "real server records bounded session health");
    context.Check(server.Stop().ok() && server.Stop().ok(), "Stop is idempotent");
    static_cast<void>(client.CancelAndDrain());
}

} // namespace

int Run() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    TestContext context;
    CheckIntegration(context);
    if (context.failures == 0) {
        std::cout << "ipc server integration tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
} // namespace ipc_server_integration_suite

// === ipc_client_suite ===
namespace ipc_client_suite {
namespace {

using namespace std::chrono_literals;
using nfm::ipc::CollectorStatusMessage;
using nfm::ipc::EncodeHelloAck;
using nfm::ipc::EncodeTrafficTick;
using nfm::ipc::FrameParseResult;
using nfm::ipc::FrameView;
using nfm::ipc::HelloAckMessage;
using nfm::ipc::IpcClient;
using nfm::ipc::IpcClientObserver;
using nfm::ipc::IpcClientProtocol;
using nfm::ipc::IpcClientState;
using nfm::ipc::MessageType;
using nfm::ipc::RankingSnapshotMessage;
using nfm::ipc::ReconnectJitter;
using nfm::ipc::ReconnectPolicy;
using nfm::ipc::TrafficTickMessage;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

FrameView View(const std::vector<std::byte>& bytes, FrameParseResult& parsed) {
    parsed = nfm::ipc::TryDecodeFrame(bytes);
    if (!parsed.frame.has_value()) {
        return {};
    }
    return {.header = parsed.frame->header, .payload = parsed.frame->payload};
}

void CheckReconnectPolicy(TestContext& context) {
    ReconnectPolicy policy;
    context.Check(policy.NextDelay() == 250ms, "first reconnect delay is 250 ms");
    context.Check(policy.NextDelay() == 500ms, "second reconnect delay is 500 ms");
    context.Check(policy.NextDelay() == 1s, "third reconnect delay is 1 second");
    context.Check(policy.NextDelay() == 2s, "fourth reconnect delay is 2 seconds");
    context.Check(policy.NextDelay() == 5s && policy.NextDelay() == 5s,
                  "reconnect delay is capped at 5 seconds");
    policy.Reset();
    context.Check(policy.attempt() == 0U && policy.NextDelay() == 250ms,
                  "successful streaming resets reconnect attempts");

    class FixedJitter final : public ReconnectJitter {
      public:
        std::chrono::milliseconds Adjust(std::chrono::milliseconds base,
                                         std::size_t attempt) noexcept override {
            return base + std::chrono::milliseconds{static_cast<std::int64_t>(attempt)};
        }
    } jitter;
    ReconnectPolicy jittered{&jitter};
    context.Check(jittered.NextDelay() == 250ms && jittered.NextDelay() == 501ms,
                  "injected jitter is deterministic");
}

void CheckProtocol(TestContext& context) {
    IpcClientProtocol client;
    FrameParseResult parsed;

    const auto hello = client.BeginHello(12344U, "NetworkFlowMonitor.Ui");
    context.Check(hello.ok() && client.state() == IpcClientState::Handshaking,
                  "client enters Handshaking after Hello");

    const auto hello_view = View(hello.outbound, parsed);
    const auto decoded_hello = nfm::ipc::DecodeHello(hello_view.payload);
    context.Check(decoded_hello.ok() && decoded_hello.message.has_value() &&
                      decoded_hello.message->client_pid == 12344U &&
                      decoded_hello.message->client_name == "NetworkFlowMonitor.Ui",
                  "Hello frame encodes properly");

    // Receive HelloAck
    const HelloAckMessage ack{
        .protocol_version = 1U,
        .collector_instance_id = "cinst_12345",
        .collector_pid = 4588U,
        .capabilities = {"etw_live_counters", "traffic_tick_4hz"},
    };
    const auto encoded_ack = EncodeHelloAck(ack);
    const auto ack_res = client.Receive(View(encoded_ack.bytes, parsed));
    context.Check(ack_res.ok() && client.state() == IpcClientState::Streaming,
                  "HelloAck transitions client to Streaming");
    context.Check(client.collector_instance_id() == "cinst_12345",
                  "collector instance ID is saved");

    // In Streaming, receive TrafficTick
    const TrafficTickMessage tick{
        .seq = 101U,
        .monotonic_ms = 83928471U,
        .download_bytes_per_sec = 5242880U,
        .upload_bytes_per_sec = 1048576U,
    };
    const auto encoded_tick = EncodeTrafficTick(tick);
    const auto tick_res = client.Receive(View(encoded_tick.bytes, parsed));
    context.Check(tick_res.ok() && tick_res.message.has_value(), "TrafficTick is received");
    if (tick_res.message.has_value()) {
        const auto* received_tick = std::get_if<TrafficTickMessage>(&*tick_res.message);
        context.Check(received_tick != nullptr && *received_tick == tick,
                      "TrafficTick fields match source");
    }

    // Send Shutdown
    const auto shutdown = client.SendShutdown("user_quit");
    context.Check(shutdown.ok() && client.state() == IpcClientState::Disconnected,
                  "SendShutdown transitions to Disconnected");
}

class Observer final : public IpcClientObserver {
  public:
    void StateChanged(IpcClientState state) noexcept override {
        last_state = state;
        ++state_changes;
    }
    void HelloAckReceived(HelloAckMessage ack) noexcept override {
        last_ack = ack;
        ++acks;
    }
    void TrafficTickReceived(TrafficTickMessage tick) noexcept override {
        last_tick = tick;
        ++ticks;
    }
    void RankingSnapshotReceived(RankingSnapshotMessage /*snapshot*/) noexcept override {
        ++snapshots;
    }
    void CollectorStatusReceived(CollectorStatusMessage /*status*/) noexcept override {
        ++statuses;
    }

    IpcClientState last_state{IpcClientState::Disconnected};
    HelloAckMessage last_ack;
    TrafficTickMessage last_tick;
    std::size_t state_changes{};
    std::size_t acks{};
    std::size_t ticks{};
    std::size_t snapshots{};
    std::size_t statuses{};
};

void CheckFacade(TestContext& context) {
    Observer observer;
    IpcClient client{observer};
    client.BeginConnecting();
    context.Check(client.state() == IpcClientState::Connecting, "facade exposes connecting state");
    context.Check(client.next_reconnect_delay() == 250ms, "facade exposes reconnect policy");
    client.TransportFailed(nfm::ipc::IpcReconnectReason::BrokenPipe);
    context.Check(client.state() == IpcClientState::Backoff,
                  "broken pipe enters recoverable backoff");
    client.Stop();
    context.Check(client.state() == IpcClientState::Disconnected && observer.state_changes >= 3U,
                  "stop returns client to disconnected state");
}

} // namespace

int Run() {
    TestContext context;
    CheckReconnectPolicy(context);
    CheckProtocol(context);
    CheckFacade(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " IPC client test failure(s)\n";
        return 1;
    }
    std::cout << "ipc client tests passed\n";
    return 0;
}
} // namespace ipc_client_suite

// === ipc_snapshot_publisher_suite ===
namespace ipc_snapshot_publisher_suite {
namespace {

using namespace std::chrono_literals;
using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::CaptureError;
using nfm::ipc::CaptureResult;
using nfm::ipc::CollectorCapture;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::DecodeCollectorStatus;
using nfm::ipc::DecodeHelloAck;
using nfm::ipc::DecodeRankingSnapshot;
using nfm::ipc::DecodeTrafficTick;
using nfm::ipc::EncodeHello;
using nfm::ipc::HelloMessage;
using nfm::ipc::IpcServer;
using nfm::ipc::MessageType;
using nfm::ipc::NamedPipeConnection;
using nfm::ipc::PipeIoResult;
using nfm::ipc::PublisherError;
using nfm::ipc::SnapshotProvider;
using nfm::ipc::SnapshotPublisher;
using nfm::ipc::WindowsPipeTransportFactory;
using nfm::ranking::ApplicationCounters;
using nfm::ranking::CounterSnapshot;
using nfm::ranking::RankingOptions;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

template <typename Completion>
bool WaitOperation(HANDLE completion_event, DWORD timeout, Completion&& complete,
                   PipeIoResult& result) {
    if (WaitForSingleObject(completion_event, timeout) != WAIT_OBJECT_0) {
        return false;
    }
    result = std::forward<Completion>(complete)();
    return result.ok();
}

bool WriteWire(NamedPipeConnection& connection, std::span<const std::byte> wire) {
    auto result = connection.BeginWriteAll(wire);
    if (!result.ok() && !result.pending) {
        return false;
    }
    if (result.pending) {
        if (!WaitOperation(
                connection.write_event(), 5000U,
                [&connection] { return connection.ContinueWriteAll(); }, result)) {
            static_cast<void>(connection.CancelAndDrain());
            return false;
        }
    }
    return result.ok();
}

bool ReadWire(NamedPipeConnection& connection, std::vector<std::byte>& wire) {
    std::array<std::byte, nfm::ipc::kFrameHeaderSize> header{};
    auto result = connection.BeginReadExact(header);
    if (!result.ok() && !result.pending) {
        return false;
    }
    if (result.pending && !WaitOperation(
                              connection.read_event(), 5000U,
                              [&connection] { return connection.ContinueReadExact(); }, result)) {
        return false;
    }
    const auto parsed_header = nfm::ipc::TryDecodeFrame(header);
    if (parsed_header.status != nfm::ipc::FrameParseStatus::NeedMoreData ||
        parsed_header.required > nfm::ipc::kFrameHeaderSize + nfm::ipc::kMaximumFramePayload) {
        return false;
    }
    wire.assign(parsed_header.required, std::byte{});
    std::ranges::copy(header, wire.begin());
    const auto payload_len = parsed_header.required - nfm::ipc::kFrameHeaderSize;
    if (payload_len > 0U) {
        auto payload = std::span{wire}.subspan(nfm::ipc::kFrameHeaderSize);
        result = connection.BeginReadExact(payload);
        if (!result.ok() && !result.pending) {
            return false;
        }
        if (result.pending &&
            !WaitOperation(
                connection.read_event(), 5000U,
                [&connection] { return connection.ContinueReadExact(); }, result)) {
            return false;
        }
    }
    return nfm::ipc::TryDecodeFrame(wire).status == nfm::ipc::FrameParseStatus::Decoded;
}

AuthorizedPrincipal Principal() {
    const auto sid = nfm::test::CurrentLogonSid();
    return {
        .mode = CollectorEndpointMode::Portable, .authorized_sid = sid, .server_logon_sid = sid};
}

class FakeProvider final : public SnapshotProvider {
  public:
    CaptureResult Capture() noexcept override {
        ++calls;
        if (failure != CaptureError::None) {
            return {.error = failure, .capture = CollectorCapture{.health = {}, .counters = {}}};
        }
        CounterSnapshot snapshot;
        snapshot.captured_at = capture_time;
        const auto ul =
            custom_upload.has_value() ? *custom_upload : static_cast<std::uint64_t>(calls);
        const auto dl =
            custom_download.has_value() ? *custom_download : static_cast<std::uint64_t>(calls);
        snapshot.applications.push_back(
            ApplicationCounters{.id = "app.test",
                                .metadata = {.display_name = "Test", .icon_key = "path/test.exe"},
                                .upload_bytes = ul,
                                .download_bytes = dl});
        return {.capture = CollectorCapture{.health = {}, .counters = std::move(snapshot)}};
    }

    int calls{};
    CaptureError failure{CaptureError::None};
    std::chrono::steady_clock::time_point capture_time = std::chrono::steady_clock::now();
    std::optional<std::uint64_t> custom_upload;
    std::optional<std::uint64_t> custom_download;
};

void CheckScheduler(TestContext& context) {
    WindowsPipeTransportFactory factory;
    IpcServer server{"cinst_sched", Principal(), factory};
    context.Check(server.Start().ok(), "publisher test server starts");

    const auto started = std::chrono::steady_clock::now() - 10s;
    FakeProvider provider;
    SnapshotPublisher publisher{"cinst_sched", started, provider, server};
    const auto first = started + 10s;

    context.Check(publisher.RunDue(first).ok() && provider.calls == 0,
                  "no connected clients do not capture");
    publisher.ClientConnected(
        1U, HelloMessage{.client_pid = 100U, .protocol_version = 1U, .client_name = "C1"});
    publisher.ClientConnected(
        2U, HelloMessage{.client_pid = 200U, .protocol_version = 1U, .client_name = "C2"});
    context.Check(publisher.RunDue(first).ok() && provider.calls == 1,
                  "multiple connected clients share one capture");
    context.Check(publisher.next_deadline().has_value() &&
                      *publisher.next_deadline() == first + 250ms,
                  "next deadline uses 250ms traffic tick interval");
    context.Check(publisher.RunDue(first + 50ms).ok() && provider.calls == 1,
                  "not yet due time does not trigger capture");
    provider.capture_time += 250ms;
    context.Check(publisher.RunDue(first + 250ms).ok() && provider.calls == 2,
                  "250ms tick interval triggers next capture");
    publisher.ClientDisconnected(1U);
    publisher.ClientDisconnected(2U);
    context.Check(!publisher.next_deadline().has_value(),
                  "disconnecting all clients clears deadline");
    context.Check(server.Stop().ok(), "publisher test server stops");
}

void CheckFailures(TestContext& context) {
    WindowsPipeTransportFactory factory;
    IpcServer server{"cinst_fail", Principal(), factory};
    context.Check(server.Start().ok(), "failure test server starts");
    const auto started = std::chrono::steady_clock::now() - 10s;
    FakeProvider provider;
    SnapshotPublisher publisher{"cinst_fail", started, provider, server};
    publisher.ClientConnected(
        1U, HelloMessage{.client_pid = 100U, .protocol_version = 1U, .client_name = "C1"});
    provider.failure = CaptureError::CollectorFailed;
    const auto now = started + 10s;
    context.Check(publisher.RunDue(now).error == PublisherError::CaptureFailed,
                  "provider failure is surfaced without a fabricated snapshot");
    context.Check(server.Stop().ok(), "failure test server stops");
}

void CheckRankingAndCollectorStatusPublishing(TestContext& context) {
    const auto principal = Principal();
    context.Check(!principal.authorized_sid.empty(), "principal SID is available");
    if (principal.authorized_sid.empty()) {
        return;
    }
    WindowsPipeTransportFactory transport;
    IpcServer server{"cinst_rank_stat", principal, transport};
    const auto started = std::chrono::steady_clock::now() - 10s;
    FakeProvider provider;
    provider.capture_time = started + 10s;
    SnapshotPublisher publisher{"cinst_rank_stat", started, provider, server,
                                RankingOptions{.window = 1000ms, .top_n = 5}};
    context.Check(server.SetSessionObserver(publisher).ok(), "observer registered on server");
    context.Check(server.Start().ok(), "ranking/status publisher test server starts");

    auto client_result = transport.CreateClient(principal);
    context.Check(client_result.ok(), "client connects to server");
    if (!client_result.ok()) {
        static_cast<void>(server.Stop());
        return;
    }
    auto& client = *client_result.connection;
    const auto hello = EncodeHello({.client_pid = static_cast<std::uint32_t>(GetCurrentProcessId()),
                                    .protocol_version = 1U,
                                    .client_name = "RankingTestClient"});
    context.Check(WriteWire(client, hello.bytes), "client sends Hello");

    std::vector<std::byte> wire;
    context.Check(ReadWire(client, wire), "client receives HelloAck");

    // First run at t0 (triggers initial tick, ranking snapshot, and collector status)
    const auto t0 = started + 10s;
    context.Check(publisher.RunDue(t0).ok(), "initial RunDue succeeds");

    // Expect TrafficTick
    context.Check(ReadWire(client, wire), "client receives initial TrafficTick");
    auto parsed = nfm::ipc::TryDecodeFrame(wire);
    context.Check(parsed.frame.has_value() &&
                      parsed.frame->header.message_type == MessageType::TrafficTick,
                  "first message is TrafficTick");

    // Expect RankingSnapshot (WarmingUp status)
    context.Check(ReadWire(client, wire), "client receives initial RankingSnapshot");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::RankingSnapshot,
                      "second message is RankingSnapshot");
        const auto snapshot = DecodeRankingSnapshot(parsed.frame->payload);
        context.Check(snapshot.ok() && snapshot.message.has_value(), "RankingSnapshot decodes");
        if (snapshot.message.has_value()) {
            context.Check(snapshot.message->status == "WarmingUp",
                          "initial ranking status is WarmingUp");
        }
    }

    // Expect CollectorStatus
    context.Check(ReadWire(client, wire), "client receives initial CollectorStatus");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::CollectorStatus,
                      "third message is CollectorStatus");
        const auto status = DecodeCollectorStatus(parsed.frame->payload);
        context.Check(status.ok() && status.message.has_value(), "CollectorStatus decodes");
        if (status.message.has_value()) {
            context.Check(status.message->etw_session_active, "CollectorStatus has etw active");
            context.Check(status.message->active_pids == 1U, "CollectorStatus has active PIDs = 1");
        }
    }

    // Advance by 1000ms: upload increases by 1,000,000 bytes, download increases by 2,000,000 bytes
    const auto t1 = t0 + 1000ms;
    provider.capture_time = t1;
    provider.custom_upload = 1'000'000U;
    provider.custom_download = 2'000'000U;
    context.Check(publisher.RunDue(t1).ok(), "t1 RunDue succeeds");

    // Expect TrafficTick
    context.Check(ReadWire(client, wire), "client receives t1 TrafficTick");

    // Expect RankingSnapshot (Ready status, calculated rate)
    context.Check(ReadWire(client, wire), "client receives t1 RankingSnapshot");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::RankingSnapshot,
                      "t1 message is RankingSnapshot");
        const auto snapshot = DecodeRankingSnapshot(parsed.frame->payload);
        context.Check(snapshot.ok() && snapshot.message.has_value(), "t1 RankingSnapshot decodes");
        if (snapshot.message.has_value()) {
            context.Check(snapshot.message->status == "Ready",
                          "t1 ranking status is Ready after window elapsed");
            context.Check(snapshot.message->applications.size() == 1U,
                          "t1 ranking has 1 application");
            if (!snapshot.message->applications.empty()) {
                const auto& app = snapshot.message->applications.front();
                context.Check(app.id == "app.test", "app id matches");
                context.Check(app.display_name == "Test", "app display_name matches");
                context.Check(app.executable_path == "path/test.exe",
                              "app executable_path matches");
                context.Check(app.upload_bytes_per_sec > 0, "app has non-zero upload rate");
                context.Check(app.download_bytes_per_sec > 0, "app has non-zero download rate");
            }
        }
    }

    // Expect CollectorStatus
    context.Check(ReadWire(client, wire), "client receives t1 CollectorStatus");

    context.Check(server.Stop().ok(), "ranking/status test server stops");
    static_cast<void>(client.CancelAndDrain());
}

} // namespace

int Run() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    TestContext context;
    CheckScheduler(context);
    CheckFailures(context);
    CheckRankingAndCollectorStatusPublishing(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " publisher test failure(s)\n";
        return 1;
    }
    std::cout << "ipc snapshot publisher tests passed\n";
    return 0;
}
} // namespace ipc_snapshot_publisher_suite

int main() {
    int failures = 0;
    failures += (ipc_server_suite::Run() != 0 ? 1 : 0);
    failures += (ipc_server_integration_suite::Run() != 0 ? 1 : 0);
    failures += (ipc_client_suite::Run() != 0 ? 1 : 0);
    failures += (ipc_snapshot_publisher_suite::Run() != 0 ? 1 : 0);
    return failures;
}
