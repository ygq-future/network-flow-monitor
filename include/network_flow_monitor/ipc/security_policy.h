#ifndef NETWORK_FLOW_MONITOR_IPC_SECURITY_POLICY_H
#define NETWORK_FLOW_MONITOR_IPC_SECURITY_POLICY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfm::ipc {

inline constexpr std::size_t kMaximumPipeNameCharacters = 256U;

enum class CollectorEndpointMode : std::uint8_t {
    Portable = 0U,
    Invalid = 0xFFU,
};

enum class PipeSecurityError : std::uint8_t {
    None,
    MissingSid,
    InvalidSid,
    InvalidMode,
    NameTooLong,
    DescriptorBuildFailed,
    ImpersonationFailed,
    TokenQueryFailed,
    Unauthorized,
    RevertFailed,
};

struct AuthorizedPrincipal {
    CollectorEndpointMode mode{};
    std::vector<std::byte> authorized_sid;
    std::vector<std::byte> server_logon_sid;
};

struct PipeNameResult {
    PipeSecurityError error{PipeSecurityError::None};
    std::wstring name;
};

[[nodiscard]] PipeSecurityError ValidateAuthorizedPrincipal(
    const AuthorizedPrincipal& principal) noexcept;
[[nodiscard]] PipeNameResult BuildPipeName(const AuthorizedPrincipal& principal);

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_SECURITY_POLICY_H
