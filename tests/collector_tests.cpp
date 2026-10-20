#include "network_flow_monitor/collector/collector_core.h"
#include "network_flow_monitor/collector/collector_host.h"
#include "network_flow_monitor/collector/etw_snapshot_provider.h"
#include "network_flow_monitor/collector/network_event_decoder.h"
#include "network_flow_monitor/collector/windows_etw_record_decoder.h"
#include "network_flow_monitor/collector/windows_etw_session.h"
#include "network_flow_monitor/collector/windows_process_probe_adapter.h"

#include <windows.h>

#include "support/current_logon_sid.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <evntrace.h>
#include <iostream>
#include <limits>
#include <optional>
#include <semaphore>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// === network_event_decoder_suite ===
namespace network_event_decoder_suite {
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

using nfm::collector::EtwProviderClass;
using nfm::collector::IpVersion;
using nfm::collector::NetworkDecodeError;
using nfm::collector::NetworkDecodeStatus;
using nfm::collector::NetworkEventRecordView;
using nfm::collector::NetworkProtocol;
using nfm::process::TraceDuration;
using nfm::process::TraceTimePoint;
using nfm::ranking::TrafficDirection;

TraceTimePoint At(std::chrono::milliseconds offset) {
    return TraceTimePoint{std::chrono::duration_cast<TraceDuration>(offset)};
}

std::array<std::byte, 8U> Payload(std::uint32_t pid, std::uint32_t size) {
    std::array<std::byte, 8U> payload{};
    static_assert(sizeof(pid) + sizeof(size) == payload.size());
    std::memcpy(payload.data(), &pid, sizeof(pid));
    std::memcpy(payload.data() + sizeof(pid), &size, sizeof(size));
    return payload;
}

void CheckDecoded(TestContext& context, EtwProviderClass provider, std::uint8_t opcode,
                  NetworkProtocol protocol, IpVersion ip_version, TrafficDirection direction,
                  bool retransmission, std::string_view name) {
    const auto payload = Payload(1234U, 4096U);
    const auto result =
        nfm::collector::DecodeNetworkEvent({.provider = provider,
                                            .version = 2U,
                                            .opcode = opcode,
                                            .observed_at = At(std::chrono::milliseconds{25}),
                                            .payload = payload});
    context.Check(result.status == NetworkDecodeStatus::Decoded, name);
    if (!result.event.has_value()) {
        context.Check(false, "decoded result contains event");
        return;
    }
    context.Check(result.event->pid == 1234U, "payload PID decoded");
    context.Check(result.event->bytes == 4096U, "payload size decoded");
    context.Check(result.event->protocol == protocol, "protocol decoded");
    context.Check(result.event->ip_version == ip_version, "IP version decoded");
    context.Check(result.event->direction == direction, "direction decoded");
    context.Check(result.event->retransmission == retransmission, "retransmit flag decoded");
    context.Check(result.event->observed_at == At(std::chrono::milliseconds{25}),
                  "event time preserved");
}

void CheckProtocolMatrix(TestContext& context) {
    CheckDecoded(context, EtwProviderClass::Tcp, 10U, NetworkProtocol::Tcp, IpVersion::V4,
                 TrafficDirection::Upload, false, "TCP IPv4 send");
    CheckDecoded(context, EtwProviderClass::Tcp, 11U, NetworkProtocol::Tcp, IpVersion::V4,
                 TrafficDirection::Download, false, "TCP IPv4 receive");
    CheckDecoded(context, EtwProviderClass::Tcp, 26U, NetworkProtocol::Tcp, IpVersion::V6,
                 TrafficDirection::Upload, false, "TCP IPv6 send");
    CheckDecoded(context, EtwProviderClass::Tcp, 27U, NetworkProtocol::Tcp, IpVersion::V6,
                 TrafficDirection::Download, false, "TCP IPv6 receive");
    CheckDecoded(context, EtwProviderClass::Tcp, 14U, NetworkProtocol::Tcp, IpVersion::V4,
                 TrafficDirection::Upload, true, "TCP IPv4 retransmit");
    CheckDecoded(context, EtwProviderClass::Tcp, 30U, NetworkProtocol::Tcp, IpVersion::V6,
                 TrafficDirection::Upload, true, "TCP IPv6 retransmit");
    CheckDecoded(context, EtwProviderClass::Udp, 10U, NetworkProtocol::Udp, IpVersion::V4,
                 TrafficDirection::Upload, false, "UDP IPv4 send");
    CheckDecoded(context, EtwProviderClass::Udp, 11U, NetworkProtocol::Udp, IpVersion::V4,
                 TrafficDirection::Download, false, "UDP IPv4 receive");
    CheckDecoded(context, EtwProviderClass::Udp, 26U, NetworkProtocol::Udp, IpVersion::V6,
                 TrafficDirection::Upload, false, "UDP IPv6 send");
    CheckDecoded(context, EtwProviderClass::Udp, 27U, NetworkProtocol::Udp, IpVersion::V6,
                 TrafficDirection::Download, false, "UDP IPv6 receive");
}

void CheckIgnoredAndRejected(TestContext& context) {
    const auto payload = Payload(7U, 8U);
    const auto other =
        nfm::collector::DecodeNetworkEvent({.provider = EtwProviderClass::Other,
                                            .version = 2U,
                                            .opcode = 10U,
                                            .observed_at = At(std::chrono::milliseconds{0}),
                                            .payload = payload});
    context.Check(other.status == NetworkDecodeStatus::Ignored, "unknown provider ignored");

    const auto control =
        nfm::collector::DecodeNetworkEvent({.provider = EtwProviderClass::Tcp,
                                            .version = 2U,
                                            .opcode = 12U,
                                            .observed_at = At(std::chrono::milliseconds{0}),
                                            .payload = payload});
    context.Check(control.status == NetworkDecodeStatus::Ignored, "TCP connection event ignored");

    const auto version =
        nfm::collector::DecodeNetworkEvent({.provider = EtwProviderClass::Udp,
                                            .version = 1U,
                                            .opcode = 10U,
                                            .observed_at = At(std::chrono::milliseconds{0}),
                                            .payload = payload});
    context.Check(version.status == NetworkDecodeStatus::Rejected, "unsupported version rejected");
    context.Check(version.error == NetworkDecodeError::UnsupportedVersion,
                  "unsupported version classified");

    const std::array<std::byte, 7U> short_payload{};
    const auto truncated =
        nfm::collector::DecodeNetworkEvent({.provider = EtwProviderClass::Tcp,
                                            .version = 2U,
                                            .opcode = 10U,
                                            .observed_at = At(std::chrono::milliseconds{0}),
                                            .payload = short_payload});
    context.Check(truncated.status == NetworkDecodeStatus::Rejected, "short payload rejected");
    context.Check(truncated.error == NetworkDecodeError::PayloadTooShort,
                  "short payload classified");
}

void CheckBoundsAndAlignment(TestContext& context) {
    std::array<std::byte, 9U> storage{};
    const auto payload = Payload(55U, UINT32_MAX);
    std::memcpy(storage.data() + 1U, payload.data(), payload.size());
    const auto unaligned = nfm::collector::DecodeNetworkEvent(
        {.provider = EtwProviderClass::Tcp,
         .version = 2U,
         .opcode = 10U,
         .observed_at = At(std::chrono::milliseconds{1}),
         .payload = std::span<const std::byte>{storage}.subspan(1U)});
    context.Check(unaligned.status == NetworkDecodeStatus::Decoded,
                  "unaligned payload is decoded safely");
    context.Check(unaligned.event.has_value() && unaligned.event->bytes == UINT32_MAX,
                  "maximum uint32 size widens exactly");

    const auto zero_payload = Payload(0U, 0U);
    const auto zero =
        nfm::collector::DecodeNetworkEvent({.provider = EtwProviderClass::Udp,
                                            .version = 2U,
                                            .opcode = 11U,
                                            .observed_at = At(std::chrono::milliseconds{2}),
                                            .payload = zero_payload});
    context.Check(zero.status == NetworkDecodeStatus::Decoded, "zero values remain valid data");
    context.Check(zero.event.has_value() && zero.event->pid == 0U && zero.event->bytes == 0U,
                  "zero PID and size preserved");
}

} // namespace

