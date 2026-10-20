#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_APP_UPDATER_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_APP_UPDATER_H

#include "network_flow_monitor/host/bridge/bridge_types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
namespace nfm::host::platform {

struct SemanticVersion {
    std::uint32_t major{0};
    std::uint32_t minor{0};
    std::uint32_t patch{0};

    [[nodiscard]] constexpr auto operator<=>(const SemanticVersion&) const noexcept = default;
};

struct ReleaseAssetInfo {
    std::string version;
    std::string download_url;
    std::uint64_t asset_size_bytes{0};
};

enum class HttpAccessMode : std::uint8_t {
    AutomaticProxy = 0,
    NamedProxy = 1,
    NoProxy = 2,
};

struct HttpAccessCandidate {
    HttpAccessMode mode{HttpAccessMode::AutomaticProxy};
    std::wstring proxy_server;
};

struct HttpProxyConfigSources {
    std::wstring_view env_proxy;
    std::wstring_view registry_proxy;
    bool registry_proxy_enabled{false};
};

struct AppUpdaterOptions {
    std::string current_version;
    std::string github_repo;
    std::filesystem::path app_root_dir;
    std::filesystem::path host_executable_path;
};

class AppUpdater {
  public:
    using StatusCallback = std::function<void(const bridge::UpdateStatusDto&)>;
    using DiagnosticCallback = std::function<void(bool is_error, std::string_view message)>;
    using ShutdownRequestCallback = std::function<void()>;

    explicit AppUpdater(AppUpdaterOptions options);
    ~AppUpdater();

    AppUpdater(const AppUpdater&) = delete;
    AppUpdater& operator=(const AppUpdater&) = delete;
    AppUpdater(AppUpdater&&) = delete;
    AppUpdater& operator=(AppUpdater&&) = delete;

    void set_status_callback(StatusCallback callback);
    void set_diagnostic_callback(DiagnosticCallback callback);
    void set_shutdown_request_callback(ShutdownRequestCallback callback);

    [[nodiscard]] bridge::UpdateStatusDto status() const;

    void check_for_updates_async();
    void download_and_apply_async();
    void cancel_and_wait() noexcept;

    // Pure testable helpers
    [[nodiscard]] static std::optional<SemanticVersion> parse_semver(
        std::string_view version_text) noexcept;
    [[nodiscard]] static bool is_newer_version(std::string_view current_version,
                                               std::string_view candidate_version) noexcept;
    [[nodiscard]] static std::optional<ReleaseAssetInfo> parse_github_release_json(
        std::string_view json_payload, std::string* error_out = nullptr);
    [[nodiscard]] static std::wstring build_apply_update_script(
        std::uint32_t host_pid, const std::filesystem::path& staged_dir,
        const std::filesystem::path& app_root_dir, const std::filesystem::path& host_exe_path);
    [[nodiscard]] static bool write_utf8_script(const std::filesystem::path& script_path,
                                                std::wstring_view script_content,
                                                std::string* error_out = nullptr);
    [[nodiscard]] static bool launch_detached_update_script(
        const std::filesystem::path& script_path, std::string* error_out = nullptr);

    [[nodiscard]] static std::wstring normalize_winhttp_proxy_server(std::wstring_view raw_proxy);
    [[nodiscard]] static std::vector<HttpAccessCandidate> build_http_access_candidates(
        const HttpProxyConfigSources& sources);

  private:
    void update_state(const bridge::UpdateStatusDto& next_status);
    void log_diagnostic(bool is_error, std::string_view message) const noexcept;
    void run_check_worker();
    void run_download_and_apply_worker(const ReleaseAssetInfo& release_info);

    AppUpdaterOptions options_;
    mutable std::mutex mutex_;
    bridge::UpdateStatusDto status_;
    std::optional<ReleaseAssetInfo> latest_release_;
    StatusCallback status_callback_;
    DiagnosticCallback diagnostic_callback_;
    ShutdownRequestCallback shutdown_callback_;
    std::atomic_bool stop_requested_{false};
    std::atomic_bool worker_busy_{false};
    std::thread worker_thread_;
};

} // namespace nfm::host::platform

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_APP_UPDATER_H
