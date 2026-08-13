#include "network_flow_monitor/collector/windows_etw_record_decoder.h"

#include "network_flow_monitor/collector/network_event_decoder.h"
#include "network_flow_monitor/process/windows_process_probe.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tdh.h>
#include <utility>

namespace nfm::collector {
namespace {

constexpr GUID kTcpIpProvider = {.Data1 = 0x9a280ac0U,
                                 .Data2 = 0xc8e0U,
                                 .Data3 = 0x11d1U,
                                 .Data4 = {0x84U, 0xe2U, 0x00U, 0xc0U, 0x4fU, 0xb9U, 0x98U, 0xa2U}};
constexpr GUID kUdpIpProvider = {.Data1 = 0xbf3a50c5U,
                                 .Data2 = 0xa9c9U,
                                 .Data3 = 0x4988U,
                                 .Data4 = {0xa0U, 0x05U, 0x2dU, 0xf0U, 0xb7U, 0xc8U, 0x0fU, 0x80U}};
constexpr GUID kProcessProvider = {
    .Data1 = 0x3d6fa8d0U,
    .Data2 = 0xfe05U,
    .Data3 = 0x11d0U,
    .Data4 = {0x9dU, 0xdaU, 0x00U, 0xc0U, 0x4fU, 0xd7U, 0xbaU, 0x7cU}};
constexpr GUID kEventTraceProvider = {
    .Data1 = 0x68fdd900U,
    .Data2 = 0x4a3eU,
    .Data3 = 0x11d1U,
    .Data4 = {0x84U, 0xf4U, 0x00U, 0x00U, 0xf8U, 0x04U, 0x64U, 0xe3U}};

constexpr std::uint8_t kProcessStartOpcode = 1U;
constexpr std::uint8_t kProcessEndOpcode = 2U;
constexpr std::uint8_t kProcessDcStartOpcode = 3U;
constexpr std::uint8_t kProcessDcEndOpcode = 4U;
constexpr std::uint8_t kProcessDefunctOpcode = 39U;

bool EqualGuid(const GUID& left, const GUID& right) noexcept {
    return IsEqualGUID(left, right) != FALSE;
}

std::wstring_view PropertyName(ProcessEtwProperty property) noexcept {
    switch (property) {
    case ProcessEtwProperty::UniqueProcessKey:
        return L"UniqueProcessKey";
    case ProcessEtwProperty::ProcessId:
        return L"ProcessId";
    case ProcessEtwProperty::ImageFileName:
        return L"ImageFileName";
    }
    return {};
}

bool IsMissingPropertyError(std::uint32_t error) noexcept {
    return error == ERROR_NOT_FOUND || error == ERROR_INVALID_NAME;
}

std::optional<process::TraceTimePoint> ConvertTimestamp(const EVENT_RECORD& record) noexcept {
    FILETIME file_time{};
    file_time.dwLowDateTime = record.EventHeader.TimeStamp.LowPart;
    file_time.dwHighDateTime = static_cast<DWORD>(record.EventHeader.TimeStamp.HighPart);
    return process::FileTimeToTraceTimePoint(file_time);
}

template <typename Value>
std::optional<Value> ReadScalar(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() != sizeof(Value)) {
        return std::nullopt;
    }
    Value value{};
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

struct TextConversionResult {
    WindowsEtwDecodeError error{WindowsEtwDecodeError::None};
    std::uint32_t win32_error{};
    std::string value;
};

TextConversionResult ConvertAnsiImage(std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return {};
    }
    const auto terminator = std::ranges::find(bytes, std::byte{});
    if (terminator != bytes.end() && std::next(terminator) != bytes.end()) {
        return {.error = WindowsEtwDecodeError::InvalidImageEncoding};
    }
    const std::size_t source_size = static_cast<std::size_t>(terminator - bytes.begin());
    if (source_size == 0U) {
        return {};
    }
    if (source_size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {.error = WindowsEtwDecodeError::ImageTooLong};
    }
    const int source_length = static_cast<int>(source_size);
    const auto* source = reinterpret_cast<const char*>(bytes.data());
    const int wide_length = MultiByteToWideChar(CP_ACP, 0U, source, source_length, nullptr, 0);
    if (wide_length == 0) {
        return {.error = WindowsEtwDecodeError::InvalidImageEncoding,
                .win32_error = GetLastError()};
    }
    std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0U, source, source_length, wide.data(), wide_length) == 0) {
        return {.error = WindowsEtwDecodeError::InvalidImageEncoding,
                .win32_error = GetLastError()};
    }
    const int utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                                wide_length, nullptr, 0, nullptr, nullptr);
    if (utf8_length == 0) {
        return {.error = WindowsEtwDecodeError::InvalidImageEncoding,
                .win32_error = GetLastError()};
    }
    std::string utf8(static_cast<std::size_t>(utf8_length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_length, utf8.data(),
                            utf8_length, nullptr, nullptr) == 0) {
        return {.error = WindowsEtwDecodeError::InvalidImageEncoding,
                .win32_error = GetLastError()};
    }
    return {.value = std::move(utf8)};
}