int Run() {
    TestContext context;
    CheckProtocolMatrix(context);
    CheckIgnoredAndRejected(context);
    CheckBoundsAndAlignment(context);
    return context.failures();
}
} // namespace network_event_decoder_suite

// === windows_etw_record_decoder_suite ===
namespace windows_etw_record_decoder_suite {
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

using nfm::collector::ProcessEtwProperty;
using nfm::collector::ProcessPropertyReader;
using nfm::collector::ProcessPropertyReadResult;
using nfm::collector::ProcessPropertyReadStatus;

class FakePropertyReader final : public ProcessPropertyReader {
  public:
    [[nodiscard]] ProcessPropertyReadResult Read(
        const EVENT_RECORD& record, ProcessEtwProperty property,
        std::span<std::byte> destination) noexcept override {
        static_cast<void>(record);
        ++total_reads_;
        switch (property) {
        case ProcessEtwProperty::UniqueProcessKey:
            ++key_reads_;
            return Copy(key_, key_status_, destination);
        case ProcessEtwProperty::ProcessId:
            ++pid_reads_;
            if (fail_pid_) {
                return {.status = ProcessPropertyReadStatus::Failed, .win32_error = 1234U};
            }
            return Copy(pid_, ProcessPropertyReadStatus::Success, destination);
        case ProcessEtwProperty::ImageFileName:
            ++image_reads_;
            if (image_too_long_) {
                return {.status = ProcessPropertyReadStatus::BufferTooSmall,
                        .required_size = 32'769U};
            }
            return Copy(image_, image_status_, destination);
        }
        return {.status = ProcessPropertyReadStatus::Failed, .win32_error = ERROR_INVALID_DATA};
    }

    template <typename Value> void SetKey(Value value) {
        key_.resize(sizeof(value));
        std::memcpy(key_.data(), &value, sizeof(value));
        key_status_ = ProcessPropertyReadStatus::Success;
    }

    void SetRawKey(std::span<const std::byte> bytes) {
        key_.assign(bytes.begin(), bytes.end());
        key_status_ = ProcessPropertyReadStatus::Success;
    }

    void SetKeyAbsent() noexcept {
        key_.clear();
        key_status_ = ProcessPropertyReadStatus::NotFound;
    }

    void SetPidBytes(std::span<const std::byte> bytes) {
        pid_.assign(bytes.begin(), bytes.end());
    }

    void SetPid(std::uint32_t pid) {
        pid_.resize(sizeof(pid));
        std::memcpy(pid_.data(), &pid, sizeof(pid));
    }

    void SetImage(std::string_view image) {
        const auto bytes = std::as_bytes(std::span{image});
        image_.assign(bytes.begin(), bytes.end());
        image_.push_back(std::byte{});
        image_status_ = ProcessPropertyReadStatus::Success;
    }

    void SetRawImage(std::span<const std::byte> image) {
        image_.assign(image.begin(), image.end());
        image_status_ = ProcessPropertyReadStatus::Success;
    }

    void SetImageTooLong() noexcept {
        image_too_long_ = true;
    }

    void SetPidFailure() noexcept {
        fail_pid_ = true;
    }

    void ResetCounts() noexcept {
        total_reads_ = 0U;
        key_reads_ = 0U;
        pid_reads_ = 0U;
        image_reads_ = 0U;
    }

    [[nodiscard]] std::uint32_t total_reads() const noexcept {
        return total_reads_;
    }

    [[nodiscard]] std::uint32_t key_reads() const noexcept {
        return key_reads_;
    }

    [[nodiscard]] std::uint32_t pid_reads() const noexcept {
        return pid_reads_;
    }

    [[nodiscard]] std::uint32_t image_reads() const noexcept {
        return image_reads_;
    }

  private:
    static ProcessPropertyReadResult Copy(const std::vector<std::byte>& source,
                                          ProcessPropertyReadStatus status,
                                          std::span<std::byte> destination) noexcept {
        if (status != ProcessPropertyReadStatus::Success) {
            return {.status = status};
        }
        if (source.size() > destination.size()) {
            return {.status = ProcessPropertyReadStatus::BufferTooSmall,
                    .required_size = source.size()};
        }
        std::memcpy(destination.data(), source.data(), source.size());
        return {.bytes_written = source.size(), .required_size = source.size()};
    }

    std::vector<std::byte> key_;
    std::vector<std::byte> pid_;
    std::vector<std::byte> image_;
    ProcessPropertyReadStatus key_status_{ProcessPropertyReadStatus::NotFound};
    ProcessPropertyReadStatus image_status_{ProcessPropertyReadStatus::NotFound};
    bool image_too_long_{};
    bool fail_pid_{};
    std::uint32_t total_reads_{};
    std::uint32_t key_reads_{};
    std::uint32_t pid_reads_{};
    std::uint32_t image_reads_{};
};

constexpr std::uint64_t kFileTimeUnixEpoch = 116'444'736'000'000'000ULL;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Opcode/version mirror EVENT_DESCRIPTOR.
EVENT_RECORD MakeRecord(const GUID& provider, std::uint8_t opcode, std::uint8_t version,
                        std::span<const std::byte> payload = {},
                        std::uint64_t file_time = kFileTimeUnixEpoch) {
    EVENT_RECORD record{};
    record.EventHeader.ProviderId = provider;
    record.EventHeader.EventDescriptor.Opcode = opcode;
    record.EventHeader.EventDescriptor.Version = version;
    record.EventHeader.TimeStamp.LowPart = static_cast<DWORD>(file_time);
    record.EventHeader.TimeStamp.HighPart = static_cast<LONG>(file_time >> 32U);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) -- EVENT_RECORD exposes read-only
    // payload as PVOID.
    record.UserData = const_cast<std::byte*>(payload.data());
    record.UserDataLength = static_cast<USHORT>(payload.size());
    return record;
}

std::array<std::byte, 8U> NetworkPayload(std::uint32_t pid, std::uint32_t bytes) {
    std::array<std::byte, 8U> payload{};
    std::memcpy(payload.data(), &pid, sizeof(pid));
    std::memcpy(payload.data() + sizeof(pid), &bytes, sizeof(bytes));
    return payload;
}

void CheckNetworkAndTimestampAdaptation(TestContext& context) {
    FakePropertyReader reader;
    nfm::collector::WindowsEtwRecordDecoder decoder{reader};
    const auto payload = NetworkPayload(42U, 128U);

    struct NetworkCase {
        const GUID* provider{};
        std::uint8_t opcode{};
        nfm::collector::NetworkProtocol protocol{nfm::collector::NetworkProtocol::Tcp};
        nfm::collector::IpVersion ip_version{nfm::collector::IpVersion::V4};
        nfm::ranking::TrafficDirection direction{nfm::ranking::TrafficDirection::Upload};
        bool retransmission{};
    };
    const std::array cases{
        NetworkCase{.provider = &nfm::collector::TcpIpProviderId(), .opcode = 10U},
        NetworkCase{.provider = &nfm::collector::TcpIpProviderId(),
                    .opcode = 11U,
                    .direction = nfm::ranking::TrafficDirection::Download},
        NetworkCase{
            .provider = &nfm::collector::TcpIpProviderId(), .opcode = 14U, .retransmission = true},
        NetworkCase{.provider = &nfm::collector::TcpIpProviderId(),
                    .opcode = 26U,
                    .ip_version = nfm::collector::IpVersion::V6},
        NetworkCase{.provider = &nfm::collector::TcpIpProviderId(),
                    .opcode = 27U,
                    .ip_version = nfm::collector::IpVersion::V6,
                    .direction = nfm::ranking::TrafficDirection::Download},
        NetworkCase{.provider = &nfm::collector::TcpIpProviderId(),
                    .opcode = 30U,
                    .ip_version = nfm::collector::IpVersion::V6,
                    .retransmission = true},
        NetworkCase{.provider = &nfm::collector::UdpIpProviderId(),
                    .opcode = 10U,
                    .protocol = nfm::collector::NetworkProtocol::Udp},
        NetworkCase{.provider = &nfm::collector::UdpIpProviderId(),
                    .opcode = 11U,
                    .protocol = nfm::collector::NetworkProtocol::Udp,
                    .direction = nfm::ranking::TrafficDirection::Download},
        NetworkCase{.provider = &nfm::collector::UdpIpProviderId(),
                    .opcode = 26U,
                    .protocol = nfm::collector::NetworkProtocol::Udp,
                    .ip_version = nfm::collector::IpVersion::V6},
        NetworkCase{.provider = &nfm::collector::UdpIpProviderId(),
                    .opcode = 27U,
                    .protocol = nfm::collector::NetworkProtocol::Udp,
                    .ip_version = nfm::collector::IpVersion::V6,
                    .direction = nfm::ranking::TrafficDirection::Download},
    };
    for (const auto& item : cases) {
        const auto decoded = decoder.Decode(MakeRecord(*item.provider, item.opcode, 2U, payload));
        context.Check(decoded.status == nfm::collector::WindowsEtwDecodeStatus::Decoded,
                      "network EVENT_RECORD decoded");
        const auto* event =
            decoded.event.has_value()
                ? std::get_if<nfm::collector::NetworkTrafficEvent>(&decoded.event.value())
                : nullptr;
        context.Check(
            event != nullptr && event->pid == 42U && event->bytes == 128U &&
                event->protocol == item.protocol && event->ip_version == item.ip_version &&
                event->direction == item.direction && event->retransmission == item.retransmission,
            "network provider/opcode payload mapped");
        if (event != nullptr) {
            context.Check(event->observed_at.time_since_epoch().count() == 0,
                          "FILETIME Unix epoch mapped exactly");
        }
    }

    const auto short_payload = std::span<const std::byte>{payload}.first(7U);
    const auto truncated =
        decoder.Decode(MakeRecord(nfm::collector::TcpIpProviderId(), 10U, 2U, short_payload));
    context.Check(truncated.status == nfm::collector::WindowsEtwDecodeStatus::Rejected &&
                      truncated.error == nfm::collector::WindowsEtwDecodeError::PayloadTooShort,
                  "network truncation propagated");
    const auto unsupported =
        decoder.Decode(MakeRecord(nfm::collector::TcpIpProviderId(), 10U, 1U, payload));
    context.Check(unsupported.error == nfm::collector::WindowsEtwDecodeError::UnsupportedVersion,
                  "network schema version propagated");

    const auto before_epoch = decoder.Decode(
        MakeRecord(nfm::collector::TcpIpProviderId(), 10U, 2U, payload, kFileTimeUnixEpoch - 1U));
    context.Check(before_epoch.error == nfm::collector::WindowsEtwDecodeError::InvalidTimestamp,
                  "pre-Unix FILETIME rejected");
    const auto overflow =
        decoder.Decode(MakeRecord(nfm::collector::TcpIpProviderId(), 10U, 2U, payload,
                                  std::numeric_limits<std::uint64_t>::max()));
    context.Check(overflow.error == nfm::collector::WindowsEtwDecodeError::InvalidTimestamp,
                  "overflow FILETIME rejected");

    const GUID unknown{.Data1 = 0x12345678U,
                       .Data2 = 0x1234U,
                       .Data3 = 0x5678U,
                       .Data4 = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U}};
    context.Check(decoder.Decode(MakeRecord(unknown, 10U, 2U, payload)).status ==
                      nfm::collector::WindowsEtwDecodeStatus::Ignored,
                  "unknown provider ignored");
    context.Check(
        decoder.Decode(MakeRecord(nfm::collector::EventTraceProviderId(), 0U, 2U)).status ==
            nfm::collector::WindowsEtwDecodeStatus::Ignored,
        "synthetic trace-header provider ignored");
    context.Check(
        decoder.Decode(MakeRecord(nfm::collector::TcpIpProviderId(), 12U, 2U, {}, 0U)).status ==
            nfm::collector::WindowsEtwDecodeStatus::Ignored,
        "ignored network opcode does not require a valid timestamp");
}

