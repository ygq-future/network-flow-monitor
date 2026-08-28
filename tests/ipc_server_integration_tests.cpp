#include "network_flow_monitor/ipc/ipc_server.h"

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

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    TestContext context;
    CheckIntegration(context);
    if (context.failures == 0) {
        std::cout << "ipc server integration tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
