#include "network_flow_monitor/ipc/security_policy.h"
#include "network_flow_monitor/ipc/windows_pipe_security.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string_view>
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

AuthorizedPrincipal PortablePrincipal() {
    const auto logon_sid = LogonSid();
    return {.mode = CollectorEndpointMode::Portable,
            .authorized_sid = logon_sid,
            .server_logon_sid = logon_sid};
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
        // Windows ACL inspection APIs require mutable legacy PSID values.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        auto* ace_sid = const_cast<DWORD*>(&ace->SidStart);
        // -- Win32 EqualSid signature.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        auto* expected_sid = const_cast<std::byte*>(sid.data());
        // -- Win32 EqualSid signature.
        if (EqualSid(ace_sid, expected_sid) != FALSE) {
            combined_mask |= ace->Mask;
            found = true;
        }
    }
    return found ? std::optional<DWORD>{combined_mask} : std::nullopt;
}

void CheckPrincipal(TestContext& context) {
    const auto principal = PortablePrincipal();
    const auto name = BuildPipeName(principal);
    context.Check(name.error == PipeSecurityError::None &&
                      name.name == LR"(\\.\pipe\NetworkFlowMonitor.portable.S-1-5-5-4660-22136)",
                  "pipe name contains the logon SID");

    auto missing = principal;
    missing.authorized_sid.clear();
    context.Check(BuildPipeName(missing).error == PipeSecurityError::MissingSid,
                  "missing authorized SID is rejected");

    auto missing_server = principal;
    missing_server.server_logon_sid.clear();
    context.Check(BuildPipeName(missing_server).error == PipeSecurityError::MissingSid,
                  "missing server SID is rejected");

    auto invalid = principal;
    invalid.mode = CollectorEndpointMode::Invalid;
    context.Check(BuildPipeName(invalid).error == PipeSecurityError::InvalidMode,
                  "unknown endpoint mode is rejected");
}

void CheckDescriptor(TestContext& context) {
    const auto principal = PortablePrincipal();
    auto result = BuildPipeSecurity(principal);
    context.Check(result.error == PipeSecurityError::None && result.descriptor != nullptr,
                  "security descriptor builds");
    if (result.descriptor == nullptr) {
        return;
    }
    BOOL present{};
    BOOL defaulted{};
    PACL acl{};
    auto* descriptor = result.descriptor->attributes()->lpSecurityDescriptor;
    context.Check(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) != FALSE &&
                      present != FALSE && acl != nullptr && defaulted == FALSE,
                  "descriptor has an explicit DACL");
    if (acl == nullptr) {
        return;
    }
    context.Check(AllowedMaskFor(acl, principal.authorized_sid) == FILE_ALL_ACCESS,
                  "shared logon SID receives server access");
    context.Check(!AllowedMaskFor(acl, WellKnownSid(WinWorldSid)).has_value() &&
                      !AllowedMaskFor(acl, WellKnownSid(WinAnonymousSid)).has_value(),
                  "broad principals receive no allow ACE");
}

class FakeSecurityApi final : public PipeSecurityApi {
  public:
    bool Impersonate(HANDLE, std::uint32_t& error) noexcept override {
        ++impersonate_calls;
        error = source_error;
        return impersonate_result;
    }
    bool QueryThreadIdentity(QueriedClientIdentity& output,
                             std::uint32_t& error) noexcept override {
        ++query_calls;
        error = source_error;
        if (query_result) {
            output = identity;
        }
        return query_result;
    }
    bool Revert(std::uint32_t& error) noexcept override {
        ++revert_calls;
        error = source_error;
        return revert_result;
    }

    QueriedClientIdentity identity;
    bool impersonate_result{true};
    bool query_result{true};
    bool revert_result{true};
    std::uint32_t source_error{5U};
    int impersonate_calls{};
    int query_calls{};
    int revert_calls{};
};

void CheckIdentity(TestContext& context) {
    const auto principal = PortablePrincipal();
    FakeSecurityApi valid;
    valid.identity.group_sids = {principal.authorized_sid};
    auto result = VerifyPipeClient(GetCurrentProcess(), principal, valid);
    context.Check(result.authorized() && valid.revert_calls == 1,
                  "matching logon group is authorized and reverted");

    FakeSecurityApi valid_user_sid;
    valid_user_sid.identity.user_sid = principal.authorized_sid;
    valid_user_sid.identity.group_sids = {WellKnownSid(WinBuiltinUsersSid)};
    result = VerifyPipeClient(GetCurrentProcess(), principal, valid_user_sid);
    context.Check(result.authorized() && valid_user_sid.revert_calls == 1,
                  "matching user SID across UAC split token is authorized and reverted");

    FakeSecurityApi wrong;
    wrong.identity.group_sids = {WellKnownSid(WinBuiltinGuestsSid)};
    result = VerifyPipeClient(GetCurrentProcess(), principal, wrong);
    context.Check(result.error == PipeSecurityError::Unauthorized && wrong.revert_calls == 1,
                  "unexpected logon is rejected and reverted");

    FakeSecurityApi impersonation_failure;
    impersonation_failure.impersonate_result = false;
    result = VerifyPipeClient(GetCurrentProcess(), principal, impersonation_failure);
    context.Check(result.error == PipeSecurityError::ImpersonationFailed &&
                      impersonation_failure.query_calls == 0 &&
                      impersonation_failure.revert_calls == 0,
                  "impersonation failure stops before token query");

    FakeSecurityApi query_failure;
    query_failure.query_result = false;
    result = VerifyPipeClient(GetCurrentProcess(), principal, query_failure);
    context.Check(result.error == PipeSecurityError::TokenQueryFailed &&
                      query_failure.revert_calls == 1,
                  "token query failure reverts once");

    FakeSecurityApi revert_failure;
    revert_failure.identity.group_sids = {principal.authorized_sid};
    revert_failure.revert_result = false;
    result = VerifyPipeClient(GetCurrentProcess(), principal, revert_failure);
    context.Check(result.error == PipeSecurityError::RevertFailed, "revert failure fails closed");
}

} // namespace

int main() {
    TestContext context;
    CheckPrincipal(context);
    CheckDescriptor(context);
    CheckIdentity(context);
    return context.failures() == 0 ? 0 : 1;
}
