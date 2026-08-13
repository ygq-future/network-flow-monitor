#ifndef NETWORK_FLOW_MONITOR_IPC_WINDOWS_PIPE_SECURITY_H
#define NETWORK_FLOW_MONITOR_IPC_WINDOWS_PIPE_SECURITY_H

#include "network_flow_monitor/ipc/security_policy.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace nfm::ipc {

inline constexpr DWORD kPipeClientAccessMask = FILE_GENERIC_READ | FILE_GENERIC_WRITE;

struct QueriedClientIdentity {
    std::vector<std::byte> user_sid;
    std::vector<std::vector<std::byte>> group_sids;
};

class PipeSecurityApi {
  public:
    PipeSecurityApi() = default;
    PipeSecurityApi(const PipeSecurityApi&) = delete;
    PipeSecurityApi& operator=(const PipeSecurityApi&) = delete;
    PipeSecurityApi(PipeSecurityApi&&) = delete;
    PipeSecurityApi& operator=(PipeSecurityApi&&) = delete;
    virtual ~PipeSecurityApi() = default;

    virtual bool Impersonate(HANDLE pipe, std::uint32_t& error) noexcept = 0;
    virtual bool QueryThreadIdentity(QueriedClientIdentity& identity,
                                     std::uint32_t& error) noexcept = 0;
    virtual bool Revert(std::uint32_t& error) noexcept = 0;
};

class WindowsPipeSecurityApi final : public PipeSecurityApi {
  public:
    bool Impersonate(HANDLE pipe, std::uint32_t& error) noexcept override;
    bool QueryThreadIdentity(QueriedClientIdentity& identity,
                             std::uint32_t& error) noexcept override;
    bool Revert(std::uint32_t& error) noexcept override;
};

class PipeSecurityDescriptor final {
  private:
    struct ConstructionToken {};

  public:
    PipeSecurityDescriptor(const PipeSecurityDescriptor&) = delete;
    PipeSecurityDescriptor& operator=(const PipeSecurityDescriptor&) = delete;
    PipeSecurityDescriptor(PipeSecurityDescriptor&& other) noexcept;
    PipeSecurityDescriptor& operator=(PipeSecurityDescriptor&& other) noexcept;
    ~PipeSecurityDescriptor() = default;

    explicit PipeSecurityDescriptor(ConstructionToken construction_token,
                                    std::vector<std::byte> descriptor) noexcept;

    [[nodiscard]] SECURITY_ATTRIBUTES* attributes() noexcept;

  private:
    friend struct PipeSecurityDescriptorFactory;

    std::vector<std::byte> descriptor_;
    SECURITY_ATTRIBUTES attributes_{};
};

struct PipeSecurityResult {
    PipeSecurityError error{PipeSecurityError::None};
    std::unique_ptr<PipeSecurityDescriptor> descriptor;
};

struct ClientIdentityResult {
    PipeSecurityError error{PipeSecurityError::None};
    std::uint32_t win32_error{};

    [[nodiscard]] bool authorized() const noexcept;
};

[[nodiscard]] PipeSecurityResult BuildPipeSecurity(const AuthorizedPrincipal& principal);
[[nodiscard]] ClientIdentityResult VerifyPipeClient(HANDLE server_pipe,
                                                    const AuthorizedPrincipal& principal,
                                                    PipeSecurityApi& api) noexcept;

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_WINDOWS_PIPE_SECURITY_H
