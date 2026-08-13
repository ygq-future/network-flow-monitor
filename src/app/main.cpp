#include "network_flow_monitor/app/collector_runtime.h"
#include "network_flow_monitor/app/runtime_mode.h"
#include "network_flow_monitor/collector/etw_authorization.h"
#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/config/config_store.h"
#include "network_flow_monitor/service/service_control.h"
#include "network_flow_monitor/ui/ui_controller.h"

#include <windows.h>

#include <array>
#include <memory>
#include <shellapi.h>
#include <span>
#include <string_view>
#include <vector>

namespace {

struct LocalFreeDeleter {
    void operator()(wchar_t** value) const noexcept {
        if (value != nullptr) {
            ::LocalFree(static_cast<void*>(value));
        }
    }
};

int ResultCode(std::uint32_t error) noexcept {
    return error == ERROR_SUCCESS ? 0 : static_cast<int>(error);
}

int CollectorResultCode(const nfm::collector::CollectorHostResult& result) noexcept {
    return result.ok() ? 0 : ResultCode(result.source_error);
}

int CurrentExecutablePath(std::wstring& path) noexcept {
    try {
        std::vector<wchar_t> storage(32'768U);
        const auto length =
            ::GetModuleFileNameW(nullptr, storage.data(), static_cast<DWORD>(storage.size()));
        if (length == 0U) {
            return ResultCode(::GetLastError());
        }
        if (length >= storage.size()) {
            return ResultCode(ERROR_INSUFFICIENT_BUFFER);
        }
        path.assign(storage.data(), length);
        return 0;
    } catch (...) {
        return ResultCode(ERROR_NOT_ENOUGH_MEMORY);
    }
}

int RunPortableCollector(std::wstring_view stop_event_name) {
    const auto principal =
        nfm::app::BuildCollectorPrincipal(nfm::ipc::CollectorEndpointMode::Portable);
    if (!principal.ok()) {
        return ResultCode(principal.source_error);
    }
    nfm::app::CollectorRuntime runtime{nfm::ipc::CollectorEndpointMode::Portable};
    return CollectorResultCode(runtime.RunPortable(stop_event_name));
}

int RunServiceCollector() {
    const auto principal =
        nfm::app::BuildCollectorPrincipal(nfm::ipc::CollectorEndpointMode::Service);
    if (!principal.ok()) {
        return ResultCode(principal.source_error);
    }
    nfm::app::CollectorRuntime runtime{nfm::ipc::CollectorEndpointMode::Service};
    nfm::service::WindowsServiceRuntimeApi service_runtime;
    nfm::service::ServiceEntry entry{runtime, service_runtime};
    const auto result = entry.Dispatch();
    if (result.ok()) {
        return 0;
    }
    return ResultCode(result.win32_error == ERROR_SUCCESS ? ERROR_SERVICE_SPECIFIC_ERROR
                                                          : result.win32_error);
}

int RunAuthorizeCollector() {
    return ResultCode(nfm::collector::AuthorizeCollectorForCurrentUser());
}

int RunServiceAction(nfm::app::RuntimeMode mode) {
    std::wstring executable_path;
    const auto path_result = CurrentExecutablePath(executable_path);
    if (path_result != 0) {
        return path_result;
    }

    nfm::service::WindowsServiceApi service_api;
    nfm::service::ServiceControl control{service_api, std::move(executable_path)};
    nfm::service::ServiceResult result;
    switch (mode) {
    case nfm::app::RuntimeMode::Install:
        result = control.Install(nfm::service::ServiceStartType::AutomaticDelayed);
        break;
    case nfm::app::RuntimeMode::ServiceInstallManual:
        result = control.Install(nfm::service::ServiceStartType::Manual);
        break;
    case nfm::app::RuntimeMode::ServiceInstallAutomatic:
        result = control.Install(nfm::service::ServiceStartType::Automatic);
        break;
    case nfm::app::RuntimeMode::ServiceInstallAutomaticDelayed:
        result = control.Install(nfm::service::ServiceStartType::AutomaticDelayed);
        break;
    case nfm::app::RuntimeMode::Uninstall:
    case nfm::app::RuntimeMode::ServiceUninstall:
        result = control.Uninstall();
        break;
    case nfm::app::RuntimeMode::ServiceStart:
        result = control.Start();
        break;
    case nfm::app::RuntimeMode::ServiceStop:
        result = control.Stop();
        break;
    case nfm::app::RuntimeMode::ServiceRestart:
        result = control.Restart();
        break;
    case nfm::app::RuntimeMode::ServiceConfigureManual:
        result = control.ConfigureStartType(nfm::service::ServiceStartType::Manual);
        break;
    case nfm::app::RuntimeMode::ServiceConfigureAutomatic:
        result = control.ConfigureStartType(nfm::service::ServiceStartType::Automatic);
        break;
    case nfm::app::RuntimeMode::ServiceConfigureAutomaticDelayed:
        result = control.ConfigureStartType(nfm::service::ServiceStartType::AutomaticDelayed);
        break;
    case nfm::app::RuntimeMode::ServiceControl:
    case nfm::app::RuntimeMode::ServiceQuery:
        result = control.Query();
        break;
    default:
        return ResultCode(ERROR_INVALID_PARAMETER);
    }
    return ResultCode(result.win32_error);
}

int RunUi(HINSTANCE instance, bool smoke_test) {
    static_cast<void>(::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    auto config = nfm::config::DefaultAppConfig();
    if (!smoke_test) {
        std::wstring executable_path;
        if (CurrentExecutablePath(executable_path) == 0) {
            try {
                const auto loaded =
                    nfm::config::LoadConfigFile(nfm::config::PortableConfigPath(executable_path),
                                                nfm::config::ConfigDocumentKind::User);
                if (loaded.status == nfm::config::ConfigLoadStatus::Loaded ||
                    loaded.status == nfm::config::ConfigLoadStatus::LoadedWithDefaults) {
                    config = loaded.config;
                }
            } catch (...) {
                config = nfm::config::DefaultAppConfig();
            }
        }
    }
    if (smoke_test) {
        config.collector.preferred_mode = nfm::config::CollectorMode::Service;
    }
    nfm::ui::UiController controller{instance, config};
    return controller.Run(smoke_test);
}

int RunMode(nfm::app::RuntimeMode mode, HINSTANCE instance, bool smoke_test) {
    if (mode == nfm::app::RuntimeMode::Invalid) {
        ::MessageBoxW(nullptr, L"Unsupported command-line arguments.", L"Network Flow Monitor",
                      MB_OK | MB_ICONERROR);
        return ERROR_INVALID_PARAMETER;
    }

    try {
        switch (mode) {
        case nfm::app::RuntimeMode::Collector:
            return RunPortableCollector({});
        case nfm::app::RuntimeMode::Service:
            return RunServiceCollector();
        case nfm::app::RuntimeMode::AuthorizeCollector:
            return RunAuthorizeCollector();
        case nfm::app::RuntimeMode::Install:
        case nfm::app::RuntimeMode::Uninstall:
        case nfm::app::RuntimeMode::ServiceControl:
        case nfm::app::RuntimeMode::ServiceInstallManual:
        case nfm::app::RuntimeMode::ServiceInstallAutomatic:
        case nfm::app::RuntimeMode::ServiceInstallAutomaticDelayed:
        case nfm::app::RuntimeMode::ServiceUninstall:
        case nfm::app::RuntimeMode::ServiceStart:
        case nfm::app::RuntimeMode::ServiceStop:
        case nfm::app::RuntimeMode::ServiceRestart:
        case nfm::app::RuntimeMode::ServiceConfigureManual:
        case nfm::app::RuntimeMode::ServiceConfigureAutomatic:
        case nfm::app::RuntimeMode::ServiceConfigureAutomaticDelayed:
        case nfm::app::RuntimeMode::ServiceQuery:
            return RunServiceAction(mode);
        case nfm::app::RuntimeMode::Ui:
            return RunUi(instance, smoke_test);
        case nfm::app::RuntimeMode::Invalid:
            return ResultCode(ERROR_INVALID_PARAMETER);
        }
    } catch (...) {
        return ResultCode(ERROR_NOT_ENOUGH_MEMORY);
    }

    return ResultCode(ERROR_INVALID_PARAMETER);
}

nfm::app::RuntimeArguments ParseCommandLineArguments(wchar_t** arguments,
                                                     int argument_count) noexcept {
    if (arguments == nullptr || argument_count < 1) {
        return {};
    }
    if (argument_count == 1) {
        return {.mode = nfm::app::RuntimeMode::Ui};
    }
    if (argument_count != 2 && argument_count != 4) {
        return {};
    }

    std::array<std::wstring_view, 3> parsed{};
    parsed[0] = arguments[1];
    if (argument_count == 2) {
        return nfm::app::ParseRuntimeArguments(
            std::span<const std::wstring_view>{parsed.data(), 1});
    }
    parsed[1] = arguments[2];
    parsed[2] = arguments[3];
    return nfm::app::ParseRuntimeArguments(parsed);
}

bool IsUiSmokeTestArguments(wchar_t** arguments, int argument_count) noexcept {
    return arguments != nullptr && argument_count == 2 && arguments[1] != nullptr &&
           std::wstring_view{arguments[1]} == L"--ui-smoke-test";
}

} // namespace

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance, // NOLINT(bugprone-easily-swappable-parameters) -- Win32 ABI fixes two
                              // adjacent HINSTANCE parameters.
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine, // NOLINT(readability-non-const-parameter) -- Win32 ABI requires LPWSTR.
    _In_ int nShowCmd) {
    static_cast<void>(hPrevInstance);
    static_cast<void>(lpCmdLine);
    static_cast<void>(nShowCmd);

    int argument_count = 0;
    wchar_t** raw_arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
    if (raw_arguments == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    const std::unique_ptr<wchar_t*, LocalFreeDeleter> arguments_owner{raw_arguments};
    const auto smoke_test = IsUiSmokeTestArguments(raw_arguments, argument_count);
    const auto arguments = smoke_test
                               ? nfm::app::RuntimeArguments{.mode = nfm::app::RuntimeMode::Ui}
                               : ParseCommandLineArguments(raw_arguments, argument_count);
    if (arguments.mode == nfm::app::RuntimeMode::Invalid) {
        return RunMode(arguments.mode, hInstance, smoke_test);
    }
    if (arguments.mode == nfm::app::RuntimeMode::Collector) {
        return RunPortableCollector(arguments.collector_stop_event);
    }
    return RunMode(arguments.mode, hInstance, smoke_test);
}
