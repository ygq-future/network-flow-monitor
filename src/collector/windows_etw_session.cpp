#include "network_flow_monitor/collector/windows_etw_session.h"

#include "network_flow_monitor/collector/etw_authorization.h"
#include "network_flow_monitor/process/windows_process_probe.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <system_error>
#include <utility>
#include <variant>

namespace nfm::collector {
namespace {

constexpr wchar_t kSessionName[] = L"NetworkFlowMonitor.SystemTrace";
constexpr GUID kSessionId = {.Data1 = 0x1473ea71U,
                             .Data2 = 0xa063U,
                             .Data3 = 0x4396U,
                             .Data4 = {0x8eU, 0xa1U, 0xe3U, 0xd1U, 0xedU, 0xe8U, 0x0aU, 0xf7U}};

std::size_t PropertiesBufferSize() noexcept {
    return sizeof(EVENT_TRACE_PROPERTIES) + sizeof(kSessionName);
}

PEVENT_TRACE_PROPERTIES Properties(std::vector<std::byte>& buffer) noexcept {
    return reinterpret_cast<PEVENT_TRACE_PROPERTIES>(buffer.data());
}

const EVENT_TRACE_PROPERTIES* Properties(const std::vector<std::byte>& buffer) noexcept {
    return reinterpret_cast<const EVENT_TRACE_PROPERTIES*>(buffer.data());
}

WindowsEtwSessionError ClassifyStartError(ULONG status) noexcept {
    if (status == ERROR_ALREADY_EXISTS) {
        return WindowsEtwSessionError::AlreadyExists;
    }
    if (status == ERROR_ACCESS_DENIED || status == ERROR_PRIVILEGE_NOT_HELD) {
        return WindowsEtwSessionError::AccessDenied;
    }
    return WindowsEtwSessionError::StartFailed;
}

void RecordSchemaValue(std::atomic_uint32_t& mask, std::uint8_t value) noexcept {
    if (value < 32U) {
        mask.fetch_or(1U << value);
    }
}

struct SchemaMaskWriters final {
    std::atomic_uint32_t& versions;
    std::atomic_uint32_t& opcodes;
    std::atomic_uint32_t& version2_opcodes;
    std::atomic_uint32_t& version3_opcodes;
    std::atomic_uint32_t& version4_opcodes;
};

void RecordSchema(const SchemaMaskWriters masks, const EVENT_DESCRIPTOR& descriptor) noexcept {
    RecordSchemaValue(masks.versions, descriptor.Version);
    RecordSchemaValue(masks.opcodes, descriptor.Opcode);
    if (descriptor.Version == 2U) {
        RecordSchemaValue(masks.version2_opcodes, descriptor.Opcode);
    } else if (descriptor.Version == 3U) {
        RecordSchemaValue(masks.version3_opcodes, descriptor.Opcode);
    } else if (descriptor.Version == 4U) {
        RecordSchemaValue(masks.version4_opcodes, descriptor.Opcode);
    }
}

} // namespace

ULONG WindowsEtwApi::StartController(PTRACEHANDLE handle, LPCWSTR name,
                                     PEVENT_TRACE_PROPERTIES properties) noexcept {
    static_cast<void>(EnableCollectorPrivileges());
    return StartTraceW(handle, name, properties);
}

TRACEHANDLE WindowsEtwApi::OpenConsumer(PEVENT_TRACE_LOGFILEW logfile) noexcept {
    return OpenTraceW(logfile);
}

ULONG WindowsEtwApi::ProcessConsumer(PTRACEHANDLE handle) noexcept {
    return ProcessTrace(handle, 1U, nullptr, nullptr);
}

ULONG WindowsEtwApi::ControlController(TRACEHANDLE handle, LPCWSTR name,
                                       PEVENT_TRACE_PROPERTIES properties,
                                       ULONG control_code) noexcept {
    return ControlTraceW(handle, name, properties, control_code);
}

ULONG WindowsEtwApi::CloseConsumer(TRACEHANDLE handle) noexcept {
    return CloseTrace(handle);
}

bool WindowsEtwSessionResult::ok() const noexcept {
    return error == WindowsEtwSessionError::None;
}

WindowsEtwSession::WindowsEtwSession(EtwApi& api, WindowsEtwSessionOptions options)
    : api_{api}, options_{options}, decoder_{property_reader_}, core_{process_probe_},
      properties_buffer_(PropertiesBufferSize()) {
    PrepareProperties();
}

WindowsEtwSession::~WindowsEtwSession() {
    static_cast<void>(Stop());
}

WindowsEtwSessionResult WindowsEtwSession::Start() noexcept {
    const std::scoped_lock lock{lifecycle_mutex_};
    if (running_) {
        return {};
    }
    {
        const std::scoped_lock core_lock{core_mutex_};
        core_.SetState(CollectorState::Starting);
    }
    stop_requested_.store(false);
    fatal_callback_.store(false);
    latest_log_buffers_lost_.store(0U);
    latest_realtime_buffers_lost_.store(0U);
    tcp_version_mask_.store(0U);
    tcp_opcode_mask_.store(0U);
    tcp_version2_opcode_mask_.store(0U);
    tcp_version3_opcode_mask_.store(0U);
    tcp_version4_opcode_mask_.store(0U);
    udp_version_mask_.store(0U);
    udp_opcode_mask_.store(0U);
    udp_version2_opcode_mask_.store(0U);
    udp_version3_opcode_mask_.store(0U);
    udp_version4_opcode_mask_.store(0U);
    process_version_mask_.store(0U);
    process_opcode_mask_.store(0U);
    process_version2_opcode_mask_.store(0U);
    process_version3_opcode_mask_.store(0U);
    process_version4_opcode_mask_.store(0U);
    statistics_ = {};
    PrepareProperties();

    ULONG status =
        api_.StartController(&controller_handle_, kSessionName, Properties(properties_buffer_));
    if (status == ERROR_ALREADY_EXISTS &&
        options_.existing_session_policy == ExistingSessionPolicy::StopAndRetry) {
        const ULONG recovery = api_.ControlController(
            0U, kSessionName, Properties(properties_buffer_), EVENT_TRACE_CONTROL_STOP);
        if (recovery != ERROR_SUCCESS && recovery != ERROR_WMI_INSTANCE_NOT_FOUND) {
            std::scoped_lock core_lock{core_mutex_};
            core_.RecordExternalError({.domain = CollectorErrorDomain::Etw,
                                       .stage = CollectorErrorStage::StopSession,
                                       .code = recovery},
                                      false, false, false);
            return {.error = WindowsEtwSessionError::StopFailed, .win32_error = recovery};
        }
        PrepareProperties();
        status =
            api_.StartController(&controller_handle_, kSessionName, Properties(properties_buffer_));
    }
    if (status != ERROR_SUCCESS) {
        std::scoped_lock core_lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Etw,
                                   .stage = CollectorErrorStage::StartSession,
                                   .code = status},
                                  true, false, false);
        return {.error = ClassifyStartError(status), .win32_error = status};
    }

    logfile_ = {};
    logfile_.LoggerName = const_cast<LPWSTR>(kSessionName);
    logfile_.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile_.EventRecordCallback = &WindowsEtwSession::EventRecordCallback;
    logfile_.BufferCallback = &WindowsEtwSession::BufferCallback;
    logfile_.Context = this;
    consumer_handle_ = api_.OpenConsumer(&logfile_);
    if (consumer_handle_ == INVALID_PROCESSTRACE_HANDLE) {
        const auto error = GetLastError();
        static_cast<void>(StopPartial());
        std::scoped_lock core_lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Win32,
                                   .stage = CollectorErrorStage::OpenConsumer,
                                   .code = error},
                                  true, false, false);
        return {.error = WindowsEtwSessionError::OpenFailed, .win32_error = error};
    }

    if (options_.simulate_worker_start_failure) {
        static_cast<void>(StopPartial());
        std::scoped_lock core_lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Internal,
                                   .stage = CollectorErrorStage::ProcessTrace,
                                   .code = ERROR_NOT_ENOUGH_MEMORY},
                                  true, false, false);
        return {.error = WindowsEtwSessionError::WorkerStartFailed,
                .win32_error = ERROR_NOT_ENOUGH_MEMORY};
    }
    try {
        const TRACEHANDLE processing_handle = consumer_handle_;
        worker_ = std::jthread{[this, processing_handle] { ProcessWorker(processing_handle); }};
    } catch (const std::system_error& error) {
        static_cast<void>(StopPartial());
        const auto error_code = static_cast<std::uint32_t>(error.code().value());
        std::scoped_lock core_lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Internal,
                                   .stage = CollectorErrorStage::ProcessTrace,
                                   .code = error_code},
                                  true, false, false);
        return {.error = WindowsEtwSessionError::WorkerStartFailed, .win32_error = error_code};
    } catch (...) {
        static_cast<void>(StopPartial());
        std::scoped_lock core_lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Internal,
                                   .stage = CollectorErrorStage::ProcessTrace,
                                   .code = ERROR_NOT_ENOUGH_MEMORY},
                                  true, false, false);
        return {.error = WindowsEtwSessionError::WorkerStartFailed,
                .win32_error = ERROR_NOT_ENOUGH_MEMORY};
    }
    running_ = true;
    {
        const std::scoped_lock core_lock{core_mutex_};
        core_.SetState(CollectorState::Running);
    }
    return {};
}

