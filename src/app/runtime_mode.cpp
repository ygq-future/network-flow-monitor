#include "network_flow_monitor/app/runtime_mode.h"

namespace nfm::app {

namespace {

constexpr std::wstring_view kCollectorSwitch{L"--collector"};
constexpr std::wstring_view kStopEventSwitch{L"--stop-event"};
constexpr std::wstring_view kParentPidSwitch{L"--parent-pid"};
constexpr std::wstring_view kStopEventPrefix{L"Local\\NetworkFlowMonitor.Stop."};
constexpr std::size_t kMaximumStopEventNameLength = 260U;

[[nodiscard]] bool IsValidStopEventName(std::wstring_view name) noexcept {
    return !name.empty() && name.size() <= kMaximumStopEventNameLength &&
           name.starts_with(kStopEventPrefix) &&
           name.find_first_of(L" \t\r\n\"") == std::wstring_view::npos;
}

[[nodiscard]] std::uint32_t ParsePid(std::wstring_view text) noexcept {
    std::uint32_t pid = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return 0;
        }
        pid = pid * 10 + static_cast<std::uint32_t>(c - L'0');
    }
    return pid;
}

} // namespace

RuntimeMode ParseRuntimeMode(std::span<const std::wstring_view> arguments) noexcept {
    if (arguments.empty()) {
        return RuntimeMode::Collector;
    }
    if (arguments.size() != 1) {
        return RuntimeMode::Invalid;
    }

    const auto argument = arguments.front();
    if (argument == L"--collector") {
        return RuntimeMode::Collector;
    }
    if (argument == L"--authorize-collector") {
        return RuntimeMode::AuthorizeCollector;
    }
    return RuntimeMode::Invalid;
}

RuntimeArguments ParseRuntimeArguments(std::span<const std::wstring_view> arguments) noexcept {
    if (arguments.empty()) {
        return {.mode = RuntimeMode::Collector};
    }
    if (arguments.size() == 1 && arguments.front() == L"--authorize-collector") {
        return {.mode = RuntimeMode::AuthorizeCollector};
    }

    std::size_t first_option = 0;
    if (arguments.front() == kCollectorSwitch) {
        first_option = 1;
    }
    if (((arguments.size() - first_option) % 2) != 0) {
        if (arguments.size() == 1 && arguments.front() == kCollectorSwitch) {
            return {.mode = RuntimeMode::Collector};
        }
        return {.mode = RuntimeMode::Invalid};
    }

    if (first_option < arguments.size()) {
        std::wstring_view stop_event{};
        std::uint32_t parent_pid = 0;

        for (std::size_t i = first_option; i + 1 < arguments.size(); i += 2) {
            if (arguments[i] == kStopEventSwitch) {
                if (!IsValidStopEventName(arguments[i + 1])) {
                    return {.mode = RuntimeMode::Invalid};
                }
                stop_event = arguments[i + 1];
            } else if (arguments[i] == kParentPidSwitch) {
                parent_pid = ParsePid(arguments[i + 1]);
                if (parent_pid == 0) {
                    return {.mode = RuntimeMode::Invalid};
                }
            } else {
                return {.mode = RuntimeMode::Invalid};
            }
        }
        return {.mode = RuntimeMode::Collector,
                .collector_stop_event = stop_event,
                .parent_pid = parent_pid};
    }

    if (first_option == 1) {
        return {.mode = RuntimeMode::Collector};
    }
    return {.mode = RuntimeMode::Invalid};
}

} // namespace nfm::app
