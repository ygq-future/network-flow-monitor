#include "network_flow_monitor/ipc/named_pipe_transport.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::ClientPipeOpenRequest;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::NamedPipeConnection;
using nfm::ipc::OverlappedPipeOperation;
using nfm::ipc::PipeApiCallResult;
using nfm::ipc::PipeIoError;
using nfm::ipc::PipeOperationKind;
using nfm::ipc::PipeReadRequest;
using nfm::ipc::PipeTransportApi;
using nfm::ipc::PipeWriteRequest;
using nfm::ipc::ServerPipeOpenRequest;
using nfm::ipc::WindowsPipeTransportFactory;

class TestContext {
  public:
    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

struct IoAction {
    bool succeeds{true};
    DWORD error{ERROR_SUCCESS};
    DWORD transferred{};
};

IoAction SuccessfulIo(DWORD transferred = 0U) noexcept {
    return {.succeeds = true, .error = ERROR_SUCCESS, .transferred = transferred};
}

IoAction FailedIo(DWORD error) noexcept {
    return {.succeeds = false, .error = error};
}

class FakePipeTransportApi final : public PipeTransportApi {
  public:
    HANDLE CreateManualResetEvent(bool inherit_handle, std::uint32_t& error) noexcept override {
        event_inherit_values.push_back(inherit_handle);
        error = event_error;
        if (event_error != ERROR_SUCCESS) {
            return nullptr;
        }
        auto* const event = event_storage.at(event_count).data();
        ++event_count;
        return event;
    }

    bool ResetManualEvent(HANDLE event, std::uint32_t& error) noexcept override {
        reset_events.push_back(event);
        error = reset_error;
        return reset_error == ERROR_SUCCESS;
    }

    void CloseKernelHandle(HANDLE handle) noexcept override {
        closed_handles.push_back(handle);
    }

    HANDLE CreateServerPipe(const ServerPipeOpenRequest& request,
                            std::uint32_t& error) noexcept override {
        ++create_server_calls;
        server_name = request.name != nullptr ? request.name : L"";
        server_open_mode = request.open_mode;
        server_pipe_mode = request.pipe_mode;
        server_maximum_instances = request.maximum_instances;
        server_security_valid = request.security != nullptr &&
                                request.security->nLength == sizeof(SECURITY_ATTRIBUTES) &&
                                request.security->lpSecurityDescriptor != nullptr;
        error = create_server_error;
        return create_server_error == ERROR_SUCCESS ? server_pipe_storage.data()
                                                    : INVALID_HANDLE_VALUE;
    }

    HANDLE OpenClientPipe(const ClientPipeOpenRequest& request,
                          std::uint32_t& error) noexcept override {
        ++open_client_calls;
        client_name = request.name != nullptr ? request.name : L"";
        client_desired_access = request.desired_access;
        client_share_mode = request.share_mode;
        client_security = request.security;
        client_creation_disposition = request.creation_disposition;
        client_flags_and_attributes = request.flags_and_attributes;
        const auto action = Take(open_client_actions, {});
        error = action.error;
        return action.succeeds ? client_pipe_storage.data() : INVALID_HANDLE_VALUE;
    }

    bool WaitForPipe(LPCWSTR name, DWORD timeout_ms, std::uint32_t& error) noexcept override {
        ++wait_calls;
        waited_name = name != nullptr ? name : L"";
        wait_timeout_ms = timeout_ms;
        const auto action = Take(wait_actions, {});
        error = action.error;
        return action.succeeds;
    }

    bool Connect(HANDLE pipe, OVERLAPPED* operation, std::uint32_t& error) noexcept override {
        connect_pipe = pipe;
        connect_overlapped = operation;
        return Apply(Take(connect_actions, {}), nullptr, error);
    }

    PipeApiCallResult Read(const PipeReadRequest& request) noexcept override {
        read_pipe = request.pipe;
        read_destinations.push_back(request.destination);
        read_lengths.push_back(request.length);
        read_overlapped = request.operation;
        const auto action = Take(read_actions, {});
        return {
            .succeeded = action.succeeds, .error = action.error, .transferred = action.transferred};
    }

