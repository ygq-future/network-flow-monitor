#include "network_flow_monitor/process/identity_resolver.h"
#include "network_flow_monitor/process/process_tracker.h"
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

// === identity_resolver_suite ===
namespace identity_resolver_suite {
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

using nfm::process::IdentityKind;
using nfm::process::IdentityResolutionError;
using nfm::process::ProcessInstanceId;
using nfm::process::ProcessProbeData;

nfm::process::IdentityResolutionResult Resolve(ProcessInstanceId instance,
                                               const ProcessProbeData& data) {
    return nfm::process::ResolveApplicationIdentity(instance, data);
}

void CheckIdentityPriority(TestContext& context) {
    const ProcessInstanceId instance{.pid = 100U, .generation = 7U};
    const auto packaged = Resolve(instance, {.application_user_model_id = "Family!App",
                                             .package_family_name = "Family",
                                             .image_path = "C:\\App\\app.exe",
                                             .image_name_hint = "app.exe"});
    context.Check(packaged.ok(), "packaged identity resolves");
    context.Check(packaged.identity.kind == IdentityKind::PackageApplication, "AUMID kind wins");
    context.Check(packaged.identity.id == "package-app:Family!App", "AUMID value wins");
    context.Check(packaged.identity.metadata.display_name == "app.exe",
                  "image hint supplies display name");
    context.Check(packaged.identity.metadata.icon_key == "C:\\App\\app.exe",
                  "resolved image path supplies icon key");

    const auto family = Resolve(instance, {.package_family_name = "Family"});
    context.Check(family.ok(), "package family resolves");
    context.Check(family.identity.kind == IdentityKind::PackageFamily,
                  "package family is second priority");
    context.Check(family.identity.id == "package-family:Family", "package family value");

    const auto executable = Resolve(instance, {.image_path = "C:/App/app.exe"});
    context.Check(executable.ok(), "executable identity resolves");
    context.Check(executable.identity.kind == IdentityKind::Executable,
                  "executable is third priority");
    context.Check(executable.identity.id == "executable:C:\\App\\app.exe",
                  "executable path is normalized");
    context.Check(executable.identity.metadata.display_name == "app.exe",
                  "path basename supplies display name");
}

void CheckSystemAndUnresolvedIdentity(TestContext& context) {
    const auto idle = Resolve({.pid = 0U, .generation = 1U}, {});
    context.Check(idle.identity.kind == IdentityKind::System, "idle is system identity");
    context.Check(idle.identity.id == "system:idle", "idle identity is stable");

    const auto kernel = Resolve({.pid = 4U, .generation = 2U}, {.image_name_hint = "System"});
    context.Check(kernel.identity.kind == IdentityKind::System,
                  "verified PID 4 is system identity");
    context.Check(kernel.identity.id == "system:kernel", "kernel identity is stable");

    const auto unverified_kernel = Resolve({.pid = 4U, .generation = 3U}, {});
    context.Check(unverified_kernel.identity.kind == IdentityKind::Unresolved,
                  "PID 4 without System hint is unresolved");

    const auto first = Resolve({.pid = 42U, .generation = 8U}, {});
    const auto second = Resolve({.pid = 42U, .generation = 9U}, {});
    context.Check(first.identity.id == "unresolved:42:8", "unresolved identity has instance");
    context.Check(first.identity.id != second.identity.id,
                  "unresolved identity does not merge generations");
}

void CheckPathIdentitySeparation(TestContext& context) {
    const ProcessInstanceId first{.pid = 1U, .generation = 1U};
    const ProcessInstanceId second{.pid = 2U, .generation = 2U};
    const auto one = Resolve(first, {.image_path = "C:\\One\\same.exe"});
    const auto two = Resolve(second, {.image_path = "D:\\Two\\same.exe"});
    context.Check(one.identity.id != two.identity.id,
                  "same filename in different paths stays separate");

    const auto same_one = Resolve(first, {.image_path = "C:\\One\\same.exe"});
    const auto same_two = Resolve(second, {.image_path = "C:\\One\\same.exe"});
    context.Check(same_one.identity.id == same_two.identity.id,
                  "same executable path merges processes");

    const auto different_case = Resolve(second, {.image_path = "c:\\one\\same.exe"});
    context.Check(same_one.identity.id != different_case.identity.id,
                  "path case is preserved to avoid false merge");

    const auto first_packaged = Resolve(first, {.application_user_model_id = "Family!First"});
    const auto second_packaged = Resolve(second, {.application_user_model_id = "Family!Second"});
    context.Check(first_packaged.identity.id != second_packaged.identity.id,
                  "different AUMIDs stay separate");
}

void CheckPathNormalization(TestContext& context) {
    using nfm::process::NormalizeWindowsImagePath;

    context.Check(NormalizeWindowsImagePath(R"(\\?\C:\App\app.exe)") == R"(C:\App\app.exe)",
                  "extended DOS path normalized");
    context.Check(NormalizeWindowsImagePath(R"(\\?\UNC\server\share\app.exe)") ==
                      R"(\\server\share\app.exe)",
                  "extended UNC path normalized");
    context.Check(NormalizeWindowsImagePath("C:\\App\\") == "C:\\App",
                  "non-root trailing separator removed");
    context.Check(NormalizeWindowsImagePath("C:\\") == "C:\\", "drive root separator retained");
    context.Check(!NormalizeWindowsImagePath("relative\\app.exe").has_value(),
                  "relative path rejected");
    context.Check(!NormalizeWindowsImagePath("").has_value(), "empty path rejected");
    const std::string embedded_null{"C:\\App\0evil.exe", 15U};
    context.Check(!NormalizeWindowsImagePath(embedded_null).has_value(), "embedded NUL rejected");

    const auto invalid =
        Resolve({.pid = 8U, .generation = 8U}, {.image_path = "relative\\app.exe"});
    context.Check(invalid.error == IdentityResolutionError::InvalidImagePath,
                  "invalid supplied path is diagnostic");
}

} // namespace

