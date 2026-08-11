#ifndef NETWORK_FLOW_MONITOR_UI_INTERACTION_STATE_H
#define NETWORK_FLOW_MONITOR_UI_INTERACTION_STATE_H

#include <chrono>
#include <cstdint>
#include <optional>

namespace nfm::ui {

enum class InteractionState : std::uint8_t {
    Hidden,
    Visible,
    RankingVisible,
    EdgeHidden,
    DisconnectedOverlay,
};

enum class MouseTarget : std::uint8_t {
    None,
    Anchor,
    Popup,
    RevealStrip
};

enum class InteractionEvent : std::uint8_t {
    Toggle,
    PointerEnter,
    PointerLeave,
    DataUpdated,
    Disconnected,
    Reconnected,
};

inline constexpr std::uint32_t kDirtyNone = 0U;
inline constexpr std::uint32_t kDirtyData = 1U << 0U;
inline constexpr std::uint32_t kDirtyLayout = 1U << 1U;
inline constexpr std::uint32_t kDirtyVisual = 1U << 2U;
inline constexpr std::uint32_t kDirtyInteraction = 1U << 3U;

struct InteractionOptions {
    std::chrono::milliseconds hover_delay{350};
    std::chrono::milliseconds dismiss_delay{250};
    std::chrono::milliseconds auto_hide_delay{500};
    bool auto_hide_enabled{true};
};

class InteractionStateMachine final {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    [[nodiscard]] InteractionState state() const noexcept {
        return state_;
    }
    [[nodiscard]] MouseTarget target() const noexcept {
        return target_;
    }
    [[nodiscard]] std::uint32_t last_dirty_flags() const noexcept {
        return dirty_flags_;
    }
    [[nodiscard]] std::optional<TimePoint> next_deadline() const noexcept {
        return deadline_;
    }

    void Handle(InteractionEvent event, TimePoint now, const InteractionOptions& options,
                MouseTarget target = MouseTarget::None) noexcept;
    void Tick(TimePoint now, const InteractionOptions& options) noexcept;

  private:
    void SetState(InteractionState state) noexcept;
    void RecalculateDeadline(TimePoint now, const InteractionOptions& options) noexcept;

    InteractionState state_{InteractionState::Hidden};
    InteractionState state_before_disconnect_{InteractionState::Hidden};
    MouseTarget target_{MouseTarget::None};
    std::optional<TimePoint> deadline_;
    std::uint32_t dirty_flags_{kDirtyInteraction | kDirtyVisual};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_INTERACTION_STATE_H
