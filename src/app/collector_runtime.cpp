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
    KernelHandle(KernelHandle&& other) noexcept : handle_{other.handle_} {
        other.handle_ = nullptr;
    }
    KernelHandle& operator=(KernelHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
                static_cast<void>(::CloseHandle(handle_));
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

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

CollectorPrincipalResult BuildPortablePrincipal() noexcept {
    try {
        HANDLE raw_token{};
        if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
            return {.source_error = ::GetLastError()};
        }
        const KernelHandle token{raw_token};
        auto user_sid = QueryTokenSid(token.get(), TokenUser);
        auto logon_sid = QueryCurrentLogonSid(token.get());
        if (user_sid.empty() && logon_sid.empty()) {
            return {.source_error = ERROR_INVALID_SID};
        }
        if (user_sid.empty()) {
            user_sid = logon_sid;
        }
        if (logon_sid.empty()) {
            logon_sid = user_sid;
        }
        return {.principal = {.mode = ipc::CollectorEndpointMode::Portable,
                              .authorized_sid = std::move(user_sid),
                              .server_logon_sid = std::move(logon_sid)}};
    } catch (...) {
        return {.source_error = ERROR_NOT_ENOUGH_MEMORY};
    }
}

collector::WindowsEtwSessionOptions SessionOptions() noexcept {
    return {.existing_session_policy = collector::ExistingSessionPolicy::StopAndRetry};
}

} // namespace

CollectorPrincipalResult BuildCollectorPrincipal() noexcept {
    return BuildPortablePrincipal();
}

std::string CreateCollectorInstanceId() {
    LARGE_INTEGER performance_counter{};
    ::QueryPerformanceCounter(&performance_counter);
    FILETIME system_time{};
    ::GetSystemTimeAsFileTime(&system_time);
    const auto part1 = static_cast<std::uint32_t>(performance_counter.QuadPart);
    const auto part2 = static_cast<std::uint32_t>(system_time.dwLowDateTime);
    std::array<char, 32> buffer{};
    static_cast<void>(std::snprintf(buffer.data(), buffer.size(), "cinst_%08x%08x", part1, part2));
    return std::string{buffer.data()};
}

CollectorRuntime::CollectorRuntime()
    : instance_{CreateCollectorInstanceId()}, principal_{BuildCollectorPrincipal().principal},
      etw_api_{}, session_{etw_api_, SessionOptions()}, provider_{session_},
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

collector::CollectorHostResult CollectorRuntime::RunPortable(std::wstring_view stop_event_name,
                                                             std::uint32_t parent_pid) noexcept {
    std::uint32_t event_error{};
    auto stop_event = OpenStopEvent(stop_event_name, event_error);
    if (!stop_event_name.empty() && stop_event.get() == nullptr) {
        return {.error = collector::CollectorHostError::StopSignalFailed,
                .source_error = event_error};
    }

    KernelHandle parent_handle{};
    if (parent_pid != 0U) {
        parent_handle = KernelHandle{::OpenProcess(SYNCHRONIZE, FALSE, parent_pid)};
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
        if (stop_event.get() != nullptr || parent_handle.get() != nullptr) {
            event_waiter =
                std::jthread([event = stop_event.get(), parent = parent_handle.get(),
                              &stop_source](const std::stop_token& waiter_stop) noexcept {
                    std::array<HANDLE, 2> handles{};
                    DWORD handle_count = 0;
                    if (event != nullptr) {
                        handles[handle_count++] = event;
                    }
                    if (parent != nullptr) {
                        handles[handle_count++] = parent;
                    }

                    while (!waiter_stop.stop_requested()) {
                        const auto result =
                            ::WaitForMultipleObjects(handle_count, handles.data(), FALSE, 50U);
                        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handle_count) {
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
