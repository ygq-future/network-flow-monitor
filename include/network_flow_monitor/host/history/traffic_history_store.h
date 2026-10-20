#ifndef NETWORK_FLOW_MONITOR_HOST_HISTORY_TRAFFIC_HISTORY_STORE_H
#define NETWORK_FLOW_MONITOR_HOST_HISTORY_TRAFFIC_HISTORY_STORE_H

#include "network_flow_monitor/host/bridge/bridge_types.h"
#include "network_flow_monitor/ipc/protocol_types.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nfm::host::history {

inline constexpr std::uint32_t kTrafficHistorySchemaVersion = 1U;
inline constexpr std::size_t kMaximumTrafficHistoryDays = 3650U; // ~10 years
inline constexpr std::size_t kMaximumAppsPerDay = 2048U;

using LocalDateProvider = std::function<std::string()>;

[[nodiscard]] std::string current_local_date_ymd();
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] std::string normalize_history_app_key(std::string_view app_id,
                                                    std::string_view executable_path,
                                                    std::string_view display_name);

class TrafficHistoryStore {
  public:
    explicit TrafficHistoryStore(std::filesystem::path file_path = {},
                                 LocalDateProvider date_provider = current_local_date_ymd);
    ~TrafficHistoryStore() = default;

    TrafficHistoryStore(const TrafficHistoryStore&) = delete;
    TrafficHistoryStore& operator=(const TrafficHistoryStore&) = delete;
    TrafficHistoryStore(TrafficHistoryStore&&) = delete;
    TrafficHistoryStore& operator=(TrafficHistoryStore&&) = delete;

    void set_file_path(std::filesystem::path file_path);
    [[nodiscard]] std::filesystem::path file_path() const;

    void set_date_provider(LocalDateProvider date_provider);

    bool load();
    bool save();
    bool save_if_dirty();

    void on_session_reset(std::string_view collector_instance_id);

    // Records incremental traffic deltas from an incoming Collector RankingSnapshotMessage,
    // automatically skipping applications matching excluded_programs.
    // Returns true if any non-zero byte delta was recorded.
    bool record_snapshot(const ipc::RankingSnapshotMessage& snapshot,
                         const std::vector<std::string>& excluded_programs);

    // Exports the non-excluded 2D history view (sorted newest date first, apps by total bytes
    // desc).
    [[nodiscard]] bridge::TrafficHistoryDto export_dto(
        const std::vector<std::string>& excluded_programs, bool resolve_icons = true) const;

    [[nodiscard]] bool is_dirty() const noexcept;

  private:
    struct AppCumulativeBaseline {
        std::uint64_t total_download_bytes{0U};
        std::uint64_t total_upload_bytes{0U};
    };

    mutable std::mutex mutex_;
    std::filesystem::path file_path_;
    LocalDateProvider date_provider_;
    std::string active_collector_instance_id_;
    std::unordered_map<std::string, AppCumulativeBaseline> session_baselines_;
    // date ("YYYY-MM-DD") -> (normalized_app_key -> entry)
    std::map<std::string, std::map<std::string, bridge::TrafficHistoryAppEntryDto>> days_;
    bool dirty_{false};
};

} // namespace nfm::host::history

#endif // NETWORK_FLOW_MONITOR_HOST_HISTORY_TRAFFIC_HISTORY_STORE_H
