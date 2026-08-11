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
    if (argument == L"--service-install-manual") {
        return RuntimeMode::ServiceInstallManual;
    }
    if (argument == L"--service-install-automatic") {
        return RuntimeMode::ServiceInstallAutomatic;
    }
    if (argument == L"--service-install-automatic-delayed") {
        return RuntimeMode::ServiceInstallAutomaticDelayed;
    }
    if (argument == L"--service-uninstall") {
        return RuntimeMode::ServiceUninstall;
    }
    if (argument == L"--service-start") {
        return RuntimeMode::ServiceStart;
    }
    if (argument == L"--service-stop") {
        return RuntimeMode::ServiceStop;
    }
    if (argument == L"--service-restart") {
        return RuntimeMode::ServiceRestart;
    }
    if (argument == L"--service-configure-manual") {
        return RuntimeMode::ServiceConfigureManual;
    }
    if (argument == L"--service-configure-automatic") {
        return RuntimeMode::ServiceConfigureAutomatic;
    }
    if (argument == L"--service-configure-automatic-delayed") {
        return RuntimeMode::ServiceConfigureAutomaticDelayed;
    }
    if (argument == L"--service-query") {
        return RuntimeMode::ServiceQuery;
    }
    return RuntimeMode::Invalid;
}

} // namespace nfm::app
