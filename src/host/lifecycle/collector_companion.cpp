#include "network_flow_monitor/host/lifecycle/collector_companion.h"

#include "network_flow_monitor/ipc/security_policy.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstring>
#include <shellapi.h>
#include <unordered_map>
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

std::string Base64Encode(const std::vector<std::uint8_t>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2U) / 3U) * 4U);
    std::size_t i = 0;
    while (i + 2U < data.size()) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16U) |
                                     (static_cast<std::uint32_t>(data[i + 1U]) << 8U) |
                                     static_cast<std::uint32_t>(data[i + 2U]);
        out.push_back(kAlphabet[(triple >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(triple >> 12U) & 0x3FU]);
        out.push_back(kAlphabet[(triple >> 6U) & 0x3FU]);
        out.push_back(kAlphabet[triple & 0x3FU]);
        i += 3U;
    }
    if (i < data.size()) {
        std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16U;
        out.push_back(kAlphabet[(triple >> 18U) & 0x3FU]);
        if (i + 1U < data.size()) {
            triple |= static_cast<std::uint32_t>(data[i + 1U]) << 8U;
            out.push_back(kAlphabet[(triple >> 12U) & 0x3FU]);
            out.push_back(kAlphabet[(triple >> 6U) & 0x3FU]);
            out.push_back('=');
        } else {
            out.push_back(kAlphabet[(triple >> 12U) & 0x3FU]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

std::string ExtractExecutableIconDataUri(const std::string& utf8_path) {
    if (utf8_path.empty() || utf8_path.starts_with("data:")) {
        return utf8_path;
    }
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, std::string> icon_cache;
    {
        const std::scoped_lock lock(cache_mutex);
        auto it = icon_cache.find(utf8_path);
        if (it != icon_cache.end()) {
            return it->second;
        }
    }

    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_path.data(),
                                         static_cast<int>(utf8_path.size()), nullptr, 0);
    if (wlen <= 0) {
        return {};
    }
    std::wstring wpath(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_path.data(), static_cast<int>(utf8_path.size()),
                        wpath.data(), wlen);

    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(wpath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_SMALLICON) == 0 ||
        sfi.hIcon == nullptr) {
        SHGetFileInfoW(wpath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    }
    if (sfi.hIcon == nullptr) {
        return {};
    }

    constexpr std::size_t kIconSize = 20U;
    constexpr int kIconSizePx = 20;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = kIconSizePx;
    bi.bV5Height = kIconSizePx; // bottom-up
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem_dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                   &bits, nullptr, 0);
    std::string result;
    if (dib != nullptr && bits != nullptr) {
        HGDIOBJ old_bmp = SelectObject(mem_dc, dib);
        constexpr std::size_t pixel_bytes = kIconSize * kIconSize * 4U;
        std::memset(bits, 0, pixel_bytes);
        DrawIconEx(mem_dc, 0, 0, sfi.hIcon, kIconSizePx, kIconSizePx, 0, nullptr, DI_NORMAL);
        GdiFlush();
        auto* px = static_cast<std::uint8_t*>(bits);
        bool any_alpha = false;
        for (std::size_t idx = 3; idx < pixel_bytes; idx += 4) {
            if (px[idx] != 0) {
                any_alpha = true;
                break;
            }
        }
        if (!any_alpha) {
            for (std::size_t idx = 0; idx < pixel_bytes; idx += 4) {
                if (px[idx] != 0 || px[idx + 1] != 0 || px[idx + 2] != 0) {
                    px[idx + 3] = 255;
                }
            }
        }

        BITMAPFILEHEADER bfh{};
        bfh.bfType = 0x4D42; // 'BM'
        bfh.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPV5HEADER));
        bfh.bfSize = static_cast<DWORD>(bfh.bfOffBits + pixel_bytes);

        std::vector<std::uint8_t> bmp_buf(bfh.bfSize);
        std::memcpy(bmp_buf.data(), &bfh, sizeof(bfh));
        std::memcpy(bmp_buf.data() + sizeof(bfh), &bi, sizeof(bi));
        std::memcpy(bmp_buf.data() + bfh.bfOffBits, bits, pixel_bytes);

        result = "data:image/bmp;base64," + Base64Encode(bmp_buf);
        SelectObject(mem_dc, old_bmp);
    }
    if (dib != nullptr) {
        DeleteObject(dib);
    }
    if (mem_dc != nullptr) {
        DeleteDC(mem_dc);
    }
    if (screen_dc != nullptr) {
        ReleaseDC(nullptr, screen_dc);
    }
    DestroyIcon(sfi.hIcon);

    if (!result.empty()) {
        const std::scoped_lock lock(cache_mutex);
        if (icon_cache.size() > 256U) {
            icon_cache.clear();
        }
        icon_cache.emplace(utf8_path, result);
    }
    return result;
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