const nfm::process::ProcessStartObservation* StartEvent(
    const nfm::collector::WindowsEtwDecodeResult& result) {
    return result.event.has_value()
               ? std::get_if<nfm::process::ProcessStartObservation>(&result.event.value())
               : nullptr;
}

void CheckProcessMapping(TestContext& context) {
    FakePropertyReader reader;
    reader.SetPid(500U);
    reader.SetKey(std::uint64_t{0x1234'5678'90AB'CDEFULL});
    reader.SetImage("sample.exe");
    nfm::collector::WindowsEtwRecordDecoder decoder{reader};

    const auto started = decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 2U));
    const auto* start = StartEvent(started);
    context.Check(started.status == nfm::collector::WindowsEtwDecodeStatus::Decoded &&
                      start != nullptr,
                  "process Start decoded");
    if (start != nullptr) {
        context.Check(start->pid == 500U && start->kernel_process_key == 0x1234'5678'90AB'CDEFULL &&
                          start->image_name_hint == "sample.exe" &&
                          start->kind == nfm::process::ProcessStartKind::Started,
                      "process Start properties mapped");
    }
    context.Check(reader.key_reads() == 1U && reader.pid_reads() == 1U &&
                      reader.image_reads() == 1U && reader.total_reads() == 3U,
                  "only approved Start properties requested");

    reader.ResetCounts();
    reader.SetKey(std::uint32_t{0x89AB'CDEFU});
    const auto rundown = decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 3U, 2U));
    const auto* rundown_start = StartEvent(rundown);
    context.Check(rundown_start != nullptr && rundown_start->kernel_process_key == 0x89AB'CDEFU &&
                      rundown_start->kind == nfm::process::ProcessStartKind::Rundown,
                  "DCStart and 32-bit key mapped");

    reader.ResetCounts();
    reader.SetKeyAbsent();
    const auto ended = decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 2U, 2U));
    const auto* end = ended.event.has_value()
                          ? std::get_if<nfm::process::ProcessEndObservation>(&ended.event.value())
                          : nullptr;
    context.Check(end != nullptr && end->pid == 500U && !end->kernel_process_key.has_value(),
                  "End permits absent process key");
    context.Check(reader.image_reads() == 0U && reader.total_reads() == 2U,
                  "End never requests image or unrelated fields");

    reader.ResetCounts();
    reader.SetKey(std::uint64_t{0x1234'5678'90AB'CDEFULL});
    reader.SetImage("sample.exe");
    const auto version4_start =
        decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 4U));
    context.Check(StartEvent(version4_start) != nullptr,
                  "live Windows process version 4 Start decoded through TDH");
    const auto version4_rundown =
        decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 3U, 4U));
    context.Check(StartEvent(version4_rundown) != nullptr,
                  "live Windows process version 4 DCStart decoded through TDH");
    const auto version4_end =
        decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 2U, 4U));
    context.Check(version4_end.status == nfm::collector::WindowsEtwDecodeStatus::Decoded,
                  "live Windows process version 4 End decoded through TDH");
    const auto documented_version3_start =
        decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 3U));
    context.Check(StartEvent(documented_version3_start) != nullptr,
                  "documented process version 3 Start decoded through TDH");

    reader.ResetCounts();
    context.Check(
        decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 4U, 2U)).status ==
                nfm::collector::WindowsEtwDecodeStatus::Ignored &&
            decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 39U, 2U)).status ==
                nfm::collector::WindowsEtwDecodeStatus::Ignored,
        "DCEnd and Defunct ignored");
    context.Check(reader.total_reads() == 0U, "ignored process events request no properties");
}

