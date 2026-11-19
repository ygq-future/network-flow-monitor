#ifndef NETWORK_FLOW_MONITOR_HOST_LIFECYCLE_DATA_LIVENESS_MONITOR_H
#define NETWORK_FLOW_MONITOR_HOST_LIFECYCLE_DATA_LIVENESS_MONITOR_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace nfm::host::lifecycle {

// Observable outcome of one liveness evaluation. The host turns each action into a log line
// and/or a recovery step; the monitor itself performs no I/O.
enum class DataLivenessAction : std::uint8_t {
    CollectorStallDetected,
    CollectorStallCleared,
    CollectorUnavailable,
    CollectorRestartRequested,
    PageStallDetected,
    PageStallCleared,
    WebViewReloadRequested,
};

struct DataLivenessPolicy {
    // Collector: a 250 ms tick / 1 s ranking stream must keep advancing.
    std::chrono::milliseconds collector_stall_log{3000};
    std::chrono::milliseconds collector_stall_restart{10000};
    // No streaming collector at all while one is expected (crash, refused elevation, wedged start).
    std::chrono::milliseconds collector_down_restart{10000};
    // The collector itself reports ETW inactivity through the status frame.
    std::chrono::milliseconds etw_inactive_restart{3000};
    // Page: the UI acknowledges the traffic frames it receives.
    std::chrono::milliseconds page_stall_log{5000};
    std::chrono::milliseconds page_stall_reload{10000};
    // Shared back-off so a recovery loop cannot spin (restart / reload storms).
    std::chrono::milliseconds recovery_cooldown{60000};
};

struct DataLivenessObservation {
    std::chrono::steady_clock::time_point now;
    // True while the launcher reports a live, streaming collector process.
    bool collector_streaming{false};
    // Monotonic count of traffic frames accepted from the collector.
    std::uint64_t frames_received{0};
    // Last collector status reported an active ETW session.
    bool etw_active{true};
    // The main WebView2 surface can receive messages.
    bool webview_ready{false};
    // Monotonic count of traffic-frame acknowledgements posted by the page.
    std::uint64_t page_acks{0};
    // Frames were forwarded to the page since the previous observation.
    bool frames_delivered{false};
    // The host still needs traffic data (it is not shutting down), so a missing collector is a
    // fault rather than an intentional stop.
    bool collector_expected{false};
};

// Distinguishes "the collector stopped producing traffic" from "the UI stopped consuming it".
// Evaluated from the surface clock (UI thread), which keeps running while the traffic pipeline is
// frozen, so a stalled pipeline always yields a log line and a bounded recovery attempt.
class DataLivenessMonitor {
  public:
    explicit DataLivenessMonitor(DataLivenessPolicy policy = {}) noexcept;

    [[nodiscard]] std::vector<DataLivenessAction> Observe(
        const DataLivenessObservation& observation);

    // Frame age measured during the latest observation, for the host's log line.
    [[nodiscard]] std::chrono::milliseconds last_stall_duration() const noexcept;

  private:
    [[nodiscard]] bool CollectorRecoveryAllowed(
        std::chrono::steady_clock::time_point now) const noexcept;
    [[nodiscard]] bool PageRecoveryAllowed(
        std::chrono::steady_clock::time_point now) const noexcept;

    DataLivenessPolicy policy_;
    std::optional<std::chrono::steady_clock::time_point> last_frame_at_;
    std::chrono::milliseconds last_stall_duration_{0};
    std::uint64_t last_frames_received_{0};
    bool collector_streaming_{false};
    bool collector_stall_logged_{false};
    bool collector_restart_issued_{false};
    std::optional<std::chrono::steady_clock::time_point> collector_recovery_at_;

    bool etw_seen_active_{false};
    std::optional<std::chrono::steady_clock::time_point> etw_inactive_since_;

    std::optional<std::chrono::steady_clock::time_point> collector_down_since_;
    bool collector_down_logged_{false};

    std::optional<std::chrono::steady_clock::time_point> last_ack_at_;
    std::uint64_t last_page_acks_{0};
    bool page_tracking_{false};
    bool page_stall_logged_{false};
    bool page_reload_issued_{false};
    std::optional<std::chrono::steady_clock::time_point> page_recovery_at_;
};

} // namespace nfm::host::lifecycle

#endif // NETWORK_FLOW_MONITOR_HOST_LIFECYCLE_DATA_LIVENESS_MONITOR_H
