#ifndef NETWORK_FLOW_MONITOR_IPC_NAMED_PIPE_TRANSPORT_H
#define NETWORK_FLOW_MONITOR_IPC_NAMED_PIPE_TRANSPORT_H

#include "network_flow_monitor/ipc/security_policy.h"
#include "network_flow_monitor/ipc/windows_pipe_security.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace nfm::ipc {

inline constexpr DWORD kMaximumPipeInstances = 4U;
inline constexpr auto kPipeBusyWait = std::chrono::milliseconds{250};
inline constexpr DWORD kPipeBufferBytes = 64U * 1024U;

enum class PipeOperationKind : std::uint8_t {
    Connect,
    Read,
    Write,
};

enum class PipeIoState : std::uint8_t {
    Idle,
    Pending,
    Completed,
    CancelRequested,
};

enum class PipeIoError : std::uint8_t {
    None,
    Busy,
    Disconnected,
    Timeout,
    InvalidState,
    ZeroProgress,
    UnexpectedMessageMode,
    WindowsFailure,
};

struct PipeIoResult {
    PipeIoError error{PipeIoError::None};
    std::uint32_t win32_error{};
    std::size_t transferred{};
    bool pending{};

    [[nodiscard]] bool ok() const noexcept;
};

struct ServerPipeOpenRequest {
    LPCWSTR name{};
    DWORD open_mode{};
    DWORD pipe_mode{};
    DWORD maximum_instances{};
    SECURITY_ATTRIBUTES* security{};
};

struct ClientPipeOpenRequest {
    LPCWSTR name{};
    DWORD desired_access{};
    DWORD share_mode{};
    SECURITY_ATTRIBUTES* security{};
    DWORD creation_disposition{};
    DWORD flags_and_attributes{};
};

struct PipeReadRequest {
    HANDLE pipe{};
    void* destination{};
    DWORD length{};
    OVERLAPPED* operation{};
};

struct PipeWriteRequest {
    HANDLE pipe{};
    const void* source{};
    DWORD length{};
    OVERLAPPED* operation{};
};

struct PipeBeginOutcome {
    bool succeeded{};
    std::uint32_t error{};
    DWORD transferred{};
};

struct PipeApiCallResult {
    bool succeeded{};
    std::uint32_t error{};
    DWORD transferred{};
};

class PipeTransportApi {
  public:
    PipeTransportApi() = default;
    PipeTransportApi(const PipeTransportApi&) = delete;
    PipeTransportApi& operator=(const PipeTransportApi&) = delete;
    PipeTransportApi(PipeTransportApi&&) = delete;
    PipeTransportApi& operator=(PipeTransportApi&&) = delete;
    virtual ~PipeTransportApi() = default;

    [[nodiscard]] virtual HANDLE CreateManualResetEvent(bool inherit_handle,
                                                        std::uint32_t& error) noexcept = 0;
    [[nodiscard]] virtual bool ResetManualEvent(HANDLE event, std::uint32_t& error) noexcept = 0;
    virtual void CloseKernelHandle(HANDLE handle) noexcept = 0;
    [[nodiscard]] virtual HANDLE CreateServerPipe(const ServerPipeOpenRequest& request,
                                                  std::uint32_t& error) noexcept = 0;
    [[nodiscard]] virtual HANDLE OpenClientPipe(const ClientPipeOpenRequest& request,
                                                std::uint32_t& error) noexcept = 0;
    [[nodiscard]] virtual bool WaitForPipe(LPCWSTR name, DWORD timeout_ms,
                                           std::uint32_t& error) noexcept = 0;
    [[nodiscard]] virtual bool Connect(HANDLE pipe, OVERLAPPED* operation,
                                       std::uint32_t& error) noexcept = 0;
    [[nodiscard]] virtual PipeApiCallResult Read(const PipeReadRequest& request) noexcept = 0;
    [[nodiscard]] virtual PipeApiCallResult Write(const PipeWriteRequest& request) noexcept = 0;
    [[nodiscard]] virtual bool GetOperationResult(HANDLE pipe, OVERLAPPED* operation,
                                                  DWORD& transferred, bool wait,
                                                  std::uint32_t& error) noexcept = 0;
    [[nodiscard]] virtual bool Cancel(HANDLE pipe, OVERLAPPED* operation,
                                      std::uint32_t& error) noexcept = 0;
    [[nodiscard]] virtual bool Disconnect(HANDLE pipe, std::uint32_t& error) noexcept = 0;
};

