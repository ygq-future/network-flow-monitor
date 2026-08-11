#include "network_flow_monitor/collector/windows_etw_session.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <semaphore>
#include <string>
#include <string_view>

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

class FakeEtwApi final : public nfm::collector::EtwApi {
  public:
    ULONG StartController(PTRACEHANDLE handle, LPCWSTR name,
                          PEVENT_TRACE_PROPERTIES properties) noexcept override {
        ++start_calls;
        captured_name = name != nullptr ? name : L"";
        captured_properties = *properties;
        const auto* logger_name = reinterpret_cast<const wchar_t*>(
            reinterpret_cast<const std::byte*>(properties) + properties->LoggerNameOffset);
        embedded_name = logger_name;
        const ULONG status = start_calls == 1U ? first_start_status : retry_start_status;
        if (status == ERROR_SUCCESS) {
            *handle = controller_value;
        }
        return status;
    }

    TRACEHANDLE OpenConsumer(PEVENT_TRACE_LOGFILEW logfile) noexcept override {
        ++open_calls;
        captured_logfile = *logfile;
        if (open_status == INVALID_PROCESSTRACE_HANDLE) {
            SetLastError(open_error);
        }
        return open_status;
    }

    ULONG ProcessConsumer(PTRACEHANDLE handle) noexcept override {
        ++process_calls;
        processed_handle = *handle;
        if (invoke_event_callback && captured_logfile.EventRecordCallback != nullptr) {
            EVENT_RECORD record{};
            record.EventHeader.ProviderId = nfm::collector::TcpIpProviderId();
            record.EventHeader.EventDescriptor.Version = 2U;
            record.EventHeader.EventDescriptor.Opcode = 10U;
            record.UserContext = captured_logfile.Context;
            captured_logfile.EventRecordCallback(&record);
        }
        if (invoke_buffer_callback && captured_logfile.BufferCallback != nullptr) {
            captured_logfile.CurrentTime = 116'444'736'000'000'000LL;
            captured_logfile.EventsLost = 2U;
            buffer_result = captured_logfile.BufferCallback(&captured_logfile);
            buffer_callback_done.release();
        }
        return process_status;
    }

    ULONG ControlController(TRACEHANDLE handle, LPCWSTR name, PEVENT_TRACE_PROPERTIES properties,
                            ULONG control_code) noexcept override {
        static_cast<void>(handle);
        static_cast<void>(name);
        if (control_code == EVENT_TRACE_CONTROL_STOP) {
            ++stop_calls;
            return stop_status;
        }
        if (control_code == EVENT_TRACE_CONTROL_FLUSH) {
            ++flush_calls;
            return flush_status;
        }
        if (control_code == EVENT_TRACE_CONTROL_QUERY) {
            ++query_calls;
            if (query_status == ERROR_SUCCESS) {
                properties->NumberOfBuffers = 7U;
                properties->BufferSize = 64U;
                properties->EventsLost = 3U;
                properties->LogBuffersLost = 4U;
                properties->RealTimeBuffersLost = 5U;
            }
            return query_status;
        }
        return ERROR_INVALID_FUNCTION;
    }

    ULONG CloseConsumer(TRACEHANDLE handle) noexcept override {
        ++close_calls;
        closed_handle = handle;
        return close_status;
    }

    ULONG first_start_status{ERROR_SUCCESS};
    ULONG retry_start_status{ERROR_SUCCESS};
    TRACEHANDLE controller_value{55U};
    TRACEHANDLE open_status{77U};
    DWORD open_error{ERROR_INVALID_HANDLE};
    ULONG process_status{ERROR_SUCCESS};
    ULONG flush_status{ERROR_SUCCESS};
    ULONG query_status{ERROR_SUCCESS};
    ULONG stop_status{ERROR_SUCCESS};
    ULONG close_status{ERROR_SUCCESS};
    bool invoke_event_callback{};
    bool invoke_buffer_callback{};
    std::atomic_uint32_t start_calls{};
    std::atomic_uint32_t open_calls{};
    std::atomic_uint32_t process_calls{};
    std::atomic_uint32_t stop_calls{};
    std::atomic_uint32_t flush_calls{};
    std::atomic_uint32_t query_calls{};
    std::atomic_uint32_t close_calls{};
    std::wstring captured_name;
    std::wstring embedded_name;
    EVENT_TRACE_PROPERTIES captured_properties{};
    EVENT_TRACE_LOGFILEW captured_logfile{};
    TRACEHANDLE processed_handle{};
    TRACEHANDLE closed_handle{};
    ULONG buffer_result{};
    std::binary_semaphore buffer_callback_done{0};
};

