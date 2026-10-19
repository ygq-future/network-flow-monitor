#include "network_flow_monitor/host/platform/rotating_file_logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <shlobj.h>
#include <system_error>
#include <utility>

namespace nfm::host::platform {

namespace {

int LevelRank(config::LogLevel level) noexcept {
    switch (level) {
    case config::LogLevel::Off:
        return 0;
    case config::LogLevel::Error:
        return 1;
    case config::LogLevel::Info:
        return 2;
    case config::LogLevel::Debug:
        return 3;
    }
    return 0;
}

const char* LevelTag(config::LogLevel level) noexcept {
    switch (level) {
    case config::LogLevel::Error:
        return "ERROR";
    case config::LogLevel::Info:
        return "INFO";
    case config::LogLevel::Debug:
        return "DEBUG";
    case config::LogLevel::Off:
    default:
        return "OFF";
    }
}
bool FileExistsWin32(const std::filesystem::path& path) noexcept {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

std::uintmax_t QueryFileSizeWin32(const std::filesystem::path& path, bool& ok) noexcept {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) == FALSE) {
        ok = false;
        return 0U;
    }
    ok = true;
    return (static_cast<std::uintmax_t>(data.nFileSizeHigh) << 32U) |
           static_cast<std::uintmax_t>(data.nFileSizeLow);
}

void RemoveFileWin32(const std::filesystem::path& path) noexcept {
    static_cast<void>(DeleteFileW(path.c_str()));
}

void ReplaceOrRenameFileWin32(const std::filesystem::path& src,
                              const std::filesystem::path& dst) noexcept {
    static_cast<void>(
        MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}
} // namespace

RotatingFileLogger::RotatingFileLogger(RotatingFileLoggerOptions options)
    : options_(std::move(options)) {}

void RotatingFileLogger::set_options(RotatingFileLoggerOptions options) {
    const std::scoped_lock lock(mutex_);
    options_ = std::move(options);
}

std::filesystem::path RotatingFileLogger::log_directory() const {
    const std::scoped_lock lock(mutex_);
    return options_.log_directory;
}

std::filesystem::path RotatingFileLogger::active_log_file_path() const {
    const std::scoped_lock lock(mutex_);
    return options_.log_directory / options_.file_name;
}

std::filesystem::path RotatingFileLogger::archive_file_path(std::size_t index) const {
    const std::scoped_lock lock(mutex_);
    return compute_archive_path_locked(index);
}

std::filesystem::path RotatingFileLogger::compute_archive_path_locked(std::size_t index) const {
    const std::filesystem::path base_name(options_.file_name);
    const std::wstring stem = base_name.stem().wstring();
    const std::wstring ext = base_name.extension().wstring();
    return options_.log_directory / (stem + L"." + std::to_wstring(index) + ext);
}

void RotatingFileLogger::rotate_if_needed_locked(std::size_t incoming_bytes) {
    if (options_.log_directory.empty() || options_.max_file_size_bytes == 0U) {
        return;
    }
    const std::filesystem::path active_path = options_.log_directory / options_.file_name;
    if (!FileExistsWin32(active_path)) {
        return;
    }
    bool size_ok = false;
    const auto current_size = QueryFileSizeWin32(active_path, size_ok);
    if (!size_ok ||
        (current_size > 0U && current_size + incoming_bytes <= options_.max_file_size_bytes)) {
        return;
    }
    if (current_size == 0U && incoming_bytes <= options_.max_file_size_bytes) {
        return;
    }

    if (options_.max_archive_count == 0U) {
        RemoveFileWin32(active_path);
        return;
    }

    // Remove the oldest archive if present
    const auto oldest_path = compute_archive_path_locked(options_.max_archive_count);
    RemoveFileWin32(oldest_path);

    // Shift archive.(i) -> archive.(i+1)
    for (std::size_t idx = options_.max_archive_count - 1U; idx >= 1U; --idx) {
        const auto src = compute_archive_path_locked(idx);
        const auto dst = compute_archive_path_locked(idx + 1U);
        if (FileExistsWin32(src)) {
            ReplaceOrRenameFileWin32(src, dst);
        }
        if (idx == 1U) {
            break;
        }
    }

    // Move active log -> archive.1
    const auto first_archive = compute_archive_path_locked(1U);
    ReplaceOrRenameFileWin32(active_path, first_archive);
}

void RotatingFileLogger::write(config::LogLevel minimum_configured_level,
                               config::LogLevel message_level, std::string_view message) noexcept {
    try {
        if (minimum_configured_level == config::LogLevel::Off ||
            message_level == config::LogLevel::Off) {
            return;
        }
        if (LevelRank(message_level) > LevelRank(minimum_configured_level)) {
            return;
        }

        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::array<char, 64> ts_buf{};
        static_cast<void>(std::snprintf(
            ts_buf.data(), ts_buf.size(), "%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth,
            st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds));

        std::string line;
        line.reserve(message.size() + 48U);
        line.push_back('[');
        line.append(ts_buf.data());
        line.append("] [");
        line.append(LevelTag(message_level));
        line.append("] ");
        line.append(message);
        line.push_back('\n');

        const std::scoped_lock lock(mutex_);
        if (options_.log_directory.empty()) {
            return;
        }

        static_cast<void>(SHCreateDirectoryExW(nullptr, options_.log_directory.c_str(), nullptr));
        rotate_if_needed_locked(line.size());

        const std::filesystem::path active_path = options_.log_directory / options_.file_name;
        std::ofstream out(active_path, std::ios::binary | std::ios::app);
        if (out.is_open()) {
            out.write(line.data(), static_cast<std::streamsize>(line.size()));
            out.flush();
        }
    } catch (...) {
    }
}

} // namespace nfm::host::platform
