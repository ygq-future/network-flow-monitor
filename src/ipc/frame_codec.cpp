#include "network_flow_monitor/ipc/frame_codec.h"

#include "yyjson.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace nfm::ipc {
namespace {

constexpr std::size_t kMagicOffset = 0U;
constexpr std::size_t kProtocolVersionOffset = 4U;
constexpr std::size_t kMessageTypeOffset = 6U;
constexpr std::size_t kPayloadLengthOffset = 8U;

struct JsonDocumentDeleter {
    void operator()(yyjson_doc* document) const noexcept {
        yyjson_doc_free(document);
    }
};
using UniqueJsonDocument = std::unique_ptr<yyjson_doc, JsonDocumentDeleter>;

struct MutableDocumentDeleter {
    void operator()(yyjson_mut_doc* document) const noexcept {
        yyjson_mut_doc_free(document);
    }
};
using UniqueMutableDocument = std::unique_ptr<yyjson_mut_doc, MutableDocumentDeleter>;

struct JsonBufferDeleter {
    void operator()(char* buffer) const noexcept {
        std::free(buffer); // NOLINT(cppcoreguidelines-no-malloc)
    }
};
using UniqueJsonBuffer = std::unique_ptr<char, JsonBufferDeleter>;

void AppendU16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>(value & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::byte>& output, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        output.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

std::uint16_t ReadU16(std::span<const std::byte> input, std::size_t offset) noexcept {
    const auto bytes = input.subspan(offset, 2U);
    const auto first = std::to_integer<std::uint8_t>(bytes.front());
    const auto second = std::to_integer<std::uint8_t>(bytes.back());
    return static_cast<std::uint16_t>(first) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(second) << 8U);
}

std::uint32_t ReadU32(std::span<const std::byte> input, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    std::size_t shift = 0U;
    for (const auto byte : input.subspan(offset, 4U)) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(byte)) << shift;
        shift += 8U;
    }
    return value;
}

bool IsKnownMessageTypeValue(std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(MessageType::Hello) &&
           value <= static_cast<std::uint16_t>(MessageType::ShutdownRequest);
}

bool IsKnownMessageType(MessageType type) noexcept {
    return IsKnownMessageTypeValue(static_cast<std::uint16_t>(type));
}

std::span<const std::byte> ExtractPayload(std::span<const std::byte> input) noexcept {
    if (input.size() >= kFrameHeaderSize && ReadU32(input, kMagicOffset) == kFrameMagic) {
        const auto payload_length = ReadU32(input, kPayloadLengthOffset);
        if (input.size() >= kFrameHeaderSize + payload_length) {
            return input.subspan(kFrameHeaderSize, payload_length);
        }
    }
    return input;
}

std::string_view JsonString(yyjson_val* value) noexcept {
    if (value == nullptr || !yyjson_is_str(value)) {
        return {};
    }
    return {yyjson_get_str(value), yyjson_get_len(value)};
}

FrameEncodeResult MakeFrameFromDoc(MessageType type, yyjson_mut_doc* doc) {
    std::size_t len{};
    const UniqueJsonBuffer buffer{yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len)};
    if (!buffer) {
        return {.error = ProtocolErrorCode::InternalFailure};
    }
    if (len > kMaximumFramePayload) {
        return {.error = ProtocolErrorCode::PayloadTooLarge};
    }
    const auto payload_bytes = std::as_bytes(std::span{buffer.get(), len});
    return EncodeFrame(FrameView{
        .header = {.protocol_version = kCurrentProtocolVersion,
                   .message_type = type,
                   .payload_length = static_cast<std::uint32_t>(len)},
        .payload = payload_bytes,
    });
}

} // namespace

FrameEncodeResult EncodeFrame(FrameView frame) {
    if (frame.header.protocol_version != kCurrentProtocolVersion) {
        return {.error = ProtocolErrorCode::VersionMismatch};
    }
    if (!IsKnownMessageType(frame.header.message_type)) {
        return {.error = ProtocolErrorCode::UnknownMessageType};
    }
    if (frame.payload.size() != frame.header.payload_length) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    if (frame.header.payload_length > kMaximumFramePayload) {
        return {.error = ProtocolErrorCode::PayloadTooLarge};
    }

    FrameEncodeResult result;
    result.bytes.reserve(kFrameHeaderSize + frame.payload.size());
    AppendU32(result.bytes, kFrameMagic);
    AppendU16(result.bytes, frame.header.protocol_version);
    AppendU16(result.bytes, static_cast<std::uint16_t>(frame.header.message_type));
    AppendU32(result.bytes, frame.header.payload_length);
    result.bytes.insert(result.bytes.end(), frame.payload.begin(), frame.payload.end());
    return result;
}

