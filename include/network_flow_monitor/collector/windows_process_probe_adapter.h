#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_PROCESS_PROBE_ADAPTER_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_PROCESS_PROBE_ADAPTER_H

#include "network_flow_monitor/collector/collector_core.h"

namespace nfm::collector {

class WindowsProcessProbeAdapter final : public ProcessProbe {
  public:
    [[nodiscard]] CollectorProcessProbeResult Probe(
        const process::ProcessProbeRequest& request) noexcept override;
};

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_PROCESS_PROBE_ADAPTER_H
