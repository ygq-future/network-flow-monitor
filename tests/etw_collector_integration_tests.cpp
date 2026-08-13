#include "network_flow_monitor/collector/windows_etw_session.h"
#include "network_flow_monitor/process/identity_resolver.h"
#include "network_flow_monitor/process/windows_process_probe.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <winsafer.h>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace {

using namespace std::chrono_literals;

constexpr std::size_t kPayloadSize = 4'096U;
constexpr auto kObservationTimeout = 8s;

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

class WinsockRuntime {
  public:
    WinsockRuntime() noexcept : error_{Start()}, started_{error_ == 0} {}

    ~WinsockRuntime() {
        if (started_) {
            static_cast<void>(WSACleanup());
        }
    }

    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;
    WinsockRuntime(WinsockRuntime&&) = delete;
    WinsockRuntime& operator=(WinsockRuntime&&) = delete;

    [[nodiscard]] bool ok() const noexcept {
        return started_;
    }

    [[nodiscard]] int error() const noexcept {
        return error_;
    }

  private:
    static int Start() noexcept {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data);
    }

    int error_{};
    bool started_{};
};

class SocketHandle {
  public:
    explicit SocketHandle(SOCKET socket = INVALID_SOCKET) noexcept : socket_{socket} {}

    ~SocketHandle() {
        Reset();
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : socket_{other.Release()} {}

    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] SOCKET get() const noexcept {
        return socket_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return socket_ != std::numeric_limits<SOCKET>::max();
    }

  private:
    [[nodiscard]] SOCKET Release() noexcept {
        const SOCKET released = socket_;
        socket_ = INVALID_SOCKET;
        return released;
    }

    void Reset(SOCKET replacement = INVALID_SOCKET) noexcept {
        if (valid()) {
            static_cast<void>(closesocket(socket_));
        }
        socket_ = replacement;
    }

    SOCKET socket_{INVALID_SOCKET};
};

class Win32Handle {
  public:
    explicit Win32Handle(HANDLE handle = nullptr) noexcept : handle_{handle} {}

    ~Win32Handle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
        }
    }

    Win32Handle(const Win32Handle&) = delete;
    Win32Handle& operator=(const Win32Handle&) = delete;
    Win32Handle(Win32Handle&&) = delete;
    Win32Handle& operator=(Win32Handle&&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

  private:
    HANDLE handle_{};
};

class SaferLevelHandle {
  public:
    explicit SaferLevelHandle(SAFER_LEVEL_HANDLE handle = nullptr) noexcept : handle_{handle} {}

    ~SaferLevelHandle() {
        if (handle_ != nullptr) {
            static_cast<void>(SaferCloseLevel(handle_));
        }
    }

    SaferLevelHandle(const SaferLevelHandle&) = delete;
    SaferLevelHandle& operator=(const SaferLevelHandle&) = delete;
    SaferLevelHandle(SaferLevelHandle&&) = delete;
    SaferLevelHandle& operator=(SaferLevelHandle&&) = delete;

    [[nodiscard]] SAFER_LEVEL_HANDLE get() const noexcept {
        return handle_;
    }

  private:
    SAFER_LEVEL_HANDLE handle_{};
};

struct TrafficResult {
    bool success{};
    int socket_error{};
    std::uint64_t bytes_each_direction{};
};

struct ExpectedTraffic {
    std::uint64_t tcp_bytes_each_direction{};
    std::uint64_t udp_bytes_each_direction{};
    std::uint64_t ipv4_bytes_each_direction{};
    std::uint64_t ipv6_bytes_each_direction{};

    [[nodiscard]] std::uint64_t total_each_direction() const noexcept {
        return tcp_bytes_each_direction + udp_bytes_each_direction;
    }
};

enum class IpFamily : std::uint8_t {
    V4 = AF_INET,
    V6 = AF_INET6,
};

void ConfigureTimeouts(SOCKET socket) noexcept {
    constexpr DWORD timeout_ms = 5'000U;
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                                 reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)));
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                                 reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)));
}

bool BindLoopback(SOCKET socket, IpFamily family) noexcept {
    if (family == IpFamily::V4) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    }
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    return bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
}

