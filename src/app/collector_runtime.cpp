#include "network_flow_monitor/app/collector_runtime.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace nfm::app {
namespace {

std::atomic<std::stop_source*>& PortableStopSource() noexcept {
    static std::atomic<std::stop_source*> source{};
    return source;
}

BOOL WINAPI PortableControlHandler(DWORD control) noexcept {
    switch (control) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT: {
        auto* const source = PortableStopSource().load(std::memory_order_acquire);
        if (source != nullptr) {
            source->request_stop();
        }
        return TRUE;
    }
    default:
        return FALSE;
    }
}

class KernelHandle final {
  public:
    explicit KernelHandle(HANDLE handle = nullptr) noexcept : handle_{handle} {}
    ~KernelHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(handle_));
        }
    }
    KernelHandle(const KernelHandle&) = delete;
    KernelHandle& operator=(const KernelHandle&) = delete;
    KernelHandle(KernelHandle&&) = delete;
    KernelHandle& operator=(KernelHandle&&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

  private:
    HANDLE handle_{};
};

[[nodiscard]] KernelHandle OpenStopEvent(std::wstring_view name, std::uint32_t& error) noexcept {
    try {
        const std::wstring owned_name{name};
        const auto event = ::OpenEventW(SYNCHRONIZE, FALSE, owned_name.c_str());
        if (event == nullptr) {
            error = ::GetLastError();
        } else {
            error = ERROR_SUCCESS;
        }
        return KernelHandle{event};
    } catch (...) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        return KernelHandle{nullptr};
    }
}

std::vector<std::byte> CopySid(PSID sid) {
    if (sid == nullptr || ::IsValidSid(sid) == FALSE) {
        return {};
    }
    const auto length = ::GetLengthSid(sid);
    std::vector<std::byte> result(length);
    if (::CopySid(length, result.data(), sid) == FALSE) {
        return {};
    }
    return result;
}

std::vector<std::byte> QueryTokenSid(HANDLE token, TOKEN_INFORMATION_CLASS information_class) {
    DWORD required{};
    static_cast<void>(::GetTokenInformation(token, information_class, nullptr, 0U, &required));
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0U) {
        return {};
    }

    std::vector<std::byte> storage(required);
    if (::GetTokenInformation(token, information_class, storage.data(), required, &required) ==
        FALSE) {
        return {};
    }

    if (information_class == TokenUser) {
        if (storage.size() < sizeof(TOKEN_USER)) {
            return {};
        }
        const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
        return CopySid(user->User.Sid);
    }
    return {};
}

std::vector<std::byte> QueryCurrentLogonSid(HANDLE token) {
    DWORD required{};
    static_cast<void>(::GetTokenInformation(token, TokenGroups, nullptr, 0U, &required));
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(TOKEN_GROUPS)) {
        return {};
    }

    std::vector<std::byte> storage(required);
    if (::GetTokenInformation(token, TokenGroups, storage.data(), required, &required) == FALSE) {
        return {};
    }
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    constexpr auto groups_offset = offsetof(TOKEN_GROUPS, Groups);
    const auto group_count = static_cast<std::size_t>(groups->GroupCount);
    if (group_count > (storage.size() - groups_offset) / sizeof(SID_AND_ATTRIBUTES)) {
        return {};
    }
    const std::span<const SID_AND_ATTRIBUTES> group_view{groups->Groups, group_count};
    for (const auto& group : group_view) {
        if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID) {
            continue;
        }
        return CopySid(group.Sid);
    }
    return {};
}

std::vector<std::byte> WellKnownSid(WELL_KNOWN_SID_TYPE type) {
    std::array<std::byte, SECURITY_MAX_SID_SIZE> storage{};
    auto size = static_cast<DWORD>(storage.size());
    if (::CreateWellKnownSid(type, nullptr, storage.data(), &size) == FALSE) {
        return {};
    }
    return {storage.begin(), storage.begin() + size};
}

CollectorPrincipalResult BuildPortablePrincipal() noexcept {
    try {
        HANDLE raw_token{};
        if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
            return {.source_error = ::GetLastError()};
        }
        const KernelHandle token{raw_token};
        auto logon_sid = QueryCurrentLogonSid(token.get());
        if (logon_sid.empty()) {
            return {.source_error = ERROR_INVALID_SID};
        }
        return {.principal = {.mode = ipc::CollectorEndpointMode::Portable,
                              .authorized_sid = logon_sid,
                              .server_logon_sid = std::move(logon_sid)}};
    } catch (...) {
        return {.source_error = ERROR_NOT_ENOUGH_MEMORY};
    }
}

CollectorPrincipalResult BuildServicePrincipal() noexcept {
    try {
        auto users_sid = WellKnownSid(WinBuiltinUsersSid);
        if (users_sid.empty()) {
            return {.source_error = ERROR_INVALID_SID};
        }
        return {.principal = {.mode = ipc::CollectorEndpointMode::Service,
                              .authorized_sid = std::move(users_sid)}};
    } catch (...) {
        return {.source_error = ERROR_NOT_ENOUGH_MEMORY};
    }
}