using nfm::collector::ExistingSessionPolicy;
using nfm::collector::WindowsEtwSession;
using nfm::collector::WindowsEtwSessionError;

void CheckConfigurationLifecycleAndStatistics(TestContext& context) {
    FakeEtwApi api;
    api.close_status = ERROR_CTX_CLOSE_PENDING;
    api.invoke_event_callback = true;
    api.invoke_buffer_callback = true;
    WindowsEtwSession session{api};
    context.Check(session.Start().ok(), "session starts");
    api.buffer_callback_done.acquire();
    context.Check(api.buffer_result == TRUE, "buffer callback continues while session is healthy");
    context.Check(api.captured_name == nfm::collector::WindowsEtwSessionName() &&
                      api.embedded_name == nfm::collector::WindowsEtwSessionName(),
                  "fixed name stored in property buffer");
    const auto& properties = api.captured_properties;
    context.Check(
        properties.Wnode.BufferSize ==
                sizeof(EVENT_TRACE_PROPERTIES) +
                    (std::wcslen(nfm::collector::WindowsEtwSessionName()) + 1U) * sizeof(wchar_t) &&
            properties.Wnode.ClientContext == 1U &&
            IsEqualGUID(properties.Wnode.Guid, nfm::collector::WindowsEtwSessionId()) != FALSE,
        "property buffer size, QPC clock, and session GUID configured");
    context.Check((properties.LogFileMode & EVENT_TRACE_SYSTEM_LOGGER_MODE) != 0U &&
                      (properties.LogFileMode & EVENT_TRACE_REAL_TIME_MODE) != 0U &&
                      properties.BufferSize == 64U && properties.FlushTimer == 1U &&
                      properties.LogFileNameOffset == 0U,
                  "system realtime 64 KiB controller configured without file");
    context.Check((properties.EnableFlags & EVENT_TRACE_FLAG_NETWORK_TCPIP) != 0U &&
                      (properties.EnableFlags & EVENT_TRACE_FLAG_PROCESS) != 0U,
                  "network and process flags enabled");
    context.Check(
        (api.captured_logfile.ProcessTraceMode & PROCESS_TRACE_MODE_REAL_TIME) != 0U &&
            (api.captured_logfile.ProcessTraceMode & PROCESS_TRACE_MODE_EVENT_RECORD) != 0U &&
            (api.captured_logfile.ProcessTraceMode & PROCESS_TRACE_MODE_RAW_TIMESTAMP) == 0U &&
            api.captured_logfile.Context == &session,
        "consumer callback context and timestamp mode configured");
    context.Check(session.FlushAndQuery().ok(), "flush and query succeed");
    const auto stats = session.statistics();
    context.Check(stats.number_of_buffers == 7U && stats.buffer_size_kib == 64U &&
                      stats.loss.events_lost == 3U && stats.loss.log_buffers_lost == 4U &&
                      stats.loss.realtime_buffers_lost == 5U,
                  "query statistics retained");
    context.Check(session.Stop().ok() && session.Stop().ok(),
                  "pending close and repeated Stop are successful");
    context.Check(api.stop_calls == 1U && api.close_calls == 1U && api.closed_handle == 77U &&
                      api.processed_handle == 77U,
                  "controller and consumer ownership kept distinct");
    const auto health = session.health();
    context.Check(health.malformed_events == 1U && health.events_received == 1U,
                  "callback result classified");
    const auto schemas = session.observed_schemas();
    context.Check(
        schemas.tcp.version_mask == (1U << 2U) && schemas.tcp.opcode_mask == (1U << 10U) &&
            schemas.tcp.version2_opcode_mask == (1U << 10U) &&
            schemas.tcp.version3_opcode_mask == 0U && schemas.tcp.version4_opcode_mask == 0U,
        "observed provider schema retained without payload data");
}

