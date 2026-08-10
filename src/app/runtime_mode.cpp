#include "network_flow_monitor/app/runtime_mode.h"

namespace nfm::app {

RuntimeMode ParseRuntimeMode(std::span<const std::wstring_view> arguments) noexcept {
    if (arguments.empty()) {
        return RuntimeMode::Ui;
    }
    if (arguments.size() != 1) {
        return RuntimeMode::Invalid;
    }

    const auto argument = arguments.front();
    if (argument == L"--collector") {
        return RuntimeMode::Collector;
    }
    if (argument == L"--service") {
        return RuntimeMode::Service;
    }
    if (argument == L"--install") {
        return RuntimeMode::Install;
    }
    if (argument == L"--uninstall") {
        return RuntimeMode::Uninstall;
    }
    if (argument == L"--service-control") {
        return RuntimeMode::ServiceControl;
    }
    return RuntimeMode::Invalid;
}

} // namespace nfm::app
