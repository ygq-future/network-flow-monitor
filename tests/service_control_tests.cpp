#include "network_flow_monitor/service/service_control.h"
#include "network_flow_monitor/service/service_entry.h"

#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using nfm::collector::CollectorHostError;
using nfm::collector::CollectorHostResult;
using nfm::service::ServiceApi;
using nfm::service::ServiceCallResult;
using nfm::service::ServiceControl;
using nfm::service::ServiceEntry;
using nfm::service::ServiceEntryError;
using nfm::service::ServiceHandleResult;
using nfm::service::ServiceHost;
using nfm::service::ServiceRegistrationResult;
using nfm::service::ServiceResult;
using nfm::service::ServiceRuntimeApi;
using nfm::service::ServiceStartType;
using nfm::service::ServiceState;
using nfm::service::ServiceStatusResult;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

class FakeServiceApi final : public ServiceApi {
  public:
    ServiceHandleResult OpenManager(DWORD) noexcept override {
        if (open_manager_error != ERROR_SUCCESS) {
            return {.win32_error = open_manager_error};
        }
        return {.handle = ManagerHandle()};
    }

    ServiceHandleResult OpenService(SC_HANDLE, LPCWSTR, DWORD) noexcept override {
        if (!installed) {
            return {.win32_error = ERROR_SERVICE_DOES_NOT_EXIST};
        }
        if (open_service_error != ERROR_SUCCESS) {
            return {.win32_error = open_service_error};
        }
        return {.handle = ServiceHandle()};
    }

    ServiceHandleResult CreateService(SC_HANDLE, LPCWSTR, LPCWSTR, DWORD start_type,
                                      LPCWSTR binary_path) noexcept override {
        ++create_calls;
        if (create_error != ERROR_SUCCESS) {
            return {.win32_error = create_error};
        }
        installed = true;
        state = SERVICE_STOPPED;
        configured_start_type = start_type;
        last_binary_path = binary_path == nullptr ? L"" : binary_path;
        return {.handle = ServiceHandle()};
    }

    ServiceCallResult ChangeServiceConfig(SC_HANDLE, DWORD start_type,
                                          LPCWSTR binary_path) noexcept override {
        ++change_calls;
        if (change_error != ERROR_SUCCESS) {
            return {.win32_error = change_error};
        }
        configured_start_type = start_type;
        if (binary_path != nullptr) {
            last_binary_path = binary_path;
        }
        return {.succeeded = true};
    }

    ServiceCallResult ChangeDelayedAutoStart(SC_HANDLE, bool enabled) noexcept override {
        delayed_change_calls++;
        if (delayed_change_error != ERROR_SUCCESS) {
            return {.win32_error = delayed_change_error};
        }
        delayed_autostart = enabled;
        return {.succeeded = true};
    }

    ServiceCallResult DeleteService(SC_HANDLE) noexcept override {
        ++delete_calls;
        if (delete_error != ERROR_SUCCESS) {
            return {.win32_error = delete_error};
        }
        installed = false;
        return {.succeeded = true};
    }

    ServiceCallResult StartService(SC_HANDLE) noexcept override {
        ++start_calls;
        if (start_error != ERROR_SUCCESS) {
            return {.win32_error = start_error};
        }
        state = SERVICE_RUNNING;
        return {.succeeded = true};
    }

    ServiceCallResult StopService(SC_HANDLE, SERVICE_STATUS_PROCESS& status) noexcept override {
        ++stop_calls;
        if (stop_error != ERROR_SUCCESS) {
            return {.win32_error = stop_error};
        }
        state = SERVICE_STOPPED;
        status.dwCurrentState = state;
        return {.succeeded = true};
    }

    ServiceStatusResult QueryStatus(SC_HANDLE) noexcept override {
        ++query_calls;
        if (query_error != ERROR_SUCCESS) {
            return {.win32_error = query_error};
        }
        SERVICE_STATUS_PROCESS status{};
        status.dwCurrentState = state;
        return {.succeeded = true, .status = status};
    }

    void CloseService(SC_HANDLE) noexcept override {
        ++close_calls;
    }

    static SC_HANDLE ManagerHandle() noexcept {
        static int marker;
        return reinterpret_cast<SC_HANDLE>(&marker);
    }

