#include "network_flow_monitor/ipc/ipc_server.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

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

int main() {
    TestContext context;
    CheckHandshakeAndStreaming(context);
    if (context.failures == 0) {
        std::cout << "ipc server tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