WindowsEtwSessionResult WindowsEtwSession::FlushAndQuery() noexcept {
    const std::scoped_lock lock{lifecycle_mutex_};
    if (!running_) {
        return {.error = WindowsEtwSessionError::NotRunning};
    }
    PrepareProperties();
    ULONG status =
        api_.ControlController(controller_handle_, kSessionName, Properties(properties_buffer_),
                               EVENT_TRACE_CONTROL_FLUSH);
    if (status != ERROR_SUCCESS) {
        const std::scoped_lock core_lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Etw,
                                   .stage = CollectorErrorStage::FlushSession,
                                   .code = status},
                                  false, false, false);
        return {.error = WindowsEtwSessionError::FlushFailed, .win32_error = status};
    }
    PrepareProperties();
    status = api_.ControlController(controller_handle_, kSessionName,
                                    Properties(properties_buffer_), EVENT_TRACE_CONTROL_QUERY);
    if (status != ERROR_SUCCESS) {
        const std::scoped_lock core_lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Etw,
                                   .stage = CollectorErrorStage::QuerySession,
                                   .code = status},
                                  false, false, false);
        return {.error = WindowsEtwSessionError::QueryFailed, .win32_error = status};
    }
    const auto* properties = Properties(properties_buffer_);
    statistics_.number_of_buffers = properties->NumberOfBuffers;
    statistics_.buffer_size_kib = properties->BufferSize;
    statistics_.loss = {.events_lost = properties->EventsLost,
                        .log_buffers_lost = properties->LogBuffersLost,
                        .realtime_buffers_lost = properties->RealTimeBuffersLost};
    latest_log_buffers_lost_.store(statistics_.loss.log_buffers_lost);
    latest_realtime_buffers_lost_.store(statistics_.loss.realtime_buffers_lost);
    const std::scoped_lock core_lock{core_mutex_};
    core_.UpdateEtwStatistics(statistics_.loss);
    return {};
}

