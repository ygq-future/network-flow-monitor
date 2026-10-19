#include "network_flow_monitor/collector/etw_authorization.h"

#include "network_flow_monitor/collector/windows_etw_session.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <evntcons.h>
#include <lm.h>
#include <ntsecapi.h>
#include <vector>

namespace nfm::collector {
namespace {

class ScopedToken final {
  public:
    explicit ScopedToken(HANDLE token) noexcept : token_{token} {}
    ~ScopedToken() {
        if (token_ != nullptr) {
            static_cast<void>(::CloseHandle(token_));
        }
    }
    ScopedToken(const ScopedToken&) = delete;
    ScopedToken& operator=(const ScopedToken&) = delete;
    ScopedToken(ScopedToken&&) = delete;
    ScopedToken& operator=(ScopedToken&&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return token_;
    }

  private:
    HANDLE token_;
};

[[nodiscard]] std::vector<std::byte> CurrentUserSid() noexcept {
    try {
        HANDLE raw_token{};
        if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
            return {};
        }
        const ScopedToken token{raw_token};

        DWORD required{};
        static_cast<void>(::GetTokenInformation(token.get(), TokenUser, nullptr, 0U, &required));
        if (required < sizeof(TOKEN_USER)) {
            return {};
        }
        std::vector<std::byte> storage(required);
        if (::GetTokenInformation(token.get(), TokenUser, storage.data(), required, &required) ==
            FALSE) {
            return {};
        }
        const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
        if (user->User.Sid == nullptr || ::IsValidSid(user->User.Sid) == FALSE) {
            return {};
        }
        const auto length = ::GetLengthSid(user->User.Sid);
        std::vector<std::byte> sid(length);
        if (::CopySid(length, sid.data(), user->User.Sid) == FALSE) {
            return {};
        }
        return sid;
    } catch (...) {
        return {};
    }
}

void TryAddCurrentUserToPerformanceLogUsers(PSID user_sid) noexcept {
    if (user_sid == nullptr || ::IsValidSid(user_sid) == FALSE) {
        return;
    }
    std::array<std::byte, SECURITY_MAX_SID_SIZE> group_sid_buf{};
    auto group_sid_size = static_cast<DWORD>(group_sid_buf.size());
    if (::CreateWellKnownSid(WinBuiltinPerfLoggingUsersSid, nullptr, group_sid_buf.data(),
                             &group_sid_size) == FALSE) {
        return;
    }
    std::array<wchar_t, 256> group_name{};
    std::array<wchar_t, 256> domain_name{};
    auto group_name_len = static_cast<DWORD>(group_name.size());
    auto domain_name_len = static_cast<DWORD>(domain_name.size());
    SID_NAME_USE name_use = SidTypeUnknown;
    if (::LookupAccountSidW(nullptr, group_sid_buf.data(), group_name.data(), &group_name_len,
                            domain_name.data(), &domain_name_len, &name_use) == FALSE) {
        return;
    }
    LOCALGROUP_MEMBERS_INFO_0 member_info{.lgrmi0_sid = user_sid};
    static_cast<void>(::NetLocalGroupAddMembers(nullptr, group_name.data(), 0U,
                                                reinterpret_cast<LPBYTE>(&member_info), 1U));
}

} // namespace

std::uint32_t AuthorizeCollectorForCurrentUser() noexcept {
    auto sid = CurrentUserSid();
    if (sid.empty()) {
        return ERROR_INVALID_SID;
    }
    auto* const sid_ptr = reinterpret_cast<PSID>(sid.data());

    // Best-effort: add current user to Performance Log Users local group per ADR 0015.
    TryAddCurrentUserToPerformanceLogUsers(sid_ptr);

    // 1. Grant SeSystemProfilePrivilege to the user account.
    LSA_OBJECT_ATTRIBUTES attributes{};
    LSA_HANDLE policy{};
    const auto policy_status = ::LsaOpenPolicy(nullptr, &attributes, POLICY_ALL_ACCESS, &policy);
    if (policy_status != 0) {
        return ::LsaNtStatusToWinError(policy_status);
    }
    LSA_UNICODE_STRING privilege{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the LSA API requires a mutable buffer.
    privilege.Buffer = const_cast<PWSTR>(SE_SYSTEM_PROFILE_NAME);
    privilege.Length = static_cast<USHORT>(sizeof(SE_SYSTEM_PROFILE_NAME) - sizeof(wchar_t));
    privilege.MaximumLength = static_cast<USHORT>(sizeof(SE_SYSTEM_PROFILE_NAME));
    const auto rights_status = ::LsaAddAccountRights(policy, sid_ptr, &privilege, 1U);
    static_cast<void>(::LsaClose(policy));
    if (rights_status != 0) {
        return ::LsaNtStatusToWinError(rights_status);
    }

    // 2. Allow the user to start, query and consume the real-time session.
    GUID session_guid = WindowsEtwSessionId();
    const auto session_status = ::EventAccessControl(
        &session_guid, EventSecurityAddDACL, sid_ptr,
        TRACELOG_CREATE_REALTIME | TRACELOG_ACCESS_REALTIME | WMIGUID_QUERY, TRUE);
    if (session_status != ERROR_SUCCESS) {
        return session_status;
    }

    // 3. Allow the user to enable the SystemTraceProvider (kernel network events). The GUID's
    // library definition is not linked, so keep the documented value inline.
    GUID provider_guid{
        .Data1 = 0x9e814aad,
        .Data2 = 0x3204,
        .Data3 = 0x11d2,
        .Data4 = {0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39},
    };
    return ::EventAccessControl(&provider_guid, EventSecurityAddDACL, sid_ptr, TRACELOG_GUID_ENABLE,
                                TRUE);
}

std::uint32_t EnableCollectorPrivileges() noexcept {
    HANDLE raw_token{};
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                           &raw_token) == FALSE) {
        return ::GetLastError();
    }
    const ScopedToken token{raw_token};

    LUID luid{};
    if (::LookupPrivilegeValueW(nullptr, SE_SYSTEM_PROFILE_NAME, &luid) == FALSE) {
        return ::GetLastError();
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1U;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    ::SetLastError(ERROR_SUCCESS);
    if (::AdjustTokenPrivileges(token.get(), FALSE, &tp, sizeof(tp), nullptr, nullptr) == FALSE) {
        return ::GetLastError();
    }
    return ::GetLastError();
}

bool IsCurrentProcessElevated() noexcept {
    HANDLE raw_token{};
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
        return false;
    }
    const ScopedToken token{raw_token};
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    if (::GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) ==
        FALSE) {
        return false;
    }
    return elevation.TokenIsElevated != 0U;
}

} // namespace nfm::collector
