#ifndef NETWORK_FLOW_MONITOR_PROCESS_IDENTITY_RESOLVER_H
#define NETWORK_FLOW_MONITOR_PROCESS_IDENTITY_RESOLVER_H

#include "network_flow_monitor/process/application_identity.h"
#include "network_flow_monitor/process/process_instance.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nfm::process {

enum class IdentityKind : std::uint8_t {
    PackageApplication,
    PackageFamily,
    Executable,
    System,
    Unresolved,
};

enum class IdentityResolutionError : std::uint8_t {
    None,
    InvalidImagePath,
};

struct ProcessProbeData {
    std::optional<std::string> application_user_model_id;
    std::optional<std::string> package_family_name;
    std::optional<std::string> image_path;
    std::string image_name_hint;
};

struct ResolvedApplicationIdentity {
    ApplicationId id;
    ApplicationMetadata metadata;
    IdentityKind kind{IdentityKind::Unresolved};

    bool operator==(const ResolvedApplicationIdentity&) const = default;
};

struct IdentityResolutionResult {
    IdentityResolutionError error{IdentityResolutionError::None};
    ResolvedApplicationIdentity identity;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] std::optional<std::string> NormalizeWindowsImagePath(std::string_view path);

[[nodiscard]] IdentityResolutionResult ResolveApplicationIdentity(ProcessInstanceId instance,
                                                                  const ProcessProbeData& data);

} // namespace nfm::process

#endif // NETWORK_FLOW_MONITOR_PROCESS_IDENTITY_RESOLVER_H