void CheckProcessFailuresAndProbeAdapter(TestContext& context) {
    FakePropertyReader reader;
    reader.SetPid(9U);
    reader.SetKey(std::uint64_t{9U});
    reader.SetImage("ok.exe");
    nfm::collector::WindowsEtwRecordDecoder decoder{reader};

    const std::array<std::byte, 3U> short_pid{};
    reader.SetPidBytes(short_pid);
    context.Check(decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 2U)).error ==
                      nfm::collector::WindowsEtwDecodeError::InvalidProcessIdSize,
                  "invalid PID property width rejected");

    reader.SetPid(9U);
    const std::array<std::byte, 6U> invalid_key{};
    reader.SetKeyAbsent();
    reader.SetRawImage(std::as_bytes(std::span<const char>{"bad\0tail", 8U}));
    context.Check(decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 2U)).error ==
                      nfm::collector::WindowsEtwDecodeError::InvalidImageEncoding,
                  "embedded image terminator rejected");

    context.Check(decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 1U)).error ==
                      nfm::collector::WindowsEtwDecodeError::UnsupportedVersion,
                  "process schema version rejected before reads");
    reader.SetRawKey(invalid_key);
    reader.SetImage("ok.exe");
    context.Check(decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 2U)).error ==
                      nfm::collector::WindowsEtwDecodeError::InvalidProcessKeySize,
                  "invalid process-key width rejected");

    FakePropertyReader oversized;
    oversized.SetPid(9U);
    oversized.SetKey(std::uint64_t{9U});
    oversized.SetImageTooLong();
    nfm::collector::WindowsEtwRecordDecoder oversized_decoder{oversized};
    context.Check(
        oversized_decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 1U, 2U)).error ==
            nfm::collector::WindowsEtwDecodeError::ImageTooLong,
        "oversized image rejected");

    FakePropertyReader failed;
    failed.SetPidFailure();
    failed.SetKeyAbsent();
    nfm::collector::WindowsEtwRecordDecoder failed_decoder{failed};
    const auto failure =
        failed_decoder.Decode(MakeRecord(nfm::collector::ProcessProviderId(), 2U, 2U));
    context.Check(failure.error == nfm::collector::WindowsEtwDecodeError::PropertyReadFailed &&
                      failure.win32_error == 1234U,
                  "TDH-style property error retained");

    nfm::collector::WindowsProcessProbeAdapter probe;
    const auto invalid_probe = probe.Probe({});
    context.Check(invalid_probe.error == nfm::collector::CollectorProcessProbeError::InvalidData &&
                      invalid_probe.diagnostic.stage ==
                          nfm::collector::CollectorErrorStage::ProbeProcess,
                  "Windows probe result adapted with diagnostics");
}

