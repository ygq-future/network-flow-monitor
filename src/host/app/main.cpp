#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/host/app/product_host.h"
#include "network_flow_monitor/host/platform/rotating_file_logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Startup diagnostics run before ProductHost owns the logger, so the log directory is resolved once
// here and shared with the unhandled-exception filter.
std::filesystem::path& StartupLogDirectory() {
    static std::filesystem::path directory;
    return directory;
}

void WriteStartupLog(nfm::config::LogLevel level, std::string_view message) noexcept {
    try {
        const auto& directory = StartupLogDirectory();
        if (directory.empty()) {
            return;
        }
        nfm::host::platform::RotatingFileLogger logger({.log_directory = directory});
        logger.write(nfm::config::LogLevel::Info, level, message);
    } catch (...) {
    }
}

// Records a startup crash that happens before the host can log anything itself. WER still receives
// the exception because the handler returns EXCEPTION_CONTINUE_SEARCH.
LONG WINAPI StartupCrashFilter(EXCEPTION_POINTERS* info) noexcept {
    try {
        const auto* record = info != nullptr ? info->ExceptionRecord : nullptr;
        if (record != nullptr) {
            const void* fault_address = record->ExceptionAddress;
            std::array<wchar_t, MAX_PATH> module_path{};
            std::uintptr_t module_base = 0;
            MEMORY_BASIC_INFORMATION region{};
            if (::VirtualQuery(fault_address, &region, sizeof(region)) != 0U) {
                module_base = reinterpret_cast<std::uintptr_t>(region.AllocationBase);
                static_cast<void>(::GetModuleFileNameW(static_cast<HMODULE>(region.AllocationBase),
                                                       module_path.data(),
                                                       static_cast<DWORD>(module_path.size())));
            }
            std::array<char, MAX_PATH * 3> module_utf8{};
            const int module_length =
                ::WideCharToMultiByte(CP_UTF8, 0, module_path.data(), -1, module_utf8.data(),
                                      static_cast<int>(module_utf8.size()) - 1, nullptr, nullptr);
            const char* module = module_length > 0 ? module_utf8.data() : "<unknown>";
            const char* access = "n/a";
            unsigned long long target = 0ULL;
            if (record->NumberParameters >= 2U) {
                access = record->ExceptionInformation[0] == 1U ? "write" : "read";
                target = static_cast<unsigned long long>(record->ExceptionInformation[1]);
            }
            std::array<char, 512> message{};
            static_cast<void>(
                std::snprintf(message.data(), message.size(),
                              "Unhandled exception 0x%08lX at %s+0x%llX (%s address=0x%llX)",
                              static_cast<unsigned long>(record->ExceptionCode), module,
                              static_cast<unsigned long long>(
                                  reinterpret_cast<std::uintptr_t>(fault_address) - module_base),
                              access, target));
            WriteStartupLog(nfm::config::LogLevel::Error, message.data());
        }
    } catch (...) {
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE /*hInstance*/, _In_opt_ HINSTANCE /*hPrevInstance*/,
                    _In_ LPWSTR lpCmdLine, _In_ int /*nShowCmd*/
) {
    try {
        StartupLogDirectory() = nfm::config::PortableLogDirectory(
            nfm::host::platform::StartupRegistration::current_executable_path());
    } catch (...) {
    }
    static_cast<void>(::SetUnhandledExceptionFilter(&StartupCrashFilter));

    // 0. Enable Windows 11 Per-Monitor V2 DPI awareness for razor-sharp rendering
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        WriteStartupLog(nfm::config::LogLevel::Error,
                        "Host startup aborted: CoInitializeEx failed");
        return 1;
    }

    // 0. Single instance verification
    HANDLE single_instance_mutex =
        CreateMutexW(nullptr, FALSE, L"Local\\NetworkFlowMonitor_SingleInstance_Mutex");
    if (single_instance_mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing_hwnd = FindWindowW(L"NetworkFlowMonitor_HostWindow", nullptr);
        if (existing_hwnd != nullptr) {
            DWORD owner_pid = 0;
            static_cast<void>(GetWindowThreadProcessId(existing_hwnd, &owner_pid));
            WriteStartupLog(nfm::config::LogLevel::Info,
                            "Host startup skipped: another instance owns the single-instance "
                            "mutex; activated its window (pid=" +
                                std::to_string(owner_pid) + ")");
            ShowWindow(existing_hwnd, SW_RESTORE);
            SetForegroundWindow(existing_hwnd);
        } else {
            WriteStartupLog(nfm::config::LogLevel::Error,
                            "Host startup aborted: the single-instance mutex is held but no host "
                            "window exists");
        }
        CloseHandle(single_instance_mutex);
        CoUninitialize();
        return 0;
    }
    WriteStartupLog(nfm::config::LogLevel::Info,
                    "Host process starting (pid=" + std::to_string(::GetCurrentProcessId()) + ")");

    nfm::host::app::ProductHostOptions options;

    // Determine executable directory
    const std::filesystem::path exe_dir =
        nfm::host::platform::StartupRegistration::current_executable_path().parent_path();

    options.collector_executable_path = exe_dir / "nfm-collector.exe";

    // Auto-discover static assets directory
    std::filesystem::path static_dir = exe_dir / "ui";
    if (!std::filesystem::exists(static_dir / "index.html")) {
        const std::array<std::filesystem::path, 4> candidates = {
            exe_dir / "apps" / "nfm-ui" / "dist",
            exe_dir.parent_path() / "apps" / "nfm-ui" / "dist",
            exe_dir.parent_path().parent_path() / "apps" / "nfm-ui" / "dist",
            exe_dir.parent_path().parent_path().parent_path() / "apps" / "nfm-ui" / "dist",
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate / "index.html")) {
                static_dir = candidate;
                break;
            }
        }
    }
    options.static_assets_dir = static_dir;

    // Parse simple command line flags
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    if (argv != nullptr) {
        for (int i = 0; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--dev-server" && i + 1 < argc) {
                options.dev_server_url = argv[++i];
            } else if (arg == L"--static-dir" && i + 1 < argc) {
                options.static_assets_dir = argv[++i];
            } else if (arg == L"--collector" && i + 1 < argc) {
                options.collector_executable_path = argv[++i];
            }
        }
        LocalFree(static_cast<HLOCAL>(argv));
    }

    int exit_code = 0;
    {
        nfm::host::app::ProductHost host(options);
        if (host.initialize()) {
            exit_code = host.run();
        } else {
            exit_code = 1;
        }
    }

    if (single_instance_mutex != nullptr) {
        CloseHandle(single_instance_mutex);
        single_instance_mutex = nullptr;
    }

    CoUninitialize();
    return exit_code;
}
