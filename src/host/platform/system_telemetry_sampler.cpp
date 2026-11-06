#include "network_flow_monitor/host/platform/system_telemetry_sampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <string>
#include <string_view>

namespace nfm::host::platform {

namespace {

constexpr double kBytesPerGib = 1024.0 * 1024.0 * 1024.0;
constexpr double kMilliwattsPerWatt = 1000.0;
constexpr double kKelvinTenthsZeroCelsius = 2731.5;
constexpr double kKelvinTenthsScale = 10.0;
constexpr double kMinValidCelsius = 1.0;
constexpr double kMaxValidCelsius = 135.0;
constexpr std::uint32_t kGpuAdapterRefreshCadence = 30U;
constexpr std::size_t kInitialPdhBufferBytes = 16384U;

[[nodiscard]] std::uint64_t filetime_to_u64(const FILETIME& ft) noexcept {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32U) |
           static_cast<std::uint64_t>(ft.dwLowDateTime);
}

[[nodiscard]] bool contains_icase(std::wstring_view haystack, std::wstring_view needle) noexcept {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    const auto match = std::ranges::search(haystack, needle, [](wchar_t a, wchar_t b) noexcept {
        return std::towlower(a) == std::towlower(b);
    });
    return !match.empty();
}
struct ParsedGpuLuid {
    LUID luid{};
    bool valid{false};
};

// Instance format: "...luid_0x00000000_0x00016ca1_phys_0..."
[[nodiscard]] ParsedGpuLuid parse_luid_from_instance(std::wstring_view instance) noexcept {
    constexpr std::wstring_view kPrefix = L"luid_0x";
    const std::size_t pos = instance.find(kPrefix);
    if (pos == std::wstring_view::npos) {
        return {};
    }
    const std::wstring_view rest = instance.substr(pos + kPrefix.size());
    const std::size_t sep = rest.find(L"_0x");
    if (sep == std::wstring_view::npos || sep == 0U || sep > 16U) {
        return {};
    }
    std::array<wchar_t, 20> high_buf{};
    for (std::size_t idx = 0U; idx < sep; ++idx) {
        high_buf[idx] = rest[idx];
    }
    wchar_t* end_high = nullptr;
    const unsigned long high_val = std::wcstoul(high_buf.data(), &end_high, 16);
    if (end_high == high_buf.data()) {
        return {};
    }

    const std::wstring_view low_part = rest.substr(sep + 3U);
    const std::size_t low_end = low_part.find(L'_');
    const std::size_t low_len = (low_end == std::wstring_view::npos) ? low_part.size() : low_end;
    if (low_len == 0U || low_len > 16U) {
        return {};
    }
    std::array<wchar_t, 20> low_buf{};
    for (std::size_t idx = 0U; idx < low_len; ++idx) {
        low_buf[idx] = low_part[idx];
    }
    wchar_t* end_low = nullptr;
    const unsigned long low_val = std::wcstoul(low_buf.data(), &end_low, 16);
    if (end_low == low_buf.data()) {
        return {};
    }

    ParsedGpuLuid out{};
    out.luid.HighPart = static_cast<LONG>(high_val);
    out.luid.LowPart = static_cast<DWORD>(low_val);
    out.valid = true;
    return out;
}

[[nodiscard]] std::wstring extract_engine_type(std::wstring_view instance) {
    constexpr std::wstring_view kTag = L"_engtype_";
    const std::size_t pos = instance.find(kTag);
    if (pos == std::wstring_view::npos) {
        return L"default";
    }
    return std::wstring(instance.substr(pos + kTag.size()));
}

[[nodiscard]] std::uint64_t query_registry_max_vram_for_description(
    std::wstring_view adapter_desc) noexcept {
    if (adapter_desc.empty()) {
        return 0U;
    }
    HKEY class_key = nullptr;
    constexpr const wchar_t* kDisplayClassPath =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kDisplayClassPath, 0U, KEY_READ, &class_key) !=
        ERROR_SUCCESS) {
        return 0U;
    }

    std::uint64_t best_bytes = 0U;
    for (DWORD idx = 0U; idx < 64U; ++idx) {
        std::array<wchar_t, 64> subkey_name{};
        auto name_len = static_cast<DWORD>(subkey_name.size());
        if (RegEnumKeyExW(class_key, idx, subkey_name.data(), &name_len, nullptr, nullptr, nullptr,
                          nullptr) != ERROR_SUCCESS) {
            break;
        }
        if (subkey_name[0] < L'0' || subkey_name[0] > L'9') {
            continue;
        }
        HKEY subkey = nullptr;
        if (RegOpenKeyExW(class_key, subkey_name.data(), 0U, KEY_READ, &subkey) != ERROR_SUCCESS) {
            continue;
        }
        std::array<wchar_t, 128> desc_buf{};
        auto desc_bytes = static_cast<DWORD>(desc_buf.size() * sizeof(wchar_t));
        DWORD desc_type = 0U;
        if (RegQueryValueExW(subkey, L"DriverDesc", nullptr, &desc_type,
                             reinterpret_cast<LPBYTE>(desc_buf.data()),
                             &desc_bytes) == ERROR_SUCCESS &&
            (desc_type == REG_SZ || desc_type == REG_EXPAND_SZ)) {
            if (std::wstring_view(desc_buf.data()) == adapter_desc) {
                std::uint64_t qw_mem = 0U;
                DWORD qw_size = sizeof(qw_mem);
                DWORD qw_type = 0U;
                if (RegQueryValueExW(subkey, L"HardwareInformation.qwMemorySize", nullptr, &qw_type,
                                     reinterpret_cast<LPBYTE>(&qw_mem),
                                     &qw_size) == ERROR_SUCCESS &&
                    qw_type == REG_QWORD) {
                    best_bytes = std::max(best_bytes, qw_mem);
                }
            }
        }
        RegCloseKey(subkey);
    }

    RegCloseKey(class_key);
    return best_bytes;
}