FrameParseResult TryDecodeFrame(std::span<const std::byte> bytes) {
    if (bytes.size() < kFrameHeaderSize) {
        return {.required = kFrameHeaderSize};
    }
    if (ReadU32(bytes, kMagicOffset) != kFrameMagic) {
        return {.status = FrameParseStatus::Rejected, .error = ProtocolErrorCode::MalformedHeader};
    }
    const auto protocol_version = ReadU16(bytes, kProtocolVersionOffset);
    if (protocol_version != kCurrentProtocolVersion) {
        return {.status = FrameParseStatus::Rejected, .error = ProtocolErrorCode::VersionMismatch};
    }
    const auto message_type_value = ReadU16(bytes, kMessageTypeOffset);
    if (!IsKnownMessageTypeValue(message_type_value)) {
        return {.status = FrameParseStatus::Rejected,
                .error = ProtocolErrorCode::UnknownMessageType};
    }
    const auto message_type = static_cast<MessageType>(message_type_value);
    const auto payload_length = ReadU32(bytes, kPayloadLengthOffset);
    if (payload_length > kMaximumFramePayload) {
        return {.status = FrameParseStatus::Rejected, .error = ProtocolErrorCode::PayloadTooLarge};
    }
    const auto required = kFrameHeaderSize + static_cast<std::size_t>(payload_length);
    if (bytes.size() < required) {
        return {.required = required};
    }

    const auto payload = bytes.subspan(kFrameHeaderSize, payload_length);
    OwnedFrame frame{
        .header = {.protocol_version = protocol_version,
                   .message_type = message_type,
                   .payload_length = payload_length},
        .payload = std::vector<std::byte>{payload.begin(), payload.end()},
    };
    return {.status = FrameParseStatus::Decoded,
            .consumed = required,
            .required = required,
            .frame = std::move(frame)};
}

FrameEncodeResult EncodeHello(const HelloMessage& message) {
    const UniqueMutableDocument doc{yyjson_mut_doc_new(nullptr)};
    if (!doc) {
        return {.error = ProtocolErrorCode::InternalFailure};
    }
    auto* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_uint(doc.get(), root, "client_pid", message.client_pid);
    yyjson_mut_obj_add_uint(doc.get(), root, "protocol_version", message.protocol_version);
    yyjson_mut_obj_add_strn(doc.get(), root, "client_name", message.client_name.data(),
                            message.client_name.size());

    return MakeFrameFromDoc(MessageType::Hello, doc.get());
}

