#include "network_flow_monitor/ipc/ipc_client.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

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

int main() {
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
