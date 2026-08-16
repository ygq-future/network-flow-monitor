#ifndef NETWORK_FLOW_MONITOR_IPC_FRAME_CODEC_H
#define NETWORK_FLOW_MONITOR_IPC_FRAME_CODEC_H

#include "network_flow_monitor/ipc/protocol_types.h"

namespace nfm::ipc {

[[nodiscard]] FrameEncodeResult EncodeFrame(FrameView frame);
[[nodiscard]] FrameParseResult TryDecodeFrame(std::span<const std::byte> bytes);

[[nodiscard]] FrameEncodeResult EncodeHello(const HelloMessage& message);
[[nodiscard]] MessageDecodeResult<HelloMessage> DecodeHello(
    std::span<const std::byte> payload_or_frame);

[[nodiscard]] FrameEncodeResult EncodeHelloAck(const HelloAckMessage& message);
[[nodiscard]] MessageDecodeResult<HelloAckMessage> DecodeHelloAck(
    std::span<const std::byte> payload_or_frame);

[[nodiscard]] FrameEncodeResult EncodeTrafficTick(const TrafficTickMessage& message);
[[nodiscard]] MessageDecodeResult<TrafficTickMessage> DecodeTrafficTick(
    std::span<const std::byte> payload_or_frame);

[[nodiscard]] FrameEncodeResult EncodeRankingSnapshot(const RankingSnapshotMessage& message);
[[nodiscard]] MessageDecodeResult<RankingSnapshotMessage> DecodeRankingSnapshot(
    std::span<const std::byte> payload_or_frame);

[[nodiscard]] FrameEncodeResult EncodeCollectorStatus(const CollectorStatusMessage& message);
[[nodiscard]] MessageDecodeResult<CollectorStatusMessage> DecodeCollectorStatus(
    std::span<const std::byte> payload_or_frame);

[[nodiscard]] FrameEncodeResult EncodeShutdownRequest(const ShutdownRequestMessage& message);
[[nodiscard]] MessageDecodeResult<ShutdownRequestMessage> DecodeShutdownRequest(
    std::span<const std::byte> payload_or_frame);

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_FRAME_CODEC_H