MessageDecodeResult<HelloMessage> DecodeHello(std::span<const std::byte> payload_or_frame) {
    const auto payload = ExtractPayload(payload_or_frame);
    const UniqueJsonDocument doc{yyjson_read(reinterpret_cast<const char*>(payload.data()),
                                             payload.size(), YYJSON_READ_NOFLAG)};
    if (!doc) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    auto* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    auto* pid_val = yyjson_obj_get(root, "client_pid");
    auto* ver_val = yyjson_obj_get(root, "protocol_version");
    auto* name_val = yyjson_obj_get(root, "client_name");

    if (!yyjson_is_uint(pid_val) || !yyjson_is_uint(ver_val) || !yyjson_is_str(name_val)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    HelloMessage msg{
        .client_pid = static_cast<std::uint32_t>(yyjson_get_uint(pid_val)),
        .protocol_version = static_cast<std::uint16_t>(yyjson_get_uint(ver_val)),
        .client_name = std::string(JsonString(name_val)),
    };
    return {.message = std::move(msg)};
}

FrameEncodeResult EncodeHelloAck(const HelloAckMessage& message) {
    const UniqueMutableDocument doc{yyjson_mut_doc_new(nullptr)};
    if (!doc) {
        return {.error = ProtocolErrorCode::InternalFailure};
    }
    auto* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_uint(doc.get(), root, "protocol_version", message.protocol_version);
    yyjson_mut_obj_add_strn(doc.get(), root, "collector_instance_id",
                            message.collector_instance_id.data(),
                            message.collector_instance_id.size());
    yyjson_mut_obj_add_uint(doc.get(), root, "collector_pid", message.collector_pid);

    auto* caps = yyjson_mut_obj_add_arr(doc.get(), root, "capabilities");
    for (const auto& cap : message.capabilities) {
        yyjson_mut_arr_add_strn(doc.get(), caps, cap.data(), cap.size());
    }

    return MakeFrameFromDoc(MessageType::HelloAck, doc.get());
}

MessageDecodeResult<HelloAckMessage> DecodeHelloAck(std::span<const std::byte> payload_or_frame) {
    const auto payload = ExtractPayload(payload_or_frame);
    const UniqueJsonDocument doc{yyjson_read(reinterpret_cast<const char*>(payload.data()),
                                             payload.size(), YYJSON_READ_NOFLAG)};
    if (!doc) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    auto* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    auto* ver_val = yyjson_obj_get(root, "protocol_version");
    auto* inst_val = yyjson_obj_get(root, "collector_instance_id");
    auto* pid_val = yyjson_obj_get(root, "collector_pid");
    auto* caps_val = yyjson_obj_get(root, "capabilities");

    if (!yyjson_is_uint(ver_val) || !yyjson_is_str(inst_val) || !yyjson_is_uint(pid_val) ||
        !yyjson_is_arr(caps_val)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    HelloAckMessage msg{
        .protocol_version = static_cast<std::uint16_t>(yyjson_get_uint(ver_val)),
        .collector_instance_id = std::string(JsonString(inst_val)),
        .collector_pid = static_cast<std::uint32_t>(yyjson_get_uint(pid_val)),
    };

    const auto max_caps = yyjson_arr_size(caps_val);
    for (size_t idx = 0; idx < max_caps; ++idx) {
        auto* cap_item = yyjson_arr_get(caps_val, idx);
        if (yyjson_is_str(cap_item)) {
            msg.capabilities.emplace_back(JsonString(cap_item));
        }
    }

    return {.message = std::move(msg)};
}

FrameEncodeResult EncodeTrafficTick(const TrafficTickMessage& message) {
    const UniqueMutableDocument doc{yyjson_mut_doc_new(nullptr)};
    if (!doc) {
        return {.error = ProtocolErrorCode::InternalFailure};
    }
    auto* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_uint(doc.get(), root, "seq", message.seq);
    yyjson_mut_obj_add_uint(doc.get(), root, "monotonic_ms", message.monotonic_ms);
    yyjson_mut_obj_add_uint(doc.get(), root, "download_bytes_per_sec",
                            message.download_bytes_per_sec);
    yyjson_mut_obj_add_uint(doc.get(), root, "upload_bytes_per_sec", message.upload_bytes_per_sec);

    return MakeFrameFromDoc(MessageType::TrafficTick, doc.get());
}

MessageDecodeResult<TrafficTickMessage> DecodeTrafficTick(
    std::span<const std::byte> payload_or_frame) {
    const auto payload = ExtractPayload(payload_or_frame);
    const UniqueJsonDocument doc{yyjson_read(reinterpret_cast<const char*>(payload.data()),
                                             payload.size(), YYJSON_READ_NOFLAG)};
    if (!doc) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    auto* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    auto* seq_val = yyjson_obj_get(root, "seq");
    auto* mono_val = yyjson_obj_get(root, "monotonic_ms");
    auto* dl_val = yyjson_obj_get(root, "download_bytes_per_sec");
    auto* ul_val = yyjson_obj_get(root, "upload_bytes_per_sec");

    if (!yyjson_is_uint(seq_val) || !yyjson_is_uint(mono_val) || !yyjson_is_uint(dl_val) ||
        !yyjson_is_uint(ul_val)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    TrafficTickMessage msg{
        .seq = yyjson_get_uint(seq_val),
        .monotonic_ms = yyjson_get_uint(mono_val),
        .download_bytes_per_sec = yyjson_get_uint(dl_val),
        .upload_bytes_per_sec = yyjson_get_uint(ul_val),
    };
    return {.message = msg};
}

FrameEncodeResult EncodeRankingSnapshot(const RankingSnapshotMessage& message) {
    const UniqueMutableDocument doc{yyjson_mut_doc_new(nullptr)};
    if (!doc) {
        return {.error = ProtocolErrorCode::InternalFailure};
    }
    auto* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_uint(doc.get(), root, "seq", message.seq);
    yyjson_mut_obj_add_uint(doc.get(), root, "monotonic_ms", message.monotonic_ms);
    yyjson_mut_obj_add_strn(doc.get(), root, "status", message.status.data(),
                            message.status.size());

    auto* total_rate = yyjson_mut_obj_add_obj(doc.get(), root, "total_rate");
    yyjson_mut_obj_add_uint(doc.get(), total_rate, "download_bytes_per_sec",
                            message.total_rate.download_bytes_per_sec);
    yyjson_mut_obj_add_uint(doc.get(), total_rate, "upload_bytes_per_sec",
                            message.total_rate.upload_bytes_per_sec);

    auto* apps = yyjson_mut_obj_add_arr(doc.get(), root, "applications");
    for (const auto& app : message.applications) {
        auto* item = yyjson_mut_arr_add_obj(doc.get(), apps);
        yyjson_mut_obj_add_strn(doc.get(), item, "id", app.id.data(), app.id.size());
        yyjson_mut_obj_add_strn(doc.get(), item, "executable_path", app.executable_path.data(),
                                app.executable_path.size());
        yyjson_mut_obj_add_strn(doc.get(), item, "display_name", app.display_name.data(),
                                app.display_name.size());
        yyjson_mut_obj_add_strn(doc.get(), item, "icon_key", app.icon_key.data(),
                                app.icon_key.size());
        yyjson_mut_obj_add_uint(doc.get(), item, "download_bytes_per_sec",
                                app.download_bytes_per_sec);
        yyjson_mut_obj_add_uint(doc.get(), item, "upload_bytes_per_sec", app.upload_bytes_per_sec);
        if (app.total_download_bytes > 0U || app.total_upload_bytes > 0U) {
            yyjson_mut_obj_add_uint(doc.get(), item, "total_download_bytes",
                                    app.total_download_bytes);
            yyjson_mut_obj_add_uint(doc.get(), item, "total_upload_bytes", app.total_upload_bytes);
        }
    }

    return MakeFrameFromDoc(MessageType::RankingSnapshot, doc.get());
}

MessageDecodeResult<RankingSnapshotMessage> DecodeRankingSnapshot(
    std::span<const std::byte> payload_or_frame) {
    const auto payload = ExtractPayload(payload_or_frame);
    const UniqueJsonDocument doc{yyjson_read(reinterpret_cast<const char*>(payload.data()),
                                             payload.size(), YYJSON_READ_NOFLAG)};
    if (!doc) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    auto* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    auto* seq_val = yyjson_obj_get(root, "seq");
    auto* mono_val = yyjson_obj_get(root, "monotonic_ms");
    auto* status_val = yyjson_obj_get(root, "status");
    auto* total_rate_val = yyjson_obj_get(root, "total_rate");
    auto* apps_val = yyjson_obj_get(root, "applications");

    if (!yyjson_is_uint(seq_val) || !yyjson_is_uint(mono_val) || !yyjson_is_str(status_val) ||
        !yyjson_is_obj(total_rate_val) || !yyjson_is_arr(apps_val)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    auto* dl_total = yyjson_obj_get(total_rate_val, "download_bytes_per_sec");
    auto* ul_total = yyjson_obj_get(total_rate_val, "upload_bytes_per_sec");
    if (!yyjson_is_uint(dl_total) || !yyjson_is_uint(ul_total)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    RankingSnapshotMessage msg{
        .seq = yyjson_get_uint(seq_val),
        .monotonic_ms = yyjson_get_uint(mono_val),
        .status = std::string(JsonString(status_val)),
        .total_rate = {.download_bytes_per_sec = yyjson_get_uint(dl_total),
                       .upload_bytes_per_sec = yyjson_get_uint(ul_total)},
    };

    const auto max_apps = yyjson_arr_size(apps_val);
    for (size_t idx = 0; idx < max_apps; ++idx) {
        auto* item = yyjson_arr_get(apps_val, idx);
        if (!yyjson_is_obj(item)) {
            continue;
        }
        auto* id = yyjson_obj_get(item, "id");
        auto* exec = yyjson_obj_get(item, "executable_path");
        auto* name = yyjson_obj_get(item, "display_name");
        auto* icon = yyjson_obj_get(item, "icon_key");
        auto* dl = yyjson_obj_get(item, "download_bytes_per_sec");
        auto* ul = yyjson_obj_get(item, "upload_bytes_per_sec");
        auto* total_dl = yyjson_obj_get(item, "total_download_bytes");
        auto* total_ul = yyjson_obj_get(item, "total_upload_bytes");
        if (yyjson_is_str(id) && yyjson_is_str(exec) && yyjson_is_str(name) &&
            yyjson_is_str(icon) && yyjson_is_uint(dl) && yyjson_is_uint(ul)) {
            msg.applications.push_back({
                .id = std::string(JsonString(id)),
                .executable_path = std::string(JsonString(exec)),
                .display_name = std::string(JsonString(name)),
                .icon_key = std::string(JsonString(icon)),
                .download_bytes_per_sec = yyjson_get_uint(dl),
                .upload_bytes_per_sec = yyjson_get_uint(ul),
                .total_download_bytes = yyjson_is_uint(total_dl) ? yyjson_get_uint(total_dl) : 0U,
                .total_upload_bytes = yyjson_is_uint(total_ul) ? yyjson_get_uint(total_ul) : 0U,
            });
        }
    }

    return {.message = std::move(msg)};
}

FrameEncodeResult EncodeCollectorStatus(const CollectorStatusMessage& message) {
    const UniqueMutableDocument doc{yyjson_mut_doc_new(nullptr)};
    if (!doc) {
        return {.error = ProtocolErrorCode::InternalFailure};
    }
    auto* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_uint(doc.get(), root, "monotonic_ms", message.monotonic_ms);
    yyjson_mut_obj_add_bool(doc.get(), root, "etw_session_active", message.etw_session_active);
    yyjson_mut_obj_add_uint(doc.get(), root, "dropped_packets", message.dropped_packets);
    yyjson_mut_obj_add_uint(doc.get(), root, "active_pids", message.active_pids);

    return MakeFrameFromDoc(MessageType::CollectorStatus, doc.get());
}

MessageDecodeResult<CollectorStatusMessage> DecodeCollectorStatus(
    std::span<const std::byte> payload_or_frame) {
    const auto payload = ExtractPayload(payload_or_frame);
    const UniqueJsonDocument doc{yyjson_read(reinterpret_cast<const char*>(payload.data()),
                                             payload.size(), YYJSON_READ_NOFLAG)};
    if (!doc) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    auto* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    auto* mono_val = yyjson_obj_get(root, "monotonic_ms");
    auto* etw_val = yyjson_obj_get(root, "etw_session_active");
    auto* dropped_val = yyjson_obj_get(root, "dropped_packets");
    auto* pids_val = yyjson_obj_get(root, "active_pids");

    if (!yyjson_is_uint(mono_val) || !yyjson_is_bool(etw_val) || !yyjson_is_uint(dropped_val) ||
        !yyjson_is_uint(pids_val)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    CollectorStatusMessage msg{
        .monotonic_ms = yyjson_get_uint(mono_val),
        .etw_session_active = yyjson_get_bool(etw_val),
        .dropped_packets = yyjson_get_uint(dropped_val),
        .active_pids = static_cast<std::uint32_t>(yyjson_get_uint(pids_val)),
    };
    return {.message = msg};
}

FrameEncodeResult EncodeShutdownRequest(const ShutdownRequestMessage& message) {
    const UniqueMutableDocument doc{yyjson_mut_doc_new(nullptr)};
    if (!doc) {
        return {.error = ProtocolErrorCode::InternalFailure};
    }
    auto* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_strn(doc.get(), root, "reason", message.reason.data(),
                            message.reason.size());

    return MakeFrameFromDoc(MessageType::ShutdownRequest, doc.get());
}

MessageDecodeResult<ShutdownRequestMessage> DecodeShutdownRequest(
    std::span<const std::byte> payload_or_frame) {
    const auto payload = ExtractPayload(payload_or_frame);
    if (payload.empty()) {
        return {.message = ShutdownRequestMessage{}};
    }
    const UniqueJsonDocument doc{yyjson_read(reinterpret_cast<const char*>(payload.data()),
                                             payload.size(), YYJSON_READ_NOFLAG)};
    if (!doc) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }
    auto* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    auto* reason_val = yyjson_obj_get(root, "reason");
    if (!yyjson_is_str(reason_val)) {
        return {.error = ProtocolErrorCode::MalformedPayload};
    }

    ShutdownRequestMessage msg{
        .reason = std::string(JsonString(reason_val)),
    };
    return {.message = std::move(msg)};
}

} // namespace nfm::ipc
