#include "network_flow_monitor/app/collector_runtime.h"
#include "network_flow_monitor/app/runtime_mode.h"
#include "network_flow_monitor/collector/etw_authorization.h"

#include <windows.h>

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

} // namespace

int wmain() {
    int argument_count = 0;
    wchar_t** raw_arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
    if (raw_arguments == nullptr) {
        return ResultCode(ERROR_INVALID_PARAMETER);
    }
    const std::unique_ptr<wchar_t*, LocalFreeDeleter> owner{raw_arguments};

    std::vector<std::wstring_view> arguments;
    arguments.reserve(argument_count > 1 ? static_cast<std::size_t>(argument_count - 1) : 0U);
    for (int index = 1; index < argument_count; ++index) {
        if (raw_arguments[index] != nullptr) {
            arguments.emplace_back(raw_arguments[index]);
        }
    }

    const auto parsed_args = nfm::app::ParseRuntimeArguments(arguments);
    if (parsed_args.mode == nfm::app::RuntimeMode::AuthorizeCollector) {
        return ResultCode(nfm::collector::AuthorizeCollectorForCurrentUser());
    }
    if (parsed_args.mode != nfm::app::RuntimeMode::Collector) {
        return ResultCode(ERROR_INVALID_PARAMETER);
    }

    if (nfm::collector::IsCurrentProcessElevated()) {
        static_cast<void>(nfm::collector::AuthorizeCollectorForCurrentUser());
    }
    static_cast<void>(nfm::collector::EnableCollectorPrivileges());

    try {
        const auto principal = nfm::app::BuildCollectorPrincipal();
        if (!principal.ok()) {
            return ResultCode(principal.source_error);
        }
        nfm::app::CollectorRuntime runtime;
        const auto result =
            runtime.RunPortable(parsed_args.collector_stop_event, parsed_args.parent_pid);
        return result.ok() ? 0 : ResultCode(result.source_error);
    } catch (...) {
        return ResultCode(ERROR_NOT_ENOUGH_MEMORY);
    }
}
