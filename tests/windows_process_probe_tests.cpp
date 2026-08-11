#include "network_flow_monitor/process/windows_process_probe.h"

#include <windows.h>

#include <aclapi.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

class UniqueTestHandle {
  public:
    explicit UniqueTestHandle(HANDLE handle = nullptr) noexcept : handle_{handle} {}
    ~UniqueTestHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
        }
    }

    UniqueTestHandle(const UniqueTestHandle&) = delete;
    UniqueTestHandle& operator=(const UniqueTestHandle&) = delete;
    UniqueTestHandle(UniqueTestHandle&&) = delete;
    UniqueTestHandle& operator=(UniqueTestHandle&&) = delete;

  private:
    HANDLE handle_;
};

class ScopedDisabledDebugPrivilege {
  public:
    ScopedDisabledDebugPrivilege() noexcept {
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_) ==
            FALSE) {
            return;
        }
        LUID debug_luid{};
        if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &debug_luid) == FALSE) {
            return;
        }
        TOKEN_PRIVILEGES requested{};
        requested.PrivilegeCount = 1U;
        requested.Privileges[0].Luid = debug_luid;
        requested.Privileges[0].Attributes = 0U;
        DWORD previous_size = sizeof(previous_);
        if (AdjustTokenPrivileges(token_, FALSE, &requested, sizeof(previous_), &previous_,
                                  &previous_size) == FALSE) {
            return;
        }
        ready_ = true;
    }

    ~ScopedDisabledDebugPrivilege() {
        if (ready_ && previous_.PrivilegeCount != 0U) {
            static_cast<void>(
                AdjustTokenPrivileges(token_, FALSE, &previous_, 0U, nullptr, nullptr));
        }
        if (token_ != nullptr) {
            static_cast<void>(CloseHandle(token_));
        }
    }

    ScopedDisabledDebugPrivilege(const ScopedDisabledDebugPrivilege&) = delete;
    ScopedDisabledDebugPrivilege& operator=(const ScopedDisabledDebugPrivilege&) = delete;
    ScopedDisabledDebugPrivilege(ScopedDisabledDebugPrivilege&&) = delete;
    ScopedDisabledDebugPrivilege& operator=(ScopedDisabledDebugPrivilege&&) = delete;

    [[nodiscard]] bool ready() const noexcept {
        return ready_;
    }

  private:
    HANDLE token_{nullptr};
    TOKEN_PRIVILEGES previous_{};
    bool ready_{};
};

using nfm::process::IdentityKind;
using nfm::process::ProcessProbeError;
using nfm::process::ProcessProbeRequest;
using nfm::process::ProcessStartKind;
using nfm::process::TraceDuration;
using nfm::process::TraceTimePoint;
using nfm::process::WindowsProcessProbeResult;

std::optional<TraceTimePoint> CurrentTraceTime() {
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    return nfm::process::FileTimeToTraceTimePoint(now);
}

