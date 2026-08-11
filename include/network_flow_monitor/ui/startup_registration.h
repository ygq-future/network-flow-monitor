#ifndef NETWORK_FLOW_MONITOR_UI_STARTUP_REGISTRATION_H
#define NETWORK_FLOW_MONITOR_UI_STARTUP_REGISTRATION_H

#include <cstdint>
#include <string>
#include <string_view>

namespace nfm::ui {

inline constexpr std::wstring_view kStartupRunValueName = L"NetworkFlowMonitor";

struct StartupRegistrationResult {
    bool succeeded{};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept {
        return succeeded;
    }
};

class StartupRunKeyStoreApi {
  public:
    StartupRunKeyStoreApi() = default;
    virtual ~StartupRunKeyStoreApi() = default;
    StartupRunKeyStoreApi(const StartupRunKeyStoreApi&) = delete;
    StartupRunKeyStoreApi& operator=(const StartupRunKeyStoreApi&) = delete;
    StartupRunKeyStoreApi(StartupRunKeyStoreApi&&) = delete;
    StartupRunKeyStoreApi& operator=(StartupRunKeyStoreApi&&) = delete;

    [[nodiscard]] virtual StartupRegistrationResult Set(std::wstring_view value_name,
                                                        std::wstring_view command) noexcept = 0;
    [[nodiscard]] virtual StartupRegistrationResult Remove(
        std::wstring_view value_name) noexcept = 0;
};

class WindowsStartupRunKeyStoreApi final : public StartupRunKeyStoreApi {
  public:
    [[nodiscard]] StartupRegistrationResult Set(std::wstring_view value_name,
                                                std::wstring_view command) noexcept override;
    [[nodiscard]] StartupRegistrationResult Remove(std::wstring_view value_name) noexcept override;
};

class StartupRegistration final {
  public:
    explicit StartupRegistration(StartupRunKeyStoreApi& store) noexcept : store_{&store} {}
    ~StartupRegistration() = default;
    StartupRegistration(const StartupRegistration&) = delete;
    StartupRegistration& operator=(const StartupRegistration&) = delete;
    StartupRegistration(StartupRegistration&&) = delete;
    StartupRegistration& operator=(StartupRegistration&&) = delete;

    [[nodiscard]] StartupRegistrationResult Apply(bool enabled) noexcept;

  private:
    [[nodiscard]] static std::wstring CurrentExecutablePath();
    [[nodiscard]] static std::wstring QuoteCommand(std::wstring_view path);

    StartupRunKeyStoreApi* store_;
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_STARTUP_REGISTRATION_H
