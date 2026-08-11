#include "network_flow_monitor/ipc/security_policy.h"

#include <windows.h>

#include <sddl.h>
#include <string_view>

namespace nfm::ipc {
namespace {

PSID MutableSid(const std::vector<std::byte>& sid) noexcept {
    // Windows SID inspection APIs are logically const but retain legacy mutable PSID signatures.
    return const_cast<std::byte*>(sid.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

bool IsCanonicalSid(const std::vector<std::byte>& sid) noexcept {
    if (sid.empty()) {
        return false;
    }
    auto* native_sid = MutableSid(sid);
    return IsValidSid(native_sid) != FALSE && GetLengthSid(native_sid) == sid.size();
}

std::wstring_view ModeName(CollectorEndpointMode mode) noexcept {
    switch (mode) {
    case CollectorEndpointMode::Portable:
        return L"portable";
    case CollectorEndpointMode::Service:
        return L"service";
    case CollectorEndpointMode::Invalid:
        return {};
    }
    return {};
}

} // namespace

PipeSecurityError ValidateAuthorizedPrincipal(const AuthorizedPrincipal& principal) noexcept {
    if (ModeName(principal.mode).empty()) {
        return PipeSecurityError::InvalidMode;
    }
    if (principal.authorized_sid.empty()) {
        return PipeSecurityError::MissingSid;
    }
    if (!IsCanonicalSid(principal.authorized_sid)) {
        return PipeSecurityError::InvalidSid;
    }
    if (principal.mode == CollectorEndpointMode::Portable) {
        if (principal.server_logon_sid.empty()) {
            return PipeSecurityError::MissingSid;
        }
        if (!IsCanonicalSid(principal.server_logon_sid)) {
            return PipeSecurityError::InvalidSid;
        }
    }
    return PipeSecurityError::None;
}

PipeNameResult BuildPipeName(const AuthorizedPrincipal& principal) {
    const auto validation_error = ValidateAuthorizedPrincipal(principal);
    if (validation_error != PipeSecurityError::None) {
        return {.error = validation_error};
    }

    LPWSTR sid_text{};
    auto* native_sid = MutableSid(principal.authorized_sid);
    if (ConvertSidToStringSidW(native_sid, &sid_text) == FALSE || sid_text == nullptr) {
        return {.error = PipeSecurityError::DescriptorBuildFailed};
    }
    const std::wstring owned_sid{sid_text};
    static_cast<void>(LocalFree(sid_text));

    std::wstring name = LR"(\\.\pipe\NetworkFlowMonitor.)";
    name.append(ModeName(principal.mode));
    name.push_back(L'.');
    name.append(owned_sid);
    if (name.size() > kMaximumPipeNameCharacters) {
        return {.error = PipeSecurityError::NameTooLong};
    }
    return {.name = std::move(name)};
}

} // namespace nfm::ipc