WindowsEtwSessionResult WindowsEtwSession::Stop() noexcept {
    const std::scoped_lock lock{lifecycle_mutex_};
    if (!running_ && controller_handle_ == 0U && consumer_handle_ == INVALID_PROCESSTRACE_HANDLE) {
        return {};
    }
    stop_requested_.store(true);
    ULONG first_error = ERROR_SUCCESS;
    if (controller_handle_ != 0U) {
        PrepareProperties();
        const ULONG status =
            api_.ControlController(controller_handle_, kSessionName, Properties(properties_buffer_),
                                   EVENT_TRACE_CONTROL_STOP);
        if (status != ERROR_SUCCESS && status != ERROR_WMI_INSTANCE_NOT_FOUND) {
            first_error = status;
        }
        controller_handle_ = 0U;
    }
    if (consumer_handle_ != INVALID_PROCESSTRACE_HANDLE) {
        const ULONG status = api_.CloseConsumer(consumer_handle_);
        if (status != ERROR_SUCCESS && status != ERROR_CTX_CLOSE_PENDING &&
            first_error == ERROR_SUCCESS) {
            first_error = status;
        }
        consumer_handle_ = INVALID_PROCESSTRACE_HANDLE;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    running_ = false;
    std::scoped_lock core_lock{core_mutex_};
    if (first_error != ERROR_SUCCESS) {
        core_.RecordExternalError({.domain = CollectorErrorDomain::Etw,
                                   .stage = CollectorErrorStage::StopSession,
                                   .code = first_error},
                                  false, false, false);
        return {.error = WindowsEtwSessionError::StopFailed, .win32_error = first_error};
    }
    core_.SetState(CollectorState::Stopped);
    return {};
}

CollectorHealth WindowsEtwSession::health() const noexcept {
    std::scoped_lock lock{core_mutex_};
    return core_.health();
}

std::size_t WindowsEtwSession::active_process_count() const noexcept {
    std::scoped_lock lock{core_mutex_};
    return core_.active_process_count();
}

WindowsEtwSessionStatistics WindowsEtwSession::statistics() const noexcept {
    std::scoped_lock lock{lifecycle_mutex_};
    return statistics_;
}

WindowsEtwSchemaObservations WindowsEtwSession::observed_schemas() const noexcept {
    return {
        .tcp = {.version_mask = tcp_version_mask_.load(),
                .opcode_mask = tcp_opcode_mask_.load(),
                .version2_opcode_mask = tcp_version2_opcode_mask_.load(),
                .version3_opcode_mask = tcp_version3_opcode_mask_.load(),
                .version4_opcode_mask = tcp_version4_opcode_mask_.load()},
        .udp = {.version_mask = udp_version_mask_.load(),
                .opcode_mask = udp_opcode_mask_.load(),
                .version2_opcode_mask = udp_version2_opcode_mask_.load(),
                .version3_opcode_mask = udp_version3_opcode_mask_.load(),
                .version4_opcode_mask = udp_version4_opcode_mask_.load()},
        .process = {.version_mask = process_version_mask_.load(),
                    .opcode_mask = process_opcode_mask_.load(),
                    .version2_opcode_mask = process_version2_opcode_mask_.load(),
                    .version3_opcode_mask = process_version3_opcode_mask_.load(),
                    .version4_opcode_mask = process_version4_opcode_mask_.load()},
    };
}

ranking::CounterSnapshot WindowsEtwSession::MakeSnapshot(
    std::chrono::steady_clock::time_point captured_at) const {
    std::scoped_lock lock{core_mutex_};
    return core_.MakeSnapshot(captured_at);
}

void WINAPI WindowsEtwSession::EventRecordCallback(PEVENT_RECORD record) noexcept {
    if (record == nullptr || record->UserContext == nullptr) {
        return;
    }
    static_cast<WindowsEtwSession*>(record->UserContext)->OnEvent(*record);
}

ULONG WINAPI WindowsEtwSession::BufferCallback(PEVENT_TRACE_LOGFILEW logfile) noexcept {
    if (logfile == nullptr || logfile->Context == nullptr) {
        return FALSE;
    }
    return static_cast<WindowsEtwSession*>(logfile->Context)->OnBuffer(*logfile);
}

void WindowsEtwSession::ProcessWorker(TRACEHANDLE processing_handle) noexcept {
    const ULONG status = api_.ProcessConsumer(&processing_handle);
    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED && status != ERROR_CTX_CLOSE_PENDING) {
        std::scoped_lock lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Etw,
                                   .stage = CollectorErrorStage::ProcessTrace,
                                   .code = status},
                                  true, false, true);
    }
}

