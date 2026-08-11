#include "network_flow_monitor/ui/collector_process.h"

#include <string>

namespace nfm::ui {
namespace {

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

DWORD WindowsShellExecuteApi::Wait(HANDLE process, DWORD timeout_ms) noexcept {
    return ::WaitForSingleObject(process, timeout_ms);
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

        SHELLEXECUTEINFOW info{.cbSize = sizeof(info),
                               .fMask = SEE_MASK_NOCLOSEPROCESS,
                               .hwnd = nullptr,
                               .lpVerb = L"runas",
                               .lpFile = path.c_str(),
                               .lpParameters = L"--collector",
                               .lpDirectory = nullptr,
                               .nShow = SW_HIDE};
        if (!api_->Execute(info) || info.hProcess == nullptr) {
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
        return {.succeeded = true, .forced_termination = false, .win32_error = ERROR_SUCCESS};
    }
    if (initial == WAIT_FAILED) {
        const auto error = api_->LastError();
        CloseProcess();
        return Failure(error);
    }

    static_cast<void>(api_->RequestStop(process_));
    if (api_->Wait(process_, ToDword(kCollectorGracefulStopTimeout)) == WAIT_OBJECT_0) {
        CloseProcess();
        return {.succeeded = true, .forced_termination = false, .win32_error = ERROR_SUCCESS};
    }

    if (!api_->Terminate(process_)) {
        const auto error = api_->LastError();
        CloseProcess();
        return Failure(error);
    }
    const auto terminated =
        api_->Wait(process_, ToDword(kCollectorTerminateWaitTimeout)) == WAIT_OBJECT_0;
    const auto error = terminated ? ERROR_SUCCESS : api_->LastError();
    CloseProcess();
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

CollectorProcessResult CollectorProcess::Failure(std::uint32_t error) noexcept {
    return {.succeeded = false, .forced_termination = false, .win32_error = error};
}

} // namespace nfm::ui