    PipeApiCallResult Write(const PipeWriteRequest& request) noexcept override {
        write_pipe = request.pipe;
        write_sources.push_back(request.source);
        write_lengths.push_back(request.length);
        write_overlapped = request.operation;
        const auto action = Take(write_actions, {});
        return {
            .succeeded = action.succeeds, .error = action.error, .transferred = action.transferred};
    }

    bool GetOperationResult(HANDLE pipe, OVERLAPPED* operation, DWORD& transferred, bool wait,
                            std::uint32_t& error) noexcept override {
        result_pipes.push_back(pipe);
        result_operations.push_back(operation);
        result_wait_values.push_back(wait);
        return Apply(Take(completion_actions, {}), &transferred, error);
    }

    bool Cancel(HANDLE pipe, OVERLAPPED* operation, std::uint32_t& error) noexcept override {
        cancel_pipes.push_back(pipe);
        cancel_operations.push_back(operation);
        const auto action = Take(cancel_actions, {});
        error = action.error;
        return action.succeeds;
    }

    bool Disconnect(HANDLE pipe, std::uint32_t& error) noexcept override {
        disconnected_handles.push_back(pipe);
        error = disconnect_error;
        return disconnect_error == ERROR_SUCCESS;
    }

    static IoAction Take(std::deque<IoAction>& actions, IoAction fallback) noexcept {
        if (actions.empty()) {
            return fallback;
        }
        const auto result = actions.front();
        actions.pop_front();
        return result;
    }

    static bool Apply(const IoAction& action, DWORD* transferred, std::uint32_t& error) noexcept {
        if (transferred != nullptr) {
            *transferred = action.transferred;
        }
        error = action.error;
        return action.succeeds;
    }

