#ifndef NETWORK_FLOW_MONITOR_SERVICE_SERVICE_CONTROL_H
#define NETWORK_FLOW_MONITOR_SERVICE_SERVICE_CONTROL_H

#include <windows.h>

#include <cstdint>
#include <string>

namespace nfm::service {

inline constexpr wchar_t kServiceName[] = L"NetworkFlowMonitor";
inline constexpr wchar_t kServiceDisplayName[] = L"Network Flow Monitor";

enum class ServiceAction : std::uint8_t {
    Install,
    Uninstall,
    Start,
    Stop,
    Restart,
    Query,
};

enum class ServiceStartType : std::uint8_t {
    Manual,
    Automatic,
    AutomaticDelayed,
    Disabled,
};

enum class ServiceState : std::uint8_t {
    Unknown,
    Stopped,
    StartPending,
    StopPending,
    Running,
    ContinuePending,
    PausePending,
    Paused,
};

enum class ServiceError : std::uint8_t {
    None,
    InvalidBinaryPath,
    ManagerOpenFailed,
    ServiceOpenFailed,
    ServiceCreateFailed,
    ServiceConfigFailed,
    ServiceDeleteFailed,
    ServiceStartFailed,
    ServiceStopFailed,
    ServiceQueryFailed,
    NotInstalled,
    Timeout,
};

struct ServiceResult {
    ServiceError error{ServiceError::None};
    ServiceState state{ServiceState::Unknown};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return error == ServiceError::None;
    }
};

struct ServiceHandleResult {
    SC_HANDLE handle{};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return handle != nullptr;
    }
};

struct ServiceCallResult {
    bool succeeded{};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return succeeded;
    }
};

struct ServiceStatusResult {
    bool succeeded{};
    SERVICE_STATUS_PROCESS status{};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return succeeded;
    }
};

class ServiceApi {
  public:
    ServiceApi() = default;
    ServiceApi(const ServiceApi&) = delete;
    ServiceApi& operator=(const ServiceApi&) = delete;
    ServiceApi(ServiceApi&&) = delete;
    ServiceApi& operator=(ServiceApi&&) = delete;
    virtual ~ServiceApi() = default;

    [[nodiscard]] virtual ServiceHandleResult OpenManager(DWORD desired_access) noexcept = 0;
    [[nodiscard]] virtual ServiceHandleResult OpenService(SC_HANDLE manager, LPCWSTR service_name,
                                                          DWORD desired_access) noexcept = 0;
    [[nodiscard]] virtual ServiceHandleResult CreateService(SC_HANDLE manager, LPCWSTR service_name,
                                                            LPCWSTR display_name, DWORD start_type,
                                                            LPCWSTR binary_path) noexcept = 0;
    [[nodiscard]] virtual ServiceCallResult ChangeServiceConfig(SC_HANDLE service, DWORD start_type,
                                                                LPCWSTR binary_path) noexcept = 0;
    [[nodiscard]] virtual ServiceCallResult ChangeDelayedAutoStart(SC_HANDLE service,
                                                                   bool enabled) noexcept = 0;
    [[nodiscard]] virtual ServiceCallResult DeleteService(SC_HANDLE service) noexcept = 0;
    [[nodiscard]] virtual ServiceCallResult StartService(SC_HANDLE service) noexcept = 0;
    [[nodiscard]] virtual ServiceCallResult StopService(
        SC_HANDLE service, SERVICE_STATUS_PROCESS& status) noexcept = 0;
    [[nodiscard]] virtual ServiceStatusResult QueryStatus(SC_HANDLE service) noexcept = 0;
    virtual void CloseService(SC_HANDLE service) noexcept = 0;
};

class WindowsServiceApi final : public ServiceApi {
  public:
    [[nodiscard]] ServiceHandleResult OpenManager(DWORD desired_access) noexcept override;
    [[nodiscard]] ServiceHandleResult OpenService(SC_HANDLE manager, LPCWSTR service_name,
                                                  DWORD desired_access) noexcept override;
    [[nodiscard]] ServiceHandleResult CreateService(SC_HANDLE manager, LPCWSTR service_name,
                                                    LPCWSTR display_name, DWORD start_type,
                                                    LPCWSTR binary_path) noexcept override;
    [[nodiscard]] ServiceCallResult ChangeServiceConfig(SC_HANDLE service, DWORD start_type,
                                                        LPCWSTR binary_path) noexcept override;
    [[nodiscard]] ServiceCallResult ChangeDelayedAutoStart(SC_HANDLE service,
                                                           bool enabled) noexcept override;
    [[nodiscard]] ServiceCallResult DeleteService(SC_HANDLE service) noexcept override;
    [[nodiscard]] ServiceCallResult StartService(SC_HANDLE service) noexcept override;
    [[nodiscard]] ServiceCallResult StopService(SC_HANDLE service,
                                                SERVICE_STATUS_PROCESS& status) noexcept override;
    [[nodiscard]] ServiceStatusResult QueryStatus(SC_HANDLE service) noexcept override;
    void CloseService(SC_HANDLE service) noexcept override;
};

class ServiceControl final {
  public:
    ServiceControl(ServiceApi& api, std::wstring binary_path);
    ~ServiceControl() = default;
    ServiceControl(const ServiceControl&) = delete;
    ServiceControl& operator=(const ServiceControl&) = delete;
    ServiceControl(ServiceControl&&) = delete;
    ServiceControl& operator=(ServiceControl&&) = delete;

    [[nodiscard]] ServiceResult Install(ServiceStartType start_type) noexcept;
    [[nodiscard]] ServiceResult Uninstall() noexcept;
    [[nodiscard]] ServiceResult Start() noexcept;
    [[nodiscard]] ServiceResult Stop() noexcept;
    [[nodiscard]] ServiceResult Restart() noexcept;
    [[nodiscard]] ServiceResult Query() noexcept;
    [[nodiscard]] ServiceResult ConfigureStartType(ServiceStartType start_type) noexcept;

  private:
    [[nodiscard]] ServiceResult OpenExisting(SC_HANDLE manager, DWORD desired_access,
                                             SC_HANDLE& service) noexcept;
    [[nodiscard]] ServiceResult WaitForState(SC_HANDLE service, ServiceState expected) noexcept;
    [[nodiscard]] static ServiceState ToState(DWORD native_state) noexcept;
    [[nodiscard]] static DWORD ToNativeStartType(ServiceStartType start_type) noexcept;
    [[nodiscard]] static bool IsDelayed(ServiceStartType start_type) noexcept;
    [[nodiscard]] static std::wstring QuoteBinaryPath(const std::wstring& path);

    ServiceApi* api_;
    std::wstring binary_path_;
    std::wstring quoted_binary_path_;
};

} // namespace nfm::service

#endif // NETWORK_FLOW_MONITOR_SERVICE_SERVICE_CONTROL_H