std::optional<std::pair<std::uint32_t, TraceTimePoint>> RunShortLivedChild() {
    std::vector<wchar_t> module_path(32'768U);
    const DWORD path_length =
        GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (path_length == 0U || path_length >= module_path.size()) {
        return std::nullopt;
    }
    const std::wstring command =
        L"\"" + std::wstring{module_path.data(), path_length} + L"\" --probe-child";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    const auto observed_at = CurrentTraceTime();
    if (!observed_at.has_value()) {
        return std::nullopt;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &process) == FALSE) {
        return std::nullopt;
    }
    const UniqueTestHandle process_handle{process.hProcess};
    const UniqueTestHandle thread_handle{process.hThread};
    if (WaitForSingleObject(process.hProcess, 10'000U) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    return std::pair{static_cast<std::uint32_t>(process.dwProcessId), *observed_at};
}

void CheckCurrentProcessProbe(TestContext& context) {
    const auto observed_at = CurrentTraceTime();
    context.Check(observed_at.has_value(), "current FILETIME converts");
    if (!observed_at.has_value()) {
        return;
    }

    const ProcessProbeRequest request{
        .instance = {.pid = static_cast<std::uint32_t>(GetCurrentProcessId()), .generation = 1U},
        .observed_at = *observed_at,
        .kind = ProcessStartKind::Started,
    };
    const WindowsProcessProbeResult result = nfm::process::ProbeWindowsProcess(request);
    context.Check(result.ok(), "current process probe succeeds");
    context.Check(result.creation_time.has_value(), "current process creation time returned");
    context.Check(result.data.image_path.has_value(), "current process image path returned");
    if (!result.ok() || !result.creation_time.has_value() || !result.data.image_path.has_value()) {
        return;
    }
    context.Check(*result.creation_time <= request.observed_at,
                  "current process creation is not after observation");
    const auto identity = nfm::process::ResolveApplicationIdentity(request.instance, result.data);
    context.Check(identity.ok(), "probed process data resolves identity");
    context.Check(identity.identity.kind == IdentityKind::Executable ||
                      identity.identity.kind == IdentityKind::PackageApplication ||
                      identity.identity.kind == IdentityKind::PackageFamily,
                  "current process has evidence-backed identity kind");

    nfm::process::ProcessTracker tracker;
    const auto tracked_start = tracker.ObserveStart({.pid = request.instance.pid,
                                                     .observed_at = request.observed_at,
                                                     .kernel_process_key = 1U,
                                                     .image_name_hint = "probe-test.exe",
                                                     .kind = ProcessStartKind::Started});
    context.Check(tracked_start.accepted(), "probe handoff tracker start accepted");
    context.Check(tracker.ApplyProbeData(tracked_start.instance, result.data) ==
                      nfm::process::ProcessTrackerError::None,
                  "probe data applies to exact tracker generation");
    const auto tracked = tracker.ResolveAt(request.instance.pid, request.observed_at);
    if (!tracked.process.has_value()) {
        context.Check(false, "probe handoff resolves tracked process");
    } else {
        context.Check(tracked.process.value().application.id == identity.identity.id,
                      "probe handoff preserves resolved application identity");
    }

    ProcessProbeRequest impossible = request;
    impossible.observed_at = *result.creation_time - TraceDuration{1};
    const auto reused = nfm::process::ProbeWindowsProcess(impossible);
    context.Check(reused.error == ProcessProbeError::PidReusedBeforeProbe,
                  "creation after Start observation rejects reused PID probe");
    impossible.kind = ProcessStartKind::Rundown;
    context.Check(nfm::process::ProbeWindowsProcess(impossible).error ==
                      ProcessProbeError::PidReusedBeforeProbe,
                  "creation after rundown observation also rejects reused PID probe");
}

void CheckFileTimeEpochConversion(TestContext& context) {
    constexpr std::uint64_t unix_epoch_file_time = 116'444'736'000'000'000ULL;
    const FILETIME epoch{
        .dwLowDateTime = static_cast<DWORD>(unix_epoch_file_time & 0xFFFF'FFFFULL),
        .dwHighDateTime = static_cast<DWORD>(unix_epoch_file_time >> 32U),
    };
    const auto converted = nfm::process::FileTimeToTraceTimePoint(epoch);
    context.Check(converted.has_value(), "FILETIME Unix epoch converts");
    if (converted.has_value()) {
        context.Check(converted->time_since_epoch() == TraceDuration::zero(),
                      "FILETIME epoch offset is exact");
    }
}

void CheckInvalidAndExitedProcess(TestContext& context) {
    const auto observed_at = CurrentTraceTime();
    if (!observed_at.has_value()) {
        context.Check(false, "invalid request test obtains current time");
        return;
    }
    const auto invalid =
        nfm::process::ProbeWindowsProcess({.instance = {.pid = 0U, .generation = 0U},
                                           .observed_at = *observed_at,
                                           .kind = ProcessStartKind::Started});
    context.Check(invalid.error == ProcessProbeError::InvalidRequest,
                  "zero process instance is invalid probe request");

    const auto child = RunShortLivedChild();
    context.Check(child.has_value(), "short-lived child created and exited");
    if (!child.has_value()) {
        return;
    }
    const auto exited =
        nfm::process::ProbeWindowsProcess({.instance = {.pid = child->first, .generation = 2U},
                                           .observed_at = child->second,
                                           .kind = ProcessStartKind::Started});
    context.Check(exited.error == ProcessProbeError::ProcessUnavailable ||
                      exited.error == ProcessProbeError::AccessDenied ||
                      exited.error == ProcessProbeError::PidReusedBeforeProbe,
                  "exited child is unavailable or safely identified as reused");
    context.Check(!exited.ok(), "exited child never fabricates successful probe data");
}

void CheckAccessDeniedProcess(TestContext& context) {
    std::vector<wchar_t> module_path(32'768U);
    const DWORD path_length =
        GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (path_length == 0U || path_length >= module_path.size()) {
        context.Check(false, "access-denied test obtains module path");
        return;
    }
    const std::wstring command =
        L"\"" + std::wstring{module_path.data(), path_length} + L"\" --probe-child";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup,
                       &process) == FALSE) {
        context.Check(false, "access-denied child created suspended");
        return;
    }
    const UniqueTestHandle process_handle{process.hProcess};
    const UniqueTestHandle thread_handle{process.hThread};

    ACL empty_dacl{};
    const bool acl_initialized =
        InitializeAcl(&empty_dacl, sizeof(empty_dacl), ACL_REVISION) != FALSE;
    context.Check(acl_initialized, "empty child DACL initialized");
    const DWORD security_result =
        acl_initialized
            ? SetSecurityInfo(process.hProcess, SE_KERNEL_OBJECT,
                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, &empty_dacl, nullptr)
            : ERROR_INVALID_ACL;
    context.Check(security_result == ERROR_SUCCESS, "empty child DACL applied");

    const auto observed_at = CurrentTraceTime();
    const ScopedDisabledDebugPrivilege disabled_debug;
    context.Check(disabled_debug.ready(), "debug privilege disabled for access check");
    if (security_result == ERROR_SUCCESS && observed_at.has_value() && disabled_debug.ready()) {
        const auto denied = nfm::process::ProbeWindowsProcess(
            {.instance = {.pid = static_cast<std::uint32_t>(process.dwProcessId), .generation = 3U},
             .observed_at = *observed_at,
             .kind = ProcessStartKind::Started});
        context.Check(denied.error == ProcessProbeError::AccessDenied,
                      "empty child DACL yields AccessDenied");
        context.Check(denied.stage == nfm::process::ProcessProbeStage::OpenProcess,
                      "access denial is classified at OpenProcess");
    } else if (!observed_at.has_value()) {
        context.Check(false, "access-denied test obtains current time");
    }

    context.Check(TerminateProcess(process.hProcess, 0U) != FALSE,
                  "controlled suspended child terminated");
    context.Check(WaitForSingleObject(process.hProcess, 10'000U) == WAIT_OBJECT_0,
                  "controlled suspended child reaped");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--probe-child") {
        return 0;
    }
    TestContext context;
    CheckFileTimeEpochConversion(context);
    CheckCurrentProcessProbe(context);
    CheckInvalidAndExitedProcess(context);
    CheckAccessDeniedProcess(context);
    return context.failures();
}
