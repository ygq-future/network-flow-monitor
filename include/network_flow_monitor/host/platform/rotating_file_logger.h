#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_ROTATING_FILE_LOGGER_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_ROTATING_FILE_LOGGER_H

#include "network_flow_monitor/config/app_config.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace nfm::host::platform {

inline constexpr std::uintmax_t kDefaultMaxLogFileSizeBytes = 2ULL * 1024ULL * 1024ULL; // 2 MiB
inline constexpr std::size_t kDefaultMaxLogArchiveCount = 5U;
inline constexpr std::wstring_view kDefaultLogFileName = L"nfm-host.log";

struct RotatingFileLoggerOptions {
    std::filesystem::path log_directory;
    std::wstring file_name{kDefaultLogFileName};
    std::uintmax_t max_file_size_bytes{kDefaultMaxLogFileSizeBytes};
    std::size_t max_archive_count{kDefaultMaxLogArchiveCount};
};

class RotatingFileLogger {
  public:
    explicit RotatingFileLogger(RotatingFileLoggerOptions options = {});
    ~RotatingFileLogger() = default;

    RotatingFileLogger(const RotatingFileLogger&) = delete;
    RotatingFileLogger& operator=(const RotatingFileLogger&) = delete;
    RotatingFileLogger(RotatingFileLogger&&) = delete;
    RotatingFileLogger& operator=(RotatingFileLogger&&) = delete;

    void set_options(RotatingFileLoggerOptions options);
    [[nodiscard]] std::filesystem::path log_directory() const;
    [[nodiscard]] std::filesystem::path active_log_file_path() const;
    [[nodiscard]] std::filesystem::path archive_file_path(std::size_t index) const;

    void write(config::LogLevel minimum_configured_level, config::LogLevel message_level,
               std::string_view message) noexcept;

  private:
    void rotate_if_needed_locked(std::size_t incoming_bytes);
    [[nodiscard]] std::filesystem::path compute_archive_path_locked(std::size_t index) const;

    mutable std::mutex mutex_;
    RotatingFileLoggerOptions options_;
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_ROTATING_FILE_LOGGER_H
