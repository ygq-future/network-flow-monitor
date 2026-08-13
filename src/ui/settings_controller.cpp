#include "network_flow_monitor/ui/settings_controller.h"

#include <windows.h>

#include <array>
#include <shellapi.h>
#include <string_view>

namespace nfm::ui {
namespace {

[[nodiscard]] config::ConfigError ExceptionError(config::ConfigErrorCode code,
                                                 std::string detail) noexcept {
    return config::ConfigError{
        .code = code, .field_path = {}, .detail = std::move(detail), .system_error = 0U};
}

[[nodiscard]] SettingsActionResult Failure(std::uint32_t error) noexcept {
    return {.succeeded = false,
            .state = service::ServiceState::Unknown,
            .win32_error = error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error};
}

[[nodiscard]] std::wstring CurrentExecutablePath() {
    std::wstring path(32'768U, L'\0');
    const auto length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0U || length >= path.size()) {
        return {};
    }
    path.resize(length);
    return path;
}

[[nodiscard]] std::wstring CommandLineFor(SettingsServiceAction action,
                                          config::ServiceStartType start_type) {
    switch (action) {
    case SettingsServiceAction::Install:
        switch (start_type) {
        case config::ServiceStartType::Manual:
            return L"--service-install-manual";
        case config::ServiceStartType::Automatic:
            return L"--service-install-automatic";
        case config::ServiceStartType::AutomaticDelayed:
            return L"--service-install-automatic-delayed";
        }
        break;
    case SettingsServiceAction::Uninstall:
        return L"--service-uninstall";
    case SettingsServiceAction::Start:
        return L"--service-start";
    case SettingsServiceAction::Stop:
        return L"--service-stop";
    case SettingsServiceAction::Restart:
        return L"--service-restart";
    case SettingsServiceAction::ConfigureStartType:
        switch (start_type) {
        case config::ServiceStartType::Manual:
            return L"--service-configure-manual";
        case config::ServiceStartType::Automatic:
            return L"--service-configure-automatic";
        case config::ServiceStartType::AutomaticDelayed:
            return L"--service-configure-automatic-delayed";
        }
        break;
    case SettingsServiceAction::Query:
        return L"--service-query";
    case SettingsServiceAction::AuthorizeCollector:
        return L"--authorize-collector";
    }
    return {};
}

[[nodiscard]] service::ServiceState StateFromResult(const service::ServiceResult& result) noexcept {
    return result.state;
}

} // namespace

config::ConfigLoadResult WindowsSettingsStoreApi::Load(const std::filesystem::path& path,
                                                       config::ConfigDocumentKind kind) {
    try {
        return config::LoadConfigFile(path, kind);
    } catch (...) {
        return {.config = config::DefaultAppConfig(),
                .status = config::ConfigLoadStatus::IoError,
                .error = ExceptionError(config::ConfigErrorCode::ReadFailed,
                                        "configuration load raised an unexpected exception")};
    }
}

config::ConfigSaveResult WindowsSettingsStoreApi::Save(const std::filesystem::path& path,
                                                       const config::AppConfig& value,
                                                       config::ConfigDocumentKind kind) {
    try {
        return config::SaveConfigFile(path, value, kind);
    } catch (...) {
        return {.saved = false,
                .error = ExceptionError(config::ConfigErrorCode::WriteFailed,
                                        "configuration save raised an unexpected exception")};
    }
}

SettingsActionResult WindowsServiceActionApi::Execute(
    SettingsServiceAction action, config::ServiceStartType start_type) noexcept {
    try {
        if (action == SettingsServiceAction::Query) {
            auto executable = CurrentExecutablePath();
            if (executable.empty()) {
                return Failure(ERROR_FILE_NOT_FOUND);
            }
            service::WindowsServiceApi api;
            service::ServiceControl control{api, std::move(executable)};
            const auto result = control.Query();
            return {.succeeded = result.ok(),
                    .state = StateFromResult(result),
                    .win32_error = result.win32_error};
        }

        const auto parameters = CommandLineFor(action, start_type);
        const auto executable = CurrentExecutablePath();
        if (parameters.empty() || executable.empty()) {
            return Failure(ERROR_INVALID_PARAMETER);
        }

        SHELLEXECUTEINFOW info{.cbSize = sizeof(info),
                               .fMask = SEE_MASK_NOCLOSEPROCESS,
                               .hwnd = nullptr,
                               .lpVerb = L"runas",
                               .lpFile = executable.c_str(),
                               .lpParameters = parameters.c_str(),
                               .lpDirectory = nullptr,
                               .nShow = SW_HIDE};
        if (::ShellExecuteExW(&info) == FALSE || info.hProcess == nullptr) {
            return Failure(::GetLastError());
        }
        const auto process = info.hProcess;
        const auto wait_result = ::WaitForSingleObject(process, 15'000U);
        if (wait_result != WAIT_OBJECT_0) {
            const auto error = wait_result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ::GetLastError();
            static_cast<void>(::CloseHandle(process));
            return Failure(error);
        }
        DWORD exit_code{};
        const auto read_exit_code = ::GetExitCodeProcess(process, &exit_code);
        static_cast<void>(::CloseHandle(process));
        if (read_exit_code == FALSE) {
            return Failure(::GetLastError());
        }
        return {.succeeded = exit_code == ERROR_SUCCESS,
                .state = service::ServiceState::Unknown,
                .win32_error = exit_code};
    } catch (...) {
        return Failure(ERROR_NOT_ENOUGH_MEMORY);
    }
}

SettingsController::SettingsController(std::filesystem::path path, config::ConfigDocumentKind kind,
                                       SettingsStoreApi& store, ServiceActionApi& service) noexcept
    : path_{std::move(path)}, kind_{kind}, store_{&store}, service_{&service},
      config_{config::DefaultAppConfig()} {}

// NOLINTNEXTLINE(bugprone-exception-escape): the body catches allocation and store failures.
SettingsLoadResult SettingsController::Load() noexcept {
    try {
        const auto result = store_->Load(path_, kind_);
        config_ = result.config;
        return {.config = result.config, .status = result.status, .error = result.error};
    } catch (...) {
        const auto error = ExceptionError(config::ConfigErrorCode::ReadFailed,
                                          "configuration load raised an unexpected exception");
        config_ = config::DefaultAppConfig();
        return {.config = config_, .status = config::ConfigLoadStatus::IoError, .error = error};
    }
}

// NOLINTNEXTLINE(bugprone-exception-escape): the body catches allocation and store failures.
SettingsSaveResult SettingsController::Save(const config::AppConfig& value) noexcept {
    try {
        const auto validation = config::ValidateAppConfig(value);
        if (!validation.ok()) {
            return {.saved = false,
                    .error = config::ConfigError{
                        .code = config::ConfigErrorCode::OutOfRange,
                        .field_path = validation.issues.front().field_path,
                        .detail = "configuration value is outside the allowed range",
                        .system_error = ERROR_INVALID_DATA}};
        }
        const auto result = store_->Save(path_, value, kind_);
        if (result.saved) {
            config_ = value;
        }
        return {.saved = result.saved, .error = result.error};
    } catch (...) {
        return {.saved = false,
                .error = ExceptionError(config::ConfigErrorCode::WriteFailed,
                                        "configuration save raised an unexpected exception")};
    }
}

SettingsActionResult SettingsController::ExecuteService(
    SettingsServiceAction action, config::ServiceStartType start_type) noexcept {
    return service_->Execute(action, start_type);
}

} // namespace nfm::ui
