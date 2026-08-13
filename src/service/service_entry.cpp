#include "network_flow_monitor/service/service_entry.h"

namespace nfm::service {

ServiceCallResult WindowsServiceRuntimeApi::StartDispatcher(SERVICE_TABLE_ENTRYW* table) noexcept {
    const auto succeeded = StartServiceCtrlDispatcherW(table);
    return {.succeeded = succeeded != FALSE,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

ServiceRegistrationResult WindowsServiceRuntimeApi::RegisterHandler(LPCWSTR service_name,
                                                                    LPHANDLER_FUNCTION_EX handler,
                                                                    LPVOID context) noexcept {
    const auto handle = RegisterServiceCtrlHandlerExW(service_name, handler, context);
    return {.handle = handle, .win32_error = handle == nullptr ? GetLastError() : ERROR_SUCCESS};
}

ServiceCallResult WindowsServiceRuntimeApi::SetStatus(SERVICE_STATUS_HANDLE handle,
                                                      const SERVICE_STATUS& status) noexcept {
    auto mutable_status = status;
    const auto succeeded = SetServiceStatus(handle, &mutable_status);
    return {.succeeded = succeeded != FALSE,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

ServiceEntry::ServiceEntry(ServiceHost& host, ServiceRuntimeApi& runtime) noexcept
    : host_{&host}, runtime_{&runtime} {}

ServiceEntryResult ServiceEntry::Fail(ServiceEntryError error, std::uint32_t win32_error) noexcept {
    return {.error = error, .win32_error = win32_error};
}

ServiceEntryResult ServiceEntry::Publish(const StatusUpdate& update) noexcept {
    status_ = {};
    status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status_.dwCurrentState = update.state;
    status_.dwControlsAccepted = update.accepted_controls;
    status_.dwWin32ExitCode = update.win32_error;
    status_.dwWaitHint = update.wait_hint;
    status_.dwCheckPoint =
        update.state == SERVICE_START_PENDING || update.state == SERVICE_STOP_PENDING ? 1U : 0U;
    const auto updated = runtime_->SetStatus(status_handle_, status_);
    if (!updated.ok()) {
        return Fail(ServiceEntryError::StatusUpdateFailed, updated.win32_error);
    }
    return {};
}

ServiceEntryResult ServiceEntry::Dispatch() noexcept {
    ServiceEntry* expected{};
    if (!active_entry_.compare_exchange_strong(expected, this)) {
        return Fail(ServiceEntryError::DispatcherFailed, ERROR_BUSY);
    }
    callback_result_ = Fail(ServiceEntryError::DispatcherFailed, ERROR_SERVICE_NOT_ACTIVE);
    static wchar_t service_name[] = L"NetworkFlowMonitor";
    SERVICE_TABLE_ENTRYW table[] = {{service_name, &ServiceMainThunk}, {nullptr, nullptr}};
    const auto dispatched = runtime_->StartDispatcher(table);
    active_entry_.store(nullptr);
    if (!dispatched.ok()) {
        return Fail(ServiceEntryError::DispatcherFailed, dispatched.win32_error);
    }
    return callback_result_;
}

ServiceEntryResult ServiceEntry::Run() noexcept {
    const auto registered =
        runtime_->RegisterHandler(kServiceName, &HandlerThunk, static_cast<LPVOID>(this));
    if (!registered.ok()) {
        return Fail(ServiceEntryError::HandlerRegistrationFailed, registered.win32_error);
    }
    status_handle_ = registered.handle;

    const auto starting = Publish({.state = SERVICE_START_PENDING,
                                   .accepted_controls = 0U,
                                   .win32_error = ERROR_SUCCESS,
                                   .wait_hint = 5000U});
    if (!starting.ok()) {
        return starting;
    }

    const auto started = host_->Start();
    if (!started.ok()) {
        const auto source_error =
            started.source_error == 0U ? ERROR_SERVICE_SPECIFIC_ERROR : started.source_error;
        static_cast<void>(Publish({.state = SERVICE_STOPPED,
                                   .accepted_controls = 0U,
                                   .win32_error = source_error,
                                   .wait_hint = 0U}));
        return Fail(ServiceEntryError::HostStartFailed, source_error);
    }

    const auto running =
        Publish({.state = SERVICE_RUNNING,
                 .accepted_controls = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN,
                 .win32_error = ERROR_SUCCESS,
                 .wait_hint = 0U});
    if (!running.ok()) {
        static_cast<void>(host_->Stop());
        return running;
    }

    const auto ran = host_->Run(stop_source_.get_token());
    const auto run_error = ran.source_error == 0U ? ERROR_SUCCESS : ran.source_error;
    const auto stopping = Publish({.state = SERVICE_STOP_PENDING,
                                   .accepted_controls = 0U,
                                   .win32_error = ERROR_SUCCESS,
                                   .wait_hint = 5000U});
    if (!stopping.ok()) {
        static_cast<void>(host_->Stop());
        return stopping;
    }

    const auto stopped = host_->Stop();
    if (!stopped.ok()) {
        const auto source_error =
            stopped.source_error == 0U ? ERROR_SERVICE_SPECIFIC_ERROR : stopped.source_error;
        static_cast<void>(Publish({.state = SERVICE_STOPPED,
                                   .accepted_controls = 0U,
                                   .win32_error = source_error,
                                   .wait_hint = 0U}));
        return Fail(ServiceEntryError::HostStopFailed, source_error);
    }
    if (!ran.ok()) {
        static_cast<void>(Publish({.state = SERVICE_STOPPED,
                                   .accepted_controls = 0U,
                                   .win32_error = run_error,
                                   .wait_hint = 0U}));
        return Fail(ServiceEntryError::HostRunFailed, run_error);
    }
    return Publish({.state = SERVICE_STOPPED,
                    .accepted_controls = 0U,
                    .win32_error = ERROR_SUCCESS,
                    .wait_hint = 0U});
}

void WINAPI ServiceEntry::ServiceMainThunk(DWORD argument_count, LPWSTR* arguments) noexcept {
    static_cast<void>(argument_count);
    static_cast<void>(arguments);
    auto* entry = active_entry_.load();
    if (entry == nullptr) {
        return;
    }
    entry->callback_result_ = entry->Run();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SCM callback ABI.
DWORD WINAPI ServiceEntry::HandlerThunk(DWORD control, DWORD event_type, LPVOID event_data,
                                        LPVOID context) noexcept {
    static_cast<void>(event_type);
    static_cast<void>(event_data);
    auto* entry = static_cast<ServiceEntry*>(context);
    if (entry == nullptr) {
        entry = active_entry_.load();
    }
    if (entry != nullptr &&
        (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)) {
        entry->stop_source_.request_stop();
    }
    return NO_ERROR;
}

} // namespace nfm::service
