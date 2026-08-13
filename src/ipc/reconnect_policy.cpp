#include "network_flow_monitor/ipc/reconnect_policy.h"

#include <algorithm>

namespace nfm::ipc {

ReconnectPolicy::ReconnectPolicy(ReconnectJitter* jitter) noexcept : jitter_{jitter} {}

std::chrono::milliseconds ReconnectPolicy::NextDelay() noexcept {
    const auto index = std::min(attempt_, kReconnectDelays.size() - 1U);
    const auto base = kReconnectDelays.at(index);
    const auto adjusted = jitter_ == nullptr ? base : jitter_->Adjust(base, attempt_);
    ++attempt_;
    return std::clamp(adjusted, std::chrono::milliseconds::zero(), kReconnectDelays.back());
}

void ReconnectPolicy::Reset() noexcept {
    attempt_ = 0U;
}

} // namespace nfm::ipc
