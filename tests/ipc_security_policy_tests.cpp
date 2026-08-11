#include "network_flow_monitor/ipc/security_policy.h"
#include "network_flow_monitor/ipc/windows_pipe_security.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::BuildPipeName;
using nfm::ipc::BuildPipeSecurity;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::PipeSecurityApi;
using nfm::ipc::PipeSecurityError;
using nfm::ipc::QueriedClientIdentity;
using nfm::ipc::VerifyPipeClient;

class TestContext {
  public:
    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

std::vector<std::byte> CopySid(PSID sid) {
    const auto size = GetLengthSid(sid);
    std::vector<std::byte> result(size);
    static_cast<void>(::CopySid(size, result.data(), sid));
    return result;
}

std::vector<std::byte> WellKnownSid(WELL_KNOWN_SID_TYPE type) {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    auto size = static_cast<DWORD>(storage.size());
    if (CreateWellKnownSid(type, nullptr, storage.data(), &size) == FALSE) {
        return {};
    }
    return {storage.begin(), storage.begin() + size};
}

std::vector<std::byte> LogonSid() {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    auto* sid = reinterpret_cast<PSID>(storage.data());
    if (InitializeSid(sid, &authority, 3U) == FALSE) {
        return {};
    }
    *GetSidSubAuthority(sid, 0U) = SECURITY_LOGON_IDS_RID;
    *GetSidSubAuthority(sid, 1U) = 0x1234U;
    *GetSidSubAuthority(sid, 2U) = 0x5678U;
    return CopySid(sid);
}

std::vector<std::byte> MaximumLengthSid() {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    auto* sid = reinterpret_cast<PSID>(storage.data());
    if (InitializeSid(sid, &authority, SID_MAX_SUB_AUTHORITIES) == FALSE) {
        return {};
    }
    for (DWORD index = 0U; index < SID_MAX_SUB_AUTHORITIES; ++index) {
        *GetSidSubAuthority(sid, index) = std::numeric_limits<DWORD>::max();
    }
    return CopySid(sid);
}

AuthorizedPrincipal ServicePrincipal() {
    return {.mode = CollectorEndpointMode::Service,
            .authorized_sid = WellKnownSid(WinBuiltinUsersSid)};
}

AuthorizedPrincipal PortablePrincipal() {
    return {.mode = CollectorEndpointMode::Portable,
            .authorized_sid = LogonSid(),
            .server_logon_sid = WellKnownSid(WinInteractiveSid)};
}

std::optional<DWORD> AllowedMaskFor(PACL acl, const std::vector<std::byte>& sid) {
    ACL_SIZE_INFORMATION information{};
    if (GetAclInformation(acl, &information, sizeof(information), AclSizeInformation) == FALSE) {
        return std::nullopt;
    }
    DWORD combined_mask{};
    bool found{};
    for (DWORD index = 0U; index < information.AceCount; ++index) {
        void* raw_ace{};
        if (GetAce(acl, index, &raw_ace) == FALSE || raw_ace == nullptr) {
            return std::nullopt;
        }
        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            continue;
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        auto* ace_sid =
            const_cast<DWORD*>(&ace->SidStart); // NOLINT(cppcoreguidelines-pro-type-const-cast):
                                                // EqualSid has a legacy mutable PSID signature.
        auto* expected_sid =
            const_cast<std::byte*>(sid.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast):
                                                // EqualSid has a legacy mutable PSID signature.
        if (EqualSid(ace_sid, expected_sid) != FALSE) {
            combined_mask |= ace->Mask;
            found = true;
        }
    }
    if (!found) {
        return std::nullopt;
    }
    return combined_mask;
}

PACL DescriptorDacl(TestContext& context, SECURITY_ATTRIBUTES* attributes) {
    context.Check(attributes != nullptr && attributes->lpSecurityDescriptor != nullptr,
                  "descriptor exposes owned security attributes");
    if (attributes == nullptr || attributes->lpSecurityDescriptor == nullptr) {
        return nullptr;
    }
    BOOL present{};
    BOOL defaulted{};
    PACL acl{};
    context.Check(GetSecurityDescriptorDacl(attributes->lpSecurityDescriptor, &present, &acl,
                                            &defaulted) != FALSE &&
                      present != FALSE && acl != nullptr && defaulted == FALSE,
                  "descriptor contains an explicit non-defaulted DACL");
    return acl;
}

void CheckNamesAndValidation(TestContext& context) {
    const auto service = ServicePrincipal();
    const auto service_name = BuildPipeName(service);
    context.Check(service_name.error == PipeSecurityError::None &&
                      service_name.name == LR"(\\.\pipe\NetworkFlowMonitor.service.S-1-5-32-545)",
                  "service endpoint uses a canonical owner SID name");

    const auto portable = PortablePrincipal();
    const auto portable_name = BuildPipeName(portable);
    context.Check(portable_name.error == PipeSecurityError::None &&
                      portable_name.name ==
                          LR"(\\.\pipe\NetworkFlowMonitor.portable.S-1-5-5-4660-22136)",
                  "portable endpoint uses a canonical logon SID name");

    auto missing = service;
    missing.authorized_sid.clear();
    context.Check(BuildPipeName(missing).error == PipeSecurityError::MissingSid,
                  "missing authorized SID is rejected");

    auto invalid = service;
    invalid.authorized_sid = {std::byte{0U}, std::byte{1U}};
    context.Check(BuildPipeName(invalid).error == PipeSecurityError::InvalidSid,
                  "malformed authorized SID is rejected");

    auto missing_server = portable;
    missing_server.server_logon_sid.clear();
    context.Check(BuildPipeName(missing_server).error == PipeSecurityError::MissingSid,
                  "portable endpoint requires a server logon SID");

    auto invalid_mode = service;
    invalid_mode.mode = CollectorEndpointMode::Invalid;
    context.Check(BuildPipeName(invalid_mode).error == PipeSecurityError::InvalidMode,
                  "unknown endpoint mode is rejected");

    auto maximum = service;
    maximum.authorized_sid = MaximumLengthSid();
    const auto maximum_name = BuildPipeName(maximum);
    context.Check(maximum_name.error == PipeSecurityError::None &&
                      maximum_name.name.size() <= nfm::ipc::kMaximumPipeNameCharacters,
                  "maximum valid SID remains inside the fixed pipe-name limit");
}

void CheckServiceDescriptor(TestContext& context) {
    const auto principal = ServicePrincipal();
    auto result = BuildPipeSecurity(principal);
    context.Check(result.error == PipeSecurityError::None && result.descriptor != nullptr,
                  "service descriptor builds");
    if (result.descriptor == nullptr) {
        return;
    }
    auto* acl = DescriptorDacl(context, result.descriptor->attributes());
    if (acl == nullptr) {
        return;
    }

    const auto system = WellKnownSid(WinLocalSystemSid);
    context.Check(AllowedMaskFor(acl, system) == FILE_ALL_ACCESS,
                  "LocalSystem receives exact server access");
    context.Check(AllowedMaskFor(acl, principal.authorized_sid) == nfm::ipc::kPipeClientAccessMask,
                  "service owner receives exact client data access");
    context.Check(!AllowedMaskFor(acl, WellKnownSid(WinWorldSid)).has_value() &&
                      !AllowedMaskFor(acl, WellKnownSid(WinAnonymousSid)).has_value() &&
                      !AllowedMaskFor(acl, WellKnownSid(WinAuthenticatedUserSid)).has_value() &&
                      !AllowedMaskFor(acl, WellKnownSid(WinBuiltinAdministratorsSid)).has_value(),
                  "broad principals receive no allow ACE");
}

void CheckPortableDescriptor(TestContext& context) {
    const auto principal = PortablePrincipal();
    auto result = BuildPipeSecurity(principal);
    context.Check(result.error == PipeSecurityError::None && result.descriptor != nullptr,
                  "portable descriptor builds");
    if (result.descriptor == nullptr) {
        return;
    }
    auto* acl = DescriptorDacl(context, result.descriptor->attributes());
    if (acl == nullptr) {
        return;
    }
    context.Check(AllowedMaskFor(acl, principal.server_logon_sid) == FILE_ALL_ACCESS,
                  "portable server logon receives exact server access");
    context.Check(AllowedMaskFor(acl, principal.authorized_sid) == nfm::ipc::kPipeClientAccessMask,
                  "portable UI logon receives exact client data access");

    auto invalid = principal;
    invalid.server_logon_sid = {std::byte{0U}};
    context.Check(BuildPipeSecurity(invalid).error == PipeSecurityError::InvalidSid,
                  "invalid portable server SID is rejected before ACL construction");

    auto shared_logon = principal;
    shared_logon.server_logon_sid = shared_logon.authorized_sid;
    auto shared_result = BuildPipeSecurity(shared_logon);
    context.Check(shared_result.error == PipeSecurityError::None &&
                      shared_result.descriptor != nullptr,
                  "common elevated portable mode accepts one shared logon SID");
    if (shared_result.descriptor != nullptr) {
        auto* shared_acl = DescriptorDacl(context, shared_result.descriptor->attributes());
        context.Check(shared_acl != nullptr &&
                          AllowedMaskFor(shared_acl, shared_logon.authorized_sid) ==
                              FILE_ALL_ACCESS,
                      "shared portable logon retains server-instance access");
    }
}

class FakeSecurityApi final : public PipeSecurityApi {
  public:
    bool Impersonate(HANDLE pipe, std::uint32_t& error) noexcept override {
        captured_pipe = pipe;
        ++impersonate_calls;
        error = impersonate_error;
        return impersonate_result;
    }

