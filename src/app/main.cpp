#include "network_flow_monitor/app/collector_runtime.h"
#include "network_flow_monitor/app/runtime_mode.h"
#include "network_flow_monitor/service/service_control.h"

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

int RunPortableCollector() {
    const auto principal =
        nfm::app::BuildCollectorPrincipal(nfm::ipc::CollectorEndpointMode::Portable);
    if (!principal.ok()) {
        return ResultCode(principal.source_error);
    }
    nfm::app::CollectorRuntime runtime{nfm::ipc::CollectorEndpointMode::Portable};
    return CollectorResultCode(runtime.RunPortable());
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
    case nfm::app::RuntimeMode::Uninstall:
        result = control.Uninstall();
        break;
    case nfm::app::RuntimeMode::ServiceControl:
        result = control.Query();
        break;
    default:
        return ResultCode(ERROR_INVALID_PARAMETER);
    }
    return ResultCode(result.win32_error);
}

int RunMode(nfm::app::RuntimeMode mode) {
    if (mode == nfm::app::RuntimeMode::Invalid) {
        ::MessageBoxW(nullptr, L"Unsupported command-line arguments.", L"Network Flow Monitor",
                      MB_OK | MB_ICONERROR);
        return ERROR_INVALID_PARAMETER;
    }

    try {
        switch (mode) {
        case nfm::app::RuntimeMode::Collector:
            return RunPortableCollector();
        case nfm::app::RuntimeMode::Service:
            return RunServiceCollector();
        case nfm::app::RuntimeMode::Install:
        case nfm::app::RuntimeMode::Uninstall:
        case nfm::app::RuntimeMode::ServiceControl:
            return RunServiceAction(mode);
        case nfm::app::RuntimeMode::Ui:
            break;
        case nfm::app::RuntimeMode::Invalid:
            return ResultCode(ERROR_INVALID_PARAMETER);
        }
    } catch (...) {
        return ResultCode(ERROR_NOT_ENOUGH_MEMORY);
    }

    ::MessageBoxW(nullptr, L"Network Flow Monitor project bootstrap is ready.",
                  L"Network Flow Monitor", MB_OK | MB_ICONINFORMATION);
    return 0;
}

nfm::app::RuntimeMode ParseCommandLineArguments(wchar_t** arguments, int argument_count) noexcept {
    if (arguments == nullptr || argument_count < 1) {
        return nfm::app::RuntimeMode::Invalid;
    }
    if (argument_count == 1) {
        return nfm::app::ParseRuntimeMode({});
    }
    if (argument_count != 2) {
        return nfm::app::RuntimeMode::Invalid;
    }

    const std::wstring_view mode_argument{arguments[1]};
    return nfm::app::ParseRuntimeMode(std::span<const std::wstring_view>{&mode_argument, 1});
}

} // namespace

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance, // NOLINT(bugprone-easily-swappable-parameters) -- Win32 ABI fixes two
                              // adjacent HINSTANCE parameters.
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine, // NOLINT(readability-non-const-parameter) -- Win32 ABI requires LPWSTR.
    _In_ int nShowCmd) {
    static_cast<void>(hInstance);
    static_cast<void>(hPrevInstance);
    static_cast<void>(lpCmdLine);
    static_cast<void>(nShowCmd);

    int argument_count = 0;
    wchar_t** raw_arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
    if (raw_arguments == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    const std::unique_ptr<wchar_t*, LocalFreeDeleter> arguments_owner{raw_arguments};
    return RunMode(ParseCommandLineArguments(raw_arguments, argument_count));
}
