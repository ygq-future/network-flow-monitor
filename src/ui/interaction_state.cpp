#include "network_flow_monitor/ui/interaction_state.h"

#include <algorithm>

namespace nfm::ui {

void InteractionStateMachine::SetState(InteractionState state) noexcept {
    if (state_ == state) {
        return;
    }
    state_ = state;
    dirty_flags_ |= kDirtyInteraction | kDirtyVisual | kDirtyLayout;
}

void InteractionStateMachine::RecalculateDeadline(TimePoint now,
                                                  const InteractionOptions& options) noexcept {
    deadline_.reset();
    if (state_ == InteractionState::Visible && target_ == MouseTarget::Anchor) {
        deadline_ = now + std::max(options.hover_delay, std::chrono::milliseconds::zero());
    } else if (state_ == InteractionState::RankingVisible && target_ == MouseTarget::None) {
        deadline_ = now + std::max(options.dismiss_delay, std::chrono::milliseconds::zero());
    } else if (state_ == InteractionState::Visible && target_ == MouseTarget::None &&
               options.auto_hide_enabled) {
        deadline_ = now + std::max(options.auto_hide_delay, std::chrono::milliseconds::zero());
    }
}

void InteractionStateMachine::Handle(InteractionEvent event, TimePoint now,
                                     const InteractionOptions& options,
                                     MouseTarget target) noexcept {
    dirty_flags_ = kDirtyNone;
    switch (event) {
    case InteractionEvent::Toggle:
        if (state_ == InteractionState::Hidden || state_ == InteractionState::EdgeHidden) {
            SetState(InteractionState::Visible);
            target_ = MouseTarget::None;
        } else if (state_ != InteractionState::DisconnectedOverlay) {
            SetState(InteractionState::Hidden);
            target_ = MouseTarget::None;
        }
        break;
    case InteractionEvent::PointerEnter:
        target_ = target;
        if (state_ == InteractionState::EdgeHidden) {
            SetState(InteractionState::Visible);
        }
        break;
    case InteractionEvent::PointerLeave:
        // Leaving the anchor starts the dismiss interval even while the pointer
        // has not yet entered the ranking popup. The popup enter event can then
        // cancel that interval by replacing the target.
        if (target == MouseTarget::None || target_ == target || target == MouseTarget::Anchor) {
            target_ = MouseTarget::None;
        }
        break;
    case InteractionEvent::DataUpdated:
        dirty_flags_ |= kDirtyData;
        break;
    case InteractionEvent::Disconnected:
        if (state_ != InteractionState::DisconnectedOverlay) {
            state_before_disconnect_ = state_;
            SetState(InteractionState::DisconnectedOverlay);
        }
        target_ = MouseTarget::None;
        break;
    case InteractionEvent::Reconnected:
        if (state_ == InteractionState::DisconnectedOverlay) {
            SetState(state_before_disconnect_);
        }
        break;
    }
    RecalculateDeadline(now, options);
}

void InteractionStateMachine::Tick(TimePoint now, const InteractionOptions& options) noexcept {
    dirty_flags_ = kDirtyNone;
    if (!deadline_.has_value() || now < *deadline_ ||
        state_ == InteractionState::DisconnectedOverlay) {
        return;
    }

    if (state_ == InteractionState::Visible && target_ == MouseTarget::Anchor) {
        SetState(InteractionState::RankingVisible);
        target_ = MouseTarget::Popup;
    } else if (state_ == InteractionState::RankingVisible && target_ == MouseTarget::None) {
        SetState(InteractionState::Visible);
    } else if (state_ == InteractionState::Visible && target_ == MouseTarget::None &&
               options.auto_hide_enabled) {
        SetState(InteractionState::EdgeHidden);
    }
    RecalculateDeadline(now, options);
}

} // namespace nfm::ui