void WindowsEtwSession::OnEvent(const EVENT_RECORD& record) noexcept {
    try {
        if (IsEqualGUID(record.EventHeader.ProviderId, TcpIpProviderId()) != FALSE) {
            RecordSchema({tcp_version_mask_, tcp_opcode_mask_, tcp_version2_opcode_mask_,
                          tcp_version3_opcode_mask_, tcp_version4_opcode_mask_},
                         record.EventHeader.EventDescriptor);
        } else if (IsEqualGUID(record.EventHeader.ProviderId, UdpIpProviderId()) != FALSE) {
            RecordSchema({udp_version_mask_, udp_opcode_mask_, udp_version2_opcode_mask_,
                          udp_version3_opcode_mask_, udp_version4_opcode_mask_},
                         record.EventHeader.EventDescriptor);
        } else if (IsEqualGUID(record.EventHeader.ProviderId, ProcessProviderId()) != FALSE) {
            RecordSchema({process_version_mask_, process_opcode_mask_,
                          process_version2_opcode_mask_, process_version3_opcode_mask_,
                          process_version4_opcode_mask_},
                         record.EventHeader.EventDescriptor);
        }
        if (options_.simulate_callback_failure) {
            throw std::bad_alloc{};
        }
        const auto decoded = decoder_.Decode(record);
        std::scoped_lock lock{core_mutex_};
        if (decoded.status == WindowsEtwDecodeStatus::Ignored) {
            core_.RecordEtwEvent(CollectorEventDisposition::Ignored);
            return;
        }
        if (decoded.status == WindowsEtwDecodeStatus::Rejected) {
            const bool unsupported = decoded.error == WindowsEtwDecodeError::UnsupportedVersion;
            core_.RecordEtwEvent(unsupported ? CollectorEventDisposition::UnsupportedSchema
                                             : CollectorEventDisposition::Malformed);
            return;
        }
        core_.RecordEtwEvent(CollectorEventDisposition::Decoded);
        if (!decoded.event.has_value()) {
            core_.RecordExternalError({.domain = CollectorErrorDomain::Internal,
                                       .stage = CollectorErrorStage::EventCallback,
                                       .code = ERROR_INVALID_DATA},
                                      true, true, true);
            fatal_callback_.store(true);
            return;
        }
        const auto& event = decoded.event.value();
        if (const auto* network = std::get_if<NetworkTrafficEvent>(&event)) {
            static_cast<void>(core_.Consume(*network));
        } else if (const auto* start = std::get_if<process::ProcessStartObservation>(&event)) {
            static_cast<void>(core_.Consume(*start));
        } else if (const auto* end = std::get_if<process::ProcessEndObservation>(&event)) {
            static_cast<void>(core_.Consume(*end));
        }
    } catch (...) {
        fatal_callback_.store(true);
        std::scoped_lock lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Callback,
                                   .stage = CollectorErrorStage::EventCallback,
                                   .code = ERROR_UNHANDLED_EXCEPTION},
                                  true, true, true);
    }
}

