#include <windows.h>

#include <array>
#include <crtdbg.h>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

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
    HeadlessTestRuntimeInitializer() noexcept {
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
    }
};

// NOLINTNEXTLINE(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables)
const HeadlessTestRuntimeInitializer kHeadlessTestRuntimeInitializer{};

} // namespace

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
}
