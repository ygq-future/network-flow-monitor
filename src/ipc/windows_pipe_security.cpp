#include "network_flow_monitor/ipc/windows_pipe_security.h"

#include <aclapi.h>
#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <vector>

namespace nfm::ipc {
namespace {

inline constexpr DWORD kMaximumTokenInformationBytes = 1U << 20U;

class LocalAllocation final {
  public:
    LocalAllocation() = default;
    explicit LocalAllocation(void* allocation) noexcept : allocation_{allocation} {}
    LocalAllocation(const LocalAllocation&) = delete;
    LocalAllocation& operator=(const LocalAllocation&) = delete;
    LocalAllocation(LocalAllocation&&) = delete;
    LocalAllocation& operator=(LocalAllocation&&) = delete;
    ~LocalAllocation() {
        if (allocation_ != nullptr) {
            static_cast<void>(LocalFree(allocation_));
        }
    }

    [[nodiscard]] void* get() const noexcept {
        return allocation_;
    }

  private:
    void* allocation_{};
};

class UniqueHandle final {
  public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_{handle} {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&&) = delete;
    UniqueHandle& operator=(UniqueHandle&&) = delete;
    ~UniqueHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
        }
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

  private:
    HANDLE handle_{};
};

class ImpersonationGuard final {
  public:
    explicit ImpersonationGuard(PipeSecurityApi& api) noexcept : api_{&api} {}
    ImpersonationGuard(const ImpersonationGuard&) = delete;
    ImpersonationGuard& operator=(const ImpersonationGuard&) = delete;
    ImpersonationGuard(ImpersonationGuard&&) = delete;
    ImpersonationGuard& operator=(ImpersonationGuard&&) = delete;
    ~ImpersonationGuard() {
        if (active_) {
            std::uint32_t ignored_error{};
            static_cast<void>(api_->Revert(ignored_error));
        }
    }

    [[nodiscard]] bool Finish(std::uint32_t& error) noexcept {
        active_ = false;
        return api_->Revert(error);
    }

  private:
    PipeSecurityApi* api_{};
    bool active_{true};
};

bool ReadTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS information_class,
                          std::vector<std::byte>& storage, std::uint32_t& error) noexcept {
    DWORD required_size{};
    static_cast<void>(GetTokenInformation(token, information_class, nullptr, 0U, &required_size));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required_size == 0U ||
        required_size > kMaximumTokenInformationBytes) {
        error = GetLastError();
        if (required_size > kMaximumTokenInformationBytes) {
            error = ERROR_INSUFFICIENT_BUFFER;
        }
        return false;
    }
    storage.resize(required_size);
    if (GetTokenInformation(token, information_class, storage.data(), required_size,
                            &required_size) == FALSE) {
        error = GetLastError();
        return false;
    }
    return true;
}

bool CopyValidatedSid(PSID sid, std::vector<std::byte>& destination) {
    if (sid == nullptr || IsValidSid(sid) == FALSE) {
        return false;
    }
    const auto sid_size = GetLengthSid(sid);
    destination.resize(sid_size);
    return CopySid(sid_size, destination.data(), sid) != FALSE;
}

