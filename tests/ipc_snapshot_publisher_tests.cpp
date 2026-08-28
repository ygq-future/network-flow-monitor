#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include <windows.h>

#include "support/current_logon_sid.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

int main() {
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
