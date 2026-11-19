#ifndef NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_LOG_H
#define NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_LOG_H

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace nfm::collector {

enum class CollectorLogSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

// Process-scoped append-only log for the portable collector. The collector owns no UI, no config
// store and no shell, so this file is the only record that survives after the IPC pipe is gone.
// Writes are no-ops until OpenCollectorLog succeeds.
void OpenCollectorLog(const std::filesystem::path& file_path) noexcept;
void CloseCollectorLog() noexcept;
void WriteCollectorLog(CollectorLogSeverity severity, std::string_view message) noexcept;

[[nodiscard]] bool CollectorLogIsOpen() noexcept;

} // namespace nfm::collector

#endif // NETWORK_FLOW_MONITOR_COLLECTOR_COLLECTOR_LOG_H
