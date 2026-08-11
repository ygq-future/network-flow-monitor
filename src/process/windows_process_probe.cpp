#include "network_flow_monitor/process/windows_process_probe.h"

#include <appmodel.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nfm::process {
namespace {

inline constexpr std::uint64_t kFileTimeUnixEpochOffset = 116'444'736'000'000'000ULL;
inline constexpr std::size_t kMaximumImagePathCharacters = 32'768U;

class UniqueProcessHandle {
  public:
    explicit UniqueProcessHandle(HANDLE handle) noexcept : handle_{handle} {}
    ~UniqueProcessHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
        }
    }

    UniqueProcessHandle(const UniqueProcessHandle&) = delete;
    UniqueProcessHandle& operator=(const UniqueProcessHandle&) = delete;
    UniqueProcessHandle(UniqueProcessHandle&&) = delete;
    UniqueProcessHandle& operator=(UniqueProcessHandle&&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

  private:
    HANDLE handle_;
};

struct StringQueryResult {
    ProcessProbeError error{ProcessProbeError::None};
    ProcessProbeStage stage{ProcessProbeStage::None};
    std::uint32_t win32_error{};
    std::optional<std::string> value;

    [[nodiscard]] bool ok() const noexcept {
        return error == ProcessProbeError::None;
    }
};

ProcessProbeError ClassifyWindowsError(std::uint32_t error) noexcept {
    if (error == ERROR_ACCESS_DENIED) {
        return ProcessProbeError::AccessDenied;
    }
    if (error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_HANDLE ||
        error == ERROR_NOT_FOUND) {
        return ProcessProbeError::ProcessUnavailable;
    }
    return ProcessProbeError::WindowsFailure;
}

StringQueryResult Failure(ProcessProbeStage stage, std::uint32_t error) noexcept {
    return {.error = ClassifyWindowsError(error), .stage = stage, .win32_error = error};
}

StringQueryResult ToUtf8(std::wstring_view value, ProcessProbeStage source_stage) {
    if (value.empty()) {
        return {.value = std::string{}};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {.error = ProcessProbeError::StringTooLong, .stage = source_stage};
    }
    const int source_length = static_cast<int>(value.size());
    const wchar_t* source = std::to_address(value.begin());
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, source_length,
                                             nullptr, 0, nullptr, nullptr);
    if (required == 0) {
        const std::uint32_t error = GetLastError();
        return {.error = error == ERROR_NO_UNICODE_TRANSLATION ? ProcessProbeError::InvalidUnicode
                                                               : ClassifyWindowsError(error),
                .stage = ProcessProbeStage::Utf8Conversion,
                .win32_error = error};
    }

    std::string converted(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, source_length, converted.data(),
                            required, nullptr, nullptr) == 0) {
        const std::uint32_t error = GetLastError();
        return {.error = error == ERROR_NO_UNICODE_TRANSLATION ? ProcessProbeError::InvalidUnicode
                                                               : ClassifyWindowsError(error),
                .stage = ProcessProbeStage::Utf8Conversion,
                .win32_error = error};
    }
    return {.value = std::move(converted)};
}

StringQueryResult QueryApplicationUserModelId(HANDLE process) {
    UINT32 length = 0U;
    LONG status = GetApplicationUserModelId(process, &length, nullptr);
    if (status == APPMODEL_ERROR_NO_APPLICATION) {
        return {};
    }
    if (status != ERROR_INSUFFICIENT_BUFFER) {
        return Failure(ProcessProbeStage::ApplicationUserModelId,
                       static_cast<std::uint32_t>(status));
    }
    if (length == 0U || length > APPLICATION_USER_MODEL_ID_MAX_LENGTH) {
        return {.error = ProcessProbeError::StringTooLong,
                .stage = ProcessProbeStage::ApplicationUserModelId};
    }

    std::vector<wchar_t> buffer(length);
    status = GetApplicationUserModelId(process, &length, buffer.data());
    if (status != ERROR_SUCCESS) {
        return Failure(ProcessProbeStage::ApplicationUserModelId,
                       static_cast<std::uint32_t>(status));
    }
    std::size_t character_count = length;
    if (character_count > 0U && buffer.at(character_count - 1U) == L'\0') {
        --character_count;
    }
    return ToUtf8(std::wstring_view{buffer.data(), character_count},
                  ProcessProbeStage::ApplicationUserModelId);
}

