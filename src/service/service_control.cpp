#include "network_flow_monitor/service/service_control.h"

#include <chrono>
#include <thread>

namespace nfm::service {
namespace {

constexpr DWORD kManagerAccess = SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE;
constexpr DWORD kExistingServiceAccess =
    SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | DELETE;
constexpr DWORD kQueryServiceAccess = SERVICE_QUERY_STATUS;
constexpr DWORD kStartServiceAccess = SERVICE_START | SERVICE_QUERY_STATUS;
constexpr DWORD kStopServiceAccess = SERVICE_STOP | SERVICE_QUERY_STATUS;
constexpr DWORD kDeleteServiceAccess = DELETE | SERVICE_QUERY_STATUS | SERVICE_STOP;
constexpr auto kTransitionTimeout = std::chrono::seconds{5};
constexpr auto kTransitionPoll = std::chrono::milliseconds{50};
constexpr std::size_t kMaximumBinaryPathCharacters = 32767U;

class ScmHandle final {
  public:
    ScmHandle(ServiceApi& api, SC_HANDLE handle) noexcept : api_{&api}, handle_{handle} {}
    ~ScmHandle() {
        if (handle_ != nullptr) {
            api_->CloseService(handle_);
        }
    }
    ScmHandle(const ScmHandle&) = delete;
    ScmHandle& operator=(const ScmHandle&) = delete;
    ScmHandle(ScmHandle&&) = delete;
    ScmHandle& operator=(ScmHandle&&) = delete;

    [[nodiscard]] SC_HANDLE get() const noexcept {
        return handle_;
    }

