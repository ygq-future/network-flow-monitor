#include "network_flow_monitor/config/config_store.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace nfm::config {
namespace {

constexpr std::uint64_t kMaximumConfigFileBytes = 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumTemporaryNameAttempts = 100U;

class UniqueHandle {
  public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_{handle} {}

    ~UniqueHandle() {
        Reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_{other.Release()} {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    void Reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

  private:
    [[nodiscard]] HANDLE Release() noexcept {
        auto* const released = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return released;
    }

    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class TemporaryPathGuard {
  public:
    explicit TemporaryPathGuard(std::filesystem::path path) : path_{std::move(path)} {}

    ~TemporaryPathGuard() {
        if (active_) {
            DeleteFileW(path_.c_str());
        }
    }

    TemporaryPathGuard(const TemporaryPathGuard&) = delete;
    TemporaryPathGuard& operator=(const TemporaryPathGuard&) = delete;
    TemporaryPathGuard(TemporaryPathGuard&&) = delete;
    TemporaryPathGuard& operator=(TemporaryPathGuard&&) = delete;

    void Release() noexcept {
        active_ = false;
    }

  private:
    std::filesystem::path path_;
    bool active_{true};
};

ConfigError StoreError(ConfigErrorCode code, std::string detail, std::uint32_t system_error) {
    return ConfigError{
        .code = code, .field_path = {}, .detail = std::move(detail), .system_error = system_error};
}

ConfigLoadResult IoFailure(ConfigErrorCode code, std::string detail, std::uint32_t system_error) {
    return ConfigLoadResult{.config = DefaultAppConfig(),
                            .status = ConfigLoadStatus::IoError,
                            .error = StoreError(code, std::move(detail), system_error)};
}

ConfigSaveResult SaveFailure(ConfigErrorCode code, std::string detail, std::uint32_t system_error) {
    return ConfigSaveResult{.saved = false,
                            .error = StoreError(code, std::move(detail), system_error)};
}

std::filesystem::path TemporarySiblingPath(const std::filesystem::path& target,
                                           std::uint32_t attempt) {
    auto name = target.filename().native();
    name += L".tmp.";
    name += std::to_wstring(GetCurrentProcessId());
    name += L".";
    name += std::to_wstring(GetCurrentThreadId());
    name += L".";
    name += std::to_wstring(GetTickCount64());
    name += L".";
    name += std::to_wstring(attempt);
    return target.parent_path() / name;
}

struct TemporaryFile {
    std::filesystem::path path;
    UniqueHandle handle;
    std::uint32_t error{};
};

TemporaryFile CreateTemporarySibling(const std::filesystem::path& target) {
    for (std::uint32_t attempt = 0; attempt < kMaximumTemporaryNameAttempts; ++attempt) {
        auto path = TemporarySiblingPath(target, attempt);
        UniqueHandle handle{CreateFileW(path.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (handle.valid()) {
            return TemporaryFile{
                .path = std::move(path), .handle = std::move(handle), .error = ERROR_SUCCESS};
        }
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return TemporaryFile{.path = std::move(path), .handle = {}, .error = error};
        }
    }
    return TemporaryFile{.path = {}, .handle = {}, .error = ERROR_FILE_EXISTS};
}

std::optional<std::uint32_t> WriteAndFlush(HANDLE file, std::string_view contents) {
    std::size_t offset{};
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto requested = static_cast<DWORD>(remaining);
        DWORD written{};
        if (WriteFile(file, contents.data() + offset, requested, &written, nullptr) == FALSE) {
            return GetLastError();
        }
        if (written == 0U) {
            return ERROR_WRITE_FAULT;
        }
        offset += written;
    }
    if (FlushFileBuffers(file) == FALSE) {
        return GetLastError();
    }
    return std::nullopt;
}

std::optional<std::uint32_t> ReplaceTarget(const std::filesystem::path& target,
                                           const std::filesystem::path& temporary) {
    const auto attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            return ERROR_DIRECTORY;
        }
        if (ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, 0U, nullptr, nullptr) ==
            FALSE) {
            return GetLastError();
        }
        return std::nullopt;
    }

