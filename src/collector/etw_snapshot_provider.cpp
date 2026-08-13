#include "network_flow_monitor/collector/etw_snapshot_provider.h"

#include <chrono>

namespace nfm::collector {

ipc::CaptureResult EtwSnapshotProvider::Capture() noexcept {
    const auto flushed = session_->FlushAndQuery();
    if (!flushed.ok()) {
        return {.error = ipc::CaptureError::FlushFailed,
                .capture = {.health = session_->health(), .counters = {}},
                .source_error = flushed.win32_error};
    }
    try {
        const auto captured_at = std::chrono::steady_clock::now();
        return {.capture = {.health = session_->health(),
                            .counters = session_->MakeSnapshot(captured_at)}};
    } catch (...) {
        return {.error = ipc::CaptureError::SnapshotFailed,
                .capture = {.health = session_->health(), .counters = {}}};
    }
}

} // namespace nfm::collector
