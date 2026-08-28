#ifndef NETWORK_FLOW_MONITOR_TESTS_SUPPORT_CURRENT_LOGON_SID_H
#define NETWORK_FLOW_MONITOR_TESTS_SUPPORT_CURRENT_LOGON_SID_H

#include <windows.h>

#include <cstddef>
#include <vector>

namespace nfm::test {

inline std::vector<std::byte> CurrentLogonSid() {
    HANDLE token{};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return {};
    }

    DWORD required{};
    static_cast<void>(GetTokenInformation(token, TokenGroups, nullptr, 0U, &required));
    if (required == 0U) {
        static_cast<void>(CloseHandle(token));
        return {};
    }

    std::vector<std::byte> storage(required);
    if (GetTokenInformation(token, TokenGroups, storage.data(), required, &required) == FALSE) {
        static_cast<void>(CloseHandle(token));
        return {};
    }
    static_cast<void>(CloseHandle(token));

    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    for (DWORD index = 0; index < groups->GroupCount; ++index) {
        const auto& group = groups->Groups[index];
        if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID ||
            IsValidSid(group.Sid) == FALSE) {
            continue;
        }
        const auto length = GetLengthSid(group.Sid);
        std::vector<std::byte> sid(length);
        if (CopySid(length, sid.data(), group.Sid) != FALSE) {
            return sid;
        }
    }
    return {};
}

} // namespace nfm::test

#endif // NETWORK_FLOW_MONITOR_TESTS_SUPPORT_CURRENT_LOGON_SID_H
