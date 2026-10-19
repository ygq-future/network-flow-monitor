#include "network_flow_monitor/app/collector_runtime.h"
#include "network_flow_monitor/app/runtime_mode.h"
#include "network_flow_monitor/build/dependencies.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

// === bootstrap_suite ===
namespace bootstrap_suite {
static_assert(sizeof(void*) == 8, "The first release supports x64 only");
static_assert(__cplusplus >= 202002L, "The project requires C++20 or newer");

int Run() {
    if (std::string_view{NFM_YYJSON_VERSION} != "0.12.0") {
        std::cerr << "Unexpected yyjson version\n";
        return 1;
    }

    return 0;
}
} // namespace bootstrap_suite

// === runtime_mode_suite ===
namespace runtime_mode_suite {
namespace {

void Check(bool condition, std::wstring_view case_name, int& failures) {
    if (!condition) {
        std::wcerr << L"Case failed: " << case_name << L'\n';
        ++failures;
    }
}

} // namespace

int Run() {
    int failures = 0;

    Check(nfm::app::ParseRuntimeArguments({}).mode == nfm::app::RuntimeMode::Collector,
          L"empty arguments start collector", failures);

    const std::array<std::wstring_view, 1> collector{L"--collector"};
    Check(nfm::app::ParseRuntimeArguments(collector).mode == nfm::app::RuntimeMode::Collector,
          L"collector compatibility switch", failures);

    const std::array<std::wstring_view, 4> owned{
        L"--stop-event", L"Local\\NetworkFlowMonitor.Stop.test", L"--parent-pid", L"1234"};
    const auto parsed = nfm::app::ParseRuntimeArguments(owned);
    Check(parsed.mode == nfm::app::RuntimeMode::Collector &&
              parsed.collector_stop_event == owned[1] && parsed.parent_pid == 1234U,
          L"owned collector arguments", failures);

    const std::array<std::wstring_view, 2> invalid_event{L"--stop-event", L"Global\\not-allowed"};
    Check(nfm::app::ParseRuntimeArguments(invalid_event).mode == nfm::app::RuntimeMode::Invalid,
          L"invalid stop event", failures);

    const std::array<std::wstring_view, 1> authorize{L"--authorize-collector"};
    Check(nfm::app::ParseRuntimeArguments(authorize).mode ==
              nfm::app::RuntimeMode::AuthorizeCollector,
          L"authorization mode", failures);

    const std::array<std::wstring_view, 1> unknown{L"--unknown"};
    Check(nfm::app::ParseRuntimeArguments(unknown).mode == nfm::app::RuntimeMode::Invalid,
          L"unknown switch", failures);
    return failures;
}
} // namespace runtime_mode_suite

// === runtime_collector_suite ===
namespace runtime_collector_suite {
namespace {

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

} // namespace

int Run() {
    TestContext context;
    const auto principal = nfm::app::BuildCollectorPrincipal();
    context.Check(principal.ok() && nfm::ipc::ValidateAuthorizedPrincipal(principal.principal) ==
                                        nfm::ipc::PipeSecurityError::None,
                  "runtime builds a valid owner-scoped principal");
    context.Check(!principal.principal.authorized_sid.empty() &&
                      !principal.principal.server_logon_sid.empty(),
                  "principal carries the current logon SID");

    const auto first = nfm::app::CreateCollectorInstanceId();
    const auto second = nfm::app::CreateCollectorInstanceId();
    context.Check(!first.empty() && !second.empty(), "runtime instances are non-empty");
    context.Check(first != second, "successive runtime instances are distinct");
    return context.failures == 0 ? 0 : 1;
}
} // namespace runtime_collector_suite

int main() {
    int failures = 0;
    failures += (bootstrap_suite::Run() != 0 ? 1 : 0);
    failures += (runtime_mode_suite::Run() != 0 ? 1 : 0);
    failures += (runtime_collector_suite::Run() != 0 ? 1 : 0);
    return failures;
}
