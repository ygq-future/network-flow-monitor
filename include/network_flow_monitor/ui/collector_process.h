#ifndef NETWORK_FLOW_MONITOR_UI_COLLECTOR_PROCESS_H
#define NETWORK_FLOW_MONITOR_UI_COLLECTOR_PROCESS_H

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <shellapi.h>
#include <string>
#include <string_view>

namespace nfm::ui {

inline constexpr auto kCollectorGracefulStopTimeout = std::chrono::milliseconds{1500};
inline constexpr auto kCollectorTerminateWaitTimeout = std::chrono::milliseconds{500};
inline constexpr auto kAuthorizationProbeTimeout = std::chrono::milliseconds{400};

struct CollectorProcessResult {
    bool succeeded{};
    bool forced_termination{};
    std::uint32_t win32_error{};
};

class ShellExecuteApi {
  public:
    ShellExecuteApi() = default;
    virtual ~ShellExecuteApi() = default;
    ShellExecuteApi(const ShellExecuteApi&) = delete;
    ShellExecuteApi& operator=(const ShellExecuteApi&) = delete;
    ShellExecuteApi(ShellExecuteApi&&) = delete;
    ShellExecuteApi& operator=(ShellExecuteApi&&) = delete;

    virtual bool Execute(SHELLEXECUTEINFOW& info) noexcept = 0;
    [[nodiscard]] virtual HANDLE CreateStopEvent(std::wstring& name) noexcept = 0;
    [[nodiscard]] virtual DWORD Wait(HANDLE process, DWORD timeout_ms) noexcept = 0;
    [[nodiscard]] virtual bool SignalStopEvent(HANDLE event) noexcept = 0;
    [[nodiscard]] virtual bool RequestStop(HANDLE process) noexcept = 0;
    [[nodiscard]] virtual bool Terminate(HANDLE process) noexcept = 0;
    virtual void Close(HANDLE process) noexcept = 0;
    virtual void CloseEvent(HANDLE event) noexcept = 0;
    [[nodiscard]] virtual std::uint32_t LastError() const noexcept = 0;
};

class WindowsShellExecuteApi final : public ShellExecuteApi {
  public:
    bool Execute(SHELLEXECUTEINFOW& info) noexcept override;
    [[nodiscard]] HANDLE CreateStopEvent(std::wstring& name) noexcept override;
    [[nodiscard]] DWORD Wait(HANDLE process, DWORD timeout_ms) noexcept override;
    [[nodiscard]] bool SignalStopEvent(HANDLE event) noexcept override;
    [[nodiscard]] bool RequestStop(HANDLE process) noexcept override;
    [[nodiscard]] bool Terminate(HANDLE process) noexcept override;
    void Close(HANDLE process) noexcept override;
    void CloseEvent(HANDLE event) noexcept override;
    [[nodiscard]] std::uint32_t LastError() const noexcept override;
};

class CollectorProcess final {
  public:
    explicit CollectorProcess(ShellExecuteApi& api) noexcept : api_{&api} {}
    ~CollectorProcess();
    CollectorProcess(const CollectorProcess&) = delete;
    CollectorProcess& operator=(const CollectorProcess&) = delete;
    CollectorProcess(CollectorProcess&&) = delete;
    CollectorProcess& operator=(CollectorProcess&&) = delete;

    [[nodiscard]] CollectorProcessResult StartPortable(std::wstring_view executable_path) noexcept;
    [[nodiscard]] CollectorProcessResult Stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] HANDLE native_handle() const noexcept {
        return process_;
    }
    [[nodiscard]] std::wstring_view stop_event_name() const noexcept {
        return stop_event_name_;
    }

  private:
    void CloseProcess() noexcept;
    void CloseStopEvent() noexcept;
    [[nodiscard]] CollectorProcessResult SpawnPortable(const std::wstring& path,
                                                       const std::wstring& parameters,
                                                       bool elevated) noexcept;
    [[nodiscard]] static CollectorProcessResult Failure(std::uint32_t error) noexcept;

    ShellExecuteApi* api_;
    HANDLE process_{};
    HANDLE stop_event_{};
    std::wstring stop_event_name_;
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_COLLECTOR_PROCESS_H