template <typename Fn>
void for_each_pdh_item(void* counter_handle, std::vector<std::uint8_t>& scratch,
                       const Fn& callback) {
    if (counter_handle == nullptr) {
        return;
    }
    DWORD buffer_size = 0U;
    DWORD item_count = 0U;
    auto* counter = static_cast<PDH_HCOUNTER>(counter_handle);
    constexpr auto kPdhMoreDataStatus = static_cast<PDH_STATUS>(PDH_MORE_DATA);
    PDH_STATUS status =
        PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
    if (status != kPdhMoreDataStatus || buffer_size == 0U) {
        return;
    }
    if (scratch.size() < buffer_size) {
        scratch.resize(static_cast<std::size_t>(buffer_size) + 4096U);
    }
    buffer_size = static_cast<DWORD>(scratch.size());
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(scratch.data());
    status =
        PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, items);
    if (status != ERROR_SUCCESS) {
        return;
    }
    for (DWORD i = 0U; i < item_count; ++i) {
        const auto& item = items[i];
        if (item.szName != nullptr && (item.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                                       item.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)) {
            callback(std::wstring_view(item.szName), item.FmtValue.doubleValue);
        }
    }
}

} // namespace

SystemTelemetrySampler::SystemTelemetrySampler() {
    pdh_buffer_.reserve(kInitialPdhBufferBytes);
}

SystemTelemetrySampler::~SystemTelemetrySampler() noexcept {
    if (pdh_query_ != nullptr) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(pdh_query_));
        pdh_query_ = nullptr;
    }
}

void SystemTelemetrySampler::ensure_initialized() noexcept {
    if (initialized_) {
        return;
    }
    initialized_ = true;

    FILETIME idle_ft{};
    FILETIME kernel_ft{};
    FILETIME user_ft{};
    if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft) != FALSE) {
        prev_idle_ticks_ = filetime_to_u64(idle_ft);
        prev_total_ticks_ = filetime_to_u64(kernel_ft) + filetime_to_u64(user_ft);
    }

    PDH_HQUERY query = nullptr;
    if (PdhOpenQueryW(nullptr, 0U, &query) != ERROR_SUCCESS || query == nullptr) {
        return;
    }
    pdh_query_ = query;

    auto add_counter = [query](const wchar_t* path) noexcept -> void* {
        PDH_HCOUNTER counter = nullptr;
        if (PdhAddEnglishCounterW(query, path, 0U, &counter) == ERROR_SUCCESS) {
            return counter;
        }
        return nullptr;
    };

    cpu_utility_counter_ = add_counter(L"\\Processor Information(_Total)\\% Processor Time");
    if (cpu_utility_counter_ == nullptr) {
        cpu_utility_counter_ = add_counter(L"\\Processor(_Total)\\% Processor Time");
    }
    cpu_power_counter_ = add_counter(L"\\Energy Meter(*)\\Power");
    thermal_zone_counter_ =
        add_counter(L"\\Thermal Zone Information(*)\\High Precision Temperature");
    gpu_engine_counter_ = add_counter(L"\\GPU Engine(*)\\Utilization Percentage");
    gpu_adapter_mem_counter_ = add_counter(L"\\GPU Adapter Memory(*)\\Dedicated Usage");

    static_cast<void>(PdhCollectQueryData(query));
}

