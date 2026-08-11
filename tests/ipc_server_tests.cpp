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
using nfm::ipc::ApplicationCounterWire;
using nfm::ipc::ApplicationHandle;
using nfm::ipc::CatalogEntry;
using nfm::ipc::ClientSessionProtocol;
using nfm::ipc::ClientSessionState;
using nfm::ipc::CollectorInstanceId;
using nfm::ipc::EncodeClientHello;
using nfm::ipc::EncodeServerHello;
using nfm::ipc::EncodeSubscribe;
using nfm::ipc::FrameView;
using nfm::ipc::MessageType;
using nfm::ipc::PublishedSnapshot;
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

FrameView View(const std::vector<std::byte>& bytes, nfm::ipc::FrameParseResult& parsed) {
    parsed = nfm::ipc::TryDecodeFrame(bytes);
    if (!parsed.frame.has_value()) {
        return {};
    }
    const auto& frame = parsed.frame.value();
    return FrameView{.header = frame.header, .payload = frame.payload};
}

CatalogEntry Entry(std::uint64_t handle, std::string_view name) {
    return CatalogEntry{.handle = ApplicationHandle{handle},
                        .metadata_revision = 1U,
                        .removed = false,
                        .metadata = {.display_name = std::string{name}, .icon_key = {}}};
}

std::shared_ptr<const PublishedSnapshot> Snapshot(std::uint64_t sequence,
                                                  std::uint64_t catalog_sequence,
                                                  std::vector<CatalogEntry> changes,
                                                  std::vector<CatalogEntry> full_catalog) {
    auto result = std::make_shared<PublishedSnapshot>();
    result->message =
        SnapshotMessage{.snapshot_sequence = sequence,
                        .required_catalog_sequence = catalog_sequence,
                        .uptime_microseconds = sequence,
                        .applications_aggregated_to_other = false,
                        .collector_health = {},
                        .ipc_health = {},
                        .applications = {ApplicationCounterWire{.handle = ApplicationHandle{1U},
                                                                .upload_bytes = sequence,
                                                                .download_bytes = sequence + 1U}}};
    result->latest_changes = std::move(changes);
    result->full_catalog = std::move(full_catalog);
    return result;
}

void Drain(TestContext& context, ClientSessionProtocol& session,
           const std::vector<MessageType>& expected) {
    for (const auto type : expected) {
        const auto* frame = session.StartNextFrame();
        context.Check(frame != nullptr && frame->header.message_type == type,
                      "outbound frame ordering is deterministic");
        if (frame != nullptr) {
            context.Check(session.CompleteActiveFrame().ok(), "outbound frame completes");
        }
    }
    context.Check(session.StartNextFrame() == nullptr, "outbound mailbox is empty");
}

