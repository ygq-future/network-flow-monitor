#include "network_flow_monitor/ipc/named_pipe_transport.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace nfm::ipc {
namespace {

PipeIoResult ClassifyWindowsError(std::uint32_t error) noexcept {
    switch (error) {
    case ERROR_PIPE_BUSY:
        return {.error = PipeIoError::Busy, .win32_error = error};
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_BROKEN_PIPE:
    case ERROR_PIPE_NOT_CONNECTED:
    case ERROR_NO_DATA:
        return {.error = PipeIoError::Disconnected, .win32_error = error};
    case ERROR_SEM_TIMEOUT:
    case ERROR_TIMEOUT:
        return {.error = PipeIoError::Timeout, .win32_error = error};
    case ERROR_MORE_DATA:
        return {.error = PipeIoError::UnexpectedMessageMode, .win32_error = error};
    default:
        return {.error = PipeIoError::WindowsFailure, .win32_error = error};
    }
}

PipeConnectionResult ClassifyConnectionError(std::uint32_t error) noexcept {
    const auto classified = ClassifyWindowsError(error);
    return {.error = classified.error, .win32_error = classified.win32_error};
}

bool IsAcceptedDrainResult(const PipeIoResult& result) noexcept {
    return result.ok() || result.error == PipeIoError::Disconnected ||
           result.win32_error == ERROR_OPERATION_ABORTED;
}

DWORD BoundedTransferLength(std::size_t size) noexcept {
    constexpr auto maximum = std::numeric_limits<DWORD>::max();
    return static_cast<DWORD>(std::min(size, static_cast<std::size_t>(maximum)));
}

} // namespace

bool PipeIoResult::ok() const noexcept {
    return error == PipeIoError::None;
}

HANDLE WindowsPipeTransportApi::CreateManualResetEvent(bool inherit_handle,
                                                       std::uint32_t& error) noexcept {
    SECURITY_ATTRIBUTES attributes{.nLength = sizeof(SECURITY_ATTRIBUTES),
                                   .lpSecurityDescriptor = nullptr,
                                   .bInheritHandle = inherit_handle ? TRUE : FALSE};
    auto* requested_attributes = inherit_handle ? &attributes : nullptr;
    auto* const event = CreateEventW(requested_attributes, TRUE, FALSE, nullptr);
    error = event != nullptr ? ERROR_SUCCESS : GetLastError();
    return event;
}

