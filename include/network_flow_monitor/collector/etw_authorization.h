#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_ETW_AUTHORIZATION_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_ETW_AUTHORIZATION_H

#include <cstdint>

namespace nfm::collector {

// Grants the current user SeSystemProfilePrivilege and EventAccessControl DACL entries for the
// SystemTraceProvider session, so the portable collector can start the ETW session without
// elevation. Must be called from an elevated process. Returns ERROR_SUCCESS on success, or the
// first Win32 error code on failure.
[[nodiscard]] std::uint32_t AuthorizeCollectorForCurrentUser() noexcept;

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_ETW_AUTHORIZATION_H
