#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_ETW_AUTHORIZATION_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_ETW_AUTHORIZATION_H

#include <cstdint>

namespace nfm::collector {

// Grants the current user SeSystemProfilePrivilege, Performance Log Users membership, and
// EventAccessControl DACL entries for the SystemTraceProvider session, so the portable collector
// can start the ETW session without elevation. Must be called from an elevated process.
// Returns ERROR_SUCCESS on success, or the first Win32 error code on failure.
[[nodiscard]] std::uint32_t AuthorizeCollectorForCurrentUser() noexcept;

// Enables SeSystemProfilePrivilege in the current process token if present.
// Returns ERROR_SUCCESS if enabled, or a Win32 error code (e.g. ERROR_NOT_ALL_ASSIGNED).
[[nodiscard]] std::uint32_t EnableCollectorPrivileges() noexcept;

// Returns true if the current process is running with an elevated token.
[[nodiscard]] bool IsCurrentProcessElevated() noexcept;

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_ETW_AUTHORIZATION_H