class WindowsPipeTransportApi final : public PipeTransportApi {
  public:
    HANDLE CreateManualResetEvent(bool inherit_handle, std::uint32_t& error) noexcept override;
    bool ResetManualEvent(HANDLE event, std::uint32_t& error) noexcept override;
    void CloseKernelHandle(HANDLE handle) noexcept override;
    HANDLE CreateServerPipe(const ServerPipeOpenRequest& request,
                            std::uint32_t& error) noexcept override;
    HANDLE OpenClientPipe(const ClientPipeOpenRequest& request,
                          std::uint32_t& error) noexcept override;
    bool WaitForPipe(LPCWSTR name, DWORD timeout_ms, std::uint32_t& error) noexcept override;
    bool Connect(HANDLE pipe, OVERLAPPED* operation, std::uint32_t& error) noexcept override;
    PipeApiCallResult Read(const PipeReadRequest& request) noexcept override;
    PipeApiCallResult Write(const PipeWriteRequest& request) noexcept override;
    bool GetOperationResult(HANDLE pipe, OVERLAPPED* operation, DWORD& transferred, bool wait,
                            std::uint32_t& error) noexcept override;
    bool Cancel(HANDLE pipe, OVERLAPPED* operation, std::uint32_t& error) noexcept override;
    bool Disconnect(HANDLE pipe, std::uint32_t& error) noexcept override;
};

class OverlappedPipeOperation {
  public:
    OverlappedPipeOperation(PipeTransportApi& api, HANDLE pipe, PipeOperationKind kind) noexcept;
    ~OverlappedPipeOperation();
    OverlappedPipeOperation(const OverlappedPipeOperation&) = delete;
    OverlappedPipeOperation& operator=(const OverlappedPipeOperation&) = delete;
    OverlappedPipeOperation(OverlappedPipeOperation&&) = delete;
    OverlappedPipeOperation& operator=(OverlappedPipeOperation&&) = delete;

    [[nodiscard]] PipeIoResult BeginConnect() noexcept;
    [[nodiscard]] PipeIoResult BeginRead(std::span<std::byte> destination) noexcept;
    [[nodiscard]] PipeIoResult BeginWrite(std::span<const std::byte> source) noexcept;
    [[nodiscard]] PipeIoResult Complete() noexcept;
    [[nodiscard]] PipeIoResult RequestCancel() noexcept;
    [[nodiscard]] HANDLE completion_event() const noexcept;
    [[nodiscard]] PipeIoState state() const noexcept;
    [[nodiscard]] PipeIoResult initialization_result() const noexcept;

  private:
    [[nodiscard]] PipeIoResult Prepare() noexcept;
    [[nodiscard]] PipeIoResult FinishBegin(const PipeBeginOutcome& outcome) noexcept;
    void DrainForDestruction() noexcept;

    PipeTransportApi& api_;
    HANDLE pipe_{};
    PipeOperationKind kind_{};
    HANDLE event_{};
    OVERLAPPED operation_{};
    PipeIoState state_{PipeIoState::Idle};
    PipeIoResult initialization_result_{};
    PipeIoResult completed_result_{};
};

class NamedPipeConnection {
  public:
    NamedPipeConnection(PipeTransportApi& api, HANDLE pipe, bool server_end) noexcept;
    ~NamedPipeConnection();
    NamedPipeConnection(const NamedPipeConnection&) = delete;
    NamedPipeConnection& operator=(const NamedPipeConnection&) = delete;
    NamedPipeConnection(NamedPipeConnection&&) = delete;
    NamedPipeConnection& operator=(NamedPipeConnection&&) = delete;