void SystemTelemetrySampler::refresh_gpu_adapters_if_needed() noexcept {
    if (!gpu_adapters_.empty() && (sample_count_ % kGpuAdapterRefreshCadence) != 0U) {
        return;
    }

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) ||
        factory == nullptr) {
        return;
    }

    std::vector<GpuAdapterInfo> discovered;
    try {
        for (UINT idx = 0U; idx < 32U; ++idx) {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(idx, &adapter) == DXGI_ERROR_NOT_FOUND ||
                adapter == nullptr) {
                break;
            }
            DXGI_ADAPTER_DESC1 desc{};
            const HRESULT hr = adapter->GetDesc1(&desc);
            adapter->Release();
            if (FAILED(hr)) {
                continue;
            }
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U ||
                desc.DedicatedVideoMemory == 0U) {
                continue;
            }
            const bool already_recorded =
                std::ranges::any_of(discovered, [&](const GpuAdapterInfo& existing) noexcept {
                    return existing.luid.LowPart == desc.AdapterLuid.LowPart &&
                           existing.luid.HighPart == desc.AdapterLuid.HighPart;
                });
            if (already_recorded) {
                continue;
            }
            auto total_bytes = static_cast<std::uint64_t>(desc.DedicatedVideoMemory);
            const std::uint64_t reg_bytes =
                query_registry_max_vram_for_description(std::wstring_view(desc.Description));
            total_bytes = std::max(total_bytes, reg_bytes);
            discovered.push_back(GpuAdapterInfo{
                .luid = desc.AdapterLuid,
                .total_dedicated_bytes = total_bytes,
            });
        }
        if (!discovered.empty()) {
            gpu_adapters_ = std::move(discovered);
        }
    } catch (...) {
    }
    factory->Release();
}

double SystemTelemetrySampler::sample_cpu_fallback_percent() noexcept {
    FILETIME idle_ft{};
    FILETIME kernel_ft{};
    FILETIME user_ft{};
    if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft) == FALSE) {
        return 0.0;
    }
    const std::uint64_t idle_now = filetime_to_u64(idle_ft);
    const std::uint64_t total_now = filetime_to_u64(kernel_ft) + filetime_to_u64(user_ft);
    const std::uint64_t idle_delta =
        (idle_now >= prev_idle_ticks_) ? (idle_now - prev_idle_ticks_) : 0U;
    const std::uint64_t total_delta =
        (total_now >= prev_total_ticks_) ? (total_now - prev_total_ticks_) : 0U;
    prev_idle_ticks_ = idle_now;
    prev_total_ticks_ = total_now;
    if (total_delta == 0U || idle_delta >= total_delta) {
        return 0.0;
    }
    const double busy_ratio =
        static_cast<double>(total_delta - idle_delta) * 100.0 / static_cast<double>(total_delta);
    return std::clamp(busy_ratio, 0.0, 100.0);
}