WindowsEtwDecodeResult PropertyFailure(ProcessPropertyReadResult result,
                                       WindowsEtwDecodeError size_error) noexcept {
    if (result.status == ProcessPropertyReadStatus::BufferTooSmall) {
        return {.status = WindowsEtwDecodeStatus::Rejected, .error = size_error};
    }
    return {.status = WindowsEtwDecodeStatus::Rejected,
            .error = WindowsEtwDecodeError::PropertyReadFailed,
            .win32_error = result.win32_error};
}

} // namespace

ProcessPropertyReadResult TdhProcessPropertyReader::Read(
    const EVENT_RECORD& record, ProcessEtwProperty property,
    std::span<std::byte> destination) noexcept {
    const auto name = PropertyName(property);
    if (name.empty()) {
        return {.status = ProcessPropertyReadStatus::Failed, .win32_error = ERROR_INVALID_NAME};
    }
    PROPERTY_DATA_DESCRIPTOR descriptor{};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(name.data());
    descriptor.ArrayIndex = ULONG_MAX;

    ULONG required_size{};
    EVENT_RECORD mutable_record = record;
    ULONG status =
        TdhGetPropertySize(&mutable_record, 0U, nullptr, 1U, &descriptor, &required_size);
    if (status != ERROR_SUCCESS) {
        return {.status = IsMissingPropertyError(status) ? ProcessPropertyReadStatus::NotFound
                                                         : ProcessPropertyReadStatus::Failed,
                .win32_error = status};
    }
    if (required_size > destination.size()) {
        return {.status = ProcessPropertyReadStatus::BufferTooSmall,
                .required_size = required_size};
    }
    if (required_size == 0U) {
        return {};
    }
    status = TdhGetProperty(&mutable_record, 0U, nullptr, 1U, &descriptor, required_size,
                            reinterpret_cast<PBYTE>(destination.data()));
    if (status != ERROR_SUCCESS) {
        return {.status = IsMissingPropertyError(status) ? ProcessPropertyReadStatus::NotFound
                                                         : ProcessPropertyReadStatus::Failed,
                .required_size = required_size,
                .win32_error = status};
    }
    return {.bytes_written = required_size, .required_size = required_size};
}

WindowsEtwRecordDecoder::WindowsEtwRecordDecoder(ProcessPropertyReader& property_reader)
    : property_reader_{property_reader}, image_buffer_(kMaximumImageBytes) {}

WindowsEtwDecodeResult WindowsEtwRecordDecoder::Decode(const EVENT_RECORD& record) noexcept {
    try {
        return DecodeImpl(record);
    } catch (const std::bad_alloc&) {
        return {.status = WindowsEtwDecodeStatus::Rejected,
                .error = WindowsEtwDecodeError::OutOfMemory};
    } catch (...) {
        return {.status = WindowsEtwDecodeStatus::Rejected,
                .error = WindowsEtwDecodeError::InvariantViolation};
    }
}

WindowsEtwDecodeResult WindowsEtwRecordDecoder::DecodeImpl(const EVENT_RECORD& record) {
    EtwProviderClass network_provider = EtwProviderClass::Other;
    if (EqualGuid(record.EventHeader.ProviderId, kTcpIpProvider)) {
        network_provider = EtwProviderClass::Tcp;
    } else if (EqualGuid(record.EventHeader.ProviderId, kUdpIpProvider)) {
        network_provider = EtwProviderClass::Udp;
    } else if (EqualGuid(record.EventHeader.ProviderId, kEventTraceProvider) ||
               !EqualGuid(record.EventHeader.ProviderId, kProcessProvider)) {
        return {};
    }

    if (network_provider != EtwProviderClass::Other) {
        if (record.UserDataLength != 0U && record.UserData == nullptr) {
            return {.status = WindowsEtwDecodeStatus::Rejected,
                    .error = WindowsEtwDecodeError::InvalidUserData};
        }
        const auto payload = std::span{static_cast<const std::byte*>(record.UserData),
                                       static_cast<std::size_t>(record.UserDataLength)};
        const auto decoded =
            DecodeNetworkEvent({.provider = network_provider,
                                .version = record.EventHeader.EventDescriptor.Version,
                                .opcode = record.EventHeader.EventDescriptor.Opcode,
                                .observed_at = {},
                                .payload = payload});
        switch (decoded.status) {
        case NetworkDecodeStatus::Ignored:
            return {};
        case NetworkDecodeStatus::Rejected:
            return {.status = WindowsEtwDecodeStatus::Rejected,
                    .error = decoded.error == NetworkDecodeError::UnsupportedVersion
                                 ? WindowsEtwDecodeError::UnsupportedVersion
                                 : WindowsEtwDecodeError::PayloadTooShort};
        case NetworkDecodeStatus::Decoded:
            break;
        }
        const auto observed_at = ConvertTimestamp(record);
        if (!observed_at.has_value()) {
            return {.status = WindowsEtwDecodeStatus::Rejected,
                    .error = WindowsEtwDecodeError::InvalidTimestamp};
        }
        if (!decoded.event.has_value()) {
            return {.status = WindowsEtwDecodeStatus::Rejected,
                    .error = WindowsEtwDecodeError::InvariantViolation};
        }
        auto event = decoded.event.value();
        event.observed_at = observed_at.value();
        return {.status = WindowsEtwDecodeStatus::Decoded, .event = event};
    }

    const auto opcode = record.EventHeader.EventDescriptor.Opcode;
    if (opcode == kProcessDcEndOpcode || opcode == kProcessDefunctOpcode ||
        (opcode != kProcessStartOpcode && opcode != kProcessEndOpcode &&
         opcode != kProcessDcStartOpcode)) {
        return {};
    }
    const auto version = record.EventHeader.EventDescriptor.Version;
    if (version != 2U && version != 3U && version != 4U) {
        return {.status = WindowsEtwDecodeStatus::Rejected,
                .error = WindowsEtwDecodeError::UnsupportedVersion};
    }
    const auto observed_at = ConvertTimestamp(record);
    if (!observed_at.has_value()) {
        return {.status = WindowsEtwDecodeStatus::Rejected,
                .error = WindowsEtwDecodeError::InvalidTimestamp};
    }
    return DecodeProcess(record, observed_at.value());
}

