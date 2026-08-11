#ifndef NETWORK_FLOW_MONITOR_UI_SETTINGS_CONTROLLER_H
#define NETWORK_FLOW_MONITOR_UI_SETTINGS_CONTROLLER_H

#include "network_flow_monitor/config/config_store.h"
#include "network_flow_monitor/config/config_validation.h"
#include "network_flow_monitor/service/service_control.h"

#include <filesystem>
#include <string>

namespace nfm::ui {

enum class SettingsServiceAction : std::uint8_t {
    Install,
    Uninstall,
    Start,
    Stop,
    Restart,
    Query,
    ConfigureStartType,
};

struct SettingsActionResult {
    bool succeeded{};
    service::ServiceState state{service::ServiceState::Unknown};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return succeeded;
    }
};

class SettingsStoreApi {
  public:
    SettingsStoreApi() = default;
    virtual ~SettingsStoreApi() = default;
    SettingsStoreApi(const SettingsStoreApi&) = delete;
    SettingsStoreApi& operator=(const SettingsStoreApi&) = delete;
    SettingsStoreApi(SettingsStoreApi&&) = delete;
    SettingsStoreApi& operator=(SettingsStoreApi&&) = delete;

    [[nodiscard]] virtual config::ConfigLoadResult Load(const std::filesystem::path& path,
                                                        config::ConfigDocumentKind kind) = 0;
    [[nodiscard]] virtual config::ConfigSaveResult Save(const std::filesystem::path& path,
                                                        const config::AppConfig& value,
                                                        config::ConfigDocumentKind kind) = 0;
};

class WindowsSettingsStoreApi final : public SettingsStoreApi {
  public:
    [[nodiscard]] config::ConfigLoadResult Load(const std::filesystem::path& path,
                                                config::ConfigDocumentKind kind) override;
    [[nodiscard]] config::ConfigSaveResult Save(const std::filesystem::path& path,
                                                const config::AppConfig& value,
                                                config::ConfigDocumentKind kind) override;
};

class ServiceActionApi {
  public:
    ServiceActionApi() = default;
    virtual ~ServiceActionApi() = default;
    ServiceActionApi(const ServiceActionApi&) = delete;
    ServiceActionApi& operator=(const ServiceActionApi&) = delete;
    ServiceActionApi(ServiceActionApi&&) = delete;
    ServiceActionApi& operator=(ServiceActionApi&&) = delete;

    [[nodiscard]] virtual SettingsActionResult Execute(
        SettingsServiceAction action, config::ServiceStartType start_type) noexcept = 0;
};

class WindowsServiceActionApi final : public ServiceActionApi {
  public:
    [[nodiscard]] SettingsActionResult Execute(
        SettingsServiceAction action, config::ServiceStartType start_type) noexcept override;
};

struct SettingsLoadResult {
    config::AppConfig config{};
    config::ConfigLoadStatus status{config::ConfigLoadStatus::Damaged};
    std::optional<config::ConfigError> error;
};

struct SettingsSaveResult {
    bool saved{};
    std::optional<config::ConfigError> error;
};

class SettingsController final {
  public:
    SettingsController(std::filesystem::path path, config::ConfigDocumentKind kind,
                       SettingsStoreApi& store, ServiceActionApi& service) noexcept;
    ~SettingsController() = default;
    SettingsController(const SettingsController&) = delete;
    SettingsController& operator=(const SettingsController&) = delete;
    SettingsController(SettingsController&&) = delete;
    SettingsController& operator=(SettingsController&&) = delete;

    [[nodiscard]] SettingsLoadResult Load() noexcept;
    [[nodiscard]] SettingsSaveResult Save(const config::AppConfig& value) noexcept;
    [[nodiscard]] SettingsActionResult ExecuteService(SettingsServiceAction action,
                                                      config::ServiceStartType start_type) noexcept;

    [[nodiscard]] const config::AppConfig& config() const noexcept {
        return config_;
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
    config::ConfigDocumentKind kind_;
    SettingsStoreApi* store_;
    ServiceActionApi* service_;
    config::AppConfig config_;
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_SETTINGS_CONTROLLER_H
