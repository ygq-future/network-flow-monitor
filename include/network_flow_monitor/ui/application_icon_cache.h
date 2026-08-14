#ifndef NETWORK_FLOW_MONITOR_UI_APPLICATION_ICON_CACHE_H
#define NETWORK_FLOW_MONITOR_UI_APPLICATION_ICON_CACHE_H

#include <windows.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace nfm::ui {

inline constexpr UINT kApplicationIconReadyMessage = WM_APP + 0x35U;
inline constexpr std::uint32_t kApplicationIconSize = 16U;
inline constexpr std::size_t kApplicationIconPixelCount =
    static_cast<std::size_t>(kApplicationIconSize) * kApplicationIconSize;

struct ApplicationIconPixels {
    std::array<std::uint32_t, kApplicationIconPixelCount> bgra{};
    bool fallback{true};
};

class ApplicationIconCache final {
  public:
    ApplicationIconCache();
    ~ApplicationIconCache();
    ApplicationIconCache(const ApplicationIconCache&) = delete;
    ApplicationIconCache& operator=(const ApplicationIconCache&) = delete;
    ApplicationIconCache(ApplicationIconCache&&) = delete;
    ApplicationIconCache& operator=(ApplicationIconCache&&) = delete;

    void SetNotifyWindow(HWND window) noexcept;
    void Request(const std::string& icon_key) noexcept;
    [[nodiscard]] std::shared_ptr<const ApplicationIconPixels> Lookup(
        const std::string& icon_key) const noexcept;
    [[nodiscard]] std::shared_ptr<const ApplicationIconPixels> fallback() const noexcept {
        return fallback_;
    }

  private:
    void Worker(const std::stop_token& stop_token) noexcept;

    std::shared_ptr<const ApplicationIconPixels> fallback_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::string> requests_;
    std::unordered_set<std::string> pending_;
    std::unordered_map<std::string, std::shared_ptr<const ApplicationIconPixels>> icons_;
    std::mutex notification_mutex_;
    HWND notify_window_{};
    std::atomic<bool> degraded_;
    std::jthread worker_;
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_APPLICATION_ICON_CACHE_H