WindowsEtwDecodeResult WindowsEtwRecordDecoder::DecodeProcess(const EVENT_RECORD& record,
                                                              process::TraceTimePoint observed_at) {
    const auto opcode = record.EventHeader.EventDescriptor.Opcode;
    std::optional<std::uint64_t> process_key;
    auto property =
        property_reader_.Read(record, ProcessEtwProperty::UniqueProcessKey, scalar_buffer_);
    if (property.status == ProcessPropertyReadStatus::Success) {
        const auto key_bytes =
            std::span<const std::byte>{scalar_buffer_}.first(property.bytes_written);
        if (const auto key64 = ReadScalar<std::uint64_t>(key_bytes); key64.has_value()) {
            process_key = key64;
        } else if (const auto key32 = ReadScalar<std::uint32_t>(key_bytes); key32.has_value()) {
            process_key.emplace(key32.value());
        } else {
            return {.status = WindowsEtwDecodeStatus::Rejected,
                    .error = WindowsEtwDecodeError::InvalidProcessKeySize};
        }
    } else if (property.status != ProcessPropertyReadStatus::NotFound) {
        return PropertyFailure(property, WindowsEtwDecodeError::InvalidProcessKeySize);
    }

    property = property_reader_.Read(record, ProcessEtwProperty::ProcessId, scalar_buffer_);
    if (property.status != ProcessPropertyReadStatus::Success) {
        return PropertyFailure(property, WindowsEtwDecodeError::InvalidProcessIdSize);
    }
    const auto pid = ReadScalar<std::uint32_t>(
        std::span<const std::byte>{scalar_buffer_}.first(property.bytes_written));
    if (!pid.has_value()) {
        return {.status = WindowsEtwDecodeStatus::Rejected,
                .error = WindowsEtwDecodeError::InvalidProcessIdSize};
    }

    if (opcode == kProcessEndOpcode) {
        return {.status = WindowsEtwDecodeStatus::Decoded,
                .event = process::ProcessEndObservation{.pid = pid.value(),
                                                        .observed_at = observed_at,
                                                        .kernel_process_key = process_key}};
    }

    std::string image_hint;
    property = property_reader_.Read(record, ProcessEtwProperty::ImageFileName, image_buffer_);
    if (property.status == ProcessPropertyReadStatus::Success) {
        const auto converted = ConvertAnsiImage(
            std::span<const std::byte>{image_buffer_}.first(property.bytes_written));
        if (converted.error != WindowsEtwDecodeError::None) {
            return {.status = WindowsEtwDecodeStatus::Rejected,
                    .error = converted.error,
                    .win32_error = converted.win32_error};
        }
        image_hint = converted.value;
    } else if (property.status != ProcessPropertyReadStatus::NotFound) {
        return PropertyFailure(property, WindowsEtwDecodeError::ImageTooLong);
    }

    return {.status = WindowsEtwDecodeStatus::Decoded,
            .event = process::ProcessStartObservation{
                .pid = pid.value(),
                .observed_at = observed_at,
                .kernel_process_key = process_key,
                .image_name_hint = std::move(image_hint),
                .kind = opcode == kProcessDcStartOpcode ? process::ProcessStartKind::Rundown
                                                        : process::ProcessStartKind::Started}};
}

const GUID& TcpIpProviderId() noexcept {
    return kTcpIpProvider;
}

const GUID& UdpIpProviderId() noexcept {
    return kUdpIpProvider;
}

const GUID& ProcessProviderId() noexcept {
    return kProcessProvider;
}

const GUID& EventTraceProviderId() noexcept {
    return kEventTraceProvider;
}

} // namespace nfm::collector