void CheckProductionTdhReaderBoundary(TestContext& context) {
    nfm::collector::TdhProcessPropertyReader reader;
    EVENT_RECORD synthetic{};
    std::array<std::byte, sizeof(std::uint32_t)> destination{};
    const auto result = reader.Read(synthetic, ProcessEtwProperty::ProcessId, destination);
    context.Check(result.status != ProcessPropertyReadStatus::Success,
                  "TDH rejects a schema-less synthetic record without crossing the boundary");
}

} // namespace

int Run() {
    TestContext context;
    CheckNetworkAndTimestampAdaptation(context);
    CheckProcessMapping(context);
    CheckProcessFailuresAndProbeAdapter(context);
    CheckProductionTdhReaderBoundary(context);
    return context.failures();
}
} // namespace windows_etw_record_decoder_suite

// === windows_etw_session_suite ===
namespace windows_etw_session_suite {
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

int Run() {
    TestContext context;
    CheckConfigurationLifecycleAndStatistics(context);
    CheckStartFailuresAndRecovery(context);
    CheckRuntimeFailuresAndDestructor(context);
    return context.failures();
}
} // namespace windows_etw_session_suite

// === collector_core_suite ===
namespace nfm::collector {

struct CollectorCoreTestAccess {
    static void SetEventsReceived(CollectorCore& core, std::uint64_t value) noexcept {
        core.health_.events_received = value;
    }

    static bool FillDynamicApplicationCapacity(CollectorCore& core) {
        for (std::size_t index = 0U; index < ranking::kMaximumDynamicApplications; ++index) {
            const auto id = std::string{"app.capacity."} + std::to_string(index);
            const auto result = core.accumulator_.AddTraffic(id, {.display_name = "Capacity"},
                                                             process::TraceTimePoint{},
                                                             ranking::TrafficDirection::Upload, 1U);
            if (!result.ok()) {
                return false;
            }
        }
        return true;
    }

    static void SaturateCapacityDiagnostics(CollectorCore& core) noexcept {
        core.health_.active_application_capacity_hits = std::numeric_limits<std::uint64_t>::max();
        core.health_.aggregated_application_events = std::numeric_limits<std::uint64_t>::max();
        core.health_.aggregated_application_bytes = std::numeric_limits<std::uint64_t>::max();
    }
};

} // namespace nfm::collector
namespace collector_core_suite {
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

class FakeProcessProbe final : public nfm::collector::ProcessProbe {
  public:
    [[nodiscard]] nfm::collector::CollectorProcessProbeResult Probe(
        const nfm::process::ProcessProbeRequest& request) noexcept override {
        ++calls_;
        if (request.instance.pid == 200U) {
            return {.error = nfm::collector::CollectorProcessProbeError::AccessDenied};
        }
        if (request.instance.pid == 100U || request.instance.pid == 101U) {
            return {.data = {.application_user_model_id = "Family!Merged"}};
        }
        if (request.instance.pid == 300U) {
            const auto application =
                request.instance.generation == 1U ? "Family!Old" : "Family!New";
            return {.data = {.application_user_model_id = application}};
        }
        return {.data = {.image_path = "C:\\Apps\\other.exe"}};
    }

    [[nodiscard]] std::uint64_t calls() const noexcept {
        return calls_;
    }

  private:
    std::uint64_t calls_{};
};

using nfm::collector::CollectorCore;
using nfm::collector::CollectorCoreError;
using nfm::collector::CollectorCoreTestAccess;
using nfm::collector::CollectorErrorDomain;
using nfm::collector::CollectorErrorStage;
using nfm::collector::CollectorState;
using nfm::collector::EtwSessionStatistics;
using nfm::collector::IpVersion;
using nfm::collector::NetworkProtocol;
using nfm::collector::NetworkTrafficEvent;
using nfm::process::ProcessEndObservation;
using nfm::process::ProcessStartKind;
using nfm::process::ProcessStartObservation;
using nfm::process::TraceDuration;
using nfm::process::TraceTimePoint;
using nfm::ranking::ApplicationCounters;
using nfm::ranking::CounterSnapshot;
using nfm::ranking::TrafficDirection;

TraceTimePoint At(std::chrono::milliseconds offset) {
    return TraceTimePoint{std::chrono::duration_cast<TraceDuration>(offset)};
}

ProcessStartObservation Start(std::uint32_t pid, std::chrono::milliseconds observed_at,
                              std::optional<std::uint64_t> kernel_key = std::nullopt,
                              ProcessStartKind kind = ProcessStartKind::Started) {
    return {.pid = pid,
            .observed_at = At(observed_at),
            .kernel_process_key = kernel_key,
            .image_name_hint = "test.exe",
            .kind = kind};
}

NetworkTrafficEvent Traffic(std::uint32_t pid, std::chrono::milliseconds observed_at,
                            TrafficDirection direction, std::uint64_t bytes,
                            bool retransmission = false,
                            NetworkProtocol protocol = NetworkProtocol::Tcp,
                            IpVersion ip_version = IpVersion::V4) {
    return {.pid = pid,
            .observed_at = At(observed_at),
            .protocol = protocol,
            .ip_version = ip_version,
            .direction = direction,
            .bytes = bytes,
            .retransmission = retransmission};
}

const ApplicationCounters* Find(const CounterSnapshot& snapshot, std::string_view id) {
    for (const auto& counters : snapshot.applications) {
        if (counters.id == id) {
            return &counters;
        }
    }
    return nullptr;
}

void CheckResolvedMergeAndUnattributed(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(100U, 0ms, 1000U)).ok(), "first start consumed");
    context.Check(core.Consume(Start(101U, 0ms, 1001U)).ok(), "second start consumed");
    context.Check(core.Consume(Traffic(100U, 1ms, TrafficDirection::Upload, 100U)).ok(),
                  "first upload consumed");
    context.Check(core.Consume(Traffic(101U, 1ms, TrafficDirection::Download, 50U)).ok(),
                  "second download consumed");

    context.Check(core.Consume(Traffic(999U, 1ms, TrafficDirection::Upload, 7U)).ok(),
                  "unmatched traffic retained");
    context.Check(core.Consume(Traffic(101U, 2ms, TrafficDirection::Download, 13U, false,
                                       NetworkProtocol::Udp, IpVersion::V6))
                      .ok(),
                  "UDP IPv6 download consumed");
    const auto snapshot = core.MakeSnapshot(std::chrono::steady_clock::time_point{});
    const auto* merged = Find(snapshot, "package-app:Family!Merged");
    context.Check(merged != nullptr, "same application identity merged");
    if (merged != nullptr) {
        context.Check(merged->upload_bytes == 100U, "merged upload correct");
        context.Check(merged->download_bytes == 63U, "merged download correct");
    }
    const auto* unattributed = Find(snapshot, nfm::collector::kUnattributedApplicationId);
    context.Check(unattributed != nullptr && unattributed->upload_bytes == 7U,
                  "unmatched bytes use unattributed identity");
    context.Check(core.health().unmatched_network_events == 1U, "unmatched event diagnosed");
    context.Check(core.health().unmatched_network_bytes == 7U, "unmatched bytes diagnosed");
    context.Check(core.health().network_events_counted == 4U &&
                      core.health().network_bytes_counted == 170U,
                  "aggregate network health counted");
    context.Check(core.health().tcp_events == 3U && core.health().tcp_bytes == 157U &&
                      core.health().udp_events == 1U && core.health().udp_bytes == 13U,
                  "protocol health counted");
    context.Check(core.health().ipv4_events == 3U && core.health().ipv4_bytes == 157U &&
                      core.health().ipv6_events == 1U && core.health().ipv6_bytes == 13U,
                  "IP version health counted");
    context.Check(core.health().upload_events == 2U && core.health().upload_bytes == 107U &&
                      core.health().download_events == 2U && core.health().download_bytes == 63U,
                  "traffic direction health counted");
    context.Check(probe.calls() == 2U, "each ordinary start probed once");
}

