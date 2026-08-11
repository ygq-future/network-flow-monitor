#include "network_flow_monitor/ipc/ipc_server.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using nfm::ipc::ApplicationCounterWire;
using nfm::ipc::ApplicationHandle;
using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::CatalogEntry;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::CollectorInstanceId;
using nfm::ipc::EncodeClientHello;
using nfm::ipc::EncodeSubscribe;
using nfm::ipc::FrameParseResult;
using nfm::ipc::FrameView;
using nfm::ipc::IpcServer;
using nfm::ipc::MessageDirection;
using nfm::ipc::MessageType;
using nfm::ipc::NamedPipeConnection;
using nfm::ipc::PipeIoResult;
using nfm::ipc::PublishedSnapshot;
using nfm::ipc::SnapshotMessage;
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

std::vector<std::byte> CurrentUserSid() {
    HANDLE raw_token{};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
        return {};
    }
    const auto token = raw_token;
    DWORD required{};
    static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0U, &required));
    if (required == 0U) {
        static_cast<void>(CloseHandle(token));
        return {};
    }
    std::vector<std::byte> storage(required);
    if (GetTokenInformation(token, TokenUser, storage.data(), required, &required) == FALSE) {
        static_cast<void>(CloseHandle(token));
        return {};
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    const auto sid = CopySid(user->User.Sid);
    static_cast<void>(CloseHandle(token));
    return sid;
}

AuthorizedPrincipal ServicePrincipal() {
    return {.mode = CollectorEndpointMode::Service,
            .authorized_sid = CurrentUserSid(),
            .server_logon_sid = {}};
}

CollectorInstanceId Instance() {
    CollectorInstanceId instance;
    for (std::size_t index = 0U; index < instance.bytes.size(); ++index) {
        instance.bytes.at(index) = static_cast<std::byte>(index + 1U);
    }
    return instance;
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
    auto payload = std::span{wire}.subspan(nfm::ipc::kFrameHeaderSize);
    result = connection.BeginReadExact(payload);
    if (!result.ok() && !result.pending) {
        return false;
    }
    if (result.pending && !WaitOperation(
                              connection.read_event(), 5000U,
                              [&connection] { return connection.ContinueReadExact(); }, result)) {
        return false;
    }
    return nfm::ipc::TryDecodeFrame(wire).status == nfm::ipc::FrameParseStatus::Decoded;
}

std::shared_ptr<const PublishedSnapshot> Snapshot() {
    auto result = std::make_shared<PublishedSnapshot>();
    result->message = SnapshotMessage{
        .snapshot_sequence = 1U,
        .required_catalog_sequence = 1U,
        .uptime_microseconds = 1U,
        .applications_aggregated_to_other = false,
        .collector_health = {},
        .ipc_health = {},
        .applications = {ApplicationCounterWire{
            .handle = ApplicationHandle{1U}, .upload_bytes = 1U, .download_bytes = 2U}}};
    result->latest_changes = {
        CatalogEntry{.handle = ApplicationHandle{1U},
                     .metadata_revision = 1U,
                     .removed = false,
                     .metadata = {.display_name = "Integration", .icon_key = {}}}};
    result->full_catalog = result->latest_changes;
    return result;
}

void CheckIntegration(TestContext& context) {
    const auto principal = ServicePrincipal();
    context.Check(!principal.authorized_sid.empty(), "service principal SID is available");
    if (principal.authorized_sid.empty()) {
        return;
    }
    WindowsPipeTransportFactory transport;
    IpcServer server{Instance(), principal, transport};
    context.Check(server.Start().ok(), "real IPC server starts");
    auto client_result = transport.CreateClient(principal);
    context.Check(client_result.ok(), "real IPC client opens owner-scoped pipe");
    if (!client_result.ok()) {
        static_cast<void>(server.Stop());
        return;
    }
    auto& client = *client_result.connection;
    const auto hello = EncodeClientHello(
        {.minimum_version = 1U, .maximum_version = 1U, .requested_refresh = 500ms}, 41U);
    context.Check(WriteWire(client, hello.bytes), "client sends Hello");

    std::vector<std::byte> wire;
    const auto first_read = ReadWire(client, wire);
    context.Check(first_read, "client receives ServerHello");
    auto parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        const auto decoded = nfm::ipc::DecodeControlMessage(
            FrameView{.header = parsed.frame->header, .payload = parsed.frame->payload},
            MessageDirection::ServerToClient);
        context.Check(decoded.ok() && parsed.frame->header.message_type == MessageType::ServerHello,
                      "real ServerHello is valid");
    }
    context.Check(ReadWire(client, wire), "client receives initial subscription acceptance");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::SubscriptionAccepted,
                      "real subscription acceptance is sent");
    }

    context.Check(server.Publish(Snapshot()).ok(), "real server publishes a snapshot");
    const auto catalog_read = ReadWire(client, wire);
    context.Check(catalog_read, "client receives catalog before snapshot");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::ApplicationCatalog,
                      "real server sends catalog first");
    }
    context.Check(ReadWire(client, wire), "client receives snapshot after catalog");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::Snapshot,
                      "real server sends snapshot second");
    }

    const auto update = EncodeSubscribe({.requested_refresh = 1s}, 42U);
    context.Check(WriteWire(client, update.bytes), "client sends subscription update");
    context.Check(ReadWire(client, wire), "client receives subscription update reply");
    parsed = nfm::ipc::TryDecodeFrame(wire);
    if (parsed.frame.has_value()) {
        context.Check(parsed.frame->header.message_type == MessageType::SubscriptionAccepted &&
                          parsed.frame->header.request_id == 42U,
                      "real subscription reply echoes request id");
    }

    const auto health = server.health();
    context.Check(health.accepted_connections >= 1U && health.frames_received >= 2U &&
                      health.frames_sent >= 4U,
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