    static SC_HANDLE ServiceHandle() noexcept {
        static int marker;
        return reinterpret_cast<SC_HANDLE>(&marker);
    }

    bool installed{};
    DWORD state{SERVICE_STOPPED};
    DWORD open_manager_error{};
    DWORD open_service_error{};
    DWORD create_error{};
    DWORD change_error{};
    DWORD delete_error{};
    DWORD start_error{};
    DWORD stop_error{};
    DWORD query_error{};
    DWORD delayed_change_error{};
    DWORD configured_start_type{};
    std::wstring last_binary_path;
    bool delayed_autostart{};
    std::uint32_t create_calls{};
    std::uint32_t change_calls{};
    std::uint32_t delayed_change_calls{};
    std::uint32_t delete_calls{};
    std::uint32_t start_calls{};
    std::uint32_t stop_calls{};
    std::uint32_t query_calls{};
    std::uint32_t close_calls{};
};

class FakeHost final : public ServiceHost {
  public:
    CollectorHostResult Start() noexcept override {
        ++start_calls;
        return start_result;
    }

    CollectorHostResult Run(const std::stop_token& token) noexcept override {
        ++run_calls;
        stop_was_requested = token.stop_requested();
        return run_result;
    }

    CollectorHostResult Stop() noexcept override {
        ++stop_calls;
        return stop_result;
    }

    CollectorHostResult start_result{};
    CollectorHostResult run_result{};
    CollectorHostResult stop_result{};
    bool stop_was_requested{};
    std::uint32_t start_calls{};
    std::uint32_t run_calls{};
    std::uint32_t stop_calls{};
};

class FakeRuntimeApi final : public ServiceRuntimeApi {
  public:
    ServiceCallResult StartDispatcher(SERVICE_TABLE_ENTRYW* table) noexcept override {
        ++dispatch_calls;
        if (dispatcher_error != ERROR_SUCCESS) {
            return {.win32_error = dispatcher_error};
        }
        table->lpServiceProc(0U, nullptr);
        return {.succeeded = true};
    }

    ServiceRegistrationResult RegisterHandler(LPCWSTR service_name, LPHANDLER_FUNCTION_EX callback,
                                              LPVOID context) noexcept override {
        static_cast<void>(service_name);
        handler = callback;
        handler_context = context;
        static int marker;
        return {.handle = reinterpret_cast<SERVICE_STATUS_HANDLE>(&marker)};
    }

    ServiceCallResult SetStatus(SERVICE_STATUS_HANDLE handle,
                                const SERVICE_STATUS& status) noexcept override {
        static_cast<void>(handle);
        states.push_back(status.dwCurrentState);
        if (trigger_stop && status.dwCurrentState == SERVICE_RUNNING && handler != nullptr) {
            trigger_stop = false;
            static_cast<void>(handler(SERVICE_CONTROL_STOP, 0U, nullptr, handler_context));
        }
        if (status_error != ERROR_SUCCESS) {
            return {.win32_error = status_error};
        }
        return {.succeeded = true};
    }

    DWORD dispatcher_error{};
    DWORD status_error{};
    bool trigger_stop{};
    LPHANDLER_FUNCTION_EX handler{};
    LPVOID handler_context{};
    std::uint32_t dispatch_calls{};
    std::vector<DWORD> states;
};

