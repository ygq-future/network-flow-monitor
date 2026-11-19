#include "network_flow_monitor/collector/etw_snapshot_provider.h"

#include "network_flow_monitor/collector/collector_log.h"

#include <chrono>
#include <cstdint>
#include <iphlpapi.h>
#include <string>

namespace nfm::collector {
namespace {

constexpr auto kDeliveryCheckInterval = std::chrono::seconds{5};

// Sum of TCP segments and UDP datagrams, both directions, across IPv4. It moves whenever the
// machine exchanges traffic, independently of whether ETW is still delivering events.
[[nodiscard]] std::uint64_t QueryNetworkStackActivity() noexcept {
    try {
        std::uint64_t activity = 0U;
        MIB_TCPSTATS tcp{};
        if (::GetTcpStatistics(&tcp) == NO_ERROR) {
            activity += tcp.dwInSegs;
            activity += tcp.dwOutSegs;
        }
        MIB_UDPSTATS udp{};
        if (::GetUdpStatistics(&udp) == NO_ERROR) {
            activity += udp.dwInDatagrams;
            activity += udp.dwOutDatagrams;
        }
        return activity;
    } catch (...) {
        return 0U;
    }
}

} // namespace

ipc::CaptureResult EtwSnapshotProvider::Capture() noexcept {
    const auto flushed = session_->FlushAndQuery();
    if (!flushed.ok()) {
        if (!capture_failing_) {
            capture_failing_ = true;
            try {
                WriteCollectorLog(CollectorLogSeverity::Error,
                                  "Capture failed (stage=flush/query, win32_error=" +
                                      std::to_string(flushed.win32_error) + ")");
            } catch (...) {
            }
        }
        return {.error = ipc::CaptureError::FlushFailed,
                .capture = {.health = session_->health(), .counters = {}},
                .source_error = flushed.win32_error};
    }
    if (capture_failing_) {
        capture_failing_ = false;
        WriteCollectorLog(CollectorLogSeverity::Info, "Capture recovered");
    }
    try {
        const auto captured_at = std::chrono::steady_clock::now();
        auto health = session_->health();
        if (!delivery_check_at_.has_value() ||
            captured_at - *delivery_check_at_ >= kDeliveryCheckInterval) {
            const EtwDeliverySample current{.stack_activity = QueryNetworkStackActivity(),
                                            .events_received = health.events_received};
            if (delivery_sample_.has_value() && IsEtwDeliveryStalled(*delivery_sample_, current)) {
                session_->MarkDeliveryStalled(current.stack_activity -
                                              delivery_sample_->stack_activity);
                health = session_->health();
            }
            delivery_sample_ = current;
            delivery_check_at_ = captured_at;
        }
        return {.capture = {.health = health,
                            .counters = session_->MakeSnapshot(captured_at),
                            .active_pids =
                                static_cast<std::uint32_t>(session_->active_process_count())}};
    } catch (...) {
        return {.error = ipc::CaptureError::SnapshotFailed,
                .capture = {.health = session_->health(), .counters = {}}};
    }
}

} // namespace nfm::collector