void CheckHandshakeAndErrors(TestContext& context) {
    static_assert(nfm::ipc::kMaximumIpcClients == 4U);
    static_assert(nfm::ipc::kHandshakeTimeout == 3s);
    static_assert(nfm::ipc::kWriteTimeout == 5s);

    ClientSessionProtocol session{7U, Instance()};
    const auto subscribe = EncodeSubscribe({.requested_refresh = 500ms}, 2U);
    nfm::ipc::FrameParseResult parsed;
    context.Check(session.Receive(View(subscribe.bytes, parsed), true).error ==
                      nfm::ipc::ClientSessionError::Protocol,
                  "subscription before Hello is rejected");
    context.Check(session.state() == ClientSessionState::Closing,
                  "protocol failure closes the candidate session");
    const auto* error_frame = session.StartNextFrame();
    context.Check(error_frame != nullptr &&
                      error_frame->header.message_type == MessageType::ProtocolError,
                  "protocol failure sends one classified error");
    if (error_frame != nullptr) {
        const auto decoded = nfm::ipc::DecodeControlMessage(
            FrameView{.header = error_frame->header, .payload = error_frame->payload},
            nfm::ipc::MessageDirection::ServerToClient);
        context.Check(decoded.ok(), "protocol error payload decodes");
        context.Check(session.CompleteActiveFrame().ok(), "protocol error completes");
    }

    ClientSessionProtocol unauthorized{8U, Instance(2U)};
    const auto hello = EncodeClientHello(
        {.minimum_version = 1U, .maximum_version = 1U, .requested_refresh = 250ms}, 11U);
    context.Check(unauthorized.Receive(View(hello.bytes, parsed), false).error ==
                      nfm::ipc::ClientSessionError::Protocol,
                  "authorization is checked after a complete Hello");
    const auto* unauthorized_error = unauthorized.StartNextFrame();
    context.Check(unauthorized_error != nullptr &&
                      unauthorized_error->header.message_type == MessageType::ProtocolError,
                  "unauthorized client receives a bounded error");

    ClientSessionProtocol mismatch{9U, Instance(3U)};
    const auto incompatible = EncodeClientHello(
        {.minimum_version = 2U, .maximum_version = 2U, .requested_refresh = 250ms}, 12U);
    context.Check(mismatch.Receive(View(incompatible.bytes, parsed), true).error ==
                      nfm::ipc::ClientSessionError::Protocol,
                  "version mismatch is rejected before streaming");

    ClientSessionProtocol valid{10U, Instance(4U)};
    context.Check(valid.Receive(View(hello.bytes, parsed), true).ok(), "valid Hello is accepted");
    const auto* server_hello = valid.StartNextFrame();
    context.Check(server_hello != nullptr &&
                      server_hello->header.message_type == MessageType::ServerHello &&
                      server_hello->header.request_id == 11U,
                  "ServerHello echoes Hello request id");
    if (server_hello != nullptr) {
        context.Check(valid.CompleteActiveFrame().ok(), "ServerHello completes");
    }
    const auto* accepted = valid.StartNextFrame();
    context.Check(accepted != nullptr &&
                      accepted->header.message_type == MessageType::SubscriptionAccepted &&
                      accepted->header.request_id == 11U,
                  "initial subscription echoes Hello request id");
    if (accepted != nullptr) {
        context.Check(valid.CompleteActiveFrame().ok(), "initial subscription completes");
    }

    const auto update = EncodeSubscribe({.requested_refresh = 1s}, 77U);
    context.Check(valid.Receive(View(update.bytes, parsed), true).ok() &&
                      valid.refresh_interval() == 1s,
                  "valid subscription update changes refresh interval");
    const auto* update_reply = valid.StartNextFrame();
    context.Check(update_reply != nullptr && update_reply->header.request_id == 77U,
                  "subscription reply echoes update request id");
    if (update_reply != nullptr) {
        static_cast<void>(valid.CompleteActiveFrame());
    }

    const auto server_to_client =
        EncodeServerHello({.selected_version = 1U,
                           .maximum_frame_payload = nfm::ipc::kMaximumFramePayload,
                           .instance = Instance(5U)},
                          91U);
    context.Check(valid.Receive(View(server_to_client.bytes, parsed), true).error ==
                      nfm::ipc::ClientSessionError::Protocol,
                  "server-only message direction is rejected");
}