StringQueryResult QueryPackageFamilyName(HANDLE process) {
    UINT32 length = 0U;
    LONG status = GetPackageFamilyName(process, &length, nullptr);
    if (status == APPMODEL_ERROR_NO_PACKAGE) {
        return {};
    }
    if (status != ERROR_INSUFFICIENT_BUFFER) {
        return Failure(ProcessProbeStage::PackageFamilyName, static_cast<std::uint32_t>(status));
    }
    constexpr UINT32 maximum_length = PACKAGE_FAMILY_NAME_MAX_LENGTH + 1U;
    if (length == 0U || length > maximum_length) {
        return {.error = ProcessProbeError::StringTooLong,
                .stage = ProcessProbeStage::PackageFamilyName};
    }

    std::vector<wchar_t> buffer(length);
    status = GetPackageFamilyName(process, &length, buffer.data());
    if (status != ERROR_SUCCESS) {
        return Failure(ProcessProbeStage::PackageFamilyName, static_cast<std::uint32_t>(status));
    }
    std::size_t character_count = length;
    if (character_count > 0U && buffer.at(character_count - 1U) == L'\0') {
        --character_count;
    }
    return ToUtf8(std::wstring_view{buffer.data(), character_count},
                  ProcessProbeStage::PackageFamilyName);
}

StringQueryResult QueryImagePath(HANDLE process) {
    std::vector<wchar_t> buffer(kMaximumImagePathCharacters);
    auto length = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageNameW(process, 0U, buffer.data(), &length) == FALSE) {
        const std::uint32_t error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            return {.error = ProcessProbeError::StringTooLong,
                    .stage = ProcessProbeStage::ImagePath,
                    .win32_error = error};
        }
        return Failure(ProcessProbeStage::ImagePath, error);
    }
    return ToUtf8(std::wstring_view{buffer.data(), length}, ProcessProbeStage::ImagePath);
}

WindowsProcessProbeResult ProbeWindowsProcessImpl(const ProcessProbeRequest& request) {
    if (request.instance.pid == 0U || request.instance.generation == 0U) {
        return {.error = ProcessProbeError::InvalidRequest};
    }

    const UniqueProcessHandle process{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, request.instance.pid)};
    if (process.get() == nullptr) {
        const std::uint32_t error = GetLastError();
        return {.error = ClassifyWindowsError(error),
                .stage = ProcessProbeStage::OpenProcess,
                .win32_error = error};
    }

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(process.get(), &creation, &exit, &kernel, &user) == FALSE) {
        const std::uint32_t error = GetLastError();
        return {.error = ClassifyWindowsError(error),
                .stage = ProcessProbeStage::CreationTime,
                .win32_error = error};
    }
    const auto creation_time = FileTimeToTraceTimePoint(creation);
    if (!creation_time.has_value()) {
        return {.error = ProcessProbeError::WindowsFailure,
                .stage = ProcessProbeStage::CreationTime,
                .win32_error = ERROR_ARITHMETIC_OVERFLOW};
    }
    if (*creation_time > request.observed_at) {
        return {.error = ProcessProbeError::PidReusedBeforeProbe,
                .stage = ProcessProbeStage::CreationTime,
                .creation_time = creation_time};
    }

    const auto application_id = QueryApplicationUserModelId(process.get());
    if (!application_id.ok()) {
        return {.error = application_id.error,
                .stage = application_id.stage,
                .win32_error = application_id.win32_error,
                .creation_time = creation_time};
    }
    const auto package_family = QueryPackageFamilyName(process.get());
    if (!package_family.ok()) {
        return {.error = package_family.error,
                .stage = package_family.stage,
                .win32_error = package_family.win32_error,
                .creation_time = creation_time};
    }
    const auto image_path = QueryImagePath(process.get());
    if (!image_path.ok()) {
        return {.error = image_path.error,
                .stage = image_path.stage,
                .win32_error = image_path.win32_error,
                .creation_time = creation_time};
    }

    return {.creation_time = creation_time,
            .data = {.application_user_model_id = application_id.value,
                     .package_family_name = package_family.value,
                     .image_path = image_path.value}};
}

} // namespace

bool WindowsProcessProbeResult::ok() const noexcept {
    return error == ProcessProbeError::None;
}

std::optional<TraceTimePoint> FileTimeToTraceTimePoint(const FILETIME& file_time) noexcept {
    const std::uint64_t ticks = (static_cast<std::uint64_t>(file_time.dwHighDateTime) << 32U) |
                                static_cast<std::uint64_t>(file_time.dwLowDateTime);
    if (ticks < kFileTimeUnixEpochOffset) {
        return std::nullopt;
    }
    const std::uint64_t unix_ticks = ticks - kFileTimeUnixEpochOffset;
    if (unix_ticks > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return TraceTimePoint{TraceDuration{static_cast<std::int64_t>(unix_ticks)}};
}

WindowsProcessProbeResult ProbeWindowsProcess(const ProcessProbeRequest& request) noexcept {
    try {
        return ProbeWindowsProcessImpl(request);
    } catch (const std::bad_alloc&) {
        return {.error = ProcessProbeError::OutOfMemory};
    } catch (...) {
        return {.error = ProcessProbeError::WindowsFailure};
    }
}

} // namespace nfm::process
