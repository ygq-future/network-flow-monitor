#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_STARTUP_REGISTRATION_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_STARTUP_REGISTRATION_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace nfm::host::platform {

inline constexpr std::wstring_view kDefaultStartupRegistrySubKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr std::wstring_view kStartupAppIdentityName = L"NetworkFlowMonitor";

struct StartupRegistrationOptions {
    std::wstring registry_subkey{kDefaultStartupRegistrySubKey};
    std::wstring app_identity_name{kStartupAppIdentityName};
    std::filesystem::path startup_folder_override;
};

struct StartupRegistrationResult {
    bool success{false};
    std::uint32_t win32_error{0};
    std::size_t removed_entries_count{0};
    std::string diagnostic_summary;
};

class StartupRegistration {
  public:
    [[nodiscard]] static std::filesystem::path current_executable_path();

    [[nodiscard]] static std::wstring extract_executable_path_from_command(
        std::wstring_view command_line);

    [[nodiscard]] static bool is_matching_application_entry(
        std::wstring_view value_name, std::wstring_view command_line,
        const std::filesystem::path& current_exe,
        std::wstring_view app_identity_name = kStartupAppIdentityName);

    [[nodiscard]] static StartupRegistrationResult sync_login_startup(
        bool enabled, const std::filesystem::path& current_exe = current_executable_path(),
        const StartupRegistrationOptions& options = {});

    [[nodiscard]] static bool is_registered_for_executable(
        const std::filesystem::path& current_exe = current_executable_path(),
        const StartupRegistrationOptions& options = {});
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_STARTUP_REGISTRATION_H
