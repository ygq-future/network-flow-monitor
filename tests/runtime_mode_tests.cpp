#include "network_flow_monitor/app/runtime_mode.h"

#include <array>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string_view>

namespace {

bool ExpectMode(std::initializer_list<std::wstring_view> arguments, nfm::app::RuntimeMode expected,
                std::wstring_view case_name) {
    const auto actual = nfm::app::ParseRuntimeMode(
        std::span<const std::wstring_view>{arguments.begin(), arguments.size()});
    if (actual == expected) {
        return true;
    }

    std::wcerr << L"Case failed: " << case_name << L'\n';
    return false;
}

} // namespace

int main() {
    int failures = 0;
    const auto check = [&failures](std::initializer_list<std::wstring_view> arguments,
                                   nfm::app::RuntimeMode expected, std::wstring_view case_name) {
        if (!ExpectMode(arguments, expected, case_name)) {
            ++failures;
        }
    };

    check({}, nfm::app::RuntimeMode::Ui, L"empty arguments select UI");
    check({L"--collector"}, nfm::app::RuntimeMode::Collector, L"collector");
    const std::array<std::wstring_view, 3> collector_with_event{
        L"--collector", L"--stop-event", L"Local\\NetworkFlowMonitor.Stop.test"};
    const auto parsed_with_event = nfm::app::ParseRuntimeArguments(collector_with_event);
    if (parsed_with_event.mode != nfm::app::RuntimeMode::Collector ||
        parsed_with_event.collector_stop_event != collector_with_event.back()) {
        std::wcerr << L"Case failed: collector stop event\n";
        ++failures;
    }
    const std::array<std::wstring_view, 3> invalid_event{L"--collector", L"--stop-event",
                                                         L"Global\\not-allowed"};
    if (nfm::app::ParseRuntimeArguments(invalid_event).mode != nfm::app::RuntimeMode::Invalid) {
        std::wcerr << L"Case failed: invalid collector stop event\n";
        ++failures;
    }
    check({L"--service"}, nfm::app::RuntimeMode::Service, L"service");
    check({L"--install"}, nfm::app::RuntimeMode::Install, L"install");
    check({L"--uninstall"}, nfm::app::RuntimeMode::Uninstall, L"uninstall");
    check({L"--service-control"}, nfm::app::RuntimeMode::ServiceControl, L"service control");
    check({L"--service-install-manual"}, nfm::app::RuntimeMode::ServiceInstallManual,
          L"manual service install");
    check({L"--service-install-automatic"}, nfm::app::RuntimeMode::ServiceInstallAutomatic,
          L"automatic service install");
    check({L"--service-install-automatic-delayed"},
          nfm::app::RuntimeMode::ServiceInstallAutomaticDelayed, L"delayed service install");
    check({L"--service-start"}, nfm::app::RuntimeMode::ServiceStart, L"service start");
    check({L"--service-stop"}, nfm::app::RuntimeMode::ServiceStop, L"service stop");
    check({L"--service-restart"}, nfm::app::RuntimeMode::ServiceRestart, L"service restart");
    check({L"--service-query"}, nfm::app::RuntimeMode::ServiceQuery, L"service query");
    check({L"--unknown"}, nfm::app::RuntimeMode::Invalid, L"unknown switch");
    check({L"--service", L"extra"}, nfm::app::RuntimeMode::Invalid, L"multiple arguments");
    return failures;
}
