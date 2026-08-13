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
using nfm::ipc::ApplicationCounterWire;
using nfm::ipc::ApplicationHandle;
using nfm::ipc::CatalogChunk;
using nfm::ipc::CatalogEntry;
using nfm::ipc::ClientSnapshot;
using nfm::ipc::CollectorInstanceId;
using nfm::ipc::EncodeCatalogChunk;
using nfm::ipc::EncodeClientHello;
using nfm::ipc::EncodeServerHello;
using nfm::ipc::EncodeSnapshot;
using nfm::ipc::EncodeSubscribe;
using nfm::ipc::EncodeSubscriptionAccepted;
using nfm::ipc::FrameParseResult;
using nfm::ipc::FrameView;
using nfm::ipc::IpcClient;
using nfm::ipc::IpcClientError;
using nfm::ipc::IpcClientObserver;
using nfm::ipc::IpcClientProtocol;
using nfm::ipc::IpcClientState;
using nfm::ipc::MessageType;
using nfm::ipc::ReconnectJitter;
using nfm::ipc::ReconnectPolicy;
using nfm::ipc::SnapshotMessage;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

CollectorInstanceId Instance(std::uint8_t seed = 1U) {
    CollectorInstanceId result;
    for (std::size_t index = 0U; index < result.bytes.size(); ++index) {
        result.bytes.at(index) = static_cast<std::byte>(seed + index);
    }
    return result;
}

CatalogEntry Entry(std::uint64_t handle, std::uint32_t revision = 1U, bool removed = false) {
    return {.handle = ApplicationHandle{handle},
            .metadata_revision = revision,
            .removed = removed,
            .metadata = removed ? nfm::process::ApplicationMetadata{}
                                : nfm::process::ApplicationMetadata{
                                      .display_name = "Test application", .icon_key = "test"}};
}

FrameView View(const std::vector<std::byte>& bytes, FrameParseResult& parsed) {
    parsed = nfm::ipc::TryDecodeFrame(bytes);
    if (!parsed.frame.has_value()) {
        return {};
    }
    return {.header = parsed.frame->header, .payload = parsed.frame->payload};
}

std::vector<std::byte> CatalogWire(const CatalogChunk& chunk) {
    const auto payload = EncodeCatalogChunk(chunk);
    if (!payload.ok()) {
        return {};
    }
    const auto frame = nfm::ipc::EncodeFrame(
        {.header = {.protocol_version = nfm::ipc::kCurrentProtocolVersion,
                    .message_type = MessageType::ApplicationCatalog,
                    .request_id = 0U,
                    .payload_length = static_cast<std::uint32_t>(payload.payload.size())},
         .payload = payload.payload});
    return frame.bytes;
}

std::vector<std::byte> SnapshotWire(std::uint64_t sequence, std::uint64_t uptime,
                                    std::uint64_t catalog_sequence,
                                    std::vector<ApplicationCounterWire> applications) {
    const auto payload =
        EncodeSnapshot(SnapshotMessage{.snapshot_sequence = sequence,
                                       .required_catalog_sequence = catalog_sequence,
                                       .uptime_microseconds = uptime,
                                       .applications_aggregated_to_other = false,
                                       .collector_health = {},
                                       .ipc_health = {},
                                       .applications = std::move(applications)});
    if (!payload.ok()) {
        return {};
    }
    const auto frame = nfm::ipc::EncodeFrame(
        {.header = {.protocol_version = nfm::ipc::kCurrentProtocolVersion,
                    .message_type = MessageType::Snapshot,
                    .request_id = 0U,
                    .payload_length = static_cast<std::uint32_t>(payload.payload.size())},
         .payload = payload.payload});
    return frame.bytes;
}

