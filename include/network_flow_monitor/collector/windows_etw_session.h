#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_ETW_SESSION_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_ETW_SESSION_H

#include "network_flow_monitor/collector/collector_core.h"
#include "network_flow_monitor/collector/windows_etw_record_decoder.h"
#include "network_flow_monitor/collector/windows_process_probe_adapter.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <evntcons.h>
#include <evntrace.h>
#include <mutex>
#include <thread>
#include <vector>

namespace nfm::collector {

class EtwApi {
  public:
    virtual ~EtwApi() = default;
    EtwApi(const EtwApi&) = delete;
    EtwApi& operator=(const EtwApi&) = delete;
    EtwApi(EtwApi&&) = delete;
    EtwApi& operator=(EtwApi&&) = delete;

    virtual ULONG StartController(PTRACEHANDLE handle, LPCWSTR name,
                                  PEVENT_TRACE_PROPERTIES properties) noexcept = 0;
    virtual TRACEHANDLE OpenConsumer(PEVENT_TRACE_LOGFILEW logfile) noexcept = 0;
    virtual ULONG ProcessConsumer(PTRACEHANDLE handle) noexcept = 0;
    virtual ULONG ControlController(TRACEHANDLE handle, LPCWSTR name,
                                    PEVENT_TRACE_PROPERTIES properties,
                                    ULONG control_code) noexcept = 0;
    virtual ULONG CloseConsumer(TRACEHANDLE handle) noexcept = 0;

  protected:
    EtwApi() = default;
};

class WindowsEtwApi final : public EtwApi {
  public:
    ULONG StartController(PTRACEHANDLE handle, LPCWSTR name,
                          PEVENT_TRACE_PROPERTIES properties) noexcept override;
    TRACEHANDLE OpenConsumer(PEVENT_TRACE_LOGFILEW logfile) noexcept override;
    ULONG ProcessConsumer(PTRACEHANDLE handle) noexcept override;
    ULONG ControlController(TRACEHANDLE handle, LPCWSTR name, PEVENT_TRACE_PROPERTIES properties,
                            ULONG control_code) noexcept override;
    ULONG CloseConsumer(TRACEHANDLE handle) noexcept override;
};

enum class ExistingSessionPolicy : std::uint8_t {
    Fail,
    StopAndRetry,
};

struct WindowsEtwSessionOptions {
    ExistingSessionPolicy existing_session_policy{ExistingSessionPolicy::Fail};
    bool simulate_worker_start_failure{};
    bool simulate_callback_failure{};
};

enum class WindowsEtwSessionError : std::uint8_t {
    None,
    AlreadyExists,
    AccessDenied,
    StartFailed,
    OpenFailed,
    WorkerStartFailed,
    FlushFailed,
    QueryFailed,
    StopFailed,
    NotRunning,
};

struct WindowsEtwSessionResult {
    WindowsEtwSessionError error{WindowsEtwSessionError::None};
    std::uint32_t win32_error{};

    [[nodiscard]] bool ok() const noexcept;
};

struct WindowsEtwSessionStatistics {
    std::uint32_t number_of_buffers{};
    std::uint32_t buffer_size_kib{};
    EtwSessionStatistics loss;
};

struct EtwSchemaObservation {
    std::uint32_t version_mask{};
    std::uint32_t opcode_mask{};
    std::uint32_t version2_opcode_mask{};
    std::uint32_t version3_opcode_mask{};
    std::uint32_t version4_opcode_mask{};
};

struct WindowsEtwSchemaObservations {
    EtwSchemaObservation tcp;
    EtwSchemaObservation udp;
    EtwSchemaObservation process;
};

class WindowsEtwSession {
  public:
    explicit WindowsEtwSession(EtwApi& api, WindowsEtwSessionOptions options = {});
    ~WindowsEtwSession();
    WindowsEtwSession(const WindowsEtwSession&) = delete;
    WindowsEtwSession& operator=(const WindowsEtwSession&) = delete;
    WindowsEtwSession(WindowsEtwSession&&) = delete;
    WindowsEtwSession& operator=(WindowsEtwSession&&) = delete;

    [[nodiscard]] WindowsEtwSessionResult Start() noexcept;
    [[nodiscard]] WindowsEtwSessionResult FlushAndQuery() noexcept;
    [[nodiscard]] WindowsEtwSessionResult Stop() noexcept;
    [[nodiscard]] CollectorHealth health() const noexcept;
    [[nodiscard]] std::size_t active_process_count() const noexcept;
    [[nodiscard]] WindowsEtwSessionStatistics statistics() const noexcept;
    [[nodiscard]] WindowsEtwSchemaObservations observed_schemas() const noexcept;
    [[nodiscard]] ranking::CounterSnapshot MakeSnapshot(
        std::chrono::steady_clock::time_point captured_at) const;

  private:
    static void WINAPI EventRecordCallback(PEVENT_RECORD record) noexcept;
    static ULONG WINAPI BufferCallback(PEVENT_TRACE_LOGFILEW logfile) noexcept;

    void ProcessWorker(TRACEHANDLE processing_handle) noexcept;
    void OnEvent(const EVENT_RECORD& record) noexcept;
    ULONG OnBuffer(const EVENT_TRACE_LOGFILEW& logfile) noexcept;
    void PrepareProperties() noexcept;
    WindowsEtwSessionResult StopPartial() noexcept;

    EtwApi& api_;
    WindowsEtwSessionOptions options_;
    WindowsProcessProbeAdapter process_probe_;
    TdhProcessPropertyReader property_reader_;
    WindowsEtwRecordDecoder decoder_;
    mutable std::mutex core_mutex_;
    mutable std::mutex lifecycle_mutex_;
    CollectorCore core_;
    std::vector<std::byte> properties_buffer_;
    EVENT_TRACE_LOGFILEW logfile_{};
    TRACEHANDLE controller_handle_{};
    TRACEHANDLE consumer_handle_{INVALID_PROCESSTRACE_HANDLE};
    std::jthread worker_;
    std::atomic_bool stop_requested_;
    std::atomic_bool fatal_callback_;
    std::atomic_uint64_t latest_log_buffers_lost_;
    std::atomic_uint64_t latest_realtime_buffers_lost_;
    std::atomic_uint32_t tcp_version_mask_;
    std::atomic_uint32_t tcp_opcode_mask_;
    std::atomic_uint32_t tcp_version2_opcode_mask_;
    std::atomic_uint32_t tcp_version3_opcode_mask_;
    std::atomic_uint32_t tcp_version4_opcode_mask_;
    std::atomic_uint32_t udp_version_mask_;
    std::atomic_uint32_t udp_opcode_mask_;
    std::atomic_uint32_t udp_version2_opcode_mask_;
    std::atomic_uint32_t udp_version3_opcode_mask_;
    std::atomic_uint32_t udp_version4_opcode_mask_;
    std::atomic_uint32_t process_version_mask_;
    std::atomic_uint32_t process_opcode_mask_;
    std::atomic_uint32_t process_version2_opcode_mask_;
    std::atomic_uint32_t process_version3_opcode_mask_;
    std::atomic_uint32_t process_version4_opcode_mask_;
    WindowsEtwSessionStatistics statistics_;
    bool running_{};
};

[[nodiscard]] const wchar_t* WindowsEtwSessionName() noexcept;
[[nodiscard]] const GUID& WindowsEtwSessionId() noexcept;

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_ETW_SESSION_H