    bool QueryThreadIdentity(QueriedClientIdentity& output,
                             std::uint32_t& error) noexcept override {
        ++query_calls;
        error = query_error;
        if (query_result) {
            output = identity;
        }
        return query_result;
    }

    bool Revert(std::uint32_t& error) noexcept override {
        ++revert_calls;
        error = revert_error;
        return revert_result;
    }

    HANDLE captured_pipe{};
    QueriedClientIdentity identity;
    bool impersonate_result{true};
    bool query_result{true};
    bool revert_result{true};
    std::uint32_t impersonate_error{5U};
    std::uint32_t query_error{6U};
    std::uint32_t revert_error{7U};
    int impersonate_calls{};
    int query_calls{};
    int revert_calls{};
};

void CheckIdentityVerification(TestContext& context) {
    auto* const pipe = GetCurrentProcess();
    const auto service = ServicePrincipal();
    FakeSecurityApi service_api;
    service_api.identity.user_sid = service.authorized_sid;
    auto result = VerifyPipeClient(pipe, service, service_api);
    context.Check(result.authorized() && service_api.impersonate_calls == 1 &&
                      service_api.query_calls == 1 && service_api.revert_calls == 1 &&
                      service_api.captured_pipe == pipe,
                  "service owner user is authorized and reverted exactly once");

    const auto portable = PortablePrincipal();
    FakeSecurityApi portable_api;
    portable_api.identity.user_sid = portable.authorized_sid;
    result = VerifyPipeClient(pipe, portable, portable_api);
    context.Check(result.error == PipeSecurityError::Unauthorized && portable_api.revert_calls == 1,
                  "portable authorization does not accept user SID in place of logon SID");
    portable_api.identity.group_sids = {portable.authorized_sid};
    result = VerifyPipeClient(pipe, portable, portable_api);
    context.Check(result.authorized() && portable_api.revert_calls == 2,
                  "portable expected logon group is authorized");

    FakeSecurityApi wrong_api;
    wrong_api.identity.user_sid = WellKnownSid(WinBuiltinGuestsSid);
    result = VerifyPipeClient(pipe, service, wrong_api);
    context.Check(result.error == PipeSecurityError::Unauthorized && wrong_api.revert_calls == 1,
                  "wrong service user is rejected and reverted");

    FakeSecurityApi impersonation_failure;
    impersonation_failure.impersonate_result = false;
    result = VerifyPipeClient(pipe, service, impersonation_failure);
    context.Check(result.error == PipeSecurityError::ImpersonationFailed &&
                      result.win32_error == impersonation_failure.impersonate_error &&
                      impersonation_failure.query_calls == 0 &&
                      impersonation_failure.revert_calls == 0,
                  "failed impersonation stops before query and does not revert");

    FakeSecurityApi query_failure;
    query_failure.query_result = false;
    result = VerifyPipeClient(pipe, service, query_failure);
    context.Check(result.error == PipeSecurityError::TokenQueryFailed &&
                      result.win32_error == query_failure.query_error &&
                      query_failure.revert_calls == 1,
                  "token query failure is diagnostic and reverts exactly once");

    FakeSecurityApi revert_failure;
    revert_failure.identity.user_sid = service.authorized_sid;
    revert_failure.revert_result = false;
    result = VerifyPipeClient(pipe, service, revert_failure);
    context.Check(result.error == PipeSecurityError::RevertFailed &&
                      result.win32_error == revert_failure.revert_error &&
                      revert_failure.revert_calls == 1,
                  "revert failure fails closed and remains diagnostic");

    auto invalid_principal = service;
    invalid_principal.mode = CollectorEndpointMode::Invalid;
    FakeSecurityApi untouched_api;
    result = VerifyPipeClient(pipe, invalid_principal, untouched_api);
    context.Check(result.error == PipeSecurityError::InvalidMode &&
                      untouched_api.impersonate_calls == 0 && untouched_api.query_calls == 0 &&
                      untouched_api.revert_calls == 0,
                  "invalid principal is rejected before entering impersonation");
}

} // namespace

int main() {
    TestContext context;
    CheckNamesAndValidation(context);
    CheckServiceDescriptor(context);
    CheckPortableDescriptor(context);
    CheckIdentityVerification(context);
    if (context.failures() != 0) {
        return 1;
    }
    std::cout << "ipc security policy tests passed\n";
    return 0;
}