bool WindowsPipeTransportApi::ResetManualEvent(HANDLE event, std::uint32_t& error) noexcept {
    const bool succeeded = ResetEvent(event) != FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

void WindowsPipeTransportApi::CloseKernelHandle(HANDLE handle) noexcept {
    static_cast<void>(CloseHandle(handle));
}

HANDLE WindowsPipeTransportApi::CreateServerPipe(const ServerPipeOpenRequest& request,
                                                 std::uint32_t& error) noexcept {
    auto* const pipe = CreateNamedPipeW(request.name, request.open_mode, request.pipe_mode,
                                        request.maximum_instances, kPipeBufferBytes,
                                        kPipeBufferBytes, 0U, request.security);
    error = pipe != INVALID_HANDLE_VALUE ? ERROR_SUCCESS : GetLastError();
    return pipe;
}

HANDLE WindowsPipeTransportApi::OpenClientPipe(const ClientPipeOpenRequest& request,
                                               std::uint32_t& error) noexcept {
    auto* const pipe =
        CreateFileW(request.name, request.desired_access, request.share_mode, request.security,
                    request.creation_disposition, request.flags_and_attributes, nullptr);
    error = pipe != INVALID_HANDLE_VALUE ? ERROR_SUCCESS : GetLastError();
    return pipe;
}

bool WindowsPipeTransportApi::WaitForPipe(LPCWSTR name, DWORD timeout_ms,
                                          std::uint32_t& error) noexcept {
    const bool succeeded = WaitNamedPipeW(name, timeout_ms) != FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

bool WindowsPipeTransportApi::Connect(HANDLE pipe, OVERLAPPED* operation,
                                      std::uint32_t& error) noexcept {
    const bool succeeded = ConnectNamedPipe(pipe, operation) != FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

PipeApiCallResult WindowsPipeTransportApi::Read(const PipeReadRequest& request) noexcept {
    DWORD transferred{};
    const bool succeeded = ReadFile(request.pipe, request.destination, request.length, &transferred,
                                    request.operation) != FALSE;
    return {.succeeded = succeeded,
            .error = succeeded ? ERROR_SUCCESS : GetLastError(),
            .transferred = transferred};
}

PipeApiCallResult WindowsPipeTransportApi::Write(const PipeWriteRequest& request) noexcept {
    DWORD transferred{};
    const bool succeeded = WriteFile(request.pipe, request.source, request.length, &transferred,
                                     request.operation) != FALSE;
    return {.succeeded = succeeded,
            .error = succeeded ? ERROR_SUCCESS : GetLastError(),
            .transferred = transferred};
}

bool WindowsPipeTransportApi::GetOperationResult(HANDLE pipe, OVERLAPPED* operation,
                                                 DWORD& transferred, bool wait,
                                                 std::uint32_t& error) noexcept {
    const bool succeeded =
        GetOverlappedResult(pipe, operation, &transferred, wait ? TRUE : FALSE) != FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

bool WindowsPipeTransportApi::Cancel(HANDLE pipe, OVERLAPPED* operation,
                                     std::uint32_t& error) noexcept {
    const bool succeeded = CancelIoEx(pipe, operation) != FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

bool WindowsPipeTransportApi::Disconnect(HANDLE pipe, std::uint32_t& error) noexcept {
    const bool succeeded = DisconnectNamedPipe(pipe) != FALSE;
    error = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded;
}

OverlappedPipeOperation::OverlappedPipeOperation(PipeTransportApi& api, HANDLE pipe,
                                                 PipeOperationKind kind) noexcept
    : api_{api}, pipe_{pipe}, kind_{kind} {
    std::uint32_t error{};
    event_ = api_.CreateManualResetEvent(false, error);
    if (event_ == nullptr) {
        initialization_result_ = ClassifyWindowsError(error);
    }
}

OverlappedPipeOperation::~OverlappedPipeOperation() {
    DrainForDestruction();
    if (event_ != nullptr) {
        api_.CloseKernelHandle(event_);
    }
}

PipeIoResult OverlappedPipeOperation::Prepare() noexcept {
    if (!initialization_result_.ok()) {
        return initialization_result_;
    }
    if (state_ != PipeIoState::Idle) {
        return {.error = PipeIoError::InvalidState};
    }
    std::uint32_t error{};
    if (!api_.ResetManualEvent(event_, error)) {
        return ClassifyWindowsError(error);
    }
    operation_ = {};
    operation_.hEvent = event_;
    completed_result_ = {};
    return {};
}

PipeIoResult OverlappedPipeOperation::FinishBegin(const PipeBeginOutcome& outcome) noexcept {
    if (outcome.succeeded ||
        (kind_ == PipeOperationKind::Connect && outcome.error == ERROR_PIPE_CONNECTED)) {
        state_ = PipeIoState::Completed;
        completed_result_ = {.transferred = outcome.transferred};
        return completed_result_;
    }
    if (outcome.error == ERROR_IO_PENDING) {
        state_ = PipeIoState::Pending;
        return {.pending = true};
    }
    state_ = PipeIoState::Idle;
    return ClassifyWindowsError(outcome.error);
}

PipeIoResult OverlappedPipeOperation::BeginConnect() noexcept {
    if (kind_ != PipeOperationKind::Connect) {
        return {.error = PipeIoError::InvalidState};
    }
    const auto prepared = Prepare();
    if (!prepared.ok()) {
        return prepared;
    }
    std::uint32_t error{};
    const bool succeeded = api_.Connect(pipe_, &operation_, error);
    return FinishBegin({.succeeded = succeeded, .error = error});
}

PipeIoResult OverlappedPipeOperation::BeginRead(std::span<std::byte> destination) noexcept {
    if (kind_ != PipeOperationKind::Read) {
        return {.error = PipeIoError::InvalidState};
    }
    const auto prepared = Prepare();
    if (!prepared.ok()) {
        return prepared;
    }
    const auto result = api_.Read({.pipe = pipe_,
                                   .destination = destination.data(),
                                   .length = BoundedTransferLength(destination.size()),
                                   .operation = &operation_});
    return FinishBegin(
        {.succeeded = result.succeeded, .error = result.error, .transferred = result.transferred});
}

PipeIoResult OverlappedPipeOperation::BeginWrite(std::span<const std::byte> source) noexcept {
    if (kind_ != PipeOperationKind::Write) {
        return {.error = PipeIoError::InvalidState};
    }
    const auto prepared = Prepare();
    if (!prepared.ok()) {
        return prepared;
    }
    const auto result = api_.Write({.pipe = pipe_,
                                    .source = source.data(),
                                    .length = BoundedTransferLength(source.size()),
                                    .operation = &operation_});
    return FinishBegin(
        {.succeeded = result.succeeded, .error = result.error, .transferred = result.transferred});
}

PipeIoResult OverlappedPipeOperation::Complete() noexcept {
    if (state_ == PipeIoState::Idle) {
        return {.error = PipeIoError::InvalidState};
    }
    if (state_ == PipeIoState::Completed) {
        state_ = PipeIoState::Idle;
        cancel_submitted_ = false;
        return completed_result_;
    }
    DWORD transferred{};
    std::uint32_t error{};
    const bool wait = (state_ != PipeIoState::CancelRequested) || cancel_submitted_;
    const bool succeeded = api_.GetOperationResult(pipe_, &operation_, transferred, wait, error);
    state_ = PipeIoState::Idle;
    cancel_submitted_ = false;
    return succeeded ? PipeIoResult{.transferred = transferred} : ClassifyWindowsError(error);
}

PipeIoResult OverlappedPipeOperation::RequestCancel() noexcept {
    if (state_ == PipeIoState::Completed) {
        return completed_result_;
    }
    if (state_ == PipeIoState::CancelRequested) {
        return {.pending = true};
    }
    if (state_ != PipeIoState::Pending) {
        return {.error = PipeIoError::InvalidState};
    }
    std::uint32_t error{};
    const bool succeeded = api_.Cancel(pipe_, &operation_, error);
    if (succeeded) {
        cancel_submitted_ = true;
        state_ = PipeIoState::CancelRequested;
        return {.win32_error = ERROR_SUCCESS, .pending = true};
    }
    if (error == ERROR_NOT_FOUND) {
        cancel_submitted_ = false;
        state_ = PipeIoState::CancelRequested;
        return {.win32_error = error, .pending = true};
    }
    return ClassifyWindowsError(error);
}

HANDLE OverlappedPipeOperation::completion_event() const noexcept {
    return event_;
}

PipeIoState OverlappedPipeOperation::state() const noexcept {
    return state_;
}

PipeIoResult OverlappedPipeOperation::initialization_result() const noexcept {
    return initialization_result_;
}

void OverlappedPipeOperation::DrainForDestruction() noexcept {
    if (state_ == PipeIoState::Pending) {
        static_cast<void>(RequestCancel());
    }
    if (state_ != PipeIoState::Idle) {
        static_cast<void>(Complete());
    }
}

NamedPipeConnection::NamedPipeConnection(PipeTransportApi& api, HANDLE pipe,
                                         bool server_end) noexcept
    : api_{api}, pipe_{pipe}, server_end_{server_end},
      connect_operation_{api, pipe, PipeOperationKind::Connect},
      read_operation_{api, pipe, PipeOperationKind::Read},
      write_operation_{api, pipe, PipeOperationKind::Write} {
    const std::array results{connect_operation_.initialization_result(),
                             read_operation_.initialization_result(),
                             write_operation_.initialization_result()};
    const auto failed =
        std::ranges::find_if(results, [](const PipeIoResult& result) { return !result.ok(); });
    if (failed != results.end()) {
        initialization_result_ = *failed;
    }
}

NamedPipeConnection::~NamedPipeConnection() {
    static_cast<void>(CancelAndDrain());
    if (server_end_ && pipe_ != nullptr && pipe_ != INVALID_HANDLE_VALUE) {
        std::uint32_t error{};
        static_cast<void>(api_.Disconnect(pipe_, error));
    }
    if (pipe_ != nullptr && pipe_ != INVALID_HANDLE_VALUE) {
        api_.CloseKernelHandle(pipe_);
    }
}

PipeIoResult NamedPipeConnection::BeginConnect() noexcept {
    return connect_operation_.BeginConnect();
}

PipeIoResult NamedPipeConnection::ContinueConnect() noexcept {
    return connect_operation_.Complete();
}

PipeIoResult NamedPipeConnection::BeginReadExact(std::span<std::byte> destination) noexcept {
    if (read_active_) {
        return {.error = PipeIoError::InvalidState};
    }
    if (destination.empty()) {
        return {};
    }
    try {
        read_buffer_.assign(destination.size(), std::byte{});
    } catch (...) {
        return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_NOT_ENOUGH_MEMORY};
    }
    read_destination_ = destination;
    read_offset_ = 0U;
    read_active_ = true;
    return StartNextRead();
}

PipeIoResult NamedPipeConnection::ContinueReadExact() noexcept {
    if (!read_active_ || (read_operation_.state() != PipeIoState::Pending &&
                          read_operation_.state() != PipeIoState::CancelRequested)) {
        return {.error = PipeIoError::InvalidState};
    }
    const auto advanced = AdvanceRead(read_operation_.Complete());
    if (!advanced.ok() || !read_active_) {
        return advanced;
    }
    return StartNextRead();
}

PipeIoResult NamedPipeConnection::StartNextRead() noexcept {
    while (read_active_) {
        const auto remaining = std::span{read_buffer_}.subspan(read_offset_);
        const auto started = read_operation_.BeginRead(remaining);
        if (!started.ok() || started.pending) {
            if (!started.ok()) {
                ResetRead();
            }
            return {.error = started.error,
                    .win32_error = started.win32_error,
                    .transferred = read_offset_,
                    .pending = started.pending};
        }
        const auto advanced = AdvanceRead(read_operation_.Complete());
        if (!advanced.ok() || advanced.pending || !read_active_) {
            return advanced;
        }
    }
    return {};
}

PipeIoResult NamedPipeConnection::AdvanceRead(PipeIoResult operation_result) noexcept {
    if (!operation_result.ok()) {
        const auto total = read_offset_;
        ResetRead();
        operation_result.transferred = total;
        return operation_result;
    }
    if (operation_result.transferred == 0U) {
        const auto total = read_offset_;
        ResetRead();
        return {.error = PipeIoError::ZeroProgress, .transferred = total};
    }
    const auto remaining = read_buffer_.size() - read_offset_;
    if (operation_result.transferred > remaining) {
        ResetRead();
        return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_INVALID_DATA};
    }
    read_offset_ += operation_result.transferred;
    if (read_offset_ != read_buffer_.size()) {
        return {.transferred = read_offset_};
    }
    std::ranges::copy(read_buffer_, read_destination_.begin());
    const auto total = read_offset_;
    ResetRead();
    return {.transferred = total};
}

PipeIoResult NamedPipeConnection::BeginWriteAll(std::span<const std::byte> source) noexcept {
    if (write_active_) {
        return {.error = PipeIoError::InvalidState};
    }
    if (source.empty()) {
        return {};
    }
    try {
        write_buffer_.assign(source.begin(), source.end());
    } catch (...) {
        return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_NOT_ENOUGH_MEMORY};
    }
    write_offset_ = 0U;
    write_active_ = true;
    return StartNextWrite();
}

PipeIoResult NamedPipeConnection::ContinueWriteAll() noexcept {
    if (!write_active_ || (write_operation_.state() != PipeIoState::Pending &&
                           write_operation_.state() != PipeIoState::CancelRequested)) {
        return {.error = PipeIoError::InvalidState};
    }
    const auto advanced = AdvanceWrite(write_operation_.Complete());
    if (!advanced.ok() || !write_active_) {
        return advanced;
    }
    return StartNextWrite();
}

PipeIoResult NamedPipeConnection::StartNextWrite() noexcept {
    while (write_active_) {
        const auto remaining = std::span<const std::byte>{write_buffer_}.subspan(write_offset_);
        const auto started = write_operation_.BeginWrite(remaining);
        if (!started.ok() || started.pending) {
            if (!started.ok()) {
                ResetWrite();
            }
            return {.error = started.error,
                    .win32_error = started.win32_error,
                    .transferred = write_offset_,
                    .pending = started.pending};
        }
        const auto advanced = AdvanceWrite(write_operation_.Complete());
        if (!advanced.ok() || advanced.pending || !write_active_) {
            return advanced;
        }
    }
    return {};
}

PipeIoResult NamedPipeConnection::AdvanceWrite(PipeIoResult operation_result) noexcept {
    if (!operation_result.ok()) {
        const auto total = write_offset_;
        ResetWrite();
        operation_result.transferred = total;
        return operation_result;
    }
    if (operation_result.transferred == 0U) {
        const auto total = write_offset_;
        ResetWrite();
        return {.error = PipeIoError::ZeroProgress, .transferred = total};
    }
    const auto remaining = write_buffer_.size() - write_offset_;
    if (operation_result.transferred > remaining) {
        ResetWrite();
        return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_INVALID_DATA};
    }
    write_offset_ += operation_result.transferred;
    if (write_offset_ != write_buffer_.size()) {
        return {.transferred = write_offset_};
    }
    const auto total = write_offset_;
    ResetWrite();
    return {.transferred = total};
}

PipeIoResult NamedPipeConnection::DrainOperation(OverlappedPipeOperation& operation) noexcept {
    if (operation.state() == PipeIoState::Idle) {
        return {};
    }
    PipeIoResult cancellation{};
    if (operation.state() == PipeIoState::Pending) {
        cancellation = operation.RequestCancel();
    }
    const auto completion = operation.Complete();
    if (!cancellation.ok()) {
        return cancellation;
    }
    return IsAcceptedDrainResult(completion) ? PipeIoResult{} : completion;
}

PipeIoResult NamedPipeConnection::CancelAndDrain() noexcept {
    PipeIoResult first_failure{};
    const std::array results{DrainOperation(connect_operation_), DrainOperation(read_operation_),
                             DrainOperation(write_operation_)};
    for (const auto& result : results) {
        if (!result.ok() && first_failure.ok()) {
            first_failure = result;
        }
    }
    ResetRead();
    ResetWrite();
    return first_failure;
}

PipeIoResult NamedPipeConnection::initialization_result() const noexcept {
    return initialization_result_;
}

HANDLE NamedPipeConnection::native_handle() const noexcept {
    return pipe_;
}

HANDLE NamedPipeConnection::connect_event() const noexcept {
    return connect_operation_.completion_event();
}

HANDLE NamedPipeConnection::read_event() const noexcept {
    return read_operation_.completion_event();
}

HANDLE NamedPipeConnection::write_event() const noexcept {
    return write_operation_.completion_event();
}

void NamedPipeConnection::ResetRead() noexcept {
    read_buffer_.clear();
    read_destination_ = {};
    read_offset_ = 0U;
    read_active_ = false;
}

void NamedPipeConnection::ResetWrite() noexcept {
    write_buffer_.clear();
    write_offset_ = 0U;
    write_active_ = false;
}

bool PipeConnectionResult::ok() const noexcept {
    return error == PipeIoError::None && connection != nullptr;
}

WindowsPipeTransportFactory::WindowsPipeTransportFactory() noexcept : api_{&windows_api_} {}

WindowsPipeTransportFactory::WindowsPipeTransportFactory(PipeTransportApi& api) noexcept
    : api_{&api} {}

PipeConnectionResult WindowsPipeTransportFactory::Adopt(HANDLE pipe, bool server_end) noexcept {
    try {
        auto connection = std::make_unique<NamedPipeConnection>(*api_, pipe, server_end);
        const auto initialized = connection->initialization_result();
        if (!initialized.ok()) {
            connection.reset();
            return {.error = initialized.error, .win32_error = initialized.win32_error};
        }
        return {.connection = std::move(connection)};
    } catch (...) {
        api_->CloseKernelHandle(pipe);
        return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_NOT_ENOUGH_MEMORY};
    }
}