    std::deque<IoAction> open_client_actions;
    std::deque<IoAction> wait_actions;
    std::deque<IoAction> connect_actions;
    std::deque<IoAction> read_actions;
    std::deque<IoAction> write_actions;
    std::deque<IoAction> completion_actions;
    std::deque<IoAction> cancel_actions;
    std::array<std::byte, 1U> server_pipe_storage{};
    std::array<std::byte, 1U> client_pipe_storage{};
    std::array<std::array<std::byte, 1U>, 12U> event_storage{};
    std::vector<bool> event_inherit_values;
    std::vector<HANDLE> reset_events;
    std::vector<HANDLE> closed_handles;
    std::vector<void*> read_destinations;
    std::vector<DWORD> read_lengths;
    std::vector<const void*> write_sources;
    std::vector<DWORD> write_lengths;
    std::vector<HANDLE> result_pipes;
    std::vector<OVERLAPPED*> result_operations;
    std::vector<bool> result_wait_values;
    std::vector<HANDLE> cancel_pipes;
    std::vector<OVERLAPPED*> cancel_operations;
    std::vector<HANDLE> disconnected_handles;
    std::wstring server_name;
    std::wstring client_name;
    std::wstring waited_name;
    bool server_security_valid{};
    SECURITY_ATTRIBUTES* client_security{};
    OVERLAPPED* connect_overlapped{};
    OVERLAPPED* read_overlapped{};
    OVERLAPPED* write_overlapped{};
    HANDLE connect_pipe{};
    HANDLE read_pipe{};
    HANDLE write_pipe{};
    DWORD server_open_mode{};
    DWORD server_pipe_mode{};
    DWORD server_maximum_instances{};
    DWORD client_desired_access{};
    DWORD client_share_mode{};
    DWORD client_creation_disposition{};
    DWORD client_flags_and_attributes{};
    DWORD wait_timeout_ms{};
    std::size_t event_count{};
    std::uint32_t create_server_calls{};
    std::uint32_t open_client_calls{};
    std::uint32_t wait_calls{};
    std::uint32_t event_error{ERROR_SUCCESS};
    std::uint32_t reset_error{ERROR_SUCCESS};
    std::uint32_t create_server_error{ERROR_SUCCESS};
    std::uint32_t disconnect_error{ERROR_SUCCESS};
};

std::vector<std::byte> CopySid(PSID sid) {
    const auto size = GetLengthSid(sid);
    std::vector<std::byte> result(size);
    static_cast<void>(::CopySid(size, result.data(), sid));
    return result;
}

std::vector<std::byte> LogonSid() {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    auto* sid = reinterpret_cast<PSID>(storage.data());
    if (InitializeSid(sid, &authority, 3U) == FALSE) {
        return {};
    }
    *GetSidSubAuthority(sid, 0U) = SECURITY_LOGON_IDS_RID;
    *GetSidSubAuthority(sid, 1U) = 0x1234U;
    *GetSidSubAuthority(sid, 2U) = 0x5678U;
    return CopySid(sid);
}

AuthorizedPrincipal PortablePrincipal() {
    const auto sid = LogonSid();
    return {
        .mode = CollectorEndpointMode::Portable, .authorized_sid = sid, .server_logon_sid = sid};
}

void CheckFactoryConfiguration(TestContext& context) {
    FakePipeTransportApi api;
    WindowsPipeTransportFactory factory{api};
    {
        auto result = factory.CreateServer(PortablePrincipal(), true);
        context.Check(result.ok(), "server factory succeeds");
        context.Check(api.create_server_calls == 1U && api.server_security_valid,
                      "server receives explicit descriptor");
        context.Check((api.server_open_mode & PIPE_ACCESS_DUPLEX) != 0U &&
                          (api.server_open_mode & FILE_FLAG_OVERLAPPED) != 0U &&
                          (api.server_open_mode & FILE_FLAG_FIRST_PIPE_INSTANCE) != 0U,
                      "server open flags are exact capabilities");
        context.Check(api.server_pipe_mode == (PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                               PIPE_REJECT_REMOTE_CLIENTS) &&
                          api.server_maximum_instances == nfm::ipc::kMaximumPipeInstances,
                      "server byte mode, local-only mode, and instance cap configured");
        context.Check(api.event_inherit_values.size() == 3U && !api.event_inherit_values.at(0U) &&
                          !api.event_inherit_values.at(1U) && !api.event_inherit_values.at(2U),
                      "operation events are not inheritable");
        context.Check(result.connection->connect_event() != result.connection->read_event() &&
                          result.connection->read_event() != result.connection->write_event(),
                      "connect read and write events are separate");
    }
    context.Check(api.disconnected_handles.size() == 1U && api.closed_handles.size() == 4U,
                  "server connection disconnects and closes pipe plus events");

    FakePipeTransportApi subsequent_api;
    WindowsPipeTransportFactory subsequent_factory{subsequent_api};
    auto subsequent = subsequent_factory.CreateServer(PortablePrincipal(), false);
    context.Check(subsequent.ok() &&
                      (subsequent_api.server_open_mode & FILE_FLAG_FIRST_PIPE_INSTANCE) == 0U,
                  "subsequent server instance omits ownership flag");
}

void CheckConnectLifecycle(TestContext& context) {
    FakePipeTransportApi api;
    api.connect_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.cancel_actions.push_back(SuccessfulIo());
    api.completion_actions.push_back(FailedIo(ERROR_OPERATION_ABORTED));
    OverlappedPipeOperation operation{api, api.server_pipe_storage.data(),
                                      PipeOperationKind::Connect};
    context.Check(operation.initialization_result().ok(), "connect operation initializes");
    const auto pending = operation.BeginConnect();
    context.Check(pending.ok() && pending.pending, "connect reports pending");
    const auto cancel = operation.RequestCancel();
    context.Check(cancel.ok() && cancel.pending, "cancel request retains pending lifetime");
    const auto completion = operation.Complete();
    context.Check(completion.error == PipeIoError::WindowsFailure &&
                      completion.win32_error == ERROR_OPERATION_ABORTED &&
                      !api.result_wait_values.empty() && api.result_wait_values.front(),
                  "cancel drains final completion before reuse");

    api.connect_actions.push_back(FailedIo(ERROR_PIPE_CONNECTED));
    const auto raced = operation.BeginConnect();
    context.Check(raced.ok() && !raced.pending && operation.Complete().ok(),
                  "already-connected race is successful completion");

    api.connect_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.cancel_actions.push_back(FailedIo(ERROR_NOT_FOUND));
    api.completion_actions.push_back(SuccessfulIo());
    context.Check(operation.BeginConnect().pending && operation.RequestCancel().ok() &&
                      operation.Complete().ok(),
                  "cancel not-found race accepts normal completion");

    api.connect_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.completion_actions.push_back(FailedIo(ERROR_INVALID_DATA));
    context.Check(operation.BeginConnect().pending &&
                      operation.Complete().win32_error == ERROR_INVALID_DATA,
                  "pending completion failure is retained");
}

void CheckExactTransfers(TestContext& context) {
    FakePipeTransportApi api;
    NamedPipeConnection connection{api, api.server_pipe_storage.data(), false};
    context.Check(connection.initialization_result().ok(), "connection initializes");

    std::array<std::byte, 4U> destination{};
    api.read_actions.push_back(SuccessfulIo(2U));
    api.read_actions.push_back(SuccessfulIo(2U));
    const auto read = connection.BeginReadExact(destination);
    context.Check(read.ok() && !read.pending && read.transferred == destination.size() &&
                      api.read_lengths.size() == 2U && api.read_lengths.at(0U) == 4U &&
                      api.read_lengths.at(1U) == 2U,
                  "partial immediate reads continue to exact length");

    const std::array source{std::byte{1U}, std::byte{2U}, std::byte{3U}, std::byte{4U}};
    api.write_actions.push_back(FailedIo(ERROR_IO_PENDING));
    api.completion_actions.push_back(SuccessfulIo(1U));
    api.write_actions.push_back(SuccessfulIo(3U));
    const auto begin_write = connection.BeginWriteAll(source);
    context.Check(begin_write.ok() && begin_write.pending, "write starts pending");
    const auto finish_write = connection.ContinueWriteAll();
    context.Check(finish_write.ok() && finish_write.transferred == source.size() &&
                      api.write_lengths.size() == 2U && api.write_lengths.at(0U) == 4U &&
                      api.write_lengths.at(1U) == 3U && api.write_sources.at(0U) != source.data(),
                  "pending partial write resumes from connection-owned copy");

    api.read_actions.push_back(SuccessfulIo());
    context.Check(connection.BeginReadExact(destination).error == PipeIoError::ZeroProgress,
                  "zero-progress success is rejected");

    api.read_actions.push_back(FailedIo(ERROR_MORE_DATA));
    context.Check(connection.BeginReadExact(destination).error ==
                      PipeIoError::UnexpectedMessageMode,
                  "message-mode result is rejected");

    api.write_actions.push_back(FailedIo(ERROR_BROKEN_PIPE));
    context.Check(connection.BeginWriteAll(source).error == PipeIoError::Disconnected,
                  "broken pipe is classified");
}

void CheckShutdownAndDestructor(TestContext& context) {
    FakePipeTransportApi api;
    {
        NamedPipeConnection connection{api, api.server_pipe_storage.data(), true};
        std::array<std::byte, 2U> destination{};
        api.read_actions.push_back(FailedIo(ERROR_IO_PENDING));
        api.cancel_actions.push_back(SuccessfulIo());
        api.completion_actions.push_back(FailedIo(ERROR_OPERATION_ABORTED));
        context.Check(connection.BeginReadExact(destination).pending,
                      "destructor case has pending read");
        const auto first = connection.CancelAndDrain();
        const auto second = connection.CancelAndDrain();
        context.Check(first.ok() && second.ok() && api.cancel_operations.size() == 1U &&
                          api.result_operations.size() == 1U,
                      "shutdown accepts cancellation and is idempotent");
    }
    context.Check(api.disconnected_handles.size() == 1U && api.closed_handles.size() == 4U,
                  "destructor cleans all owned handles");

    FakePipeTransportApi cancel_failure_api;
    NamedPipeConnection cancel_failure_connection{
        cancel_failure_api, cancel_failure_api.server_pipe_storage.data(), true};
    std::array<std::byte, 1U> destination{};
    cancel_failure_api.read_actions.push_back(FailedIo(ERROR_IO_PENDING));
    cancel_failure_api.cancel_actions.push_back(FailedIo(ERROR_ACCESS_DENIED));
    cancel_failure_api.completion_actions.push_back(FailedIo(ERROR_OPERATION_ABORTED));
    context.Check(cancel_failure_connection.BeginReadExact(destination).pending &&
                      cancel_failure_connection.CancelAndDrain().win32_error ==
                          ERROR_ACCESS_DENIED &&
                      cancel_failure_api.result_operations.size() == 1U,
                  "cancel failure still drains operation before releasing owned buffer");
}

void CheckClientOpenAndFailures(TestContext& context) {
    FakePipeTransportApi api;
    api.open_client_actions.push_back(FailedIo(ERROR_PIPE_BUSY));
    api.wait_actions.push_back(SuccessfulIo());
    api.open_client_actions.push_back(SuccessfulIo());
    WindowsPipeTransportFactory factory{api};
    auto result = factory.CreateClient(PortablePrincipal());
    context.Check(result.ok() && api.open_client_calls == 2U && api.wait_calls == 1U,
                  "busy client waits then retries once");
    context.Check(api.client_desired_access == nfm::ipc::kPipeClientAccessMask &&
                      api.client_share_mode == 0U && api.client_security == nullptr &&
                      api.client_creation_disposition == OPEN_EXISTING &&
                      api.client_flags_and_attributes ==
                          (FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION),
                  "client requests exact rights and identification SQOS");
    context.Check(api.wait_timeout_ms == nfm::ipc::kPipeBusyWait.count(),
                  "busy wait uses bounded transport timeout");

    FakePipeTransportApi absent_api;
    absent_api.open_client_actions.push_back(FailedIo(ERROR_FILE_NOT_FOUND));
    WindowsPipeTransportFactory absent_factory{absent_api};
    context.Check(absent_factory.CreateClient(PortablePrincipal()).error ==
                          PipeIoError::Disconnected &&
                      absent_api.wait_calls == 0U,
                  "non-busy open failure does not wait");

    FakePipeTransportApi timeout_api;
    timeout_api.open_client_actions.push_back(FailedIo(ERROR_PIPE_BUSY));
    timeout_api.wait_actions.push_back(FailedIo(ERROR_SEM_TIMEOUT));
    WindowsPipeTransportFactory timeout_factory{timeout_api};
    context.Check(timeout_factory.CreateClient(PortablePrincipal()).error == PipeIoError::Timeout,
                  "busy wait timeout is classified");

    FakePipeTransportApi conflict_api;
    conflict_api.create_server_error = ERROR_ACCESS_DENIED;
    WindowsPipeTransportFactory conflict_factory{conflict_api};
    context.Check(conflict_factory.CreateServer(PortablePrincipal(), true).error ==
                      PipeIoError::Busy,
                  "first-instance ownership conflict is classified");

    FakePipeTransportApi event_failure_api;
    event_failure_api.event_error = ERROR_NOT_ENOUGH_MEMORY;
    WindowsPipeTransportFactory event_failure_factory{event_failure_api};
    auto event_failure = event_failure_factory.CreateServer(PortablePrincipal(), true);
    context.Check(event_failure.error == PipeIoError::WindowsFailure &&
                      event_failure.win32_error == ERROR_NOT_ENOUGH_MEMORY &&
                      event_failure_api.closed_handles.size() == 1U,
                  "event construction failure closes pipe");
}

} // namespace

int main() {
    static_assert(nfm::ipc::kMaximumPipeInstances == 4U);
    static_assert(nfm::ipc::kPipeBusyWait.count() <=
                  static_cast<std::int64_t>(std::numeric_limits<DWORD>::max()));
    TestContext context;
    CheckFactoryConfiguration(context);
    CheckConnectLifecycle(context);
    CheckExactTransfers(context);
    CheckShutdownAndDestructor(context);
    CheckClientOpenAndFailures(context);
    return context.failures() == 0 ? 0 : 1;
}