    const auto attribute_error = GetLastError();
    if (attribute_error != ERROR_FILE_NOT_FOUND && attribute_error != ERROR_PATH_NOT_FOUND) {
        return attribute_error;
    }
    if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE) {
        return std::nullopt;
    }

    const auto move_error = GetLastError();
    if (move_error != ERROR_FILE_EXISTS && move_error != ERROR_ALREADY_EXISTS) {
        return move_error;
    }
    if (ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, 0U, nullptr, nullptr) == FALSE) {
        return GetLastError();
    }
    return std::nullopt;
}

} // namespace

ConfigLoadResult LoadConfigFile(const std::filesystem::path& path, ConfigDocumentKind kind) {
    const UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!file.valid()) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return ConfigLoadResult{.config = DefaultAppConfig(),
                                    .status = ConfigLoadStatus::Missing,
                                    .error = std::nullopt};
        }
        return IoFailure(ConfigErrorCode::OpenFailed, "cannot open configuration file", error);
    }

    FILE_STANDARD_INFO information{};
    if (GetFileInformationByHandleEx(file.get(), FileStandardInfo, &information,
                                     sizeof(information)) == FALSE) {
        return IoFailure(ConfigErrorCode::ReadFailed, "cannot inspect configuration file",
                         GetLastError());
    }
    if (information.Directory != FALSE) {
        return IoFailure(ConfigErrorCode::ReadFailed, "configuration path is a directory",
                         ERROR_DIRECTORY);
    }
    const auto file_size = static_cast<std::uint64_t>(information.EndOfFile.QuadPart);
    if (file_size > kMaximumConfigFileBytes) {
        return ConfigLoadResult{.config = DefaultAppConfig(),
                                .status = ConfigLoadStatus::Damaged,
                                .error = StoreError(ConfigErrorCode::FileTooLarge,
                                                    "configuration file exceeds the 1 MiB limit",
                                                    ERROR_FILE_TOO_LARGE)};
    }

    std::string contents(static_cast<std::size_t>(file_size), '\0');
    std::size_t offset{};
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto requested = static_cast<DWORD>(remaining);
        DWORD read{};
        if (ReadFile(file.get(), contents.data() + offset, requested, &read, nullptr) == FALSE) {
            return IoFailure(ConfigErrorCode::ReadFailed, "cannot read configuration file",
                             GetLastError());
        }
        if (read == 0U) {
            return IoFailure(ConfigErrorCode::ReadFailed, "configuration file ended early",
                             ERROR_HANDLE_EOF);
        }
        offset += read;
    }
    return ParseConfigJson(contents, kind);
}

ConfigSaveResult SaveConfigFile(const std::filesystem::path& path, const AppConfig& config,
                                ConfigDocumentKind kind) {
    auto serialized = SerializeConfigJson(config, kind);
    if (!serialized.ok()) {
        return ConfigSaveResult{.saved = false, .error = std::move(serialized.error)};
    }

    auto temporary = CreateTemporarySibling(path);
    if (!temporary.handle.valid()) {
        return SaveFailure(ConfigErrorCode::WriteFailed, "cannot create temporary config file",
                           temporary.error);
    }
    TemporaryPathGuard cleanup{temporary.path};
    if (const auto error = WriteAndFlush(temporary.handle.get(), serialized.json)) {
        return SaveFailure(ConfigErrorCode::WriteFailed, "cannot write temporary config file",
                           *error);
    }
    temporary.handle.Reset();

    if (const auto error = ReplaceTarget(path, temporary.path)) {
        return SaveFailure(ConfigErrorCode::ReplaceFailed,
                           "cannot atomically replace configuration file", *error);
    }
    cleanup.Release();
    return ConfigSaveResult{.saved = true, .error = std::nullopt};
}

} // namespace nfm::config