PipeConnectionResult WindowsPipeTransportFactory::CreateServer(
    const AuthorizedPrincipal& principal, bool acquire_endpoint_ownership) noexcept {
    try {
        const auto name = BuildPipeName(principal);
        if (name.error != PipeSecurityError::None) {
            return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_INVALID_PARAMETER};
        }
        auto security = BuildPipeSecurity(principal);
        if (security.error != PipeSecurityError::None || security.descriptor == nullptr) {
            return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_ACCESS_DENIED};
        }
        DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (acquire_endpoint_ownership) {
            open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        }
        constexpr DWORD pipe_mode =
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
        std::uint32_t error{};
        auto* const pipe = api_->CreateServerPipe({.name = name.name.c_str(),
                                                   .open_mode = open_mode,
                                                   .pipe_mode = pipe_mode,
                                                   .maximum_instances = kMaximumPipeInstances,
                                                   .security = security.descriptor->attributes()},
                                                  error);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (error == ERROR_PIPE_BUSY || error == ERROR_ACCESS_DENIED) {
                return {.error = PipeIoError::Busy, .win32_error = error};
            }
            return ClassifyConnectionError(error);
        }
        return Adopt(pipe, true);
    } catch (...) {
        return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_NOT_ENOUGH_MEMORY};
    }
}

