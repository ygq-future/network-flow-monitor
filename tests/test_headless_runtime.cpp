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
int nfm_suite_bootstrap_tests();
int nfm_suite_runtime_mode_tests();
int nfm_suite_runtime_collector_integration_tests();
int nfm_suite_config_model_tests();
int nfm_suite_config_codec_tests();
int nfm_suite_config_store_tests();
int nfm_suite_application_accumulator_tests();
int nfm_suite_ranking_engine_tests();
int nfm_suite_identity_resolver_tests();
int nfm_suite_process_tracker_tests();
int nfm_suite_windows_process_probe_tests(int argc, char** argv);
int nfm_suite_network_event_decoder_tests();
int nfm_suite_collector_core_tests();
int nfm_suite_collector_host_tests();
int nfm_suite_ipc_frame_codec_tests();
int nfm_suite_ipc_snapshot_codec_tests();
int nfm_suite_ipc_application_catalog_tests();
int nfm_suite_ipc_security_policy_tests();
int nfm_suite_ipc_named_pipe_transport_tests();
int nfm_suite_ipc_server_tests();
int nfm_suite_ipc_server_integration_tests();
int nfm_suite_ipc_snapshot_publisher_tests();
int nfm_suite_ipc_client_tests();
int nfm_suite_ipc_integration_tests();
int nfm_suite_windows_etw_record_decoder_tests();
int nfm_suite_windows_etw_session_tests();
int nfm_suite_etw_collector_integration_tests(int argc, char** argv);
int nfm_suite_host_bridge_tests();
int nfm_suite_host_webview_policy_tests();
int nfm_suite_host_lifecycle_tests();
int nfm_suite_dock_capsule_coordinator_tests();
// NOLINTEND(readability-identifier-naming)

namespace {

struct SuiteEntry {
    std::string_view name;
    int (*run_no_args)();
    int (*run_with_args)(int, char**);
    bool requires_admin{false};
};

constexpr std::array<SuiteEntry, 31> kSuites = {{
    {.name = "bootstrap.toolchain",
     .run_no_args = &nfm_suite_bootstrap_tests,
     .run_with_args = nullptr},
    {.name = "app.runtime_mode",
     .run_no_args = &nfm_suite_runtime_mode_tests,
     .run_with_args = nullptr},
    {.name = "app.runtime_collector_integration",
     .run_no_args = &nfm_suite_runtime_collector_integration_tests,
     .run_with_args = nullptr},
    {.name = "config.model",
     .run_no_args = &nfm_suite_config_model_tests,
     .run_with_args = nullptr},
    {.name = "config.codec",
     .run_no_args = &nfm_suite_config_codec_tests,
     .run_with_args = nullptr},
    {.name = "config.store",
     .run_no_args = &nfm_suite_config_store_tests,
     .run_with_args = nullptr},
    {.name = "ranking.accumulator",
     .run_no_args = &nfm_suite_application_accumulator_tests,
     .run_with_args = nullptr},
    {.name = "ranking.engine",
     .run_no_args = &nfm_suite_ranking_engine_tests,
     .run_with_args = nullptr},
    {.name = "identity.resolver",
     .run_no_args = &nfm_suite_identity_resolver_tests,
     .run_with_args = nullptr},
    {.name = "process.tracker",
     .run_no_args = &nfm_suite_process_tracker_tests,
     .run_with_args = nullptr},
    {.name = "process.windows_probe",
     .run_no_args = nullptr,
     .run_with_args = &nfm_suite_windows_process_probe_tests},
    {.name = "collector.network_decoder",
     .run_no_args = &nfm_suite_network_event_decoder_tests,
     .run_with_args = nullptr},
    {.name = "collector.core",
     .run_no_args = &nfm_suite_collector_core_tests,
     .run_with_args = nullptr},
    {.name = "collector.host",
     .run_no_args = &nfm_suite_collector_host_tests,
     .run_with_args = nullptr},
    {.name = "ipc.frame_codec",
     .run_no_args = &nfm_suite_ipc_frame_codec_tests,
     .run_with_args = nullptr},
    {.name = "ipc.snapshot_codec",
     .run_no_args = &nfm_suite_ipc_snapshot_codec_tests,
     .run_with_args = nullptr},
    {.name = "ipc.application_catalog",
     .run_no_args = &nfm_suite_ipc_application_catalog_tests,
     .run_with_args = nullptr},
    {.name = "ipc.security_policy",
     .run_no_args = &nfm_suite_ipc_security_policy_tests,
     .run_with_args = nullptr},
    {.name = "ipc.named_pipe_transport",
     .run_no_args = &nfm_suite_ipc_named_pipe_transport_tests,
     .run_with_args = nullptr},
    {.name = "ipc.server", .run_no_args = &nfm_suite_ipc_server_tests, .run_with_args = nullptr},
    {.name = "ipc.server_integration",
     .run_no_args = &nfm_suite_ipc_server_integration_tests,
     .run_with_args = nullptr},
    {.name = "ipc.snapshot_publisher",
     .run_no_args = &nfm_suite_ipc_snapshot_publisher_tests,
     .run_with_args = nullptr},
    {.name = "ipc.client", .run_no_args = &nfm_suite_ipc_client_tests, .run_with_args = nullptr},
    {.name = "ipc.integration",
     .run_no_args = &nfm_suite_ipc_integration_tests,
     .run_with_args = nullptr,
     .requires_admin = true},
    {.name = "collector.windows_record_decoder",
     .run_no_args = &nfm_suite_windows_etw_record_decoder_tests,
     .run_with_args = nullptr},
    {.name = "collector.windows_session",
     .run_no_args = &nfm_suite_windows_etw_session_tests,
     .run_with_args = nullptr},
    {.name = "collector.etw_integration",
     .run_no_args = nullptr,
     .run_with_args = &nfm_suite_etw_collector_integration_tests,
     .requires_admin = true},
    {.name = "host.bridge", .run_no_args = &nfm_suite_host_bridge_tests, .run_with_args = nullptr},
    {.name = "host.webview_policy",
     .run_no_args = &nfm_suite_host_webview_policy_tests,
     .run_with_args = nullptr},
    {.name = "host.lifecycle",
     .run_no_args = &nfm_suite_host_lifecycle_tests,
     .run_with_args = nullptr},
    {.name = "host.coordinator",
     .run_no_args = &nfm_suite_dock_capsule_coordinator_tests,
     .run_with_args = nullptr},
}};

} // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        const std::string_view arg{argv[1]};
        if (arg == "--probe-child") {
            return nfm_suite_windows_process_probe_tests(argc, argv);
        }
        if (arg == "--child" || arg == "--expect-access-denied") {
            return nfm_suite_etw_collector_integration_tests(argc, argv);
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
