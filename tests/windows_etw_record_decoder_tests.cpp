#include "network_flow_monitor/collector/windows_etw_record_decoder.h"
#include "network_flow_monitor/collector/windows_process_probe_adapter.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <evntrace.h>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

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

int main() {
    TestContext context;
    CheckNetworkAndTimestampAdaptation(context);
    CheckProcessMapping(context);
    CheckProcessFailuresAndProbeAdapter(context);
    CheckProductionTdhReaderBoundary(context);
    return context.failures();
}
