#include "network_flow_monitor/host/lifecycle/collector_companion.h"

#include "network_flow_monitor/ipc/security_policy.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <utility>
#include <vector>

namespace nfm::host::lifecycle {

namespace {

std::vector<std::byte> QueryCurrentProcessLogonSid() {
    HANDLE token{};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return {};
    }
    DWORD required{};
    static_cast<void>(GetTokenInformation(token, TokenGroups, nullptr, 0U, &required));
    if (required == 0U) {
        CloseHandle(token);
        return {};
    }
    std::vector<std::byte> storage(required);
    if (GetTokenInformation(token, TokenGroups, storage.data(), required, &required) == FALSE) {
        CloseHandle(token);
        return {};
    }
    CloseHandle(token);
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    for (DWORD index = 0; index < groups->GroupCount; ++index) {
        const auto& group = groups->Groups[index];
        if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID ||
            IsValidSid(group.Sid) == FALSE) {
            continue;
        }
        const auto length = GetLengthSid(group.Sid);
        std::vector<std::byte> sid(length);
        if (CopySid(length, sid.data(), group.Sid) != FALSE) {
            return sid;
        }
    }
    return {};
}

} // namespace

CollectorCompanion::CollectorCompanion(std::filesystem::path collector_executable_path)
    : collector_path_(std::move(collector_executable_path)),
      transport_factory_(std::make_unique<ipc::WindowsPipeTransportFactory>()) {
    auto logon_sid = QueryCurrentProcessLogonSid();
    ipc::AuthorizedPrincipal principal{
        .mode = ipc::CollectorEndpointMode::Portable,
        .authorized_sid = logon_sid,
        .server_logon_sid = std::move(logon_sid),
    };
    ipc_client_ =
        std::make_unique<ipc::IpcClient>(std::move(principal), *transport_factory_, *this);
}

CollectorCompanion::~CollectorCompanion() {
    stop();
}

void CollectorCompanion::set_traffic_tick_callback(TrafficTickCallback callback) {
    const std::scoped_lock lock(mutex_);
    traffic_tick_cb_ = std::move(callback);
}

void CollectorCompanion::set_collector_status_callback(CollectorStatusCallback callback) {
    const std::scoped_lock lock(mutex_);
    collector_status_cb_ = std::move(callback);
}

void CollectorCompanion::set_session_reset_callback(SessionResetCallback callback) {
    const std::scoped_lock lock(mutex_);
    session_reset_cb_ = std::move(callback);
}

bool CollectorCompanion::start() {
    {
        const std::scoped_lock lock(mutex_);
        if (info_.state == CollectorState::Running || info_.state == CollectorState::Starting) {
            return true;
        }

        info_.state = CollectorState::Starting;
        info_.error_message.clear();

        if (!collector_path_.empty() && std::filesystem::exists(collector_path_)) {
            // Generate unique stop event name matching required prefix
            auto current_pid = GetCurrentProcessId();
            stop_event_name_ = "Local\\NetworkFlowMonitor.Stop." + std::to_string(current_pid);

            std::wstring w_event_name(stop_event_name_.begin(), stop_event_name_.end());
            HANDLE event_h = CreateEventW(nullptr, TRUE, FALSE, w_event_name.c_str());
            if (event_h != nullptr) {
                stop_event_handle_ = static_cast<void*>(event_h);
            }

            std::wstring cmd = L"\"" + collector_path_.wstring() + L"\" --collector --parent-pid " +
                               std::to_wstring(current_pid) + L" --stop-event " + w_event_name;

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};

            std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
            cmd_buf.push_back(L'\0');

            BOOL created = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
                                          CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

            if (created != FALSE) {
                CloseHandle(pi.hThread);
                collector_process_handle_ = static_cast<void*>(pi.hProcess);
                info_.process_id = pi.dwProcessId;
            } else {
                info_.state = CollectorState::Failed;
                info_.error_message = "Failed to launch collector process";
            }
        }
    }

    if (ipc_client_) {
        static_cast<void>(ipc_client_->Start());
    }

    const std::scoped_lock lock(mutex_);
    return info_.state != CollectorState::Failed;
}

