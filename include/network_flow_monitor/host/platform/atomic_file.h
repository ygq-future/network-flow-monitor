#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_ATOMIC_FILE_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_ATOMIC_FILE_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace nfm::host::platform {

// Reads a UTF-8 text file, rejecting directories, empty files and anything over max_bytes.
[[nodiscard]] std::optional<std::string> ReadFileUtf8(const std::filesystem::path& path,
                                                      std::uint64_t max_bytes);

// Writes through a uniquely named temp file in the target directory, then replaces the target.
[[nodiscard]] bool WriteFileAtomically(const std::filesystem::path& target,
                                       std::string_view contents);

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_ATOMIC_FILE_H