PipeConnectionResult WindowsPipeTransportFactory::CreateClient(
    const AuthorizedPrincipal& principal) noexcept {
    try {
        const auto name = BuildPipeName(principal);
        if (name.error != PipeSecurityError::None) {
            return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_INVALID_PARAMETER};
        }
        constexpr DWORD client_flags =
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION;
        std::uint32_t error{};
        auto* pipe = api_->OpenClientPipe({.name = name.name.c_str(),
                                           .desired_access = kPipeClientAccessMask,
                                           .share_mode = 0U,
                                           .security = nullptr,
                                           .creation_disposition = OPEN_EXISTING,
                                           .flags_and_attributes = client_flags},
                                          error);
        if (pipe == INVALID_HANDLE_VALUE && error == ERROR_PIPE_BUSY) {
            std::uint32_t wait_error{};
            if (!api_->WaitForPipe(name.name.c_str(), static_cast<DWORD>(kPipeBusyWait.count()),
                                   wait_error)) {
                return ClassifyConnectionError(wait_error);
            }
            pipe = api_->OpenClientPipe({.name = name.name.c_str(),
                                         .desired_access = kPipeClientAccessMask,
                                         .share_mode = 0U,
                                         .security = nullptr,
                                         .creation_disposition = OPEN_EXISTING,
                                         .flags_and_attributes = client_flags},
                                        error);
        }
        if (pipe == INVALID_HANDLE_VALUE) {
            return ClassifyConnectionError(error);
        }
        return Adopt(pipe, false);
    } catch (...) {
        return {.error = PipeIoError::WindowsFailure, .win32_error = ERROR_NOT_ENOUGH_MEMORY};
    }
}

} // namespace nfm::ipc
