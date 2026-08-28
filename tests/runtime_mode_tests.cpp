#include "network_flow_monitor/app/runtime_mode.h"

#include <array>
#include <iostream>
#include <span>
#include <string_view>

namespace {

void Check(bool condition, std::wstring_view case_name, int& failures) {
    if (!condition) {
        std::wcerr << L"Case failed: " << case_name << L'\n';
        ++failures;
    }
}

} // namespace

int main() {
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
