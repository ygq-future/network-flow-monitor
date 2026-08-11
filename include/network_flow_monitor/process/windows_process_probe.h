#ifndef NETWORK_FLOW_MONITOR_PROCESS_WINDOWS_PROCESS_PROBE_H
#define NETWORK_FLOW_MONITOR_PROCESS_WINDOWS_PROCESS_PROBE_H

#include "network_flow_monitor/process/process_tracker.h"

#include <windows.h>

#include <cstdint>
#include <optional>

namespace nfm::process {

enum class ProcessProbeStage : std::uint8_t {
    None,
    OpenProcess,
    CreationTime,
    ApplicationUserModelId,
    PackageFamilyName,
    ImagePath,
    Utf8Conversion,
};

enum class ProcessProbeError : std::uint8_t {
    None,
    InvalidRequest,
    ProcessUnavailable,
    AccessDenied,
    PidReusedBeforeProbe,
    StringTooLong,
    InvalidUnicode,
    OutOfMemory,
    WindowsFailure,
};

struct WindowsProcessProbeResult {
    ProcessProbeError error{ProcessProbeError::None};
    ProcessProbeStage stage{ProcessProbeStage::None};
    std::uint32_t win32_error{};
    std::optional<TraceTimePoint> creation_time;
    ProcessProbeData data;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] std::optional<TraceTimePoint> FileTimeToTraceTimePoint(
    const FILETIME& file_time) noexcept;

[[nodiscard]] WindowsProcessProbeResult ProbeWindowsProcess(
    const ProcessProbeRequest& request) noexcept;

} // namespace nfm::process

#endif // NETWORK_FLOW_MONITOR_PROCESS_WINDOWS_PROCESS_PROBE_H