bool SendAll(SOCKET socket, std::span<const char> payload) noexcept {
    std::size_t sent_total = 0U;
    while (sent_total < payload.size()) {
        const auto remaining = payload.size() - sent_total;
        const int request = static_cast<int>(
            std::min(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int sent = send(socket, payload.data() + sent_total, request, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

bool ReceiveAll(SOCKET socket, std::span<char> payload) noexcept {
    std::size_t received_total = 0U;
    while (received_total < payload.size()) {
        const auto remaining = payload.size() - received_total;
        const int request = static_cast<int>(
            std::min(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int received = recv(socket, payload.data() + received_total, request, 0);
        if (received == SOCKET_ERROR || received == 0) {
            return false;
        }
        received_total += static_cast<std::size_t>(received);
    }
    return true;
}

TrafficResult RunTcpLoopback(IpFamily ip_family, std::size_t repetitions) noexcept {
    const int family = static_cast<int>(ip_family);
    const SocketHandle listener{socket(family, SOCK_STREAM, IPPROTO_TCP)};
    if (!listener.valid()) {
        return {.socket_error = WSAGetLastError()};
    }
    ConfigureTimeouts(listener.get());
    if (!BindLoopback(listener.get(), ip_family) || listen(listener.get(), 1) != 0) {
        return {.socket_error = WSAGetLastError()};
    }

    sockaddr_storage endpoint{};
    int endpoint_length = sizeof(endpoint);
    if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&endpoint), &endpoint_length) !=
        0) {
        return {.socket_error = WSAGetLastError()};
    }

    const SocketHandle client{socket(family, SOCK_STREAM, IPPROTO_TCP)};
    if (!client.valid()) {
        return {.socket_error = WSAGetLastError()};
    }
    ConfigureTimeouts(client.get());
    if (connect(client.get(), reinterpret_cast<const sockaddr*>(&endpoint), endpoint_length) != 0) {
        return {.socket_error = WSAGetLastError()};
    }

    const SocketHandle server{accept(listener.get(), nullptr, nullptr)};
    if (!server.valid()) {
        return {.socket_error = WSAGetLastError()};
    }
    ConfigureTimeouts(server.get());

    std::array<char, kPayloadSize> payload{};
    std::array<char, kPayloadSize> received{};
    payload.fill('\x5a');
    for (std::size_t index = 0U; index < repetitions; ++index) {
        if (!SendAll(client.get(), payload) || !ReceiveAll(server.get(), received) ||
            !SendAll(server.get(), payload) || !ReceiveAll(client.get(), received)) {
            return {.socket_error = WSAGetLastError()};
        }
    }
    return {.success = true,
            .bytes_each_direction = static_cast<std::uint64_t>(kPayloadSize) * repetitions * 2U};
}

TrafficResult RunUdpLoopback(IpFamily ip_family, std::size_t repetitions) noexcept {
    const int family = static_cast<int>(ip_family);
    const SocketHandle server{socket(family, SOCK_DGRAM, IPPROTO_UDP)};
    if (!server.valid()) {
        return {.socket_error = WSAGetLastError()};
    }
    ConfigureTimeouts(server.get());
    if (!BindLoopback(server.get(), ip_family)) {
        return {.socket_error = WSAGetLastError()};
    }

    sockaddr_storage endpoint{};
    int endpoint_length = sizeof(endpoint);
    if (getsockname(server.get(), reinterpret_cast<sockaddr*>(&endpoint), &endpoint_length) != 0) {
        return {.socket_error = WSAGetLastError()};
    }

    const SocketHandle client{socket(family, SOCK_DGRAM, IPPROTO_UDP)};
    if (!client.valid()) {
        return {.socket_error = WSAGetLastError()};
    }
    ConfigureTimeouts(client.get());
    if (connect(client.get(), reinterpret_cast<const sockaddr*>(&endpoint), endpoint_length) != 0) {
        return {.socket_error = WSAGetLastError()};
    }

    std::array<char, kPayloadSize> payload{};
    std::array<char, kPayloadSize> received{};
    payload.fill('\x6b');
    sockaddr_storage peer{};
    for (std::size_t index = 0U; index < repetitions; ++index) {
        if (send(client.get(), payload.data(), static_cast<int>(payload.size()), 0) !=
            static_cast<int>(payload.size())) {
            return {.socket_error = WSAGetLastError()};
        }
        int peer_length = sizeof(peer);
        if (recvfrom(server.get(), received.data(), static_cast<int>(received.size()), 0,
                     reinterpret_cast<sockaddr*>(&peer),
                     &peer_length) != static_cast<int>(received.size())) {
            return {.socket_error = WSAGetLastError()};
        }
        if (sendto(server.get(), payload.data(), static_cast<int>(payload.size()), 0,
                   reinterpret_cast<const sockaddr*>(&peer),
                   peer_length) != static_cast<int>(payload.size())) {
            return {.socket_error = WSAGetLastError()};
        }
        if (recv(client.get(), received.data(), static_cast<int>(received.size()), 0) !=
            static_cast<int>(received.size())) {
            return {.socket_error = WSAGetLastError()};
        }
    }
    return {.success = true,
            .bytes_each_direction = static_cast<std::uint64_t>(kPayloadSize) * repetitions * 2U};
}

std::optional<ExpectedTraffic> RunTrafficMatrix(std::size_t repetitions) noexcept {
    const auto tcp4 = RunTcpLoopback(IpFamily::V4, repetitions);
    const auto tcp6 = RunTcpLoopback(IpFamily::V6, repetitions);
    const auto udp4 = RunUdpLoopback(IpFamily::V4, repetitions);
    const auto udp6 = RunUdpLoopback(IpFamily::V6, repetitions);
    if (!tcp4.success || !tcp6.success || !udp4.success || !udp6.success) {
        std::cerr << "Loopback traffic failed; WSA errors: " << tcp4.socket_error << ", "
                  << tcp6.socket_error << ", " << udp4.socket_error << ", " << udp6.socket_error
                  << '\n';
        return std::nullopt;
    }
    return ExpectedTraffic{
        .tcp_bytes_each_direction = tcp4.bytes_each_direction + tcp6.bytes_each_direction,
        .udp_bytes_each_direction = udp4.bytes_each_direction + udp6.bytes_each_direction,
        .ipv4_bytes_each_direction = tcp4.bytes_each_direction + udp4.bytes_each_direction,
        .ipv6_bytes_each_direction = tcp6.bytes_each_direction + udp6.bytes_each_direction,
    };
}

const nfm::ranking::ApplicationCounters* FindApplication(
    const nfm::ranking::CounterSnapshot& snapshot, std::string_view application_id) noexcept {
    for (const auto& counters : snapshot.applications) {
        if (counters.id == application_id) {
            return &counters;
        }
    }
    return nullptr;
}

struct SnapshotTransition final {
    const nfm::ranking::CounterSnapshot& before;
    const nfm::ranking::CounterSnapshot& after;
};

bool HasApplicationDelta(const SnapshotTransition transition,
                         std::uint64_t minimum_each_direction) noexcept {
    return std::ranges::any_of(transition.after.applications, [&](const auto& current) {
        const auto* previous = FindApplication(transition.before, current.id);
        const std::uint64_t previous_upload = previous != nullptr ? previous->upload_bytes : 0U;
        const std::uint64_t previous_download = previous != nullptr ? previous->download_bytes : 0U;
        return current.upload_bytes >= previous_upload &&
               current.download_bytes >= previous_download &&
               current.upload_bytes - previous_upload >= minimum_each_direction &&
               current.download_bytes - previous_download >= minimum_each_direction;
    });
}

std::optional<std::string> CurrentApplicationId() noexcept {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const auto observed_at = nfm::process::FileTimeToTraceTimePoint(now);
    if (!observed_at.has_value()) {
        return std::nullopt;
    }
    const nfm::process::ProcessProbeRequest request{
        .instance = {.pid = GetCurrentProcessId(), .generation = 1U},
        .observed_at = observed_at.value(),
    };
    const auto probed = nfm::process::ProbeWindowsProcess(request);
    if (!probed.ok()) {
        return std::nullopt;
    }
    const auto resolved = nfm::process::ResolveApplicationIdentity(request.instance, probed.data);
    if (!resolved.ok()) {
        return std::nullopt;
    }
    return resolved.identity.id;
}

template <typename Predicate> bool WaitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + kObservationTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

std::optional<std::uint64_t> ApplicationBytes(nfm::collector::WindowsEtwSession& session,
                                              std::string_view application_id,
                                              nfm::ranking::TrafficDirection direction) {
    const auto snapshot = session.MakeSnapshot(std::chrono::steady_clock::now());
    const auto* counters = FindApplication(snapshot, application_id);
    if (counters == nullptr) {
        return std::nullopt;
    }
    return direction == nfm::ranking::TrafficDirection::Upload ? counters->upload_bytes
                                                               : counters->download_bytes;
}

std::optional<std::wstring> CurrentExecutablePath() {
    std::vector<wchar_t> module_path(32'768U);
    const DWORD length =
        GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0U || length >= module_path.size()) {
        std::cerr << "GetModuleFileNameW failed: " << GetLastError() << '\n';
        return std::nullopt;
    }
    return std::wstring{module_path.data(), length};
}

bool StartChildTrafficProcess() {
    const auto module_path = CurrentExecutablePath();
    if (!module_path.has_value()) {
        return false;
    }
    std::wstring command = L"\"" + *module_path + L"\" --child";
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &startup, &process) == FALSE) {
        std::cerr << "CreateProcessW failed: " << GetLastError() << '\n';
        return false;
    }
    const Win32Handle process_handle{process.hProcess};
    const Win32Handle thread_handle{process.hThread};
    if (WaitForSingleObject(process_handle.get(), 15'000U) != WAIT_OBJECT_0) {
        std::cerr << "Child traffic process timed out\n";
        return false;
    }
    DWORD exit_code = 1U;
    if (GetExitCodeProcess(process_handle.get(), &exit_code) == FALSE || exit_code != 0U) {
        std::cerr << "Child traffic process failed: " << exit_code << '\n';
        return false;
    }
    return true;
}

bool RunDeElevatedAccessDeniedProcess() {
    const auto module_path = CurrentExecutablePath();
    if (!module_path.has_value()) {
        return false;
    }

    SAFER_LEVEL_HANDLE safer_level_raw{};
    if (SaferCreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_NORMALUSER, SAFER_LEVEL_OPEN,
                         &safer_level_raw, nullptr) == FALSE) {
        std::cerr << "SaferCreateLevel(NormalUser) failed: " << GetLastError() << '\n';
        return false;
    }
    const SaferLevelHandle safer_level{safer_level_raw};

    HANDLE standard_token_raw{};
    if (SaferComputeTokenFromLevel(safer_level.get(), nullptr, &standard_token_raw, 0U, nullptr) ==
        FALSE) {
        std::cerr << "SaferComputeTokenFromLevel(NormalUser) failed: " << GetLastError() << '\n';
        return false;
    }
    const Win32Handle standard_token{standard_token_raw};

    DWORD has_restrictions{};
    DWORD returned_length{};
    if (GetTokenInformation(standard_token.get(), TokenHasRestrictions, &has_restrictions,
                            sizeof(has_restrictions), &returned_length) == FALSE) {
        std::cerr << "GetTokenInformation(NormalUser restrictions) failed: " << GetLastError()
                  << '\n';
        return false;
    }

    alignas(DWORD) std::array<std::byte, SECURITY_MAX_SID_SIZE> administrators_sid{};
    DWORD administrators_sid_size = static_cast<DWORD>(administrators_sid.size());
    if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators_sid.data(),
                           &administrators_sid_size) == FALSE) {
        std::cerr << "CreateWellKnownSid(Administrators) failed: " << GetLastError() << '\n';
        return false;
    }
    HANDLE membership_token_raw{};
    if (DuplicateTokenEx(standard_token.get(), TOKEN_QUERY, nullptr, SecurityIdentification,
                         TokenImpersonation, &membership_token_raw) == FALSE) {
        std::cerr << "DuplicateTokenEx(NormalUser membership) failed: " << GetLastError() << '\n';
        return false;
    }
    const Win32Handle membership_token{membership_token_raw};
    BOOL is_administrator{};
    if (CheckTokenMembership(membership_token.get(), administrators_sid.data(),
                             &is_administrator) == FALSE) {
        std::cerr << "CheckTokenMembership(Administrators) failed: " << GetLastError() << '\n';
        return false;
    }
    if (has_restrictions == 0U || is_administrator != FALSE) {
        std::cerr << "Safer NormalUser did not produce a restricted non-administrator token\n";
        return false;
    }

    std::wstring command = L"\"" + *module_path + L"\" --expect-access-denied";
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    if (CreateProcessAsUserW(standard_token.get(), nullptr, command.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == FALSE) {
        std::cerr << "CreateProcessAsUserW with Safer NormalUser token failed: " << GetLastError()
                  << '\n';
        return false;
    }
    const Win32Handle process_handle{process.hProcess};
    const Win32Handle thread_handle{process.hThread};
    if (WaitForSingleObject(process_handle.get(), 15'000U) != WAIT_OBJECT_0) {
        std::cerr << "De-elevated ETW child timed out\n";
        static_cast<void>(TerminateProcess(process_handle.get(), ERROR_TIMEOUT));
        static_cast<void>(WaitForSingleObject(process_handle.get(), 5'000U));
        return false;
    }
    DWORD exit_code = 1U;
    if (GetExitCodeProcess(process_handle.get(), &exit_code) == FALSE || exit_code != 0U) {
        std::cerr << "De-elevated ETW child failed: " << exit_code << '\n';
        return false;
    }
    return true;
}

bool SessionNoLongerExists() noexcept {
    constexpr std::size_t name_bytes = sizeof(L"NetworkFlowMonitor.SystemTrace");
    alignas(EVENT_TRACE_PROPERTIES)
        std::array<std::byte, sizeof(EVENT_TRACE_PROPERTIES) + name_bytes>
            buffer{};
    auto* properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
    properties->Wnode.BufferSize = static_cast<ULONG>(buffer.size());
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    std::memcpy(buffer.data() + properties->LoggerNameOffset,
                nfm::collector::WindowsEtwSessionName(), name_bytes);
    return ControlTraceW(0U, nfm::collector::WindowsEtwSessionName(), properties,
                         EVENT_TRACE_CONTROL_QUERY) == ERROR_WMI_INSTANCE_NOT_FOUND;
}

int RunChildMode() {
    const WinsockRuntime winsock;
    if (!winsock.ok()) {
        return winsock.error();
    }
    const auto traffic = RunTcpLoopback(IpFamily::V4, 4U);
    return traffic.success ? 0 : traffic.socket_error;
}

int RunAccessDeniedMode() {
    nfm::collector::WindowsEtwApi api;
    nfm::collector::WindowsEtwSession session{api};
    const auto started = session.Start();
    if (started.error == nfm::collector::WindowsEtwSessionError::AccessDenied &&
        (started.win32_error == ERROR_ACCESS_DENIED ||
         started.win32_error == ERROR_PRIVILEGE_NOT_HELD)) {
        return 0;
    }
    if (started.ok()) {
        static_cast<void>(session.Stop());
    }
    std::cerr << "De-elevated ETW start returned error class "
              << static_cast<unsigned>(started.error) << " and Win32 error " << started.win32_error
              << '\n';
    return 1;
}

int RunIntegration() {
    const auto integration_started_at = std::chrono::steady_clock::now();
    TestContext context;
    const WinsockRuntime winsock;
    context.Check(winsock.ok(), "Winsock 2.2 starts");
    if (!winsock.ok()) {
        return context.failures();
    }

    const auto application_id = CurrentApplicationId();
    context.Check(application_id.has_value(), "current executable identity resolves");
    if (!application_id.has_value()) {
        return context.failures();
    }

    nfm::collector::WindowsEtwApi api;
    nfm::collector::WindowsEtwSession session{api};
    const auto started = session.Start();
    if (started.error == nfm::collector::WindowsEtwSessionError::AccessDenied) {
        std::cerr << "Administrator elevation is required for collector.etw_integration; Win32 "
                     "error: "
                  << started.win32_error << '\n';
    } else if (!started.ok()) {
        std::cerr << "StartTrace/OpenTrace failed; error class: "
                  << static_cast<unsigned>(started.error)
                  << ", Win32 error: " << started.win32_error << '\n';
    }
    context.Check(started.ok(), "custom real-time system logger starts");
    if (!started.ok()) {
        return context.failures();
    }

    context.Check(session.FlushAndQuery().ok(), "initial ETW flush/query succeeds");
    context.Check(
        WaitUntil([&session] { return session.health().system_process_rundown_confirmed; }),
        "DCStart confirms PID 4 with System image hint");

    const auto baseline_upload =
        ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Upload)
            .value_or(0U);
    const auto baseline_download =
        ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Download)
            .value_or(0U);
    const auto matrix = RunTrafficMatrix(2U);
    context.Check(matrix.has_value(), "TCP/UDP IPv4/IPv6 loopback matrix completes");
    if (matrix.has_value()) {
        context.Check(session.FlushAndQuery().ok(), "traffic ETW flush/query succeeds");
        const auto expected_upload = baseline_upload + matrix->total_each_direction();
        const auto expected_download = baseline_download + matrix->total_each_direction();
        context.Check(WaitUntil([&] {
                          return ApplicationBytes(session, *application_id,
                                                  nfm::ranking::TrafficDirection::Upload)
                                         .value_or(0U) >= expected_upload &&
                                 ApplicationBytes(session, *application_id,
                                                  nfm::ranking::TrafficDirection::Download)
                                         .value_or(0U) >= expected_download;
                      }),
                      "payload PID attributes matrix bytes to current executable");
        const auto health = session.health();
        context.Check(health.tcp_bytes >= matrix->tcp_bytes_each_direction * 2U &&
                          health.udp_bytes >= matrix->udp_bytes_each_direction * 2U,
                      "TCP and UDP bytes observed");
        context.Check(health.ipv4_bytes >= matrix->ipv4_bytes_each_direction * 2U &&
                          health.ipv6_bytes >= matrix->ipv6_bytes_each_direction * 2U,
                      "IPv4 and IPv6 bytes observed");
    }

    const auto before_child_snapshot = session.MakeSnapshot(std::chrono::steady_clock::now());
    const auto before_child_ends = session.health().process_end_events;
    context.Check(StartChildTrafficProcess(), "short-lived child sends loopback traffic and exits");
    context.Check(session.FlushAndQuery().ok(), "child ETW flush/query succeeds");
    constexpr std::uint64_t child_bytes_each_direction = kPayloadSize * 4U * 2U;
    context.Check(WaitUntil([&] {
                      const auto health = session.health();
                      return health.process_end_events > before_child_ends &&
                             HasApplicationDelta(
                                 {.before = before_child_snapshot,
                                  .after = session.MakeSnapshot(std::chrono::steady_clock::now())},
                                 child_bytes_each_direction);
                  }),
                  "retired child instance retains a complete application-bucket delta");

    const auto before_burst_upload =
        ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Upload)
            .value_or(0U);
    const auto before_burst_download =
        ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Download)
            .value_or(0U);
    const auto before_loss = session.health().events_lost + session.health().log_buffers_lost +
                             session.health().realtime_buffers_lost;
    const auto burst = RunTcpLoopback(IpFamily::V4, 128U);
    context.Check(burst.success, "bounded high-rate TCP burst completes");
    context.Check(session.FlushAndQuery().ok(), "burst ETW flush/query succeeds");
    if (burst.success) {
        const bool burst_accounted = WaitUntil([&] {
            const auto health = session.health();
            const auto current_loss =
                health.events_lost + health.log_buffers_lost + health.realtime_buffers_lost;
            const bool complete =
                ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Upload)
                        .value_or(0U) >= before_burst_upload + burst.bytes_each_direction &&
                ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Download)
                        .value_or(0U) >= before_burst_download + burst.bytes_each_direction;
            return complete || current_loss > before_loss;
        });
        context.Check(burst_accounted, "burst is complete or ETW loss is visible");
        const auto health = session.health();
        const auto current_loss =
            health.events_lost + health.log_buffers_lost + health.realtime_buffers_lost;
        if (current_loss > before_loss) {
            context.Check(health.state == nfm::collector::CollectorState::Degraded &&
                              (current_loss != 0U || health.unmatched_network_bytes != 0U),
                          "loss degrades health with diagnostic evidence");
        }
    }

    const auto schemas = session.observed_schemas();
    constexpr std::uint32_t version2 = 1U << 2U;
    constexpr std::uint32_t network_opcodes = (1U << 10U) | (1U << 11U) | (1U << 26U) | (1U << 27U);
    constexpr std::uint32_t process_opcodes = (1U << 1U) | (1U << 2U) | (1U << 3U);
    context.Check((schemas.tcp.version_mask & version2) != 0U &&
                      (schemas.tcp.version2_opcode_mask & network_opcodes) == network_opcodes,
                  "live TCP version/opcode matrix observed");
    context.Check((schemas.udp.version_mask & version2) != 0U &&
                      (schemas.udp.version2_opcode_mask & network_opcodes) == network_opcodes,
                  "live UDP version/opcode matrix observed");
    const bool complete_process_schema =
        (schemas.process.version2_opcode_mask & process_opcodes) == process_opcodes ||
        (schemas.process.version3_opcode_mask & process_opcodes) == process_opcodes ||
        (schemas.process.version4_opcode_mask & process_opcodes) == process_opcodes;
    context.Check(complete_process_schema,
                  "live process Start/End/DCStart uses one supported schema version");

    const auto statistics = session.statistics();
    const auto health = session.health();
    const auto final_upload =
        ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Upload);
    const auto final_download =
        ApplicationBytes(session, *application_id, nfm::ranking::TrafficDirection::Download);
    const auto final_snapshot = session.MakeSnapshot(std::chrono::steady_clock::now());
    std::cout << "ETW live diagnostics: buffers=" << statistics.number_of_buffers
              << ", buffer_kib=" << statistics.buffer_size_kib
              << ", events_lost=" << statistics.loss.events_lost
              << ", log_buffers_lost=" << statistics.loss.log_buffers_lost
              << ", realtime_buffers_lost=" << statistics.loss.realtime_buffers_lost
              << ", received=" << health.events_received << ", decoded=" << health.events_decoded
              << ", tcp_versions=0x" << std::hex << schemas.tcp.version_mask << ", tcp_opcodes=0x"
              << schemas.tcp.opcode_mask << ", udp_versions=0x" << schemas.udp.version_mask
              << ", udp_opcodes=0x" << schemas.udp.opcode_mask << ", process_versions=0x"
              << schemas.process.version_mask << ", process_opcodes=0x"
              << schemas.process.opcode_mask << ", process_v2_opcodes=0x"
              << schemas.process.version2_opcode_mask << ", process_v3_opcodes=0x"
              << schemas.process.version3_opcode_mask << ", process_v4_opcodes=0x"
              << schemas.process.version4_opcode_mask << std::dec
              << ", process_start=" << health.process_start_events
              << ", process_rundown=" << health.process_rundown_events
              << ", process_end=" << health.process_end_events
              << ", process_probe_failures=" << health.process_probe_failures
              << ", malformed=" << health.malformed_events
              << ", unsupported_schema=" << health.unsupported_schema_events
              << ", unmatched_bytes=" << health.unmatched_network_bytes
              << ", first_error_domain=" << static_cast<unsigned>(health.first_error.domain)
              << ", first_error_stage=" << static_cast<unsigned>(health.first_error.stage)
              << ", first_error_code=" << health.first_error.code
              << ", last_error_domain=" << static_cast<unsigned>(health.last_error.domain)
              << ", last_error_stage=" << static_cast<unsigned>(health.last_error.stage)
              << ", last_error_code=" << health.last_error.code
              << ", app_found=" << (final_upload.has_value() && final_download.has_value())
              << ", app_upload=" << final_upload.value_or(0U)
              << ", app_download=" << final_download.value_or(0U)
              << ", snapshot_apps=" << final_snapshot.applications.size() << ", duration_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - integration_started_at)
                     .count()
              << '\n';

    context.Check(session.Stop().ok(), "live session stops and worker joins");
    context.Check(SessionNoLongerExists(), "named ETW session no longer exists after Stop");
    context.Check(RunDeElevatedAccessDeniedProcess(),
                  "de-elevated child reports explicit ETW AccessDenied");
    return context.failures();
}

} // namespace

int main(int argc, char** argv) {
    static_cast<void>(
        SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX));
    if (argc == 2 && std::string_view{argv[1]} == "--child") {
        return RunChildMode();
    }
    if (argc == 2 && std::string_view{argv[1]} == "--expect-access-denied") {
        return RunAccessDeniedMode();
    }
    return RunIntegration();
}
