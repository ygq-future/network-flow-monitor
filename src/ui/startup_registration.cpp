#include "network_flow_monitor/ui/startup_registration.h"

#include <windows.h>

#include <limits>
#include <string>

namespace nfm::ui {
namespace {

[[nodiscard]] StartupRegistrationResult Failure(std::uint32_t error) noexcept {
    return {.succeeded = false,
            .win32_error = error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error};
}

[[nodiscard]] StartupRegistrationResult Success() noexcept {
    return {.succeeded = true, .win32_error = ERROR_SUCCESS};
}

class RegistryKey final {
  public:
    explicit RegistryKey(HKEY key = nullptr) noexcept : key_{key} {}
    ~RegistryKey() {
        if (key_ != nullptr) {
            static_cast<void>(::RegCloseKey(key_));
        }
    }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    RegistryKey(RegistryKey&&) = delete;
    RegistryKey& operator=(RegistryKey&&) = delete;

    [[nodiscard]] HKEY get() const noexcept {
        return key_;
    }

    void Reset(HKEY key) noexcept {
        if (key_ != nullptr) {
            static_cast<void>(::RegCloseKey(key_));
        }
        key_ = key;
    }

  private:
    HKEY key_;
};

[[nodiscard]] StartupRegistrationResult OpenRunKey(RegistryKey& key) noexcept {
    HKEY raw_key{};
    const auto status =
        ::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0U, nullptr, 0U, KEY_SET_VALUE, nullptr, &raw_key, nullptr);
    if (status != ERROR_SUCCESS) {
        return Failure(status);
    }
    key.Reset(raw_key);
    return Success();
}

[[nodiscard]] StartupRegistrationResult OpenExistingRunKey(RegistryKey& key) noexcept {
    HKEY raw_key{};
    const auto status =
        ::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0U,
                        KEY_SET_VALUE, &raw_key);
    if (status != ERROR_SUCCESS) {
        return Failure(status);
    }
    key.Reset(raw_key);
    return Success();
}

} // namespace

StartupRegistrationResult WindowsStartupRunKeyStoreApi::Set(std::wstring_view value_name,
                                                            std::wstring_view command) noexcept {
    if (value_name.empty() || command.empty() ||
        command.size() > (std::numeric_limits<DWORD>::max() / sizeof(wchar_t)) - 1U) {
        return Failure(ERROR_INVALID_PARAMETER);
    }
    try {
        RegistryKey key;
        const auto opened = OpenRunKey(key);
        if (!opened.ok()) {
            return opened;
        }
        const auto bytes = static_cast<DWORD>((command.size() + 1U) * sizeof(wchar_t));
        const std::wstring value{command};
        const auto status =
            ::RegSetValueExW(key.get(), std::wstring{value_name}.c_str(), 0U, REG_SZ,
                             reinterpret_cast<const BYTE*>(value.c_str()), bytes);
        return status == ERROR_SUCCESS ? Success() : Failure(status);
    } catch (...) {
        return Failure(ERROR_NOT_ENOUGH_MEMORY);
    }
}

StartupRegistrationResult WindowsStartupRunKeyStoreApi::Remove(
    std::wstring_view value_name) noexcept {
    if (value_name.empty()) {
        return Failure(ERROR_INVALID_PARAMETER);
    }
    try {
        RegistryKey key;
        const auto opened = OpenExistingRunKey(key);
        if (!opened.ok() && opened.win32_error == ERROR_FILE_NOT_FOUND) {
            return Success();
        }
        if (!opened.ok()) {
            return opened;
        }
        const auto status = ::RegDeleteValueW(key.get(), std::wstring{value_name}.c_str());
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? Success()
                                                                         : Failure(status);
    } catch (...) {
        return Failure(ERROR_NOT_ENOUGH_MEMORY);
    }
}

std::wstring StartupRegistration::CurrentExecutablePath() {
    std::wstring buffer(32'768U, L'\0');
    const auto length =
        ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return buffer;
}

std::wstring StartupRegistration::QuoteCommand(std::wstring_view path) {
    if (path.empty() || path.find(L'"') != std::wstring_view::npos) {
        return {};
    }
    return L"\"" + std::wstring{path} + L"\"";
}

StartupRegistrationResult StartupRegistration::Apply(bool enabled) noexcept {
    try {
        if (!enabled) {
            return store_->Remove(kStartupRunValueName);
        }
        const auto command = QuoteCommand(CurrentExecutablePath());
        if (command.empty()) {
            return Failure(ERROR_FILE_NOT_FOUND);
        }
        return store_->Set(kStartupRunValueName, command);
    } catch (...) {
        return Failure(ERROR_NOT_ENOUGH_MEMORY);
    }
}

} // namespace nfm::ui
