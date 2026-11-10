#include "test_headless_runtime.h"

#include <windows.h>

#include <array>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <string_view>

namespace {

HDESK& headless_desktop_slot() noexcept {
    static HDESK desktop = nullptr;
    return desktop;
}

HDESK create_headless_desktop() noexcept {
    std::array<wchar_t, 64> name{};
    if (std::swprintf(name.data(), name.size(), L"NFM_Headless_Test_Desktop_%lu",
                      static_cast<unsigned long>(GetCurrentProcessId())) < 0) {
        return nullptr;
    }
    return CreateDesktopW(name.data(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
}

int HeadlessCrtReportHook(int /*report_type*/, char* message, int* return_value) noexcept {
    if (message != nullptr) {
        OutputDebugStringA(message);
    }
    if (return_value != nullptr) {
        *return_value = 0;
    }
    // Returning FALSE allows _CrtSetReportMode(_CRTDBG_MODE_FILE) to write to stderr,
    // while preventing any GUI dialog box from opening.
    return FALSE;
}

struct HeadlessTestRuntimeInitializer {
    HDESK original_desktop_{nullptr};
    HDESK headless_desktop_{nullptr};

    HeadlessTestRuntimeInitializer() noexcept
        : original_desktop_(GetThreadDesktop(GetCurrentThreadId())),
          headless_desktop_(create_headless_desktop()) {
        static_cast<void>(SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS |
                                       SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX));
        static_cast<void>(_set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
        static_cast<void>(_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &HeadlessCrtReportHook));
#ifdef _DEBUG
        static_cast<void>(_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR));
        static_cast<void>(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR));
        static_cast<void>(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR));
#endif
        if (headless_desktop_ == nullptr) {
            static_cast<void>(std::fprintf(
                stderr,
                "Failed to create the headless test desktop (error %lu); refusing to run tests "
                "on the interactive desktop\n",
                static_cast<unsigned long>(GetLastError())));
            std::_Exit(2);
        }
        headless_desktop_slot() = headless_desktop_;
        if (SetThreadDesktop(headless_desktop_) == FALSE) {
            static_cast<void>(std::fprintf(
                stderr,
                "Failed to bind the test thread to the headless desktop (error %lu); refusing to "
                "run tests on the interactive desktop\n",
                static_cast<unsigned long>(GetLastError())));
            std::_Exit(2);
        }
    }

    HeadlessTestRuntimeInitializer(const HeadlessTestRuntimeInitializer&) = delete;
    HeadlessTestRuntimeInitializer& operator=(const HeadlessTestRuntimeInitializer&) = delete;
    HeadlessTestRuntimeInitializer(HeadlessTestRuntimeInitializer&&) = delete;
    HeadlessTestRuntimeInitializer& operator=(HeadlessTestRuntimeInitializer&&) = delete;

    ~HeadlessTestRuntimeInitializer() {
        headless_desktop_slot() = nullptr;
        if (original_desktop_ != nullptr) {
            static_cast<void>(SetThreadDesktop(original_desktop_));
        }
        if (headless_desktop_ != nullptr) {
            static_cast<void>(CloseDesktop(headless_desktop_));
        }
    }
};
// NOLINTNEXTLINE(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables)
const HeadlessTestRuntimeInitializer kHeadlessTestRuntimeInitializer{};

} // namespace

HDESK headless_test_desktop() noexcept {
    return headless_desktop_slot();
}

bool is_on_headless_test_desktop() noexcept {
    HDESK expected = headless_desktop_slot();
    HDESK current = GetThreadDesktop(GetCurrentThreadId());
    if (expected == nullptr || current == nullptr) {
        return false;
    }
    std::array<wchar_t, 64> expected_name{};
    std::array<wchar_t, 64> current_name{};
    DWORD needed = 0;
    if (GetUserObjectInformationW(expected, UOI_NAME, expected_name.data(),
                                  static_cast<DWORD>(expected_name.size() * sizeof(wchar_t)),
                                  &needed) == FALSE) {
        return false;
    }
    if (GetUserObjectInformationW(current, UOI_NAME, current_name.data(),
                                  static_cast<DWORD>(current_name.size() * sizeof(wchar_t)),
                                  &needed) == FALSE) {
        return false;
    }
    return std::wcscmp(expected_name.data(), current_name.data()) == 0;
}