bool StartStreaming(TestContext& context, IpcClientProtocol& client, std::uint64_t request_id,
                    std::uint8_t instance_seed) {
    FrameParseResult parsed;
    const auto hello = client.BeginHello(request_id);
    const auto server_hello =
        EncodeServerHello({.selected_version = 1U,
                           .maximum_frame_payload = nfm::ipc::kMaximumFramePayload,
                           .instance = Instance(instance_seed)},
                          request_id);
    const auto subscribe = client.Receive(View(server_hello.bytes, parsed));
    const auto accepted = EncodeSubscriptionAccepted({.effective_refresh = 250ms}, request_id + 1U);
    const auto accepted_result = client.Receive(View(accepted.bytes, parsed));
    const bool success = hello.ok() && subscribe.ok() && !subscribe.outbound.empty() &&
                         accepted_result.ok() && client.state() == IpcClientState::Streaming;
    context.Check(success, "test client completes the streaming handshake");
    return success;
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
    IpcClientProtocol client{250ms};
    FrameParseResult parsed;
    const auto hello = client.BeginHello(10U);
    context.Check(hello.ok() && client.state() == IpcClientState::Handshaking,
                  "client enters handshaking after Hello");
    const auto hello_view = View(hello.outbound, parsed);
    const auto decoded_hello =
        nfm::ipc::DecodeControlMessage(hello_view, nfm::ipc::MessageDirection::ClientToServer);
    context.Check(decoded_hello.ok() && hello_view.header.request_id == 10U,
                  "Hello preserves request id");

    const auto server_hello =
        EncodeServerHello({.selected_version = 1U,
                           .maximum_frame_payload = nfm::ipc::kMaximumFramePayload,
                           .instance = Instance()},
                          10U);
    const auto first_subscribe = client.Receive(View(server_hello.bytes, parsed));
    context.Check(first_subscribe.ok() && !first_subscribe.outbound.empty(),
                  "ServerHello is accepted and produces a subscription");
    client.ResetForReconnect();
    const auto hello_again = client.BeginHello(10U);
    context.Check(hello_again.ok(), "client can start a same-instance reconnect");
    const auto server_hello_again =
        EncodeServerHello({.selected_version = 1U,
                           .maximum_frame_payload = nfm::ipc::kMaximumFramePayload,
                           .instance = Instance()},
                          10U);
    const auto subscribe = client.Receive(View(server_hello_again.bytes, parsed));
    context.Check(subscribe.ok() && !subscribe.outbound.empty(),
                  "ServerHello produces a subscription request");
    const auto subscribe_frame = View(subscribe.outbound, parsed);
    context.Check(subscribe_frame.header.request_id == 11U,
                  "subscription request id follows Hello");
    const auto accepted = EncodeSubscriptionAccepted({.effective_refresh = 250ms}, 11U);
    context.Check(client.Receive(View(accepted.bytes, parsed)).ok() &&
                      client.state() == IpcClientState::Streaming,
                  "subscription acceptance enters streaming");

    IpcClientProtocol before_catalog_client{250ms};
    context.Check(StartStreaming(context, before_catalog_client, 30U, 3U),
                  "precondition client streams");
    const auto before_catalog = before_catalog_client.Receive(View(
        SnapshotWire(
            1U, 100U, 1U,
            {ApplicationCounterWire{.handle = {1U}, .upload_bytes = 1U, .download_bytes = 1U}}),
        parsed));
    context.Check(before_catalog.error == IpcClientError::CatalogOutOfOrder,
                  "snapshot before catalog is rejected");

    const auto full = CatalogWire({.sequence = 1U,
                                   .chunk_index = 0U,
                                   .chunk_count = 1U,
                                   .full_baseline = true,
                                   .entries = {Entry(1U)}});
    const auto full_result = client.Receive(View(full, parsed));
    context.Check(full_result.ok(), "full catalog baseline is applied");
    const auto snapshot = client.Receive(View(
        SnapshotWire(
            1U, 100U, 1U,
            {ApplicationCounterWire{.handle = {1U}, .upload_bytes = 1U, .download_bytes = 2U}}),
        parsed));
    context.Check(snapshot.ok() && snapshot.snapshot.has_value(),
                  "snapshot after catalog is delivered");
    if (snapshot.snapshot.has_value()) {
        context.Check(snapshot.snapshot->counters.applications.size() == 1U &&
                          snapshot.snapshot->counters.applications.front().id.starts_with("ipc:"),
                      "wire handles become stable IPC application ids");
    }

    IpcClientProtocol unknown_client{250ms};
    context.Check(StartStreaming(context, unknown_client, 40U, 4U),
                  "unknown-handle client streams");
    context.Check(unknown_client.Receive(View(full, parsed)).ok(),
                  "unknown-handle client receives baseline");
    const auto unknown = unknown_client.Receive(View(
        SnapshotWire(
            2U, 200U, 1U,
            {ApplicationCounterWire{.handle = {99U}, .upload_bytes = 1U, .download_bytes = 1U}}),
        parsed));
    context.Check(unknown.error == IpcClientError::UnknownApplication,
                  "unknown snapshot handle is rejected");

    const auto delta = CatalogWire({.sequence = 2U,
                                    .chunk_index = 0U,
                                    .chunk_count = 1U,
                                    .full_baseline = false,
                                    .entries = {Entry(1U, 2U, true)}});
    const auto delta_result = client.Receive(View(delta, parsed));
    context.Check(delta_result.ok(), "catalog removal delta is applied");
    const auto removed_snapshot = client.Receive(View(SnapshotWire(2U, 200U, 2U, {}), parsed));
    context.Check(removed_snapshot.ok(), "snapshot after a catalog removal is accepted");

    IpcClientProtocol backward_client{250ms};
    context.Check(StartStreaming(context, backward_client, 50U, 5U),
                  "backward-uptime client streams");
    context.Check(backward_client.Receive(View(full, parsed)).ok(),
                  "backward-uptime client receives baseline");
    const auto first_backward_snapshot = backward_client.Receive(View(
        SnapshotWire(
            1U, 200U, 1U,
            {ApplicationCounterWire{.handle = {1U}, .upload_bytes = 1U, .download_bytes = 1U}}),
        parsed));
    context.Check(first_backward_snapshot.ok(), "backward-uptime client receives first snapshot");
    const auto backward = backward_client.Receive(View(SnapshotWire(2U, 150U, 1U, {}), parsed));
    context.Check(backward.error == IpcClientError::SnapshotOutOfOrder,
                  "non-increasing snapshot uptime is rejected");

    client.ResetForReconnect();
    context.Check(client.catalog_sequence() == 0U && !client.has_catalog(),
                  "same-instance reconnect requires a fresh catalog baseline");
    const auto changed_hello = client.BeginHello(20U);
    context.Check(changed_hello.ok(), "new instance handshake can restart");
}

class Observer final : public IpcClientObserver {
  public:
    void StateChanged(IpcClientState state) noexcept override {
        last_state = state;
        ++state_changes;
    }
    void SnapshotReceived(ClientSnapshot) noexcept override {
        ++snapshots;
    }

    IpcClientState last_state{IpcClientState::Disconnected};
    std::size_t state_changes{};
    std::size_t snapshots{};
};

void CheckFacade(TestContext& context) {
    Observer observer;
    IpcClient client{observer, 250ms};
    client.BeginConnecting();
    context.Check(client.state() == IpcClientState::Connecting, "facade exposes connecting state");
    context.Check(client.next_reconnect_delay() == 250ms, "facade exposes reconnect policy");
    client.TransportFailed(nfm::ipc::IpcReconnectReason::BrokenPipe);
    context.Check(client.state() == IpcClientState::Backoff,
                  "broken pipe enters recoverable backoff");
    client.Stop();
    context.Check(client.state() == IpcClientState::Disconnected && observer.state_changes >= 3U,
                  "stop returns UI to disconnected state");
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
