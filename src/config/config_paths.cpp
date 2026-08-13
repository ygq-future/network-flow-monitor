#include "network_flow_monitor/config/config_paths.h"

namespace nfm::config {

std::filesystem::path PortableConfigPath(const std::filesystem::path& executable_path) {
    return executable_path.parent_path() / L"NetworkFlowMonitor.json";
}

std::filesystem::path InstalledUserConfigPath(const std::filesystem::path& local_app_data) {
    return local_app_data / L"NetworkFlowMonitor" / L"config.json";
}

std::filesystem::path MachineServiceConfigPath(const std::filesystem::path& program_data) {
    return program_data / L"NetworkFlowMonitor" / L"service.json";
}

} // namespace nfm::config