void CollectorCompanion::stop() {
    if (ipc_client_) {
        ipc_client_->Stop();
    }

    void* proc_h = nullptr;
    void* stop_h = nullptr;
    {
        const std::scoped_lock lock(mutex_);
        proc_h = collector_process_handle_;
        collector_process_handle_ = nullptr;
        stop_h = stop_event_handle_;
        stop_event_handle_ = nullptr;
        info_.state = CollectorState::Stopped;
    }

    if (stop_h != nullptr) {
        SetEvent(static_cast<HANDLE>(stop_h));
    }

    if (proc_h != nullptr) {
        WaitForSingleObject(static_cast<HANDLE>(proc_h), 1000);
        CloseHandle(static_cast<HANDLE>(proc_h));
    }

    if (stop_h != nullptr) {
        CloseHandle(static_cast<HANDLE>(stop_h));
    }
}

bool CollectorCompanion::restart() {
    stop();
    return start();
}

CollectorInfo CollectorCompanion::get_info() const {
    const std::scoped_lock lock(mutex_);
    return info_;
}

bool CollectorCompanion::is_running() const noexcept {
    const std::scoped_lock lock(mutex_);
    return info_.state == CollectorState::Running;
}

void CollectorCompanion::StateChanged(ipc::IpcClientState state) noexcept {
    try {
        const std::scoped_lock lock(mutex_);
        if (state == ipc::IpcClientState::Streaming) {
            info_.state = CollectorState::Running;
        } else if (state == ipc::IpcClientState::Failed ||
                   state == ipc::IpcClientState::Unauthorized) {
            info_.state = CollectorState::Failed;
        }
    } catch (...) {
    }
}

void CollectorCompanion::HelloAckReceived(ipc::HelloAckMessage ack) noexcept {
    try {
        inject_hello_ack(ack);
    } catch (...) {
    }
}

void CollectorCompanion::TrafficTickReceived(ipc::TrafficTickMessage tick) noexcept {
    try {
        inject_traffic_tick(tick);
    } catch (...) {
    }
}

void CollectorCompanion::RankingSnapshotReceived(
    ipc::RankingSnapshotMessage /*snapshot*/) noexcept {}

void CollectorCompanion::CollectorStatusReceived(ipc::CollectorStatusMessage status) noexcept {
    try {
        inject_collector_status(status);
    } catch (...) {
    }
}

void CollectorCompanion::inject_traffic_tick(const ipc::TrafficTickMessage& tick) {
    TrafficTickCallback cb;
    {
        const std::scoped_lock lock(mutex_);
        cb = traffic_tick_cb_;
    }
    if (cb) {
        bridge::TrafficTickDto dto{
            .seq = tick.seq,
            .monotonic_ms = tick.monotonic_ms,
            .download_bytes_per_sec = tick.download_bytes_per_sec,
            .upload_bytes_per_sec = tick.upload_bytes_per_sec,
        };
        cb(dto);
    }
}

void CollectorCompanion::inject_collector_status(const ipc::CollectorStatusMessage& status) {
    CollectorStatusCallback cb;
    {
        const std::scoped_lock lock(mutex_);
        cb = collector_status_cb_;
    }
    if (cb) {
        bridge::CollectorStatusDto dto{
            .monotonic_ms = status.monotonic_ms,
            .etw_session_active = status.etw_session_active,
            .dropped_packets = status.dropped_packets,
            .active_pids = status.active_pids,
        };
        cb(dto);
    }
}

void CollectorCompanion::inject_hello_ack(const ipc::HelloAckMessage& ack) {
    SessionResetCallback cb;
    {
        const std::scoped_lock lock(mutex_);
        info_.instance_id = ack.collector_instance_id;
        info_.process_id = ack.collector_pid;
        info_.state = CollectorState::Running;
        cb = session_reset_cb_;
    }
    if (cb) {
        bridge::SessionResetDto dto{
            .collector_instance_id = ack.collector_instance_id,
            .collector_pid = ack.collector_pid,
        };
        cb(dto);
    }
}

} // namespace nfm::host::lifecycle
