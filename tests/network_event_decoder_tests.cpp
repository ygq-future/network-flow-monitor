#include "network_flow_monitor/collector/network_event_decoder.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
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

int main() {
    TestContext context;
    CheckProtocolMatrix(context);
    CheckIgnoredAndRejected(context);
    CheckBoundsAndAlignment(context);
    return context.failures();
}
