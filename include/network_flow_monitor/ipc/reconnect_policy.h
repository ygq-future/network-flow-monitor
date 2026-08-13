#ifndef NETWORK_FLOW_MONITOR_IPC_RECONNECT_POLICY_H
#define NETWORK_FLOW_MONITOR_IPC_RECONNECT_POLICY_H

#include <array>
#include <chrono>
#include <cstddef>

namespace nfm::ipc {

inline constexpr std::array kReconnectDelays{
    std::chrono::milliseconds{250},   std::chrono::milliseconds{500},
    std::chrono::milliseconds{1'000}, std::chrono::milliseconds{2'000},
    std::chrono::milliseconds{5'000},
};

class ReconnectJitter {
  public:
    ReconnectJitter() = default;
    ReconnectJitter(const ReconnectJitter&) = delete;
    ReconnectJitter& operator=(const ReconnectJitter&) = delete;
    ReconnectJitter(ReconnectJitter&&) = delete;
    ReconnectJitter& operator=(ReconnectJitter&&) = delete;
    virtual ~ReconnectJitter() = default;

    [[nodiscard]] virtual std::chrono::milliseconds Adjust(std::chrono::milliseconds base,
                                                           std::size_t attempt) noexcept = 0;
};

class ReconnectPolicy final {
  public:
    explicit ReconnectPolicy(ReconnectJitter* jitter = nullptr) noexcept;

    [[nodiscard]] std::chrono::milliseconds NextDelay() noexcept;
    void Reset() noexcept;

    [[nodiscard]] std::size_t attempt() const noexcept {
        return attempt_;
    }

  private:
    ReconnectJitter* jitter_{};
    std::size_t attempt_{};
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_RECONNECT_POLICY_H