PSID MutableSid(const std::vector<std::byte>& sid) noexcept {
    // Windows SID inspection/ACL APIs are logically const but use legacy mutable PSID signatures.
    return const_cast<std::byte*>(sid.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

bool SidsEqual(const std::vector<std::byte>& left, const std::vector<std::byte>& right) noexcept {
    if (left.empty() || right.empty()) {
        return false;
    }
    auto* left_sid = MutableSid(left);
    auto* right_sid = MutableSid(right);
    return IsValidSid(left_sid) != FALSE && IsValidSid(right_sid) != FALSE &&
           EqualSid(left_sid, right_sid) != FALSE;
}

bool IdentityMatches(const AuthorizedPrincipal& principal,
                     const QueriedClientIdentity& identity) noexcept {
    switch (principal.mode) {
    case CollectorEndpointMode::Portable:
        return std::ranges::any_of(identity.group_sids, [&](const auto& group_sid) {
            return SidsEqual(group_sid, principal.authorized_sid);
        });
    case CollectorEndpointMode::Service:
        return SidsEqual(identity.user_sid, principal.authorized_sid);
    case CollectorEndpointMode::Invalid:
        return false;
    }
    return false;
}

EXPLICIT_ACCESSW TrusteeEntry(DWORD permissions, PSID sid) noexcept {
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = permissions;
    entry.grfAccessMode = SET_ACCESS;
    entry.grfInheritance = NO_INHERITANCE;
    entry.Trustee.pMultipleTrustee = nullptr;
    entry.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    return entry;
}

} // namespace

struct PipeSecurityDescriptorFactory {
    static std::unique_ptr<PipeSecurityDescriptor> Create(std::vector<std::byte> descriptor) {
        return std::make_unique<PipeSecurityDescriptor>(PipeSecurityDescriptor::ConstructionToken{},
                                                        std::move(descriptor));
    }
};

PipeSecurityDescriptor::PipeSecurityDescriptor(ConstructionToken construction_token,
                                               std::vector<std::byte> descriptor) noexcept
    : descriptor_{std::move(descriptor)}, attributes_{.nLength = sizeof(SECURITY_ATTRIBUTES),
                                                      .lpSecurityDescriptor = descriptor_.data(),
                                                      .bInheritHandle = FALSE} {
    static_cast<void>(construction_token);
}

PipeSecurityDescriptor::PipeSecurityDescriptor(PipeSecurityDescriptor&& other) noexcept
    : descriptor_{std::move(other.descriptor_)}, attributes_{other.attributes_} {
    attributes_.lpSecurityDescriptor = descriptor_.data();
    other.attributes_.lpSecurityDescriptor = nullptr;
}

PipeSecurityDescriptor& PipeSecurityDescriptor::operator=(PipeSecurityDescriptor&& other) noexcept {
    if (this != &other) {
        descriptor_ = std::move(other.descriptor_);
        attributes_ = other.attributes_;
        attributes_.lpSecurityDescriptor = descriptor_.data();
        other.attributes_.lpSecurityDescriptor = nullptr;
    }
    return *this;
}

SECURITY_ATTRIBUTES* PipeSecurityDescriptor::attributes() noexcept {
    return &attributes_;
}

bool ClientIdentityResult::authorized() const noexcept {
    return error == PipeSecurityError::None;
}

PipeSecurityResult BuildPipeSecurity(const AuthorizedPrincipal& principal) {
    const auto validation_error = ValidateAuthorizedPrincipal(principal);
    if (validation_error != PipeSecurityError::None) {
        return {.error = validation_error};
    }

    std::array<std::byte, SECURITY_MAX_SID_SIZE> system_storage{};
    auto system_size = static_cast<DWORD>(system_storage.size());
    if (CreateWellKnownSid(WinLocalSystemSid, nullptr, system_storage.data(), &system_size) ==
        FALSE) {
        return {.error = PipeSecurityError::DescriptorBuildFailed};
    }

    std::vector<EXPLICIT_ACCESSW> entries;
    entries.reserve(3U);
    entries.push_back(TrusteeEntry(FILE_ALL_ACCESS, system_storage.data()));
    if (principal.mode == CollectorEndpointMode::Portable) {
        entries.push_back(TrusteeEntry(FILE_ALL_ACCESS, MutableSid(principal.server_logon_sid)));
    }
    entries.push_back(TrusteeEntry(kPipeClientAccessMask, MutableSid(principal.authorized_sid)));

    PACL raw_acl{};
    if (SetEntriesInAclW(static_cast<ULONG>(entries.size()), entries.data(), nullptr, &raw_acl) !=
            ERROR_SUCCESS ||
        raw_acl == nullptr) {
        return {.error = PipeSecurityError::DescriptorBuildFailed};
    }
    const LocalAllocation acl{raw_acl};

    SECURITY_DESCRIPTOR absolute_descriptor{};
    if (InitializeSecurityDescriptor(&absolute_descriptor, SECURITY_DESCRIPTOR_REVISION) == FALSE ||
        SetSecurityDescriptorDacl(&absolute_descriptor, TRUE, raw_acl, FALSE) == FALSE ||
        SetSecurityDescriptorControl(&absolute_descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED) ==
            FALSE) {
        return {.error = PipeSecurityError::DescriptorBuildFailed};
    }

    DWORD descriptor_size{};
    static_cast<void>(MakeSelfRelativeSD(&absolute_descriptor, nullptr, &descriptor_size));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || descriptor_size == 0U) {
        return {.error = PipeSecurityError::DescriptorBuildFailed};
    }
    std::vector<std::byte> descriptor(descriptor_size);
    if (MakeSelfRelativeSD(&absolute_descriptor, descriptor.data(), &descriptor_size) == FALSE) {
        return {.error = PipeSecurityError::DescriptorBuildFailed};
    }
    descriptor.resize(descriptor_size);
    return {.descriptor = PipeSecurityDescriptorFactory::Create(std::move(descriptor))};
}