void CollectorCompanion::set_ranking_snapshot_callback(RankingSnapshotCallback callback) {
    const std::scoped_lock lock(mutex_);
    ranking_snapshot_cb_ = std::move(callback);
}

void CollectorCompanion::set_collector_status_callback(CollectorStatusCallback callback) {
    const std::scoped_lock lock(mutex_);
    collector_status_cb_ = std::move(callback);
}

void CollectorCompanion::set_session_reset_callback(SessionResetCallback callback) {
    const std::scoped_lock lock(mutex_);
    session_reset_cb_ = std::move(callback);
}
void CollectorCompanion::set_diagnostic_callback(DiagnosticCallback callback) {
    const std::scoped_lock lock(mutex_);
    diagnostic_cb_ = std::move(callback);
}

void CollectorCompanion::emit_diagnostic(bool is_error, std::string_view message) noexcept {
    try {
        DiagnosticCallback cb;
        {
            const std::scoped_lock lock(mutex_);
            cb = diagnostic_cb_;
        }
        if (cb) {
            cb(is_error, message);
        }
    } catch (...) {
    }
}

bool CollectorCompanion::start() {
    std::string diag_msg;
    bool diag_error = false;
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
            } else {
                const DWORD evt_err = GetLastError();
                diag_error = true;
                diag_msg = "CreateEventW failed for collector stop event '" + stop_event_name_ +
                           "' (win32_error=" + std::to_string(evt_err) + ")";
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
                diag_msg = "Collector companion launched (pid=" + std::to_string(pi.dwProcessId) +
                           ", stop_event=" + stop_event_name_ + ")";
            } else {
                const DWORD create_err = GetLastError();
                info_.state = CollectorState::Failed;
                info_.error_message = "Failed to launch collector process (win32_error=" +
                                      std::to_string(create_err) + ")";
                diag_error = true;
                diag_msg = info_.error_message;
            }
        } else if (!collector_path_.empty()) {
            info_.state = CollectorState::Failed;
            info_.error_message = "Collector executable not found";
            diag_error = true;
            diag_msg = "Collector executable missing at configured path";
        }
    }
    if (!diag_msg.empty()) {
        emit_diagnostic(diag_error, diag_msg);
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
        std::string diag_msg;
        bool diag_error = false;
        {
            const std::scoped_lock lock(mutex_);
            if (state == ipc::IpcClientState::Streaming) {
                info_.state = CollectorState::Running;
            } else if (state == ipc::IpcClientState::Failed ||
                       state == ipc::IpcClientState::Unauthorized) {
                info_.state = CollectorState::Failed;
                diag_error = true;
                std::string exit_code_suffix;
                if (collector_process_handle_ != nullptr) {
                    DWORD exit_code = 0;
                    if (GetExitCodeProcess(static_cast<HANDLE>(collector_process_handle_),
                                           &exit_code) != FALSE &&
                        exit_code != STILL_ACTIVE) {
                        exit_code_suffix = ", collector_exit_code=" + std::to_string(exit_code);
                    }
                }
                if (state == ipc::IpcClientState::Unauthorized) {
                    info_.error_message = "IPC pipe authorization rejected";
                    diag_msg = "Collector IPC state=Unauthorized (pid=" +
                               std::to_string(info_.process_id) + exit_code_suffix + ")";
                } else {
                    if (info_.error_message.empty()) {
                        info_.error_message = "Collector IPC connection failed";
                    }
                    diag_msg =
                        "Collector IPC state=Failed (pid=" + std::to_string(info_.process_id) +
                        exit_code_suffix + ")";
                }
            }
        }
        if (!diag_msg.empty()) {
            emit_diagnostic(diag_error, diag_msg);
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

void CollectorCompanion::RankingSnapshotReceived(ipc::RankingSnapshotMessage snapshot) noexcept {
    try {
        inject_ranking_snapshot(snapshot);
    } catch (...) {
    }
}
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

void CollectorCompanion::inject_ranking_snapshot(const ipc::RankingSnapshotMessage& snapshot) {
    RankingSnapshotCallback cb;
    {
        const std::scoped_lock lock(mutex_);
        cb = ranking_snapshot_cb_;
    }
    if (cb) {
        bridge::RankingSnapshotDto dto{
            .seq = snapshot.seq,
            .monotonic_ms = snapshot.monotonic_ms,
            .status = snapshot.status,
            .total_download_bytes_per_sec = snapshot.total_rate.download_bytes_per_sec,
            .total_upload_bytes_per_sec = snapshot.total_rate.upload_bytes_per_sec,
        };
        dto.applications.reserve(snapshot.applications.size());
        for (const auto& app : snapshot.applications) {
            const std::string& path_source =
                !app.executable_path.empty() ? app.executable_path : app.icon_key;
            std::string resolved_icon = ExtractExecutableIconDataUri(path_source);
            if (resolved_icon.empty()) {
                resolved_icon = app.icon_key;
            }
            dto.applications.push_back(bridge::RankingAppDto{
                .id = app.id,
                .executable_path = app.executable_path,
                .display_name = app.display_name,
                .icon_key = std::move(resolved_icon),
                .download_bytes_per_sec = app.download_bytes_per_sec,
                .upload_bytes_per_sec = app.upload_bytes_per_sec,
            });
        }
        cb(dto);
    }
}

void CollectorCompanion::inject_collector_status(const ipc::CollectorStatusMessage& status) {
    CollectorStatusCallback cb;
    CollectorInfo current_info;
    {
        const std::scoped_lock lock(mutex_);
        cb = collector_status_cb_;
        current_info = info_;
    }
    if (cb) {
        std::string state_str = "Running";
        if (current_info.state == CollectorState::Starting) {
            state_str = "Starting";
        } else if (current_info.state == CollectorState::Stopped) {
            state_str = "Stopped";
        } else if (current_info.state == CollectorState::Failed) {
            state_str = "Failed";
        }
        bridge::CollectorStatusDto dto{
            .monotonic_ms = status.monotonic_ms,
            .etw_session_active = status.etw_session_active,
            .dropped_packets = status.dropped_packets,
            .active_pids = status.active_pids,
            .state = std::move(state_str),
            .process_id = current_info.process_id,
            .instance_id = current_info.instance_id,
            .is_elevated = status.etw_session_active,
            .last_error = current_info.error_message,
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
        info_.error_message.clear();
        cb = session_reset_cb_;
    }
    emit_diagnostic(
        false, "Collector IPC handshake established (pid=" + std::to_string(ack.collector_pid) +
                   ", instance=" + ack.collector_instance_id + ")");
    if (cb) {
        bridge::SessionResetDto dto{
            .collector_instance_id = ack.collector_instance_id,
            .collector_pid = ack.collector_pid,
        };
        cb(dto);
    }
}

} // namespace nfm::host::lifecycle