void CheckProbeFailureAndReuseBoundary(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(200U, 0ms, 2000U)).ok(),
                  "access-denied process start retained");
    context.Check(core.Consume(Traffic(200U, 1ms, TrafficDirection::Upload, 5U)).ok(),
                  "unresolved process traffic retained");
    auto snapshot = core.MakeSnapshot(std::chrono::steady_clock::time_point{});
    context.Check(Find(snapshot, "unresolved:200:1") != nullptr,
                  "probe failure remains instance-scoped unresolved");
    context.Check(core.health().process_probe_failures == 1U, "probe failure diagnosed");
    context.Check(core.health().first_error.domain == CollectorErrorDomain::ProcessProbe &&
                      core.health().first_error.stage == CollectorErrorStage::ProbeProcess,
                  "probe failure classified");

    CollectorCore reuse_core{probe};
    context.Check(reuse_core.Consume(Start(300U, 0ms, 3000U)).ok(), "old PID start consumed");
    context.Check(reuse_core.Consume(Start(300U, 10ms, 3001U)).ok(), "reused PID start consumed");
    context.Check(reuse_core.Consume(Traffic(300U, 9ms, TrafficDirection::Upload, 9U)).ok(),
                  "late old event consumed");
    context.Check(reuse_core.Consume(Traffic(300U, 10ms, TrafficDirection::Upload, 10U)).ok(),
                  "boundary new event consumed");
    snapshot = reuse_core.MakeSnapshot(std::chrono::steady_clock::time_point{});
    const auto* old_process = Find(snapshot, "package-app:Family!Old");
    const auto* new_process = Find(snapshot, "package-app:Family!New");
    context.Check(old_process != nullptr && old_process->upload_bytes == 9U,
                  "pre-boundary event maps old generation");
    context.Check(new_process != nullptr && new_process->upload_bytes == 10U,
                  "boundary event maps new generation");
}

void CheckEndRetransmitOverflowAndHealth(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(400U, 0ms, 4000U)).ok(), "overflow process starts");
    context.Check(core.Consume(Traffic(400U, 1ms, TrafficDirection::Upload,
                                       std::numeric_limits<std::uint64_t>::max(), true))
                      .ok(),
                  "maximum counter value accepted once");
    context.Check(core.health().tcp_retransmit_events == 1U, "retransmit event diagnosed");
    context.Check(core.health().tcp_retransmitted_bytes ==
                      std::numeric_limits<std::uint64_t>::max(),
                  "retransmit bytes saturate exactly");
    const auto overflow = core.Consume(Traffic(400U, 2ms, TrafficDirection::Upload, 1U));
    context.Check(overflow.error == CollectorCoreError::CounterOverflow,
                  "counter overflow classified");
    context.Check(core.health().state == CollectorState::Failed,
                  "counter overflow fails collector");
    context.Check(core.health().first_error.domain == CollectorErrorDomain::Accumulator &&
                      core.health().first_error.stage == CollectorErrorStage::AccumulateTraffic,
                  "counter overflow classified in health");

    CollectorCore lifecycle{probe};
    auto system_rundown = Start(4U, 0ms, 4U, ProcessStartKind::Rundown);
    system_rundown.image_name_hint = "sYsTeM";
    context.Check(lifecycle.Consume(std::move(system_rundown)).ok(),
                  "system process rundown consumed");
    context.Check(lifecycle.health().system_process_rundown_confirmed,
                  "PID 4 classification requires System rundown hint");
    context.Check(lifecycle.Consume(Start(500U, 5ms, 5000U)).ok(), "end process starts");
    const auto bad_end = lifecycle.Consume(
        ProcessEndObservation{.pid = 500U, .observed_at = At(4ms), .kernel_process_key = 5000U});
    context.Check(bad_end.error == CollectorCoreError::LifecycleRejected,
                  "end before start classified");
    context.Check(lifecycle.health().lifecycle_errors == 1U, "lifecycle failure diagnosed");
    context.Check(lifecycle.health().first_error.domain == CollectorErrorDomain::ProcessTracker &&
                      lifecycle.health().first_error.stage ==
                          CollectorErrorStage::ApplyProcessLifecycle,
                  "lifecycle failure classified in health");

    lifecycle.UpdateEtwStatistics(
        {.events_lost = 10U, .log_buffers_lost = 0U, .realtime_buffers_lost = 2U});
    context.Check(lifecycle.health().state == CollectorState::Degraded, "ETW loss degrades health");
    context.Check(lifecycle.health().events_lost == 10U &&
                      lifecycle.health().realtime_buffers_lost == 2U,
                  "ETW loss values retained");
    lifecycle.UpdateEtwStatistics(
        {.events_lost = 1U, .log_buffers_lost = 0U, .realtime_buffers_lost = 0U});
    context.Check(lifecycle.health().statistics_resets == 1U,
                  "decreasing ETW counters establishes new baseline");

    CollectorCoreTestAccess::SetEventsReceived(lifecycle,
                                               std::numeric_limits<std::uint64_t>::max());
    lifecycle.RecordEtwEvent(nfm::collector::CollectorEventDisposition::Decoded);
    context.Check(lifecycle.Consume(Traffic(999U, 6ms, TrafficDirection::Download, 1U)).ok(),
                  "event remains consumable at diagnostic saturation");
    context.Check(lifecycle.health().events_received == std::numeric_limits<std::uint64_t>::max(),
                  "shared diagnostic increment never wraps");
}

