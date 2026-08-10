#include "network_flow_monitor/app/runtime_mode.h"

#include <windows.h>

#include <memory>
#include <shellapi.h>
#include <span>
#include <string_view>

namespace {

struct LocalFreeDeleter {
    void operator()(wchar_t** value) const noexcept {
        if (value != nullptr) {
            ::LocalFree(static_cast<void*>(value));
        }
    }
};

int RunMode(nfm::app::RuntimeMode mode) {
    if (mode == nfm::app::RuntimeMode::Invalid) {
        ::MessageBoxW(nullptr, L"Unsupported command-line arguments.", L"Network Flow Monitor",
                      MB_OK | MB_ICONERROR);
        return ERROR_INVALID_PARAMETER;
    }

    if (mode != nfm::app::RuntimeMode::Ui) {
        return ERROR_CALL_NOT_IMPLEMENTED;
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