SystemTelemetrySnapshot SystemTelemetrySampler::sample() noexcept {
    ensure_initialized();
    refresh_gpu_adapters_if_needed();
    ++sample_count_;

    SystemTelemetrySnapshot snapshot{};

    // 1. Memory usage via GlobalMemoryStatusEx
    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status) != FALSE && mem_status.ullTotalPhys > 0U) {
        const std::uint64_t used_phys = (mem_status.ullTotalPhys >= mem_status.ullAvailPhys)
                                            ? (mem_status.ullTotalPhys - mem_status.ullAvailPhys)
                                            : 0U;
        snapshot.memory_used_gb = static_cast<double>(used_phys) / kBytesPerGib;
        snapshot.memory_total_gb = static_cast<double>(mem_status.ullTotalPhys) / kBytesPerGib;
        snapshot.memory_usage_percent = std::clamp(static_cast<double>(used_phys) * 100.0 /
                                                       static_cast<double>(mem_status.ullTotalPhys),
                                                   0.0, 100.0);
    }

    const double fallback_cpu = sample_cpu_fallback_percent();
    snapshot.cpu_usage_percent = fallback_cpu;

    if (pdh_query_ == nullptr) {
        return snapshot;
    }

    auto* query = static_cast<PDH_HQUERY>(pdh_query_);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        return snapshot;
    }

    // 2. CPU Utility (%)
    if (cpu_utility_counter_ != nullptr) {
        PDH_FMT_COUNTERVALUE fmt_val{};
        if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(cpu_utility_counter_),
                                        PDH_FMT_DOUBLE, nullptr, &fmt_val) == ERROR_SUCCESS &&
            (fmt_val.CStatus == PDH_CSTATUS_VALID_DATA ||
             fmt_val.CStatus == PDH_CSTATUS_NEW_DATA) &&
            std::isfinite(fmt_val.doubleValue)) {
            snapshot.cpu_usage_percent = std::clamp(fmt_val.doubleValue, 0.0, 100.0);
        }
    }

    // 3. CPU Package Power (W) via Energy Meter (RAPL_Package*_PKG or _Total)
    if (cpu_power_counter_ != nullptr) {
        double pkg_milliwatts = 0.0;
        bool has_pkg = false;
        double total_milliwatts = 0.0;
        try {
            for_each_pdh_item(
                cpu_power_counter_, pdh_buffer_,
                [&](std::wstring_view name, double val_mw) noexcept {
                    if (!std::isfinite(val_mw) || val_mw <= 0.0) {
                        return;
                    }
                    if (contains_icase(name, L"_PKG") || contains_icase(name, L"package")) {
                        if (contains_icase(name, L"_PKG") || !contains_icase(name, L"_CORE")) {
                            pkg_milliwatts += val_mw;
                            has_pkg = true;
                        }
                    } else if (name == L"_Total" || name == L"_total") {
                        total_milliwatts = val_mw;
                    }
                });
        } catch (...) {
        }
        const double chosen_mw = has_pkg ? pkg_milliwatts : total_milliwatts;
        if (chosen_mw > 0.0) {
            snapshot.cpu_package_power_watts = chosen_mw / kMilliwattsPerWatt;
        }
    }

    // 4. CPU Temperature (Celsius) via ACPI Thermal Zone Information
    if (thermal_zone_counter_ != nullptr) {
        std::optional<double> preferred_tz01{};
        std::optional<double> max_valid_temp{};
        try {
            for_each_pdh_item(
                thermal_zone_counter_, pdh_buffer_,
                [&](std::wstring_view name, double raw_kelvin_tenths) noexcept {
                    if (!std::isfinite(raw_kelvin_tenths)) {
                        return;
                    }
                    const double celsius =
                        (raw_kelvin_tenths - kKelvinTenthsZeroCelsius) / kKelvinTenthsScale;
                    if (celsius < kMinValidCelsius || celsius > kMaxValidCelsius) {
                        return;
                    }
                    if (contains_icase(name, L"TZ01") || contains_icase(name, L"CPU")) {
                        if (!preferred_tz01.has_value() || celsius > *preferred_tz01) {
                            preferred_tz01 = celsius;
                        }
                    }
                    if (!max_valid_temp.has_value() || celsius > *max_valid_temp) {
                        max_valid_temp = celsius;
                    }
                });
        } catch (...) {
        }
        snapshot.cpu_temperature_celsius =
            preferred_tz01.has_value() ? preferred_tz01 : max_valid_temp;
    }

    // 5. GPU Usage (%) & Dedicated VRAM (GB / Total GB)
    // Select primary physical GPU (preferring dedicated adapter with largest total VRAM).
    struct PerAdapterStats {
        LUID luid{};
        std::uint64_t total_bytes{0};
        std::uint64_t used_bytes{0};
        double max_engine_utilization{0.0};
    };

    std::vector<PerAdapterStats> adapter_stats;
    try {
        adapter_stats.reserve(gpu_adapters_.size() + 2U);
        for (const auto& info : gpu_adapters_) {
            adapter_stats.push_back(PerAdapterStats{
                .luid = info.luid,
                .total_bytes = info.total_dedicated_bytes,
                .used_bytes = 0U,
                .max_engine_utilization = 0.0,
            });
        }

        auto find_or_add_adapter = [&](LUID luid) -> PerAdapterStats* {
            for (auto& entry : adapter_stats) {
                if (entry.luid.LowPart == luid.LowPart && entry.luid.HighPart == luid.HighPart) {
                    return &entry;
                }
            }
            adapter_stats.push_back(PerAdapterStats{.luid = luid});
            return &adapter_stats.back();
        };

        if (gpu_adapter_mem_counter_ != nullptr) {
            for_each_pdh_item(gpu_adapter_mem_counter_, pdh_buffer_,
                              [&](std::wstring_view name, double bytes_used) {
                                  if (!std::isfinite(bytes_used) || bytes_used < 0.0) {
                                      return;
                                  }
                                  const ParsedGpuLuid parsed = parse_luid_from_instance(name);
                                  if (!parsed.valid) {
                                      return;
                                  }
                                  if (auto* st = find_or_add_adapter(parsed.luid)) {
                                      const auto used_u64 = static_cast<std::uint64_t>(bytes_used);
                                      st->used_bytes = std::max(st->used_bytes, used_u64);
                                  }
                              });
        }

        if (gpu_engine_counter_ != nullptr) {
            struct EngineBucket {
                LUID luid{};
                std::wstring engine_type;
                double sum_util{0.0};
            };
            std::vector<EngineBucket> buckets;
            for_each_pdh_item(
                gpu_engine_counter_, pdh_buffer_, [&](std::wstring_view name, double util_val) {
                    if (!std::isfinite(util_val) || util_val <= 0.0) {
                        return;
                    }
                    const ParsedGpuLuid parsed = parse_luid_from_instance(name);
                    if (!parsed.valid) {
                        return;
                    }
                    std::wstring eng_type = extract_engine_type(name);
                    for (auto& b : buckets) {
                        if (b.luid.LowPart == parsed.luid.LowPart &&
                            b.luid.HighPart == parsed.luid.HighPart && b.engine_type == eng_type) {
                            b.sum_util += util_val;
                            return;
                        }
                    }
                    buckets.push_back(EngineBucket{
                        .luid = parsed.luid,
                        .engine_type = std::move(eng_type),
                        .sum_util = util_val,
                    });
                });
            for (const auto& b : buckets) {
                if (auto* st = find_or_add_adapter(b.luid)) {
                    const double clamped = std::clamp(b.sum_util, 0.0, 100.0);
                    st->max_engine_utilization = std::max(st->max_engine_utilization, clamped);
                }
            }
        }

        if (!adapter_stats.empty()) {
            // Choose adapter:
            // If an adapter has active utilization or dedicated VRAM usage and total_bytes > 0,
            // pick the primary dedicated GPU (largest total_bytes, or highest utilization).
            const PerAdapterStats* best = &adapter_stats.front();
            for (const auto& candidate : adapter_stats) {
                if (candidate.total_bytes > best->total_bytes) {
                    best = &candidate;
                }
            }
            // Maximum utilization across all hardware adapters so iGPU + dGPU Hybrid activity is
            // always reflected accurately in GPU usage %, while VRAM shows the primary GPU's VRAM.
            double max_overall_gpu_util = best->max_engine_utilization;
            for (const auto& candidate : adapter_stats) {
                if (candidate.total_bytes > 0U) {
                    max_overall_gpu_util =
                        std::max(max_overall_gpu_util, candidate.max_engine_utilization);
                }
            }
            snapshot.gpu_usage_percent = std::clamp(max_overall_gpu_util, 0.0, 100.0);
            snapshot.gpu_vram_used_gb = static_cast<double>(best->used_bytes) / kBytesPerGib;
            snapshot.gpu_vram_total_gb = static_cast<double>(best->total_bytes) / kBytesPerGib;
            if (best->total_bytes > 0U) {
                snapshot.gpu_vram_usage_percent =
                    std::clamp(static_cast<double>(best->used_bytes) * 100.0 /
                                   static_cast<double>(best->total_bytes),
                               0.0, 100.0);
            }
        }
    } catch (...) {
    }

    return snapshot;
}

} // namespace nfm::host::platform
