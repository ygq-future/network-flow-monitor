#ifndef NETWORK_FLOW_MONITOR_SERVICE_SERVICE_ENTRY_H
#define NETWORK_FLOW_MONITOR_SERVICE_SERVICE_ENTRY_H

#include "network_flow_monitor/collector/collector_host.h"
#include "network_flow_monitor/service/service_control.h"

#include <atomic>
#include <cstdint>
#include <stop_token>

namespace nfm::service {

class ServiceHost {
  public:
    ServiceHost() = default;
    ServiceHost(const ServiceHost&) = delete;
    ServiceHost& operator=(const ServiceHost&) = delete;
    ServiceHost(ServiceHost&&) = delete;
    ServiceHost& operator=(ServiceHost&&) = delete;
    virtual ~ServiceHost() = default;

    [[nodiscard]] virtual collector::CollectorHostResult Start() noexcept = 0;
    [[nodiscard]] virtual collector::CollectorHostResult Run(
        const std::stop_token& stop_token) noexcept = 0;
    [[nodiscard]] virtual collector::CollectorHostResult Stop() noexcept = 0;
};

struct ServiceRegistrationResult {
    SERVICE_STATUS_HANDLE handle{};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return handle != nullptr;
    }
};

class ServiceRuntimeApi {
  public:
    ServiceRuntimeApi() = default;
    ServiceRuntimeApi(const ServiceRuntimeApi&) = delete;
    ServiceRuntimeApi& operator=(const ServiceRuntimeApi&) = delete;
    ServiceRuntimeApi(ServiceRuntimeApi&&) = delete;
    ServiceRuntimeApi& operator=(ServiceRuntimeApi&&) = delete;
    virtual ~ServiceRuntimeApi() = default;

    [[nodiscard]] virtual ServiceCallResult StartDispatcher(
        SERVICE_TABLE_ENTRYW* table) noexcept = 0;
    [[nodiscard]] virtual ServiceRegistrationResult RegisterHandler(LPCWSTR service_name,
                                                                    LPHANDLER_FUNCTION_EX handler,
                                                                    LPVOID context) noexcept = 0;
    [[nodiscard]] virtual ServiceCallResult SetStatus(SERVICE_STATUS_HANDLE handle,
                                                      const SERVICE_STATUS& status) noexcept = 0;
};

class WindowsServiceRuntimeApi final : public ServiceRuntimeApi {
  public:
    [[nodiscard]] ServiceCallResult StartDispatcher(SERVICE_TABLE_ENTRYW* table) noexcept override;
    [[nodiscard]] ServiceRegistrationResult RegisterHandler(LPCWSTR service_name,
                                                            LPHANDLER_FUNCTION_EX handler,
                                                            LPVOID context) noexcept override;
    [[nodiscard]] ServiceCallResult SetStatus(SERVICE_STATUS_HANDLE handle,
                                              const SERVICE_STATUS& status) noexcept override;
};

enum class ServiceEntryError : std::uint8_t {
    None,
    DispatcherFailed,
    HandlerRegistrationFailed,
    StatusUpdateFailed,
    HostStartFailed,
    HostRunFailed,
    HostStopFailed,
};

struct ServiceEntryResult {
    ServiceEntryError error{ServiceEntryError::None};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return error == ServiceEntryError::None;
    }
};

class ServiceEntry final {
  public:
    ServiceEntry(ServiceHost& host, ServiceRuntimeApi& runtime) noexcept;
    ~ServiceEntry() = default;
    ServiceEntry(const ServiceEntry&) = delete;
    ServiceEntry& operator=(const ServiceEntry&) = delete;
    ServiceEntry(ServiceEntry&&) = delete;
    ServiceEntry& operator=(ServiceEntry&&) = delete;

    [[nodiscard]] ServiceEntryResult Dispatch() noexcept;
    [[nodiscard]] ServiceEntryResult Run() noexcept;

  private:
    struct StatusUpdate {
        DWORD state{};
        DWORD accepted_controls{};
        DWORD win32_error{};
        DWORD wait_hint{};
    };

    static void WINAPI ServiceMainThunk(DWORD argument_count, LPWSTR* arguments) noexcept;
    static DWORD WINAPI HandlerThunk(DWORD control, DWORD event_type, LPVOID event_data,
                                     LPVOID context) noexcept;

    [[nodiscard]] ServiceEntryResult Publish(const StatusUpdate& update) noexcept;
    [[nodiscard]] ServiceEntryResult Fail(ServiceEntryError error,
                                          std::uint32_t win32_error) noexcept;

    inline static std::atomic<ServiceEntry*> active_entry_{};
    ServiceHost* host_;
    ServiceRuntimeApi* runtime_;
    SERVICE_STATUS_HANDLE status_handle_{};
    SERVICE_STATUS status_{};
    std::stop_source stop_source_;
    ServiceEntryResult callback_result_{};
};

} // namespace nfm::service

#endif // NETWORK_FLOW_MONITOR_SERVICE_SERVICE_ENTRY_H
