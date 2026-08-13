#include "network_flow_monitor/ui/collector_process.h"

#include <atomic>
#include <string>

namespace nfm::ui {
namespace {

std::atomic_uint64_t g_stop_event_sequence{};

[[nodiscard]] std::wstring MakeStopEventName() {
    LARGE_INTEGER counter{};
    if (::QueryPerformanceCounter(&counter) == FALSE) {
        counter.QuadPart = static_cast<LONGLONG>(::GetTickCount64());
    }
    return L"Local\\NetworkFlowMonitor.Stop." + std::to_wstring(::GetCurrentProcessId()) + L"." +
           std::to_wstring(static_cast<std::uint64_t>(counter.QuadPart)) + L"." +
           std::to_wstring(g_stop_event_sequence.fetch_add(1U, std::memory_order_relaxed));
}

[[nodiscard]] DWORD ToDword(std::chrono::milliseconds timeout) noexcept {
    const auto count = timeout.count();
    if (count <= 0) {
        return 0U;
    }
    if (count >= static_cast<std::int64_t>(INFINITE - 1U)) {
        return INFINITE - 1U;
    }
    return static_cast<DWORD>(count);
}

[[nodiscard]] std::wstring CurrentExecutablePath() {
    std::wstring storage(32'768U, L'\0');
    const auto length =
        ::GetModuleFileNameW(nullptr, storage.data(), static_cast<DWORD>(storage.size()));
    if (length == 0U || length >= storage.size()) {
        return {};
    }
    storage.resize(length);
    return storage;
}

} // namespace

bool WindowsShellExecuteApi::Execute(SHELLEXECUTEINFOW& info) noexcept {
    return ::ShellExecuteExW(&info) != FALSE;
}

HANDLE WindowsShellExecuteApi::CreateStopEvent(std::wstring& name) noexcept {
    try {
        name = MakeStopEventName();
        return ::CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    } catch (...) {
        name.clear();
        return nullptr;
    }
}

DWORD WindowsShellExecuteApi::Wait(HANDLE process, DWORD timeout_ms) noexcept {
    return ::WaitForSingleObject(process, timeout_ms);
}

bool WindowsShellExecuteApi::SignalStopEvent(HANDLE event) noexcept {
    return event != nullptr && ::SetEvent(event) != FALSE;
}

bool WindowsShellExecuteApi::RequestStop(HANDLE process) noexcept {
    const auto process_id = ::GetProcessId(process);
    return process_id != 0U && ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process_id) != FALSE;
}

bool WindowsShellExecuteApi::Terminate(HANDLE process) noexcept {
    return ::TerminateProcess(process, ERROR_PROCESS_ABORTED) != FALSE;
}

void WindowsShellExecuteApi::Close(HANDLE process) noexcept {
    if (process != nullptr) {
        static_cast<void>(::CloseHandle(process));
    }
}

void WindowsShellExecuteApi::CloseEvent(HANDLE event) noexcept {
    if (event != nullptr) {
        static_cast<void>(::CloseHandle(event));
    }
}

std::uint32_t WindowsShellExecuteApi::LastError() const noexcept {
    return ::GetLastError();
}

CollectorProcess::~CollectorProcess() {
    static_cast<void>(Stop());
}

CollectorProcessResult CollectorProcess::StartPortable(std::wstring_view executable_path) noexcept {
    if (process_ != nullptr) {
        return Failure(ERROR_ALREADY_INITIALIZED);
    }
    try {
        auto path = std::wstring{executable_path};
        if (path.empty()) {
            path = CurrentExecutablePath();
        }
        if (path.empty()) {
            return Failure(ERROR_FILE_NOT_FOUND);
        }

        std::wstring event_name;
        stop_event_ = api_->CreateStopEvent(event_name);
        if (stop_event_ == nullptr || event_name.empty()) {
            CloseStopEvent();
            return Failure(api_->LastError());
        }
        stop_event_name_ = std::move(event_name);
        const auto parameters = L"--collector --stop-event \"" + stop_event_name_ + L"\"";

        SHELLEXECUTEINFOW info{.cbSize = sizeof(info),
                               .fMask = SEE_MASK_NOCLOSEPROCESS,
                               .hwnd = nullptr,
                               .lpVerb = L"runas",
                               .lpFile = path.c_str(),
                               .lpParameters = parameters.c_str(),
                               .lpDirectory = nullptr,
                               .nShow = SW_HIDE};
        if (!api_->Execute(info) || info.hProcess == nullptr) {
            CloseStopEvent();
            return Failure(api_->LastError());
        }
        process_ = info.hProcess;
        return {.succeeded = true, .forced_termination = false, .win32_error = ERROR_SUCCESS};
    } catch (...) {
        return Failure(ERROR_NOT_ENOUGH_MEMORY);
    }
}

CollectorProcessResult CollectorProcess::Stop() noexcept {
    if (process_ == nullptr) {
        return {.succeeded = true, .forced_termination = false, .win32_error = ERROR_SUCCESS};
    }

    const auto initial = api_->Wait(process_, 0U);
    if (initial == WAIT_OBJECT_0) {
        CloseProcess();
        CloseStopEvent();
        return {.succeeded = true, .forced_termination = false, .win32_error = ERROR_SUCCESS};
    }
    if (initial == WAIT_FAILED) {
        const auto error = api_->LastError();
        CloseProcess();
        CloseStopEvent();
        return Failure(error);
    }

    static_cast<void>(api_->SignalStopEvent(stop_event_));
    static_cast<void>(api_->RequestStop(process_));
    if (api_->Wait(process_, ToDword(kCollectorGracefulStopTimeout)) == WAIT_OBJECT_0) {
        CloseProcess();
        CloseStopEvent();
        return {.succeeded = true, .forced_termination = false, .win32_error = ERROR_SUCCESS};
    }

    if (!api_->Terminate(process_)) {
        const auto error = api_->LastError();
        CloseProcess();
        CloseStopEvent();
        return Failure(error);
    }
    const auto terminated =
        api_->Wait(process_, ToDword(kCollectorTerminateWaitTimeout)) == WAIT_OBJECT_0;
    const auto error = terminated ? ERROR_SUCCESS : api_->LastError();
    CloseProcess();
    CloseStopEvent();
    return {.succeeded = terminated, .forced_termination = true, .win32_error = error};
}

bool CollectorProcess::running() const noexcept {
    return process_ != nullptr && api_->Wait(process_, 0U) == WAIT_TIMEOUT;
}

void CollectorProcess::CloseProcess() noexcept {
    if (process_ != nullptr) {
        api_->Close(process_);
        process_ = nullptr;
    }
}

void CollectorProcess::CloseStopEvent() noexcept {
    if (stop_event_ != nullptr) {
        api_->CloseEvent(stop_event_);
        stop_event_ = nullptr;
    }
    stop_event_name_.clear();
}

CollectorProcessResult CollectorProcess::Failure(std::uint32_t error) noexcept {
    return {.succeeded = false, .forced_termination = false, .win32_error = error};
}

} // namespace nfm::ui
