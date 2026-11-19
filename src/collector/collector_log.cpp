#include "network_flow_monitor/collector/collector_log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdio>
#include <mutex>
#include <share.h>

namespace nfm::collector {
namespace {

constexpr std::uintmax_t kMaximumLogBytes = 1024ULL * 1024ULL;
constexpr std::size_t kMaximumMessageCharacters = 512U;
constexpr std::size_t kTimestampCharacters = 32U;

struct CollectorLogState {
    std::mutex mutex;
    std::FILE* file{nullptr};
};

CollectorLogState& State() noexcept {
    static CollectorLogState state;
    return state;
}

[[nodiscard]] std::string_view SeverityName(CollectorLogSeverity severity) noexcept {
    switch (severity) {
    case CollectorLogSeverity::Info:
        return "INFO";
    case CollectorLogSeverity::Warning:
        return "WARN";
    case CollectorLogSeverity::Error:
        return "ERROR";
    }
    return "INFO";
}

void RotateIfOversized(const std::filesystem::path& file_path) noexcept {
    try {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (::GetFileAttributesExW(file_path.c_str(), GetFileExInfoStandard, &attributes) ==
            FALSE) {
            return;
        }
        const auto size = (static_cast<std::uintmax_t>(attributes.nFileSizeHigh) << 32U) |
                          static_cast<std::uintmax_t>(attributes.nFileSizeLow);
        if (size < kMaximumLogBytes) {
            return;
        }
        auto archived = file_path;
        archived += L".1";
        static_cast<void>(::DeleteFileW(archived.c_str()));
        static_cast<void>(
            ::MoveFileExW(file_path.c_str(), archived.c_str(), MOVEFILE_REPLACE_EXISTING));
    } catch (...) {
    }
}

} // namespace

void OpenCollectorLog(const std::filesystem::path& file_path) noexcept {
    try {
        if (file_path.empty()) {
            return;
        }
        const auto parent = file_path.parent_path();
        if (!parent.empty()) {
            static_cast<void>(::CreateDirectoryW(parent.c_str(), nullptr));
        }
        RotateIfOversized(file_path);
        // _SH_DENYNO keeps the log readable while the collector runs, which is the whole point of
        // a file that has to be inspected during a live incident.
        std::FILE* file = ::_wfsopen(file_path.c_str(), L"wb", _SH_DENYNO);
        if (file == nullptr) {
            return;
        }
        const std::scoped_lock lock{State().mutex};
        if (State().file != nullptr) {
            static_cast<void>(std::fclose(State().file));
        }
        State().file = file;
    } catch (...) {
    }
}

void CloseCollectorLog() noexcept {
    const std::scoped_lock lock{State().mutex};
    if (State().file != nullptr) {
        static_cast<void>(std::fclose(State().file));
        State().file = nullptr;
    }
}

bool CollectorLogIsOpen() noexcept {
    const std::scoped_lock lock{State().mutex};
    return State().file != nullptr;
}

void WriteCollectorLog(CollectorLogSeverity severity, std::string_view message) noexcept {
    try {
        const std::scoped_lock lock{State().mutex};
        if (State().file == nullptr) {
            return;
        }
        SYSTEMTIME local_time{};
        ::GetLocalTime(&local_time);
        std::array<char, kTimestampCharacters> timestamp{};
        static_cast<void>(std::snprintf(
            timestamp.data(), timestamp.size(), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
            static_cast<unsigned>(local_time.wYear), static_cast<unsigned>(local_time.wMonth),
            static_cast<unsigned>(local_time.wDay), static_cast<unsigned>(local_time.wHour),
            static_cast<unsigned>(local_time.wMinute), static_cast<unsigned>(local_time.wSecond),
            static_cast<unsigned>(local_time.wMilliseconds)));
        const auto severity_name = SeverityName(severity);
        const auto bounded_message = message.substr(0U, kMaximumMessageCharacters);
        static_cast<void>(std::fprintf(State().file, "[%s] [%.*s] %.*s\n", timestamp.data(),
                                       static_cast<int>(severity_name.size()), severity_name.data(),
                                       static_cast<int>(bounded_message.size()),
                                       bounded_message.data()));
        static_cast<void>(std::fflush(State().file));
    } catch (...) {
    }
}

} // namespace nfm::collector
