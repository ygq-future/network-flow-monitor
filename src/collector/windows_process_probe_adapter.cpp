#include "network_flow_monitor/collector/windows_process_probe_adapter.h"

#include "network_flow_monitor/process/windows_process_probe.h"

#include <cstdint>
#include <utility>

namespace nfm::collector {
namespace {

CollectorProcessProbeError MapError(process::ProcessProbeError error) noexcept {
    switch (error) {
    case process::ProcessProbeError::None:
        return CollectorProcessProbeError::None;
    case process::ProcessProbeError::ProcessUnavailable:
        return CollectorProcessProbeError::ProcessUnavailable;
    case process::ProcessProbeError::AccessDenied:
        return CollectorProcessProbeError::AccessDenied;
    case process::ProcessProbeError::PidReusedBeforeProbe:
        return CollectorProcessProbeError::PidReusedBeforeProbe;
    case process::ProcessProbeError::OutOfMemory:
        return CollectorProcessProbeError::OutOfMemory;
    case process::ProcessProbeError::InvalidRequest:
    case process::ProcessProbeError::StringTooLong:
    case process::ProcessProbeError::InvalidUnicode:
        return CollectorProcessProbeError::InvalidData;
    case process::ProcessProbeError::WindowsFailure:
        return CollectorProcessProbeError::WindowsFailure;
    }
    return CollectorProcessProbeError::WindowsFailure;
}

} // namespace

CollectorProcessProbeResult WindowsProcessProbeAdapter::Probe(
    const process::ProcessProbeRequest& request) noexcept {
    auto result = process::ProbeWindowsProcess(request);
    if (result.ok()) {
        return {.source_stage = static_cast<std::uint32_t>(result.stage),
                .data = std::move(result.data)};
    }

    const std::uint32_t diagnostic_code =
        result.win32_error != 0U ? result.win32_error : static_cast<std::uint32_t>(result.error);
    return {.error = MapError(result.error),
            .diagnostic = {.domain = result.win32_error != 0U ? CollectorErrorDomain::Win32
                                                              : CollectorErrorDomain::ProcessProbe,
                           .stage = CollectorErrorStage::ProbeProcess,
                           .code = diagnostic_code},
            .source_stage = static_cast<std::uint32_t>(result.stage)};
}

} // namespace nfm::collector