void CheckCapacityAggregationAndHealth(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(CollectorCoreTestAccess::FillDynamicApplicationCapacity(core),
                  "collector dynamic capacity filled");
    context.Check(core.Consume(Start(600U, 0ms, 6000U)).ok(), "overflow identity process starts");
    context.Check(core.Consume(Traffic(600U, 1ms, TrafficDirection::Upload, 9U)).ok(),
                  "capacity event is retained through Other");

    const auto snapshot = core.MakeSnapshot({});
    const auto* const other = Find(snapshot, nfm::ranking::kOtherApplicationId);
    context.Check(other != nullptr && other->upload_bytes == 9U,
                  "capacity bytes are visible in deterministic Other bucket");
    context.Check(core.health().active_application_capacity_hits == 1U &&
                      core.health().aggregated_application_events == 1U &&
                      core.health().aggregated_application_bytes == 9U,
                  "capacity aggregation diagnostics counted");
    context.Check(core.health().network_events_counted == 1U &&
                      core.health().network_bytes_counted == 9U &&
                      core.health().upload_events == 1U && core.health().upload_bytes == 9U,
                  "global direction totals include aggregated event");
    context.Check(core.health().state == CollectorState::Degraded,
                  "first capacity hit degrades a running collector");

    CollectorCoreTestAccess::SaturateCapacityDiagnostics(core);
    context.Check(core.Consume(Traffic(600U, 2ms, TrafficDirection::Upload, 4U)).ok(),
                  "aggregation remains available with saturated diagnostics");
    context.Check(core.health().active_application_capacity_hits ==
                          std::numeric_limits<std::uint64_t>::max() &&
                      core.health().aggregated_application_events ==
                          std::numeric_limits<std::uint64_t>::max() &&
                      core.health().aggregated_application_bytes ==
                          std::numeric_limits<std::uint64_t>::max(),
                  "capacity diagnostics saturate without wrapping");
}

void CheckCollectorWatermarkPrunesAccumulator(TestContext& context) {
    using namespace std::chrono_literals;
    FakeProcessProbe probe;
    CollectorCore core{probe};
    context.Check(core.Consume(Start(700U, 0ms, 7000U)).ok(), "retained process starts");
    context.Check(core.Consume(Traffic(700U, 0ms, TrafficDirection::Upload, 5U)).ok(),
                  "dynamic traffic counted before watermark");
    context.Check(core.Consume(Traffic(999U, 0ms, TrafficDirection::Download, 2U)).ok(),
                  "fixed unattributed traffic counted before watermark");
    context.Check(core.AdvanceWatermark(At(15s)).ok(), "exact retention watermark accepted");
    auto snapshot = core.MakeSnapshot({});
    context.Check(Find(snapshot, "executable:C:\\Apps\\other.exe") != nullptr,
                  "collector preserves dynamic entry at exact boundary");

    context.Check(core.AdvanceWatermark(At(15s) + TraceDuration{1}).ok(),
                  "post-retention watermark accepted");
    snapshot = core.MakeSnapshot({});
    context.Check(Find(snapshot, "executable:C:\\Apps\\other.exe") == nullptr,
                  "collector watermark prunes expired dynamic entry");
    context.Check(Find(snapshot, nfm::collector::kUnattributedApplicationId) != nullptr,
                  "collector watermark preserves fixed unattributed entry");
}

} // namespace

int Run() {
    TestContext context;
    CheckResolvedMergeAndUnattributed(context);
    CheckProbeFailureAndReuseBoundary(context);
    CheckEndRetransmitOverflowAndHealth(context);
    CheckCapacityAggregationAndHealth(context);
    CheckCollectorWatermarkPrunesAccumulator(context);
    return context.failures();
}
} // namespace collector_core_suite

// === collector_host_suite ===
namespace collector_host_suite {
namespace {

using namespace std::chrono_literals;
using nfm::collector::CollectorHost;
using nfm::collector::CollectorHostError;
using nfm::collector::CollectorHostState;
using nfm::collector::EtwApi;
using nfm::collector::EtwSnapshotProvider;
using nfm::collector::HostWait;
using nfm::collector::WindowsEtwSession;
using nfm::ipc::AuthorizedPrincipal;
using nfm::ipc::CollectorEndpointMode;
using nfm::ipc::HelloMessage;
using nfm::ipc::IpcServer;
using nfm::ipc::SnapshotPublisher;
using nfm::ipc::WindowsPipeTransportFactory;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

class FakeEtwApi final : public EtwApi {
  public:
    ULONG StartController(PTRACEHANDLE handle, LPCWSTR /*name*/,
                          PEVENT_TRACE_PROPERTIES /*properties*/) noexcept override {
        ++start_calls;
        if (start_status == ERROR_SUCCESS) {
            *handle = 11U;
        }
        return start_status;
    }

    TRACEHANDLE OpenConsumer(PEVENT_TRACE_LOGFILEW /*logfile*/) noexcept override {
        ++open_calls;
        if (open_status == INVALID_PROCESSTRACE_HANDLE) {
            SetLastError(open_error);
        }
        return open_status;
    }

    ULONG ProcessConsumer(PTRACEHANDLE /*handles*/) noexcept override {
        ++process_calls;
        return process_status;
    }

    ULONG ControlController(TRACEHANDLE /*handle*/, LPCWSTR /*name*/,
                            PEVENT_TRACE_PROPERTIES /*properties*/,
                            ULONG control_code) noexcept override {
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
            return query_status;
        }
        return ERROR_INVALID_FUNCTION;
    }

    ULONG CloseConsumer(TRACEHANDLE /*handle*/) noexcept override {
        ++close_calls;
        return close_status;
    }

    ULONG start_status{ERROR_SUCCESS};
    TRACEHANDLE open_status{77U};
    DWORD open_error{ERROR_INVALID_HANDLE};
    ULONG process_status{ERROR_SUCCESS};
    ULONG flush_status{ERROR_SUCCESS};
    ULONG query_status{ERROR_SUCCESS};
    ULONG stop_status{ERROR_SUCCESS};
    ULONG close_status{ERROR_SUCCESS};
    std::atomic_uint32_t start_calls{};
    std::atomic_uint32_t open_calls{};
    std::atomic_uint32_t process_calls{};
    std::atomic_uint32_t flush_calls{};
    std::atomic_uint32_t query_calls{};
    std::atomic_uint32_t stop_calls{};
    std::atomic_uint32_t close_calls{};
};

class FakeWait final : public HostWait {
  public:
    bool Until(const std::stop_token&,
               std::optional<std::chrono::steady_clock::time_point>) noexcept override {
        ++calls;
        if (return_true_once) {
            return_true_once = false;
            return true;
        }
        return false;
    }