collector::WindowsEtwSessionOptions SessionOptionsFor(ipc::CollectorEndpointMode mode) noexcept {
    // A portable collector recovers a session left behind by a prior forced kill (see ADR-0014).
    // The service keeps the strict policy until portable/service coexistence is decided.
    return {.existing_session_policy = mode == ipc::CollectorEndpointMode::Portable
                                           ? collector::ExistingSessionPolicy::StopAndRetry
                                           : collector::ExistingSessionPolicy::Fail};
}

} // namespace

CollectorPrincipalResult BuildCollectorPrincipal(ipc::CollectorEndpointMode mode) noexcept {
    switch (mode) {
    case ipc::CollectorEndpointMode::Portable:
        return BuildPortablePrincipal();
    case ipc::CollectorEndpointMode::Service:
        return BuildServicePrincipal();
    case ipc::CollectorEndpointMode::Invalid:
        return {.source_error = ERROR_INVALID_PARAMETER};
    }
    return {.source_error = ERROR_INVALID_PARAMETER};
}

ipc::CollectorInstanceId CreateCollectorInstanceId() noexcept {
    ipc::CollectorInstanceId instance;
    LARGE_INTEGER performance_counter{};
    ::QueryPerformanceCounter(&performance_counter);
    FILETIME system_time{};
    ::GetSystemTimeAsFileTime(&system_time);
    const std::array<std::uint64_t, 2U> values{
        static_cast<std::uint64_t>(performance_counter.QuadPart) ^
            (static_cast<std::uint64_t>(::GetCurrentProcessId()) << 32U),
        (static_cast<std::uint64_t>(system_time.dwHighDateTime) << 32U) |
            static_cast<std::uint64_t>(system_time.dwLowDateTime)};
    static_assert(sizeof(values) == sizeof(instance.bytes));
    std::memcpy(instance.bytes.data(), values.data(), sizeof(values));
    return instance;
}

CollectorRuntime::CollectorRuntime(ipc::CollectorEndpointMode mode)
    : instance_{CreateCollectorInstanceId()}, principal_{BuildCollectorPrincipal(mode).principal},
      etw_api_{}, session_{etw_api_, SessionOptionsFor(mode)}, provider_{session_},
      started_at_{std::chrono::steady_clock::now()}, server_{instance_, principal_, transport_},
      publisher_{instance_, started_at_, provider_, server_}, wait_{},
      host_{session_, server_, publisher_, wait_} {}

collector::CollectorHostResult CollectorRuntime::Start() noexcept {
    return host_.Start();
}

collector::CollectorHostResult CollectorRuntime::Run(const std::stop_token& stop_token) noexcept {
    return host_.Run(stop_token);
}

collector::CollectorHostResult CollectorRuntime::Stop() noexcept {
    return host_.Stop();
}

collector::CollectorHostResult CollectorRuntime::RunPortable(
    std::wstring_view stop_event_name) noexcept {
    std::uint32_t event_error{};
    auto stop_event = OpenStopEvent(stop_event_name, event_error);
    if (!stop_event_name.empty() && stop_event.get() == nullptr) {
        return {.error = collector::CollectorHostError::StopSignalFailed,
                .source_error = event_error};
    }

    const auto started = Start();
    if (!started.ok()) {
        return started;
    }

    std::stop_source stop_source; // NOLINT(misc-const-correctness): Win32 handler requests stop.
    PortableStopSource().store(&stop_source, std::memory_order_release);
    const bool handler_registered = ::SetConsoleCtrlHandler(&PortableControlHandler, TRUE) != FALSE;
    try {
        std::jthread event_waiter;
        if (stop_event.get() != nullptr) {
            event_waiter = std::jthread([event = stop_event.get(), &stop_source](
                                            const std::stop_token& waiter_stop) noexcept {
                while (!waiter_stop.stop_requested()) {
                    const auto result = ::WaitForSingleObject(event, 50U);
                    if (result == WAIT_OBJECT_0) {
                        stop_source.request_stop();
                        break;
                    }
                    if (result == WAIT_FAILED) {
                        break;
                    }
                }
            });
        }
        const auto run_result = Run(stop_source.get_token());
        event_waiter.request_stop();
        return [&]() noexcept {
            if (handler_registered) {
                static_cast<void>(::SetConsoleCtrlHandler(&PortableControlHandler, FALSE));
            }
            PortableStopSource().store(nullptr, std::memory_order_release);
            const auto stop_result = Stop();
            return run_result.ok() ? stop_result : run_result;
        }();
    } catch (...) {
        if (handler_registered) {
            static_cast<void>(::SetConsoleCtrlHandler(&PortableControlHandler, FALSE));
        }
        PortableStopSource().store(nullptr, std::memory_order_release);
        static_cast<void>(Stop());
        return {.error = collector::CollectorHostError::StopSignalFailed,
                .source_error = ERROR_NOT_ENOUGH_MEMORY};
    }
}

} // namespace nfm::app