    [[nodiscard]] PipeIoResult BeginConnect() noexcept;
    [[nodiscard]] PipeIoResult ContinueConnect() noexcept;
    [[nodiscard]] PipeIoResult BeginReadExact(std::span<std::byte> destination) noexcept;
    [[nodiscard]] PipeIoResult ContinueReadExact() noexcept;
    [[nodiscard]] PipeIoResult BeginWriteAll(std::span<const std::byte> source) noexcept;
    [[nodiscard]] PipeIoResult ContinueWriteAll() noexcept;
    [[nodiscard]] PipeIoResult CancelAndDrain() noexcept;
    [[nodiscard]] PipeIoResult initialization_result() const noexcept;
    [[nodiscard]] HANDLE native_handle() const noexcept;
    [[nodiscard]] HANDLE connect_event() const noexcept;
    [[nodiscard]] HANDLE read_event() const noexcept;
    [[nodiscard]] HANDLE write_event() const noexcept;

  private:
    [[nodiscard]] PipeIoResult AdvanceRead(PipeIoResult operation_result) noexcept;
    [[nodiscard]] PipeIoResult AdvanceWrite(PipeIoResult operation_result) noexcept;
    [[nodiscard]] PipeIoResult StartNextRead() noexcept;
    [[nodiscard]] PipeIoResult StartNextWrite() noexcept;
    [[nodiscard]] static PipeIoResult DrainOperation(OverlappedPipeOperation& operation) noexcept;
    void ResetRead() noexcept;
    void ResetWrite() noexcept;

    PipeTransportApi& api_;
    HANDLE pipe_{};
    bool server_end_{};
    OverlappedPipeOperation connect_operation_;
    OverlappedPipeOperation read_operation_;
    OverlappedPipeOperation write_operation_;
    PipeIoResult initialization_result_{};
    std::vector<std::byte> read_buffer_;
    std::span<std::byte> read_destination_;
    std::size_t read_offset_{};
    bool read_active_{};
    std::vector<std::byte> write_buffer_;
    std::size_t write_offset_{};
    bool write_active_{};
};

struct PipeConnectionResult {
    PipeIoError error{PipeIoError::None};
    std::uint32_t win32_error{};
    std::unique_ptr<NamedPipeConnection> connection;

    [[nodiscard]] bool ok() const noexcept;
};

class PipeTransportFactory {
  public:
    PipeTransportFactory() = default;
    PipeTransportFactory(const PipeTransportFactory&) = delete;
    PipeTransportFactory& operator=(const PipeTransportFactory&) = delete;
    PipeTransportFactory(PipeTransportFactory&&) = delete;
    PipeTransportFactory& operator=(PipeTransportFactory&&) = delete;
    virtual ~PipeTransportFactory() = default;

    [[nodiscard]] virtual PipeConnectionResult CreateServer(
        const AuthorizedPrincipal& principal, bool acquire_endpoint_ownership) noexcept = 0;
    [[nodiscard]] virtual PipeConnectionResult CreateClient(
        const AuthorizedPrincipal& principal) noexcept = 0;
};

class WindowsPipeTransportFactory final : public PipeTransportFactory {
  public:
    WindowsPipeTransportFactory() noexcept;
    explicit WindowsPipeTransportFactory(PipeTransportApi& api) noexcept;

    PipeConnectionResult CreateServer(const AuthorizedPrincipal& principal,
                                      bool acquire_endpoint_ownership) noexcept override;
    PipeConnectionResult CreateClient(const AuthorizedPrincipal& principal) noexcept override;

  private:
    [[nodiscard]] PipeConnectionResult Adopt(HANDLE pipe, bool server_end) noexcept;

    WindowsPipeTransportApi windows_api_;
    PipeTransportApi* api_{};
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_NAMED_PIPE_TRANSPORT_H
