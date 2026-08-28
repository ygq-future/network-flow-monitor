#ifndef NETWORK_FLOW_MONITOR_APP_RUNTIME_MODE_H
#define NETWORK_FLOW_MONITOR_APP_RUNTIME_MODE_H

#include <cstdint>
#include <span>
#include <string_view>

namespace nfm::app {

enum class RuntimeMode : std::uint8_t {
    Collector,
    AuthorizeCollector,
    Invalid,
};

struct RuntimeArguments {
    RuntimeMode mode{RuntimeMode::Invalid};
    std::wstring_view collector_stop_event;
    std::uint32_t parent_pid{0};
};

[[nodiscard]] RuntimeMode ParseRuntimeMode(std::span<const std::wstring_view> arguments) noexcept;
[[nodiscard]] RuntimeArguments ParseRuntimeArguments(
    std::span<const std::wstring_view> arguments) noexcept;

} // namespace nfm::app

#endif // NETWORK_FLOW_MONITOR_APP_RUNTIME_MODE_H