bool enter_headless_test_desktop() noexcept {
    HDESK desktop = headless_desktop_slot();
    if (desktop == nullptr || SetThreadDesktop(desktop) == FALSE) {
        return false;
    }
    return is_on_headless_test_desktop();
}

// NOLINTBEGIN(readability-identifier-naming)
int nfm_suite_app_runtime_tests();
int nfm_suite_config_tests();
int nfm_suite_process_tests(int argc, char** argv);
int nfm_suite_ranking_tests();
int nfm_suite_collector_tests();
int nfm_suite_etw_integration_tests(int argc, char** argv);
int nfm_suite_ipc_protocol_tests();
int nfm_suite_ipc_session_tests();
int nfm_suite_ipc_integration_tests();
int nfm_suite_host_tests();
// NOLINTEND(readability-identifier-naming)

namespace {

struct SuiteEntry {
    std::string_view name;
    int (*run_no_args)();
    int (*run_with_args)(int, char**);
    bool requires_admin{false};
};

constexpr std::array<SuiteEntry, 10> kSuites = {{
    {.name = "app.runtime", .run_no_args = &nfm_suite_app_runtime_tests, .run_with_args = nullptr},
    {.name = "config.suite", .run_no_args = &nfm_suite_config_tests, .run_with_args = nullptr},
    {.name = "process.suite", .run_no_args = nullptr, .run_with_args = &nfm_suite_process_tests},
    {.name = "ranking.suite", .run_no_args = &nfm_suite_ranking_tests, .run_with_args = nullptr},
    {.name = "collector.suite",
     .run_no_args = &nfm_suite_collector_tests,
     .run_with_args = nullptr},
    {.name = "collector.etw_integration",
     .run_no_args = nullptr,
     .run_with_args = &nfm_suite_etw_integration_tests,
     .requires_admin = true},
    {.name = "ipc.protocol",
     .run_no_args = &nfm_suite_ipc_protocol_tests,
     .run_with_args = nullptr},
    {.name = "ipc.session", .run_no_args = &nfm_suite_ipc_session_tests, .run_with_args = nullptr},
    {.name = "ipc.integration",
     .run_no_args = &nfm_suite_ipc_integration_tests,
     .run_with_args = nullptr,
     .requires_admin = true},
    {.name = "host.suite", .run_no_args = &nfm_suite_host_tests, .run_with_args = nullptr},
}};

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            const std::string_view arg{argv[1]};
            if (arg == "--probe-child") {
                return nfm_suite_process_tests(argc, argv);
            }
            if (arg == "--child" || arg == "--expect-access-denied") {
                return nfm_suite_etw_integration_tests(argc, argv);
            }
            for (const auto& suite : kSuites) {
                if (suite.name == arg) {
                    if (suite.run_no_args != nullptr) {
                        return suite.run_no_args();
                    }
                    std::array<char*, 1> single_arg{argv[0]};
                    return suite.run_with_args(1, single_arg.data());
                }
            }
            std::cerr << "Unknown test suite: " << arg << '\n';
            return 1;
        }

        int failed_suites = 0;
        for (const auto& suite : kSuites) {
            if (suite.requires_admin) {
                continue;
            }
            int rc = 0;
            if (suite.run_no_args != nullptr) {
                rc = suite.run_no_args();
            } else {
                std::array<char*, 1> single_arg{argv[0]};
                rc = suite.run_with_args(1, single_arg.data());
            }
            if (rc != 0) {
                std::cerr << "[FAIL] " << suite.name << '\n';
                ++failed_suites;
            }
        }
        return failed_suites;
    } catch (...) {
        return 1;
    }
}
