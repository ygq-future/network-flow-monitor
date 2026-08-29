#include "network_flow_monitor/host/app/product_host.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <vector>

int WINAPI wWinMain(_In_ HINSTANCE /*hInstance*/, _In_opt_ HINSTANCE /*hPrevInstance*/,
                    _In_ LPWSTR lpCmdLine, _In_ int /*nShowCmd*/
) {
    // 0. Enable Windows 11 Per-Monitor V2 DPI awareness for razor-sharp rendering
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        return 1;
    }

    // 0. Single instance verification
    HANDLE single_instance_mutex =
        CreateMutexW(nullptr, FALSE, L"Local\\NetworkFlowMonitor_SingleInstance_Mutex");
    if (single_instance_mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing_hwnd = FindWindowW(L"NetworkFlowMonitor_HostWindow", nullptr);
        if (existing_hwnd != nullptr) {
            ShowWindow(existing_hwnd, SW_RESTORE);
            SetForegroundWindow(existing_hwnd);
        }
        CloseHandle(single_instance_mutex);
        CoUninitialize();
        return 0;
    }

    nfm::host::app::ProductHostOptions options;

    // Determine executable directory
    std::array<wchar_t, MAX_PATH> exe_path_buf{};
    GetModuleFileNameW(nullptr, exe_path_buf.data(), MAX_PATH);
    std::filesystem::path exe_dir = std::filesystem::path(exe_path_buf.data()).parent_path();

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