bool WindowsPipeSecurityApi::Impersonate(HANDLE pipe, std::uint32_t& error) noexcept {
    if (ImpersonateNamedPipeClient(pipe) == FALSE) {
        error = GetLastError();
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool WindowsPipeSecurityApi::QueryThreadIdentity(QueriedClientIdentity& identity,
                                                 std::uint32_t& error) noexcept {
    HANDLE raw_token{};
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw_token) == FALSE) {
        error = GetLastError();
        return false;
    }
    const UniqueHandle token{raw_token};

    QueriedClientIdentity candidate;
    std::vector<std::byte> user_storage;
    if (!ReadTokenInformation(token.get(), TokenUser, user_storage, error)) {
        return false;
    }
    if (user_storage.size() < sizeof(TOKEN_USER)) {
        error = ERROR_BAD_LENGTH;
        return false;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(user_storage.data());
    if (!CopyValidatedSid(user->User.Sid, candidate.user_sid)) {
        error = ERROR_INVALID_SID;
        return false;
    }

    std::vector<std::byte> groups_storage;
    if (!ReadTokenInformation(token.get(), TokenGroups, groups_storage, error)) {
        return false;
    }
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(groups_storage.data());
    constexpr auto groups_offset = offsetof(TOKEN_GROUPS, Groups);
    if (groups_storage.size() < groups_offset ||
        groups->GroupCount > (groups_storage.size() - groups_offset) / sizeof(SID_AND_ATTRIBUTES)) {
        error = ERROR_BAD_LENGTH;
        return false;
    }
    candidate.group_sids.reserve(groups->GroupCount);
    const std::span<const SID_AND_ATTRIBUTES> group_view{groups->Groups, groups->GroupCount};
    for (const auto& group : group_view) {
        if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID) {
            continue;
        }
        std::vector<std::byte> group_sid;
        if (!CopyValidatedSid(group.Sid, group_sid)) {
            error = ERROR_INVALID_SID;
            return false;
        }
        candidate.group_sids.push_back(std::move(group_sid));
    }
    identity = std::move(candidate);
    error = ERROR_SUCCESS;
    return true;
}

bool WindowsPipeSecurityApi::Revert(std::uint32_t& error) noexcept {
    if (RevertToSelf() == FALSE) {
        error = GetLastError();
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

ClientIdentityResult VerifyPipeClient(HANDLE server_pipe, const AuthorizedPrincipal& principal,
                                      PipeSecurityApi& api) noexcept {
    const auto validation_error = ValidateAuthorizedPrincipal(principal);
    if (validation_error != PipeSecurityError::None) {
        return {.error = validation_error, .win32_error = ERROR_INVALID_PARAMETER};
    }

    std::uint32_t win32_error{};
    if (!api.Impersonate(server_pipe, win32_error)) {
        return {.error = PipeSecurityError::ImpersonationFailed, .win32_error = win32_error};
    }
    ImpersonationGuard guard{api};

    QueriedClientIdentity identity;
    const bool query_succeeded = api.QueryThreadIdentity(identity, win32_error);
    const bool identity_matches = query_succeeded && IdentityMatches(principal, identity);
    const auto query_error = win32_error;
    if (!guard.Finish(win32_error)) {
        return {.error = PipeSecurityError::RevertFailed, .win32_error = win32_error};
    }
    if (!query_succeeded) {
        return {.error = PipeSecurityError::TokenQueryFailed, .win32_error = query_error};
    }
    if (!identity_matches) {
        return {.error = PipeSecurityError::Unauthorized};
    }
    return {};
}

} // namespace nfm::ipc