ULONG WindowsEtwSession::OnBuffer(const EVENT_TRACE_LOGFILEW& logfile) noexcept {
    try {
        ULARGE_INTEGER ticks{};
        ticks.QuadPart = static_cast<ULONGLONG>(logfile.CurrentTime);
        FILETIME file_time{.dwLowDateTime = ticks.LowPart, .dwHighDateTime = ticks.HighPart};
        if (const auto watermark = process::FileTimeToTraceTimePoint(file_time);
            watermark.has_value()) {
            std::scoped_lock lock{core_mutex_};
            static_cast<void>(core_.AdvanceWatermark(watermark.value()));
            core_.UpdateEtwStatistics(
                {.events_lost = logfile.EventsLost,
                 .log_buffers_lost = latest_log_buffers_lost_.load(),
                 .realtime_buffers_lost = latest_realtime_buffers_lost_.load()});
        }
    } catch (...) {
        fatal_callback_.store(true);
        std::scoped_lock lock{core_mutex_};
        core_.RecordExternalError({.domain = CollectorErrorDomain::Callback,
                                   .stage = CollectorErrorStage::BufferCallback,
                                   .code = ERROR_UNHANDLED_EXCEPTION},
                                  true, true, true);
    }
    return stop_requested_.load() || fatal_callback_.load() ? FALSE : TRUE;
}

void WindowsEtwSession::PrepareProperties() noexcept {
    std::ranges::fill(properties_buffer_, std::byte{});
    auto* properties = Properties(properties_buffer_);
    properties->Wnode.BufferSize = static_cast<ULONG>(properties_buffer_.size());
    properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    properties->Wnode.ClientContext = 1U;
    properties->Wnode.Guid = kSessionId;
    properties->BufferSize = 64U;
    properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
    properties->FlushTimer = 1U;
    properties->EnableFlags = EVENT_TRACE_FLAG_NETWORK_TCPIP | EVENT_TRACE_FLAG_PROCESS;
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    std::memcpy(properties_buffer_.data() + properties->LoggerNameOffset, kSessionName,
                sizeof(kSessionName));
}

WindowsEtwSessionResult WindowsEtwSession::StopPartial() noexcept {
    if (consumer_handle_ != INVALID_PROCESSTRACE_HANDLE) {
        static_cast<void>(api_.CloseConsumer(consumer_handle_));
        consumer_handle_ = INVALID_PROCESSTRACE_HANDLE;
    }
    if (controller_handle_ != 0U) {
        PrepareProperties();
        static_cast<void>(api_.ControlController(controller_handle_, kSessionName,
                                                 Properties(properties_buffer_),
                                                 EVENT_TRACE_CONTROL_STOP));
        controller_handle_ = 0U;
    }
    return {};
}

const wchar_t* WindowsEtwSessionName() noexcept {
    return kSessionName;
}

const GUID& WindowsEtwSessionId() noexcept {
    return kSessionId;
}

} // namespace nfm::collector
