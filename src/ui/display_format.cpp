#include "network_flow_monitor/ui/display_format.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace nfm::ui {
namespace {

[[nodiscard]] double TotalBytesPerSecond(const ranking::TransferRate& rate) noexcept {
    return rate.upload_bytes_per_second + rate.download_bytes_per_second;
}

[[nodiscard]] std::wstring FormatNumber(double value) {
    int precision = 2;
    if (value >= 100.0) {
        precision = 0;
    } else if (value >= 10.0) {
        precision = 1;
    }
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

[[nodiscard]] std::wstring DirectionRate(const wchar_t* label, double value,
                                         config::SpeedUnit unit) {
    std::wstring result{label};
    result.append(FormatRate(value, unit));
    return result;
}

} // namespace

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const std::string input{value};
    const auto length = static_cast<int>(std::min<std::size_t>(
        value.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    if (length <= 0) {
        return {};
    }
    const auto required =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), length, nullptr, 0);
    if (required > 0) {
        std::wstring result(static_cast<std::size_t>(required), L'\0');
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), length,
                                  result.data(), required) == required) {
            return result;
        }
    }

    const auto fallback_required =
        ::MultiByteToWideChar(CP_ACP, 0, input.c_str(), length, nullptr, 0);
    if (fallback_required <= 0) {
        return L"?";
    }
    std::wstring result(static_cast<std::size_t>(fallback_required), L'\0');
    if (::MultiByteToWideChar(CP_ACP, 0, input.c_str(), length, result.data(), fallback_required) !=
        fallback_required) {
        return L"?";
    }
    return result;
}

std::wstring FormatRate(double bytes_per_second, config::SpeedUnit unit) {
    if (!std::isfinite(bytes_per_second) || bytes_per_second < 0.0) {
        return L"—";
    }

    const bool bits = unit == config::SpeedUnit::Bits;
    double value = bits ? bytes_per_second * 8.0 : bytes_per_second;
    if (!std::isfinite(value)) {
        return L"—";
    }
    constexpr std::array<std::wstring_view, 5U> byte_suffixes{L"B/s", L"KiB/s", L"MiB/s", L"GiB/s",
                                                              L"TiB/s"};
    constexpr std::array<std::wstring_view, 5U> bit_suffixes{L"bit/s", L"Kbit/s", L"Mbit/s",
                                                             L"Gbit/s", L"Tbit/s"};
    const auto& suffixes = bits ? bit_suffixes : byte_suffixes;
    std::size_t suffix_index = 0U;
    while (value >= 1024.0 && suffix_index + 1U < suffixes.size()) {
        value /= 1024.0;
        ++suffix_index;
    }
    std::wstring result = FormatNumber(value);
    result.push_back(L' ');
    result.append(suffixes.at(suffix_index));
    return result;
}

std::wstring ApplicationLabel(const ranking::RankedApplication& application) {
    if (!application.metadata.display_name.empty()) {
        return Utf8ToWide(application.metadata.display_name);
    }
    if (!application.id.empty()) {
        return Utf8ToWide(application.id);
    }
    return L"Unknown application";
}

std::wstring FormatApplicationRate(const ranking::RankedApplication& application,
                                   const WindowDisplayOptions& options) {
    std::wstring result = ApplicationLabel(application);
    result.append(L"  ");
    bool has_direction = false;
    if (options.show_download) {
        result.append(
            DirectionRate(L"↓ ", application.rate.download_bytes_per_second, options.speed_unit));
        has_direction = true;
    }
    if (options.show_upload) {
        if (has_direction) {
            result.append(L"  ");
        }
        result.append(
            DirectionRate(L"↑ ", application.rate.upload_bytes_per_second, options.speed_unit));
        has_direction = true;
    }
    if (!has_direction) {
        result.append(FormatRate(TotalBytesPerSecond(application.rate), options.speed_unit));
    }
    return result;
}

std::wstring FormatTotalRate(const ranking::TransferRate& rate,
                             const WindowDisplayOptions& options) {
    std::wstring result;
    bool has_direction = false;
    if (options.show_download) {
        result.append(DirectionRate(L"Down ", rate.download_bytes_per_second, options.speed_unit));
        has_direction = true;
    }
    if (options.show_upload) {
        if (has_direction) {
            result.append(L"    ");
        }
        result.append(DirectionRate(L"Up ", rate.upload_bytes_per_second, options.speed_unit));
        has_direction = true;
    }
    if (!has_direction) {
        result = FormatRate(TotalBytesPerSecond(rate), options.speed_unit);
    }
    return result;
}

} // namespace nfm::ui