    std::uint32_t calls{};
    bool return_true_once{};
};

AuthorizedPrincipal Principal() {
    const auto sid = nfm::test::CurrentLogonSid();
    return {
        .mode = CollectorEndpointMode::Portable, .authorized_sid = sid, .server_logon_sid = sid};
}

void CheckProvider(TestContext& context) {
    FakeEtwApi api;
    WindowsEtwSession session{api};
    EtwSnapshotProvider provider{session};
    context.Check(session.Start().ok(), "provider session starts");
    const auto capture = provider.Capture();
    context.Check(capture.error == nfm::ipc::CaptureError::None &&
                      capture.capture.counters.captured_at !=
                          std::chrono::steady_clock::time_point{},
                  "provider flushes and timestamps one snapshot");
    context.Check(api.flush_calls == 1U && api.query_calls == 1U,
                  "provider performs one flush and query");
    context.Check(session.Stop().ok(), "provider session stops");

    FakeEtwApi failed_api;
    failed_api.flush_status = ERROR_WRITE_FAULT;
    WindowsEtwSession failed_session{failed_api};
    EtwSnapshotProvider failed_provider{failed_session};
    context.Check(failed_session.Start().ok(), "failure session starts");
    const auto failed = failed_provider.Capture();
    context.Check(failed.error == nfm::ipc::CaptureError::FlushFailed &&
                      failed.source_error == ERROR_WRITE_FAULT,
                  "provider classifies flush failure without fabricating data");
    context.Check(failed_session.Stop().ok(), "failure session stops");
}

void CheckHostLifecycle(TestContext& context) {
    const auto principal = Principal();
    context.Check(!principal.authorized_sid.empty(), "host test has current user SID");
    if (principal.authorized_sid.empty()) {
        return;
    }

    FakeEtwApi api;
    WindowsEtwSession session{api};
    EtwSnapshotProvider provider{session};
    WindowsPipeTransportFactory transport;
    IpcServer server{"cinst_host", principal, transport};
    SnapshotPublisher publisher{"cinst_host", std::chrono::steady_clock::now() - 1s, provider,
                                server};
    FakeWait wait;
    CollectorHost host{session, server, publisher, wait};

    context.Check(host.Start().ok() && host.state() == CollectorHostState::Running,
                  "host starts ETW and IPC in order");
    context.Check(api.start_calls == 1U &&
                      server.health().state == nfm::ipc::IpcServerState::Running,
                  "host owns one live ETW session and server");
    context.Check(host.Run(std::stop_token{}).ok() && wait.calls == 1U && api.flush_calls == 0U,
                  "host run waits without capturing when no client is subscribed");
    publisher.ClientConnected(
        1U, HelloMessage{.client_pid = 12344U, .protocol_version = 1U, .client_name = "HostTest"});
    wait.return_true_once = true;
    const auto calls_before_due_run = api.flush_calls.load();
    context.Check(host.Run(std::stop_token{}).ok() &&
                      api.flush_calls.load() == calls_before_due_run + 1U,
                  "host captures once a connected client is due");
    context.Check(host.Stop().ok() && host.state() == CollectorHostState::Stopped,
                  "host stops IPC before ETW and reaches stopped state");
    context.Check(host.Stop().ok(), "host stop is idempotent");
}

void CheckStartFailure(TestContext& context) {
    const auto principal = Principal();
    if (principal.authorized_sid.empty()) {
        return;
    }
    FakeEtwApi api;
    api.start_status = ERROR_ACCESS_DENIED;
    WindowsEtwSession session{api};
    EtwSnapshotProvider provider{session};
    WindowsPipeTransportFactory transport;
    IpcServer server{"cinst_host_fail", principal, transport};
    SnapshotPublisher publisher{"cinst_host_fail", std::chrono::steady_clock::now() - 1s, provider,
                                server};
    FakeWait wait;
    CollectorHost host{session, server, publisher, wait};
    const auto result = host.Start();
    context.Check(result.error == CollectorHostError::EtwStartFailed &&
                      host.state() == CollectorHostState::Failed,
                  "host classifies ETW startup denial and rolls back IPC server");
    context.Check(server.health().state == nfm::ipc::IpcServerState::Stopped,
                  "IPC server is stopped when ETW start fails");
    context.Check(host.Stop().ok(), "failed host can be stopped safely");

    // Verify second CollectorHost instance fails at IPC ownership before touching ETW
    FakeEtwApi active_etw;
    WindowsEtwSession active_session{active_etw};
    EtwSnapshotProvider active_provider{active_session};
    IpcServer active_server{"cinst_active", principal, transport};
    SnapshotPublisher active_publisher{"cinst_active", std::chrono::steady_clock::now() - 1s,
                                       active_provider, active_server};
    FakeWait active_wait;
    CollectorHost active_host{active_session, active_server, active_publisher, active_wait};
    context.Check(active_host.Start().ok(), "first collector host starts");

    FakeEtwApi second_etw;
    WindowsEtwSession second_session{
        second_etw,
        {.existing_session_policy = nfm::collector::ExistingSessionPolicy::StopAndRetry}};
    EtwSnapshotProvider second_provider{second_session};
    IpcServer second_server{"cinst_second", principal, transport};
    SnapshotPublisher second_publisher{"cinst_second", std::chrono::steady_clock::now() - 1s,
                                       second_provider, second_server};
    FakeWait second_wait;
    CollectorHost second_host{second_session, second_server, second_publisher, second_wait};
    const auto second_result = second_host.Start();
    context.Check(second_result.error == CollectorHostError::IpcStartFailed &&
                      second_host.state() == CollectorHostState::Failed,
                  "second collector host fails immediately on IPC endpoint ownership");
    context.Check(second_etw.start_calls == 0U && second_etw.stop_calls == 0U &&
                      active_etw.stop_calls == 0U,
                  "second collector host never starts or stops the active ETW session");
    context.Check(active_host.Stop().ok(), "first collector host stops cleanly");
}

} // namespace

int Run() {
    TestContext context;
    CheckProvider(context);
    CheckHostLifecycle(context);
    CheckStartFailure(context);
    if (context.failures != 0) {
        std::cerr << context.failures << " collector host test failure(s)\n";
        return 1;
    }
    std::cout << "collector host tests passed\n";
    return 0;
}
} // namespace collector_host_suite

int main() {
    int failures = 0;
    failures += (network_event_decoder_suite::Run() != 0 ? 1 : 0);
    failures += (windows_etw_record_decoder_suite::Run() != 0 ? 1 : 0);
    failures += (windows_etw_session_suite::Run() != 0 ? 1 : 0);
    failures += (collector_core_suite::Run() != 0 ? 1 : 0);
    failures += (collector_host_suite::Run() != 0 ? 1 : 0);
    return failures;
}