void CheckCatalogMailbox(TestContext& context) {
    ClientSessionProtocol session{21U, Instance(6U)};
    const auto hello = EncodeClientHello(
        {.minimum_version = 1U, .maximum_version = 1U, .requested_refresh = 500ms}, 1U);
    nfm::ipc::FrameParseResult parsed;
    context.Check(session.Receive(View(hello.bytes, parsed), true).ok(),
                  "mailbox session handshakes");
    Drain(context, session, {MessageType::ServerHello, MessageType::SubscriptionAccepted});

    const auto first = Snapshot(1U, 1U, {Entry(1U, "A")}, {Entry(1U, "A")});
    context.Check(session.QueueSnapshot(first).ok(), "first snapshot queues");
    const auto* baseline = session.StartNextFrame();
    context.Check(baseline != nullptr &&
                      baseline->header.message_type == MessageType::ApplicationCatalog,
                  "full catalog precedes first snapshot");
    if (baseline != nullptr) {
        const auto catalog = nfm::ipc::DecodeCatalogChunk(baseline->payload);
        context.Check(catalog.ok() && catalog.chunk.has_value(),
                      "new session catalog payload decodes");
        if (catalog.chunk.has_value()) {
            context.Check(catalog.chunk->full_baseline && catalog.chunk->sequence == 1U,
                          "new session receives a full catalog baseline");
        }
        static_cast<void>(session.CompleteActiveFrame());
    }
    const auto* first_snapshot = session.StartNextFrame();
    context.Check(first_snapshot != nullptr &&
                      first_snapshot->header.message_type == MessageType::Snapshot,
                  "snapshot follows its catalog");
    context.Check(session.QueueSnapshot(Snapshot(2U, 2U, {Entry(1U, "A")}, {Entry(1U, "A")})).ok(),
                  "new snapshot replaces unsent mailbox data");
    if (first_snapshot != nullptr) {
        static_cast<void>(session.CompleteActiveFrame());
    }
    const auto* delta = session.StartNextFrame();
    context.Check(delta != nullptr && delta->header.message_type == MessageType::ApplicationCatalog,
                  "contiguous snapshot sends a delta catalog");
    if (delta != nullptr) {
        const auto catalog = nfm::ipc::DecodeCatalogChunk(delta->payload);
        context.Check(catalog.ok() && catalog.chunk.has_value(), "delta payload decodes");
        if (catalog.chunk.has_value()) {
            context.Check(!catalog.chunk->full_baseline && catalog.chunk->sequence == 2U,
                          "delta catalog has the exact next sequence");
        }
        static_cast<void>(session.CompleteActiveFrame());
    }
    const auto* second_snapshot = session.StartNextFrame();
    context.Check(second_snapshot != nullptr &&
                      second_snapshot->header.message_type == MessageType::Snapshot,
                  "delta catalog is complete before snapshot");
    context.Check(session.QueueSnapshot(Snapshot(3U, 3U, {Entry(1U, "A")}, {Entry(1U, "A")})).ok(),
                  "third snapshot queues while second is active");
    context.Check(session.QueueSnapshot(Snapshot(5U, 5U, {Entry(1U, "A")}, {Entry(1U, "A")})).ok(),
                  "latest pending snapshot replaces an older pending snapshot");
    if (second_snapshot != nullptr) {
        static_cast<void>(session.CompleteActiveFrame());
    }
    const auto* latest = session.StartNextFrame();
    context.Check(latest != nullptr &&
                      latest->header.message_type == MessageType::ApplicationCatalog,
                  "latest pending snapshot is promoted after active frame");
    if (latest != nullptr) {
        const auto catalog = nfm::ipc::DecodeCatalogChunk(latest->payload);
        context.Check(catalog.ok() && catalog.chunk.has_value(), "gap catalog payload decodes");
        if (catalog.chunk.has_value()) {
            context.Check(catalog.chunk->full_baseline && catalog.chunk->sequence == 5U,
                          "catalog gap forces a new full baseline");
        }
        static_cast<void>(session.CompleteActiveFrame());
    }
    const auto* latest_snapshot = session.StartNextFrame();
    context.Check(latest_snapshot != nullptr &&
                      latest_snapshot->header.message_type == MessageType::Snapshot,
                  "full baseline is followed by the latest snapshot");
    if (latest_snapshot != nullptr) {
        static_cast<void>(session.CompleteActiveFrame());
    }

    const auto status = std::make_shared<nfm::ipc::CollectorStatusMessage>();
    status->uptime_microseconds = 9U;
    context.Check(session.QueueStatus(status).ok(), "collector status can be pushed");
    const auto* status_frame = session.StartNextFrame();
    context.Check(status_frame != nullptr &&
                      status_frame->header.message_type == MessageType::CollectorStatus,
                  "collector status uses its own message type");
}

} // namespace

int main() {
    TestContext context;
    CheckHandshakeAndErrors(context);
    CheckCatalogMailbox(context);
    if (context.failures == 0) {
        std::cout << "ipc server tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