void CheckStartFailuresAndRecovery(TestContext& context) {
    FakeEtwApi denied;
    denied.first_start_status = ERROR_ACCESS_DENIED;
    WindowsEtwSession denied_session{denied};
    context.Check(denied_session.Start().error == WindowsEtwSessionError::AccessDenied,
                  "access denial classified");

    FakeEtwApi conflict;
    conflict.first_start_status = ERROR_ALREADY_EXISTS;
    WindowsEtwSession conflict_session{conflict};
    context.Check(conflict_session.Start().error == WindowsEtwSessionError::AlreadyExists &&
                      conflict.stop_calls == 0U,
                  "existing session is not stolen by default");

    FakeEtwApi recovery;
    recovery.first_start_status = ERROR_ALREADY_EXISTS;
    WindowsEtwSession recovery_session{
        recovery, {.existing_session_policy = ExistingSessionPolicy::StopAndRetry}};
    context.Check(recovery_session.Start().ok(), "explicit recovery retries start");
    context.Check(recovery_session.Stop().ok() && recovery.start_calls == 2U &&
                      recovery.stop_calls == 2U,
                  "explicit recovery alone stops the conflicting session");

    FakeEtwApi open_failure;
    open_failure.open_status = INVALID_PROCESSTRACE_HANDLE;
    WindowsEtwSession open_session{open_failure};
    context.Check(open_session.Start().error == WindowsEtwSessionError::OpenFailed &&
                      open_failure.stop_calls == 1U,
                  "OpenTrace failure cleans controller");

    FakeEtwApi worker_failure;
    WindowsEtwSession worker_session{worker_failure, {.simulate_worker_start_failure = true}};
    context.Check(worker_session.Start().error == WindowsEtwSessionError::WorkerStartFailed &&
                      worker_failure.close_calls == 1U && worker_failure.stop_calls == 1U,
                  "worker failure cleans consumer then controller");
}

void CheckRuntimeFailuresAndDestructor(TestContext& context) {
    FakeEtwApi flush_failure;
    flush_failure.flush_status = ERROR_WRITE_FAULT;
    WindowsEtwSession flush_session{flush_failure};
    context.Check(flush_session.Start().ok() &&
                      flush_session.FlushAndQuery().error == WindowsEtwSessionError::FlushFailed,
                  "flush failure classified");
    context.Check(flush_session.Stop().ok(), "flush-failure session stops");

    FakeEtwApi query_failure;
    query_failure.query_status = ERROR_INVALID_DATA;
    WindowsEtwSession query_session{query_failure};
    context.Check(query_session.Start().ok() &&
                      query_session.FlushAndQuery().error == WindowsEtwSessionError::QueryFailed,
                  "query failure classified");
    context.Check(query_session.Stop().ok(), "query-failure session stops");

    FakeEtwApi process_failure;
    process_failure.process_status = ERROR_INVALID_FUNCTION;
    WindowsEtwSession process_session{process_failure};
    context.Check(process_session.Start().ok() && process_session.Stop().ok(),
                  "ProcessTrace failure session joins");
    context.Check(process_session.health().unexpected_failures == 1U,
                  "ProcessTrace failure retained");

    FakeEtwApi callback_failure;
    callback_failure.invoke_event_callback = true;
    callback_failure.invoke_buffer_callback = true;
    WindowsEtwSession callback_session{callback_failure, {.simulate_callback_failure = true}};
    context.Check(callback_session.Start().ok() && callback_session.Stop().ok(),
                  "callback failure session stops");
    context.Check(callback_session.health().callback_failures == 1U,
                  "callback exception contained and retained");
    context.Check(callback_failure.buffer_result == FALSE,
                  "fatal callback requests ProcessTrace termination at next buffer");

    FakeEtwApi stop_failure;
    stop_failure.stop_status = ERROR_WRITE_FAULT;
    WindowsEtwSession stop_session{stop_failure};
    context.Check(stop_session.Start().ok() &&
                      stop_session.Stop().error == WindowsEtwSessionError::StopFailed,
                  "stop failure classified after cleanup");

    FakeEtwApi destructor_api;
    {
        WindowsEtwSession destructor_session{destructor_api};
        context.Check(destructor_session.Start().ok(), "destructor session starts");
    }
    context.Check(destructor_api.stop_calls == 1U && destructor_api.close_calls == 1U,
                  "destructor performs cleanup");
}

} // namespace

int main() {
    TestContext context;
    CheckConfigurationLifecycleAndStatistics(context);
    CheckStartFailuresAndRecovery(context);
    CheckRuntimeFailuresAndDestructor(context);
    return context.failures();
}
