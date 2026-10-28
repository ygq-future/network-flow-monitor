#include "network_flow_monitor/host/platform/atomic_file.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <utility>

namespace nfm::host::platform {
namespace {

constexpr std::uint32_t kMaxTempAttempts = 64U;

class UniqueHandle {
  public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() {
        Reset();
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }
    void Reset() noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = nullptr;
    }

  private:
    HANDLE handle_{nullptr};
};

} // namespace

std::optional<std::string> ReadFileUtf8(const std::filesystem::path& path,
                                        std::uint64_t max_bytes) {
    if (path.empty()) {
        return std::nullopt;
    }
    UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!file.valid()) {
        return std::nullopt;
    }
    FILE_STANDARD_INFO info{};
    if (GetFileInformationByHandleEx(file.get(), FileStandardInfo, &info, sizeof(info)) == FALSE ||
        info.Directory != FALSE) {
        return std::nullopt;
    }
    const auto file_size = static_cast<std::uint64_t>(info.EndOfFile.QuadPart);
    if (file_size == 0U || (max_bytes > 0U && file_size > max_bytes)) {
        return std::nullopt;
    }
    std::string contents(static_cast<std::size_t>(file_size), '\0');
    std::size_t offset = 0U;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20U));
        DWORD bytes_read = 0U;
        if (ReadFile(file.get(), contents.data() + offset, chunk, &bytes_read, nullptr) == FALSE ||
            bytes_read == 0U) {
            return std::nullopt;
        }
        offset += bytes_read;
    }
    return contents;
}

bool WriteFileAtomically(const std::filesystem::path& target, std::string_view contents) {
    if (target.empty()) {
        return false;
    }
    if (const auto parent = target.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::filesystem::path temp_path;
    UniqueHandle temp_handle;
    for (std::uint32_t attempt = 0U; attempt < kMaxTempAttempts; ++attempt) {
        auto candidate_name = target.filename().native();
        candidate_name += L".tmp.";
        candidate_name += std::to_wstring(GetCurrentProcessId());
        candidate_name += L".";
        candidate_name += std::to_wstring(GetTickCount64());
        candidate_name += L".";
        candidate_name += std::to_wstring(attempt);
        auto candidate = target.parent_path() / candidate_name;
        UniqueHandle h{CreateFileW(candidate.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (h.valid()) {
            temp_path = std::move(candidate);
            temp_handle = std::move(h);
            break;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    if (!temp_handle.valid()) {
        return false;
    }

    std::size_t offset = 0U;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20U));
        DWORD written = 0U;
        if (WriteFile(temp_handle.get(), contents.data() + offset, chunk, &written, nullptr) ==
                FALSE ||
            written == 0U) {
            temp_handle.Reset();
            DeleteFileW(temp_path.c_str());
            return false;
        }
        offset += written;
    }
    if (FlushFileBuffers(temp_handle.get()) == FALSE) {
        temp_handle.Reset();
        DeleteFileW(temp_path.c_str());
        return false;
    }
    temp_handle.Reset();

    const DWORD attrs = GetFileAttributesW(target.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            DeleteFileW(temp_path.c_str());
            return false;
        }
        if (ReplaceFileW(target.c_str(), temp_path.c_str(), nullptr, 0U, nullptr, nullptr) !=
            FALSE) {
            return true;
        }
    }
    if (MoveFileExW(temp_path.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    DeleteFileW(temp_path.c_str());
    return false;
}

} // namespace nfm::host::platform
