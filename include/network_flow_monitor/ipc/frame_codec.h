#ifndef NETWORK_FLOW_MONITOR_IPC_FRAME_CODEC_H
#define NETWORK_FLOW_MONITOR_IPC_FRAME_CODEC_H

#include "network_flow_monitor/ipc/protocol_types.h"

namespace nfm::ipc {

[[nodiscard]] FrameEncodeResult EncodeFrame(FrameView frame);
[[nodiscard]] FrameParseResult TryDecodeFrame(std::span<const std::byte> bytes);

[[nodiscard]] FrameEncodeResult EncodeClientHello(const ClientHello& message,
                                                  std::uint64_t request_id);
[[nodiscard]] FrameEncodeResult EncodeServerHello(const ServerHello& message,
                                                  std::uint64_t request_id);
[[nodiscard]] FrameEncodeResult EncodeSubscribe(const Subscribe& message, std::uint64_t request_id);
[[nodiscard]] FrameEncodeResult EncodeSubscriptionAccepted(const SubscriptionAccepted& message,
                                                           std::uint64_t request_id);
[[nodiscard]] FrameEncodeResult EncodeProtocolError(const ProtocolErrorMessage& message,
                                                    std::uint64_t request_id);
[[nodiscard]] ControlDecodeResult DecodeControlMessage(FrameView frame,
                                                       MessageDirection expected_direction);

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_FRAME_CODEC_H