  private:
    ServiceApi* api_;
    SC_HANDLE handle_;
};

[[nodiscard]] ServiceResult Failure(ServiceError error, std::uint32_t win32_error = 0U,
                                    ServiceState state = ServiceState::Unknown) noexcept {
    return {.error = error, .state = state, .win32_error = win32_error};
}

[[nodiscard]] bool IsMissing(std::uint32_t win32_error) noexcept {
    return win32_error == ERROR_SERVICE_DOES_NOT_EXIST;
}

[[nodiscard]] bool IsAlreadyRunning(std::uint32_t win32_error) noexcept {
    return win32_error == ERROR_SERVICE_ALREADY_RUNNING;
}

[[nodiscard]] bool IsNotActive(std::uint32_t win32_error) noexcept {
    return win32_error == ERROR_SERVICE_NOT_ACTIVE;
}

} // namespace

ServiceHandleResult WindowsServiceApi::OpenManager(DWORD desired_access) noexcept {
    const auto handle = OpenSCManagerW(nullptr, nullptr, desired_access);
    return {.handle = handle, .win32_error = handle == nullptr ? GetLastError() : ERROR_SUCCESS};
}

ServiceHandleResult WindowsServiceApi::OpenService(SC_HANDLE manager, LPCWSTR service_name,
                                                   DWORD desired_access) noexcept {
    const auto handle = ::OpenServiceW(manager, service_name, desired_access);
    return {.handle = handle, .win32_error = handle == nullptr ? GetLastError() : ERROR_SUCCESS};
}

ServiceHandleResult WindowsServiceApi::CreateService(SC_HANDLE manager, LPCWSTR service_name,
                                                     LPCWSTR display_name, DWORD start_type,
                                                     LPCWSTR binary_path) noexcept {
    const auto handle = ::CreateServiceW(
        manager, service_name, display_name,
        SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | DELETE,
        SERVICE_WIN32_OWN_PROCESS, start_type, SERVICE_ERROR_NORMAL, binary_path, nullptr, nullptr,
        nullptr, nullptr, nullptr);
    return {.handle = handle, .win32_error = handle == nullptr ? GetLastError() : ERROR_SUCCESS};
}

ServiceCallResult WindowsServiceApi::ChangeServiceConfig(SC_HANDLE service, DWORD start_type,
                                                         LPCWSTR binary_path) noexcept {
    const auto succeeded =
        ::ChangeServiceConfigW(service, SERVICE_NO_CHANGE, start_type, SERVICE_ERROR_NORMAL,
                               binary_path, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    return {.succeeded = succeeded != FALSE,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

ServiceCallResult WindowsServiceApi::ChangeDelayedAutoStart(SC_HANDLE service,
                                                            bool enabled) noexcept {
    SERVICE_DELAYED_AUTO_START_INFO info{.fDelayedAutostart = enabled ? TRUE : FALSE};
    const auto succeeded =
        ::ChangeServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &info);
    return {.succeeded = succeeded != FALSE,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

ServiceCallResult WindowsServiceApi::DeleteService(SC_HANDLE service) noexcept {
    const auto succeeded = ::DeleteService(service);
    return {.succeeded = succeeded != FALSE,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

ServiceCallResult WindowsServiceApi::StartService(SC_HANDLE service) noexcept {
    const auto succeeded = ::StartServiceW(service, 0U, nullptr);
    return {.succeeded = succeeded != FALSE,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

ServiceCallResult WindowsServiceApi::StopService(SC_HANDLE service,
                                                 SERVICE_STATUS_PROCESS& status) noexcept {
    SERVICE_STATUS native_status{};
    const auto succeeded = ControlService(service, SERVICE_CONTROL_STOP, &native_status);
    status.dwCurrentState = native_status.dwCurrentState;
    status.dwWin32ExitCode = native_status.dwWin32ExitCode;
    status.dwWaitHint = native_status.dwWaitHint;
    status.dwCheckPoint = native_status.dwCheckPoint;
    return {.succeeded = succeeded != FALSE,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

ServiceStatusResult WindowsServiceApi::QueryStatus(SC_HANDLE service) noexcept {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes_returned{};
    const auto succeeded =
        QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status),
                             sizeof(status), &bytes_returned);
    return {.succeeded = succeeded != FALSE,
            .status = status,
            .win32_error = succeeded != FALSE ? ERROR_SUCCESS : GetLastError()};
}

void WindowsServiceApi::CloseService(SC_HANDLE service) noexcept {
    static_cast<void>(::CloseServiceHandle(service));
}

ServiceControl::ServiceControl(ServiceApi& api, std::wstring binary_path)
    : api_{&api}, binary_path_{std::move(binary_path)},
      quoted_binary_path_{BuildServiceCommandLine(binary_path_)} {}

std::wstring ServiceControl::BuildServiceCommandLine(const std::wstring& path) {
    if (path.empty() || path.size() > kMaximumBinaryPathCharacters ||
        path.find(L'"') != std::wstring::npos) {
        return {};
    }
    // The fixed internal switch is required for SCM to enter the service dispatcher.
    return L"\"" + path + L"\" --service";
}

DWORD ServiceControl::ToNativeStartType(ServiceStartType start_type) noexcept {
    switch (start_type) {
    case ServiceStartType::Manual:
        return SERVICE_DEMAND_START;
    case ServiceStartType::Automatic:
    case ServiceStartType::AutomaticDelayed:
        return SERVICE_AUTO_START;
    case ServiceStartType::Disabled:
        return SERVICE_DISABLED;
    }
    return SERVICE_DEMAND_START;
}

bool ServiceControl::IsDelayed(ServiceStartType start_type) noexcept {
    return start_type == ServiceStartType::AutomaticDelayed;
}

ServiceState ServiceControl::ToState(DWORD native_state) noexcept {
    switch (native_state) {
    case SERVICE_STOPPED:
        return ServiceState::Stopped;
    case SERVICE_START_PENDING:
        return ServiceState::StartPending;
    case SERVICE_STOP_PENDING:
        return ServiceState::StopPending;
    case SERVICE_RUNNING:
        return ServiceState::Running;
    case SERVICE_CONTINUE_PENDING:
        return ServiceState::ContinuePending;
    case SERVICE_PAUSE_PENDING:
        return ServiceState::PausePending;
    case SERVICE_PAUSED:
        return ServiceState::Paused;
    default:
        return ServiceState::Unknown;
    }
}

ServiceResult ServiceControl::OpenExisting(SC_HANDLE manager, DWORD desired_access,
                                           SC_HANDLE& service) noexcept {
    const auto opened = api_->OpenService(manager, kServiceName, desired_access);
    if (opened.ok()) {
        service = opened.handle;
        return {};
    }
    if (IsMissing(opened.win32_error)) {
        return Failure(ServiceError::NotInstalled, opened.win32_error, ServiceState::Stopped);
    }
    return Failure(ServiceError::ServiceOpenFailed, opened.win32_error);
}

ServiceResult ServiceControl::Install(ServiceStartType start_type) noexcept {
    if (quoted_binary_path_.empty()) {
        return Failure(ServiceError::InvalidBinaryPath);
    }
    const auto manager_result = api_->OpenManager(kManagerAccess);
    if (!manager_result.ok()) {
        return Failure(ServiceError::ManagerOpenFailed, manager_result.win32_error);
    }
    const ScmHandle manager{*api_, manager_result.handle};

    SC_HANDLE service_handle{};
    const auto existing = OpenExisting(manager.get(), kExistingServiceAccess, service_handle);
    if (existing.ok()) {
        const ScmHandle service{*api_, service_handle};
        const auto changed = api_->ChangeServiceConfig(service.get(), ToNativeStartType(start_type),
                                                       quoted_binary_path_.c_str());
        if (!changed.ok()) {
            return Failure(ServiceError::ServiceConfigFailed, changed.win32_error);
        }
        const auto delayed = api_->ChangeDelayedAutoStart(service.get(), IsDelayed(start_type));
        if (!delayed.ok()) {
            return Failure(ServiceError::ServiceConfigFailed, delayed.win32_error);
        }
        return {.state = ServiceState::Stopped};
    }
    if (existing.error != ServiceError::NotInstalled) {
        return existing;
    }

    const auto created =
        api_->CreateService(manager.get(), kServiceName, kServiceDisplayName,
                            ToNativeStartType(start_type), quoted_binary_path_.c_str());
    if (created.ok()) {
        const ScmHandle service{*api_, created.handle};
        const auto delayed = api_->ChangeDelayedAutoStart(service.get(), IsDelayed(start_type));
        if (!delayed.ok()) {
            return Failure(ServiceError::ServiceConfigFailed, delayed.win32_error);
        }
        return {.state = ServiceState::Stopped};
    }
    if (created.win32_error != ERROR_SERVICE_EXISTS) {
        return Failure(ServiceError::ServiceCreateFailed, created.win32_error);
    }

    service_handle = nullptr;
    const auto raced = OpenExisting(manager.get(), kExistingServiceAccess, service_handle);
    if (!raced.ok()) {
        return raced;
    }
    const ScmHandle service{*api_, service_handle};
    const auto changed = api_->ChangeServiceConfig(service.get(), ToNativeStartType(start_type),
                                                   quoted_binary_path_.c_str());
    if (!changed.ok()) {
        return Failure(ServiceError::ServiceConfigFailed, changed.win32_error);
    }
    const auto delayed = api_->ChangeDelayedAutoStart(service.get(), IsDelayed(start_type));
    if (!delayed.ok()) {
        return Failure(ServiceError::ServiceConfigFailed, delayed.win32_error);
    }
    return {.state = ServiceState::Stopped};
}

ServiceResult ServiceControl::ConfigureStartType(ServiceStartType start_type) noexcept {
    const auto manager_result = api_->OpenManager(SC_MANAGER_CONNECT);
    if (!manager_result.ok()) {
        return Failure(ServiceError::ManagerOpenFailed, manager_result.win32_error);
    }
    const ScmHandle manager{*api_, manager_result.handle};
    SC_HANDLE service_handle{};
    const auto opened = OpenExisting(manager.get(), SERVICE_CHANGE_CONFIG, service_handle);
    if (!opened.ok()) {
        return opened;
    }
    const ScmHandle service{*api_, service_handle};
    const auto changed =
        api_->ChangeServiceConfig(service.get(), ToNativeStartType(start_type), nullptr);
    if (!changed.ok()) {
        return Failure(ServiceError::ServiceConfigFailed, changed.win32_error);
    }
    const auto delayed = api_->ChangeDelayedAutoStart(service.get(), IsDelayed(start_type));
    if (!delayed.ok()) {
        return Failure(ServiceError::ServiceConfigFailed, delayed.win32_error);
    }
    return {};
}

ServiceResult ServiceControl::WaitForState(SC_HANDLE service, ServiceState expected) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + kTransitionTimeout;
    for (;;) {
        const auto queried = api_->QueryStatus(service);
        if (!queried.ok()) {
            return Failure(ServiceError::ServiceQueryFailed, queried.win32_error);
        }
        const auto state = ToState(queried.status.dwCurrentState);
        if (state == expected) {
            return {.state = state};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return Failure(ServiceError::Timeout, ERROR_TIMEOUT, state);
        }
        std::this_thread::sleep_for(kTransitionPoll);
    }
}

ServiceResult ServiceControl::Start() noexcept {
    const auto manager_result = api_->OpenManager(SC_MANAGER_CONNECT);
    if (!manager_result.ok()) {
        return Failure(ServiceError::ManagerOpenFailed, manager_result.win32_error);
    }
    const ScmHandle manager{*api_, manager_result.handle};
    SC_HANDLE service_handle{};
    const auto opened = OpenExisting(manager.get(), kStartServiceAccess, service_handle);
    if (!opened.ok()) {
        return opened;
    }
    const ScmHandle service{*api_, service_handle};
    const auto queried = api_->QueryStatus(service.get());
    if (!queried.ok()) {
        return Failure(ServiceError::ServiceQueryFailed, queried.win32_error);
    }
    const auto current = ToState(queried.status.dwCurrentState);
    if (current == ServiceState::Running) {
        return {.state = current};
    }
    const auto started = api_->StartService(service.get());
    if (!started.ok() && !IsAlreadyRunning(started.win32_error)) {
        return Failure(ServiceError::ServiceStartFailed, started.win32_error, current);
    }
    const auto waited = WaitForState(service.get(), ServiceState::Running);
    if (!waited.ok()) {
        return waited;
    }
    return waited;
}

ServiceResult ServiceControl::Stop() noexcept {
    const auto manager_result = api_->OpenManager(SC_MANAGER_CONNECT);
    if (!manager_result.ok()) {
        return Failure(ServiceError::ManagerOpenFailed, manager_result.win32_error);
    }
    const ScmHandle manager{*api_, manager_result.handle};
    SC_HANDLE service_handle{};
    const auto opened = OpenExisting(manager.get(), kStopServiceAccess, service_handle);
    if (!opened.ok()) {
        return opened;
    }
    const ScmHandle service{*api_, service_handle};
    const auto queried = api_->QueryStatus(service.get());
    if (!queried.ok()) {
        return Failure(ServiceError::ServiceQueryFailed, queried.win32_error);
    }
    const auto current = ToState(queried.status.dwCurrentState);
    if (current == ServiceState::Stopped) {
        return {.state = current};
    }
    SERVICE_STATUS_PROCESS status{};
    const auto stopped = api_->StopService(service.get(), status);
    if (!stopped.ok() && !IsNotActive(stopped.win32_error)) {
        return Failure(ServiceError::ServiceStopFailed, stopped.win32_error, current);
    }
    const auto waited = WaitForState(service.get(), ServiceState::Stopped);
    if (!waited.ok()) {
        return waited;
    }
    return waited;
}

ServiceResult ServiceControl::Restart() noexcept {
    const auto stopped = Stop();
    if (!stopped.ok() && stopped.error != ServiceError::NotInstalled) {
        return stopped;
    }
    if (stopped.error == ServiceError::NotInstalled) {
        return stopped;
    }
    return Start();
}

ServiceResult ServiceControl::Uninstall() noexcept {
    const auto stopped = Stop();
    if (!stopped.ok() && stopped.error != ServiceError::NotInstalled) {
        return stopped;
    }
    if (stopped.error == ServiceError::NotInstalled) {
        return {.state = ServiceState::Stopped};
    }

    const auto manager_result = api_->OpenManager(SC_MANAGER_CONNECT);
    if (!manager_result.ok()) {
        return Failure(ServiceError::ManagerOpenFailed, manager_result.win32_error);
    }
    const ScmHandle manager{*api_, manager_result.handle};
    SC_HANDLE service_handle{};
    const auto opened = OpenExisting(manager.get(), kDeleteServiceAccess, service_handle);
    if (!opened.ok()) {
        if (opened.error == ServiceError::NotInstalled) {
            return {.state = ServiceState::Stopped};
        }
        return opened;
    }
    const ScmHandle service{*api_, service_handle};
    const auto deleted = api_->DeleteService(service.get());
    if (!deleted.ok() && deleted.win32_error != ERROR_SERVICE_MARKED_FOR_DELETE) {
        return Failure(ServiceError::ServiceDeleteFailed, deleted.win32_error);
    }
    return {.state = ServiceState::Stopped};
}

ServiceResult ServiceControl::Query() noexcept {
    const auto manager_result = api_->OpenManager(SC_MANAGER_CONNECT);
    if (!manager_result.ok()) {
        return Failure(ServiceError::ManagerOpenFailed, manager_result.win32_error);
    }
    const ScmHandle manager{*api_, manager_result.handle};
    SC_HANDLE service_handle{};
    const auto opened = OpenExisting(manager.get(), kQueryServiceAccess, service_handle);
    if (!opened.ok()) {
        return opened;
    }
    const ScmHandle service{*api_, service_handle};
    const auto queried = api_->QueryStatus(service.get());
    if (!queried.ok()) {
        return Failure(ServiceError::ServiceQueryFailed, queried.win32_error);
    }
    return {.state = ToState(queried.status.dwCurrentState)};
}

} // namespace nfm::service
