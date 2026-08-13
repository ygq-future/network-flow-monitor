#include "network_flow_monitor/process/identity_resolver.h"

#include <algorithm>
#include <string>
#include <utility>

namespace nfm::process {
namespace {

char ToLowerAscii(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
}

bool EqualsAsciiCaseInsensitive(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ToLowerAscii(left.at(index)) != ToLowerAscii(right.at(index))) {
            return false;
        }
    }
    return true;
}

bool StartsWithAsciiCaseInsensitive(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
           EqualsAsciiCaseInsensitive(value.substr(0U, prefix.size()), prefix);
}

bool IsAsciiAlpha(char value) noexcept {
    const char lower = ToLowerAscii(value);
    return lower >= 'a' && lower <= 'z';
}

bool IsDriveAbsolute(std::string_view path) noexcept {
    return path.size() >= 3U && IsAsciiAlpha(path.at(0U)) && path.at(1U) == ':' &&
           path.at(2U) == '\\';
}

bool IsUncAbsolute(std::string_view path) noexcept {
    if (!path.starts_with("\\\\")) {
        return false;
    }
    const auto server_end = path.find('\\', 2U);
    if (server_end == std::string_view::npos || server_end == 2U) {
        return false;
    }
    const auto share_start = server_end + 1U;
    const auto share_end = path.find('\\', share_start);
    return share_start < path.size() && share_end != share_start;
}

bool IsExtendedAbsolute(std::string_view path) noexcept {
    return path.size() > 4U && path.starts_with(R"(\\?\)");
}

std::size_t UncRootLength(std::string_view path) noexcept {
    const auto server_end = path.find('\\', 2U);
    if (server_end == std::string_view::npos) {
        return path.size();
    }
    const auto share_end = path.find('\\', server_end + 1U);
    return share_end == std::string_view::npos ? path.size() : share_end;
}

std::string DisplayName(const ProcessProbeData& data,
                        const std::optional<std::string>& normalized_path) {
    if (!data.image_name_hint.empty()) {
        return data.image_name_hint;
    }
    if (normalized_path.has_value()) {
        const auto separator = normalized_path->find_last_of('\\');
        if (separator == std::string::npos) {
            return *normalized_path;
        }
        if (separator + 1U < normalized_path->size()) {
            return normalized_path->substr(separator + 1U);
        }
    }
    if (data.application_user_model_id.has_value() && !data.application_user_model_id->empty()) {
        return *data.application_user_model_id;
    }
    if (data.package_family_name.has_value() && !data.package_family_name->empty()) {
        return *data.package_family_name;
    }
    return {};
}

ResolvedApplicationIdentity MakeIdentity(std::string id, std::string display_name,
                                         std::string icon_key, IdentityKind kind) {
    return {.id = std::move(id),
            .metadata = {.display_name = std::move(display_name), .icon_key = std::move(icon_key)},
            .kind = kind};
}

} // namespace

bool IdentityResolutionResult::ok() const noexcept {
    return error == IdentityResolutionError::None;
}

std::optional<std::string> NormalizeWindowsImagePath(std::string_view path) {
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }

    std::string normalized{path};
    std::ranges::replace(normalized, '/', '\\');
    constexpr std::string_view extended_unc_prefix{R"(\\?\UNC\)"};
    constexpr std::string_view extended_prefix{R"(\\?\)"};
    if (StartsWithAsciiCaseInsensitive(normalized, extended_unc_prefix)) {
        normalized = R"(\\)" + normalized.substr(extended_unc_prefix.size());
    } else if (normalized.starts_with(extended_prefix) &&
               IsDriveAbsolute(std::string_view{normalized}.substr(extended_prefix.size()))) {
        normalized.erase(0U, extended_prefix.size());
    }

    if (!IsDriveAbsolute(normalized) && !IsUncAbsolute(normalized) &&
        !IsExtendedAbsolute(normalized)) {
        return std::nullopt;
    }

    const std::size_t root_length = IsDriveAbsolute(normalized) ? 3U : UncRootLength(normalized);
    while (normalized.size() > root_length && normalized.back() == '\\') {
        normalized.pop_back();
    }
    return normalized;
}

IdentityResolutionResult ResolveApplicationIdentity(ProcessInstanceId instance,
                                                    const ProcessProbeData& data) {
    std::optional<std::string> normalized_path;
    if (data.image_path.has_value() && !data.image_path->empty()) {
        normalized_path = NormalizeWindowsImagePath(*data.image_path);
        if (!normalized_path.has_value()) {
            return {.error = IdentityResolutionError::InvalidImagePath};
        }
    }

    const auto display_name = DisplayName(data, normalized_path);
    const auto icon_key = normalized_path.value_or(std::string{});
    if (data.application_user_model_id.has_value() && !data.application_user_model_id->empty()) {
        return {.identity = MakeIdentity("package-app:" + *data.application_user_model_id,
                                         display_name, icon_key, IdentityKind::PackageApplication)};
    }
    if (data.package_family_name.has_value() && !data.package_family_name->empty()) {
        return {.identity = MakeIdentity("package-family:" + *data.package_family_name,
                                         display_name, icon_key, IdentityKind::PackageFamily)};
    }
    if (normalized_path.has_value()) {
        return {.identity = MakeIdentity("executable:" + *normalized_path, display_name,
                                         *normalized_path, IdentityKind::Executable)};
    }
    if (instance.pid == 0U) {
        return {.identity =
                    MakeIdentity("system:idle", "System Idle Process", {}, IdentityKind::System)};
    }
    if (instance.pid == 4U && EqualsAsciiCaseInsensitive(data.image_name_hint, "System")) {
        return {.identity = MakeIdentity("system:kernel", "System", {}, IdentityKind::System)};
    }
    return {.identity = MakeIdentity("unresolved:" + std::to_string(instance.pid) + ":" +
                                         std::to_string(instance.generation),
                                     display_name, {}, IdentityKind::Unresolved)};
}

} // namespace nfm::process