int Run() {
    TestContext context;
    CheckIdentityPriority(context);
    CheckSystemAndUnresolvedIdentity(context);
    CheckPathIdentitySeparation(context);
    CheckPathNormalization(context);
    return context.failures();
}
} // namespace identity_resolver_suite

// === process_tracker_suite ===
namespace nfm::process {

struct ProcessTrackerTestAccess {
    static void SetNextGeneration(ProcessTracker& tracker, std::uint64_t generation) noexcept {
        tracker.next_generation_ = generation;
    }

    static void SetUnmatchedNetworkEvents(ProcessTracker& tracker, std::uint64_t value) noexcept {
        tracker.diagnostics_.unmatched_network_events = value;
    }

    [[nodiscard]] static std::size_t RetiredSize(const ProcessTracker& tracker) noexcept {
        return tracker.retired_.size();
    }
};

} // namespace nfm::process
namespace process_tracker_suite {
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

using nfm::process::ProcessEndObservation;
using nfm::process::ProcessInstanceId;
using nfm::process::ProcessLookupResult;
using nfm::process::ProcessStartKind;
using nfm::process::ProcessStartObservation;
using nfm::process::ProcessTracker;
using nfm::process::ProcessTrackerError;
using nfm::process::ProcessTrackerTestAccess;
using nfm::process::TraceDuration;
using nfm::process::TraceTimePoint;

TraceTimePoint At(std::chrono::milliseconds offset) {
    return TraceTimePoint{std::chrono::duration_cast<TraceDuration>(offset)};
}

void CheckApplicationId(TestContext& context, const ProcessLookupResult& result,
                        std::string_view expected, std::string_view name) {
    if (!result.process.has_value()) {
        context.Check(false, name);
        return;
    }
    context.Check(result.process.value().application.id == expected, name);
}

void CheckInstance(TestContext& context, const ProcessLookupResult& result,
                   ProcessInstanceId expected, std::string_view name) {
    if (!result.process.has_value()) {
        context.Check(false, name);
        return;
    }
    context.Check(result.process.value().instance == expected, name);
}

void CheckDisplayName(TestContext& context, const ProcessLookupResult& result,
                      std::string_view expected, std::string_view name) {
    if (!result.process.has_value()) {
        context.Check(false, name);
        return;
    }
    context.Check(result.process.value().application.metadata.display_name == expected, name);
}

ProcessStartObservation Start(std::uint32_t pid, std::chrono::milliseconds observed_at,
                              std::optional<std::uint64_t> kernel_key = std::nullopt,
                              std::string image_name = {},
                              ProcessStartKind kind = ProcessStartKind::Started) {
    return {.pid = pid,
            .observed_at = At(observed_at),
            .kernel_process_key = kernel_key,
            .image_name_hint = std::move(image_name),
            .kind = kind};
}

ProcessEndObservation End(std::uint32_t pid, std::chrono::milliseconds observed_at,
                          std::optional<std::uint64_t> kernel_key = std::nullopt) {
    return {.pid = pid, .observed_at = At(observed_at), .kernel_process_key = kernel_key};
}

void CheckBasicLifecycle(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto started = tracker.ObserveStart(Start(100U, 0ms, 1000U, "app.exe"));
    context.Check(started.accepted(), "basic start accepted");
    context.Check(started.instance == ProcessInstanceId{.pid = 100U, .generation = 1U},
                  "first generation assigned");
    context.Check(started.probe_request.has_value(), "ordinary process requests probe");

    const auto unresolved = tracker.ResolveAt(100U, At(0ms));
    context.Check(unresolved.found(), "active process resolves at start");
    CheckApplicationId(context, unresolved, "unresolved:100:1", "active process starts unresolved");

    context.Check(tracker.ApplyProbeData(started.instance, {.image_path = "C:\\Apps\\app.exe"}) ==
                      ProcessTrackerError::None,
                  "probe data applied");
    const auto resolved = tracker.ResolveAt(100U, At(5ms));
    CheckApplicationId(context, resolved, "executable:C:\\Apps\\app.exe",
                       "resolved application returned");

    context.Check(tracker.ObserveEnd(End(100U, 10ms, 1000U)) == ProcessTrackerError::None,
                  "matching end accepted");
    context.Check(tracker.ResolveAt(100U, At(9ms)).found(),
                  "retired process resolves late pre-end event");
    context.Check(!tracker.ResolveAt(100U, At(10ms)).found(),
                  "half-open end boundary does not resolve old process");
}

void CheckDuplicateStartAndStaleEnd(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto first = tracker.ObserveStart(Start(200U, 0ms, 2000U));
    const auto duplicate = tracker.ObserveStart(Start(200U, 1ms, 2000U, "dup.exe"));
    context.Check(duplicate.accepted(), "duplicate start accepted as observation");
    context.Check(duplicate.instance == first.instance, "duplicate start keeps generation");
    context.Check(!duplicate.probe_request.has_value(), "duplicate start avoids second probe");
    CheckDisplayName(context, tracker.ResolveAt(200U, At(1ms)), "dup.exe",
                     "duplicate start enriches empty display name");

    context.Check(tracker.ObserveEnd(End(200U, 2ms, 9999U)) == ProcessTrackerError::StaleEnd,
                  "mismatched kernel key is stale end");
    context.Check(tracker.ResolveAt(200U, At(3ms)).found(),
                  "stale end does not close active process");
    context.Check(tracker.diagnostics().stale_end_events == 1U, "stale end diagnostic increments");

    const auto backwards = tracker.ObserveStart(Start(200U, -1ms, 2000U));
    context.Check(backwards.error == ProcessTrackerError::NonIncreasingStartTime,
                  "backward duplicate start rejected");
    CheckInstance(context, tracker.ResolveAt(200U, At(3ms)), first.instance,
                  "rejected start preserves active instance");
}

void CheckPidReuseBoundaries(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto old_process = tracker.ObserveStart(Start(300U, 0ms, 3000U, "old.exe"));
    context.Check(
        tracker.ApplyProbeData(old_process.instance, {.image_path = "C:\\Old\\same.exe"}) ==
            ProcessTrackerError::None,
        "old identity applied");

    const auto new_process = tracker.ObserveStart(Start(300U, 10ms, 3001U, "new.exe"));
    context.Check(new_process.accepted(), "PID reuse start accepted");
    context.Check(new_process.instance.generation == 2U, "PID reuse gets new generation");
    context.Check(tracker.diagnostics().implicit_pid_reuses == 1U, "implicit PID reuse diagnosed");

    const auto before = tracker.ResolveAt(300U, At(9ms));
    const auto boundary = tracker.ResolveAt(300U, At(10ms));
    CheckInstance(context, before, old_process.instance,
                  "event before reuse resolves old instance");
    CheckInstance(context, boundary, new_process.instance,
                  "event at reuse boundary resolves new instance");

    context.Check(
        tracker.ApplyProbeData(old_process.instance, {.image_path = "C:\\Old\\late.exe"}) ==
            ProcessTrackerError::None,
        "late probe can enrich retained old instance");
    CheckApplicationId(context, tracker.ResolveAt(300U, At(9ms)), "executable:C:\\Old\\late.exe",
                       "late old probe updates only old generation");
    CheckApplicationId(context, tracker.ResolveAt(300U, At(10ms)), "unresolved:300:2",
                       "late old probe never overwrites reused PID");

    const auto no_key_reuse = tracker.ObserveStart(Start(300U, 20ms));
    context.Check(no_key_reuse.instance.generation == 3U,
                  "later start without comparable key is new generation");
}

void CheckExplicitPidReuse(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto old_process = tracker.ObserveStart(Start(350U, 0ms, 3500U));
    context.Check(tracker.ObserveEnd(End(350U, 10ms, 3500U)) == ProcessTrackerError::None,
                  "explicit reuse old process ends");

    const auto new_process = tracker.ObserveStart(Start(350U, 20ms, 3501U));
    context.Check(new_process.accepted(), "explicit reuse new process starts");
    context.Check(new_process.instance.generation == 2U, "explicit reuse gets a new generation");
    CheckInstance(context, tracker.ResolveAt(350U, At(9ms)), old_process.instance,
                  "explicit reuse retains old interval");
    context.Check(!tracker.ResolveAt(350U, At(10ms)).found(),
                  "explicit reuse gap does not guess an instance");
    CheckInstance(context, tracker.ResolveAt(350U, At(20ms)), new_process.instance,
                  "explicit reuse resolves new interval");
    context.Check(tracker.diagnostics().implicit_pid_reuses == 0U,
                  "explicit reuse is not diagnosed as implicit reuse");
}

void CheckRetiredExpiryAndStaleProbe(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto started = tracker.ObserveStart(Start(400U, 0ms, 4000U));
    context.Check(tracker.ObserveEnd(End(400U, 1ms, 4000U)) == ProcessTrackerError::None,
                  "expiry process ended");
    context.Check(tracker.AdvanceWatermark(At(30'000ms)) == ProcessTrackerError::None,
                  "pre-expiry watermark accepted");
    context.Check(tracker.ResolveAt(400U, At(0ms)).found(),
                  "retired process retained before deadline");
    context.Check(tracker.AdvanceWatermark(At(30'001ms)) == ProcessTrackerError::None,
                  "expiry watermark accepted");
    context.Check(!tracker.ResolveAt(400U, At(0ms)).found(), "retired process removed at deadline");
    context.Check(tracker.diagnostics().expired_retired_processes == 1U,
                  "expiry diagnostic increments");
    context.Check(tracker.ApplyProbeData(started.instance, {.image_path = "C:\\Late\\app.exe"}) ==
                      ProcessTrackerError::StaleProbeResult,
                  "probe after expiry is stale");
    context.Check(tracker.diagnostics().stale_probe_results == 1U,
                  "stale probe diagnostic increments");
    context.Check(tracker.AdvanceWatermark(At(30'000ms)) ==
                      ProcessTrackerError::WatermarkMovedBackward,
                  "watermark rollback rejected");
}

void CheckSystemProbeSuppressionAndInvalidData(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    const auto idle = tracker.ObserveStart(Start(0U, 0ms));
    context.Check(idle.accepted() && !idle.probe_request.has_value(), "idle identity skips probe");
    CheckApplicationId(context, tracker.ResolveAt(0U, At(0ms)), "system:idle",
                       "idle identity resolved");

    const auto kernel = tracker.ObserveStart(Start(4U, 0ms, 4U, "System"));
    context.Check(kernel.accepted() && !kernel.probe_request.has_value(),
                  "verified kernel identity skips probe");
    CheckApplicationId(context, tracker.ResolveAt(4U, At(0ms)), "system:kernel",
                       "kernel identity resolved");

    const auto ordinary = tracker.ObserveStart(Start(500U, 0ms));
    context.Check(tracker.ApplyProbeData(ordinary.instance, {.image_path = "relative\\app.exe"}) ==
                      ProcessTrackerError::InvalidProbeData,
                  "invalid probe path rejected");
    CheckApplicationId(context, tracker.ResolveAt(500U, At(1ms)), "unresolved:500:3",
                       "invalid probe preserves unresolved identity");
}

void CheckCapacityGenerationAndSaturation(TestContext& context) {
    using namespace std::chrono_literals;
    ProcessTracker tracker;
    constexpr std::uint32_t first_pid = 10'000U;
    for (std::size_t index = 0; index <= nfm::process::kMaximumRetiredProcesses; ++index) {
        const auto time = std::chrono::milliseconds{static_cast<std::int64_t>(index)};
        const auto started =
            tracker.ObserveStart(Start(first_pid + static_cast<std::uint32_t>(index), time));
        context.Check(started.accepted(), "capacity start accepted");
        context.Check(tracker.ObserveEnd(End(first_pid + static_cast<std::uint32_t>(index),
                                             time + 1ms)) == ProcessTrackerError::None,
                      "capacity end accepted");
    }
    context.Check(ProcessTrackerTestAccess::RetiredSize(tracker) ==
                      nfm::process::kMaximumRetiredProcesses,
                  "retired cache remains bounded");
    context.Check(tracker.diagnostics().capacity_evictions == 1U, "capacity eviction diagnosed");
    context.Check(!tracker.ResolveAt(first_pid, At(0ms)).found(),
                  "oldest retired instance evicted first");

    ProcessTracker exhausted;
    ProcessTrackerTestAccess::SetNextGeneration(exhausted,
                                                std::numeric_limits<std::uint64_t>::max());
    const auto last = exhausted.ObserveStart(Start(600U, 0ms));
    context.Check(last.accepted() &&
                      last.instance.generation == std::numeric_limits<std::uint64_t>::max(),
                  "maximum generation allocated once");
    const auto overflow = exhausted.ObserveStart(Start(601U, 1ms));
    context.Check(overflow.error == ProcessTrackerError::GenerationExhausted,
                  "generation never wraps to zero");

    ProcessTracker saturation;
    ProcessTrackerTestAccess::SetUnmatchedNetworkEvents(saturation,
                                                        std::numeric_limits<std::uint64_t>::max());
    context.Check(!saturation.ResolveAt(999U, At(0ms)).found(),
                  "unmatched lookup remains diagnostic at saturation");
    context.Check(saturation.diagnostics().unmatched_network_events ==
                      std::numeric_limits<std::uint64_t>::max(),
                  "diagnostic counter saturates");
}

} // namespace

int Run() {
    TestContext context;
    CheckBasicLifecycle(context);
    CheckDuplicateStartAndStaleEnd(context);
    CheckPidReuseBoundaries(context);
    CheckExplicitPidReuse(context);
    CheckRetiredExpiryAndStaleProbe(context);
    CheckSystemProbeSuppressionAndInvalidData(context);
    CheckCapacityGenerationAndSaturation(context);
    return context.failures();
}
} // namespace process_tracker_suite

// === windows_process_probe_suite ===
namespace windows_process_probe_suite {
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

int Run(int argc, char** argv) {
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
} // namespace windows_process_probe_suite

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--probe-child") {
        return 0;
    }
    int failures = 0;
    failures += (identity_resolver_suite::Run() != 0 ? 1 : 0);
    failures += (process_tracker_suite::Run() != 0 ? 1 : 0);
    failures += (windows_process_probe_suite::Run(argc, argv) != 0 ? 1 : 0);
    return failures;
}
