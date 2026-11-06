#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_SYSTEM_TELEMETRY_SAMPLER_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_SYSTEM_TELEMETRY_SAMPLER_H

#include <cstdint>
#include <optional>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace nfm::host::platform {

struct SystemTelemetrySnapshot {
    double cpu_usage_percent{0.0};
    double memory_usage_percent{0.0};
    double memory_used_gb{0.0};
    double memory_total_gb{0.0};
    std::optional<double> cpu_temperature_celsius;
    std::optional<double> cpu_package_power_watts;
    double gpu_usage_percent{0.0};
    double gpu_vram_usage_percent{0.0};
    double gpu_vram_used_gb{0.0};
    double gpu_vram_total_gb{0.0};

    bool operator==(const SystemTelemetrySnapshot&) const = default;
};

class SystemTelemetrySampler {
  public:
    SystemTelemetrySampler();
    ~SystemTelemetrySampler() noexcept;

    SystemTelemetrySampler(const SystemTelemetrySampler&) = delete;
    SystemTelemetrySampler& operator=(const SystemTelemetrySampler&) = delete;
    SystemTelemetrySampler(SystemTelemetrySampler&&) = delete;
    SystemTelemetrySampler& operator=(SystemTelemetrySampler&&) = delete;

    [[nodiscard]] SystemTelemetrySnapshot sample() noexcept;

  private:
    struct GpuAdapterInfo {
        LUID luid{};
        std::uint64_t total_dedicated_bytes{0};
    };

    void ensure_initialized() noexcept;
    void refresh_gpu_adapters_if_needed() noexcept;
    [[nodiscard]] double sample_cpu_fallback_percent() noexcept;

    void* pdh_query_{nullptr};
    void* cpu_utility_counter_{nullptr};
    void* cpu_power_counter_{nullptr};
    void* thermal_zone_counter_{nullptr};
    void* gpu_engine_counter_{nullptr};
    void* gpu_adapter_mem_counter_{nullptr};

    bool initialized_{false};
    std::uint64_t prev_idle_ticks_{0};
    std::uint64_t prev_total_ticks_{0};
    std::uint32_t sample_count_{0};
    std::vector<GpuAdapterInfo> gpu_adapters_;
    std::vector<std::uint8_t> pdh_buffer_;
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_SYSTEM_TELEMETRY_SAMPLER_H
