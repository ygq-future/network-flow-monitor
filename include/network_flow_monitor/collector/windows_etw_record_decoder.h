#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_ETW_RECORD_DECODER_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_ETW_RECORD_DECODER_H

#include "network_flow_monitor/collector/event_types.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <evntcons.h>
#include <evntrace.h>
#include <optional>
#include <span>
#include <vector>

namespace nfm::collector {

enum class ProcessEtwProperty : std::uint8_t {
    UniqueProcessKey,
    ProcessId,
    ImageFileName,
};

enum class ProcessPropertyReadStatus : std::uint8_t {
    Success,
    NotFound,
    BufferTooSmall,
    Failed,
};

struct ProcessPropertyReadResult {
    ProcessPropertyReadStatus status{ProcessPropertyReadStatus::Success};
    std::size_t bytes_written{};
    std::size_t required_size{};
    std::uint32_t win32_error{};
};

class ProcessPropertyReader {
  public:
    virtual ~ProcessPropertyReader() = default;
    ProcessPropertyReader(const ProcessPropertyReader&) = delete;
    ProcessPropertyReader& operator=(const ProcessPropertyReader&) = delete;
    ProcessPropertyReader(ProcessPropertyReader&&) = delete;
    ProcessPropertyReader& operator=(ProcessPropertyReader&&) = delete;

    [[nodiscard]] virtual ProcessPropertyReadResult Read(
        const EVENT_RECORD& record, ProcessEtwProperty property,
        std::span<std::byte> destination) noexcept = 0;

  protected:
    ProcessPropertyReader() = default;
};

class TdhProcessPropertyReader final : public ProcessPropertyReader {
  public:
    [[nodiscard]] ProcessPropertyReadResult Read(
        const EVENT_RECORD& record, ProcessEtwProperty property,
        std::span<std::byte> destination) noexcept override;
};

enum class WindowsEtwDecodeStatus : std::uint8_t {
    Decoded,
    Ignored,
    Rejected,
};

enum class WindowsEtwDecodeError : std::uint8_t {
    None,
    UnsupportedVersion,
    InvalidTimestamp,
    PayloadTooShort,
    InvalidUserData,
    PropertyReadFailed,
    InvalidProcessIdSize,
    InvalidProcessKeySize,
    ImageTooLong,
    InvalidImageEncoding,
    OutOfMemory,
    InvariantViolation,
};

struct WindowsEtwDecodeResult {
    WindowsEtwDecodeStatus status{WindowsEtwDecodeStatus::Ignored};
    WindowsEtwDecodeError error{WindowsEtwDecodeError::None};
    std::uint32_t win32_error{};
    std::optional<CollectorEvent> event;
};

class WindowsEtwRecordDecoder {
  public:
    explicit WindowsEtwRecordDecoder(ProcessPropertyReader& property_reader);
    ~WindowsEtwRecordDecoder() = default;
    WindowsEtwRecordDecoder(const WindowsEtwRecordDecoder&) = delete;
    WindowsEtwRecordDecoder& operator=(const WindowsEtwRecordDecoder&) = delete;
    WindowsEtwRecordDecoder(WindowsEtwRecordDecoder&&) = delete;
    WindowsEtwRecordDecoder& operator=(WindowsEtwRecordDecoder&&) = delete;

    [[nodiscard]] WindowsEtwDecodeResult Decode(const EVENT_RECORD& record) noexcept;

  private:
    [[nodiscard]] WindowsEtwDecodeResult DecodeImpl(const EVENT_RECORD& record);
    [[nodiscard]] WindowsEtwDecodeResult DecodeProcess(const EVENT_RECORD& record,
                                                       process::TraceTimePoint observed_at);

    static constexpr std::size_t kMaximumImageBytes = 32'768U;

    ProcessPropertyReader& property_reader_;
    std::array<std::byte, sizeof(std::uint64_t)> scalar_buffer_{};
    std::vector<std::byte> image_buffer_;
};

[[nodiscard]] const GUID& TcpIpProviderId() noexcept;
[[nodiscard]] const GUID& UdpIpProviderId() noexcept;
[[nodiscard]] const GUID& ProcessProviderId() noexcept;
[[nodiscard]] const GUID& EventTraceProviderId() noexcept;

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_WINDOWS_ETW_RECORD_DECODER_H
