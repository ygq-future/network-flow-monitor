#include "network_flow_monitor/host/lifecycle/collector_companion.h"

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

CollectorCompanion::CollectorCompanion(std::filesystem::path collector_executable_path)
    : collector_path_(std::move(collector_executable_path)),
      ipc_client_(std::make_unique<ipc::IpcClient>(*this)) {}

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
    const std::scoped_lock lock(mutex_);
    if (info_.state == CollectorState::Running || info_.state == CollectorState::Starting) {
        return true;
    }

    info_.state = CollectorState::Starting;
    info_.error_message.clear();

    if (!collector_path_.empty() && std::filesystem::exists(collector_path_)) {
        // Generate unique stop event name
        auto current_pid = GetCurrentProcessId();
        stop_event_name_ = "Local\\NfmStopEvent_" + std::to_string(current_pid);

        std::wstring w_event_name(stop_event_name_.begin(), stop_event_name_.end());
        HANDLE event_h = CreateEventW(nullptr, TRUE, FALSE, w_event_name.c_str());
        if (event_h != nullptr) {
            stop_event_handle_ = static_cast<void*>(event_h);
        }

        std::wstring cmd = L"\"" + collector_path_.wstring() + L"\" --parent-pid " +
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

    if (ipc_client_) {
        static_cast<void>(ipc_client_->Start());
    }

    return info_.state != CollectorState::Failed;
}

void CollectorCompanion::stop() {
    if (ipc_client_) {
        ipc_client_->Stop();
    }

    if (stop_event_handle_ != nullptr) {
        SetEvent(static_cast<HANDLE>(stop_event_handle_));
    }

    if (collector_process_handle_ != nullptr) {
        WaitForSingleObject(static_cast<HANDLE>(collector_process_handle_), 1000);
        CloseHandle(static_cast<HANDLE>(collector_process_handle_));
        collector_process_handle_ = nullptr;
    }

    if (stop_event_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stop_event_handle_));
        stop_event_handle_ = nullptr;
    }

    const std::scoped_lock lock(mutex_);
    info_.state = CollectorState::Stopped;
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