void CheckServiceControl(TestContext& context) {
    FakeServiceApi api;
    ServiceControl control{api, L"C:\\Program Files\\Network Flow Monitor\\NetworkFlowMonitor.exe"};

    const auto installed = control.Install(ServiceStartType::Manual);
    context.Check(installed.ok() && api.installed && api.create_calls == 1U,
                  "install creates the fixed service once");
    context.Check(api.configured_start_type == SERVICE_DEMAND_START && !api.delayed_autostart &&
                      api.last_binary_path ==
                          L"\"C:\\Program Files\\Network Flow Monitor\\NetworkFlowMonitor.exe\"",
                  "install quotes the executable path and selects manual startup");

    context.Check(control.ConfigureStartType(ServiceStartType::Automatic).ok() &&
                      api.configured_start_type == SERVICE_AUTO_START && !api.delayed_autostart,
                  "startup type changes through typed configuration");
    context.Check(control.ConfigureStartType(ServiceStartType::AutomaticDelayed).ok() &&
                      api.configured_start_type == SERVICE_AUTO_START && api.delayed_autostart,
                  "delayed automatic startup uses the SCM delayed-start flag");
    api.state = SERVICE_RUNNING;
    context.Check(control.Query().ok() && control.Query().state == ServiceState::Running,
                  "query returns the native running state");
    api.state = SERVICE_STOPPED;
    context.Check(control.Start().ok() && api.state == SERVICE_RUNNING && api.start_calls == 1U,
                  "start transitions a stopped service");
    context.Check(control.Start().ok() && api.start_calls == 1U,
                  "start is idempotent when already running");
    context.Check(control.Stop().ok() && api.state == SERVICE_STOPPED && api.stop_calls == 1U,
                  "stop transitions a running service");
    context.Check(control.Stop().ok() && api.stop_calls == 1U,
                  "stop is idempotent when already stopped");
    context.Check(control.Restart().ok() && api.state == SERVICE_RUNNING,
                  "restart composes stop and start");
    context.Check(control.Uninstall().ok() && !api.installed && api.delete_calls == 1U,
                  "uninstall stops and deletes the service");
    context.Check(control.Query().error == nfm::service::ServiceError::NotInstalled,
                  "query classifies a missing service");

    FakeServiceApi invalid_api;
    ServiceControl invalid{invalid_api, {}};
    context.Check(invalid.Install(ServiceStartType::Manual).error ==
                      nfm::service::ServiceError::InvalidBinaryPath,
                  "empty service binary path is rejected");

    FakeServiceApi denied_api;
    denied_api.open_manager_error = ERROR_ACCESS_DENIED;
    ServiceControl denied{denied_api, L"C:\\NetworkFlowMonitor.exe"};
    context.Check(denied.Query().error == nfm::service::ServiceError::ManagerOpenFailed,
                  "SCM access denial remains classified");
}

void CheckServiceEntry(TestContext& context) {
    FakeHost host;
    FakeRuntimeApi runtime;
    runtime.trigger_stop = true;
    ServiceEntry entry{host, runtime};
    const auto dispatched = entry.Dispatch();
    context.Check(dispatched.ok() && runtime.dispatch_calls == 1U,
                  "service entry dispatches through the SCM callback");
    context.Check(host.start_calls == 1U && host.run_calls == 1U && host.stop_calls == 1U &&
                      host.stop_was_requested,
                  "service entry starts, observes stop, runs and stops only the host");
    context.Check(runtime.states.size() == 4U && runtime.states.at(0) == SERVICE_START_PENDING &&
                      runtime.states.at(1) == SERVICE_RUNNING &&
                      runtime.states.at(2) == SERVICE_STOP_PENDING &&
                      runtime.states.at(3) == SERVICE_STOPPED,
                  "service entry reports the complete SCM state sequence");

    FakeHost failed_host;
    failed_host.start_result = {.error = CollectorHostError::EtwStartFailed,
                                .source_error = ERROR_ACCESS_DENIED};
    FakeRuntimeApi failed_runtime;
    ServiceEntry failed_entry{failed_host, failed_runtime};
    const auto failed = failed_entry.Dispatch();
    context.Check(failed.error == ServiceEntryError::HostStartFailed &&
                      failed_host.run_calls == 0U && failed_host.stop_calls == 0U &&
                      failed_runtime.states.back() == SERVICE_STOPPED,
                  "service entry classifies startup failure without running the host");

    FakeHost dispatcher_host;
    FakeRuntimeApi dispatcher_runtime;
    dispatcher_runtime.dispatcher_error = ERROR_SERVICE_NOT_ACTIVE;
    ServiceEntry dispatcher_entry{dispatcher_host, dispatcher_runtime};
    context.Check(dispatcher_entry.Dispatch().error == ServiceEntryError::DispatcherFailed,
                  "dispatcher failure is returned to the caller");
}

} // namespace

int main() {
    TestContext context;
    CheckServiceControl(context);
    CheckServiceEntry(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " service control test failure(s)\n";
        return 1;
    }
    std::cout << "service control tests passed\n";
    return 0;
}
