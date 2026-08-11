#include "network_flow_monitor/ipc/snapshot_publisher.h"

#include "network_flow_monitor/config/app_config.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <ranges>
#include <utility>

namespace nfm::ipc {
namespace {

PublisherResult PublisherFailure(PublisherError error) noexcept {
    return {.error = error};
}

} // namespace

SnapshotPublisher::SnapshotPublisher(CollectorInstanceId instance,
                                     std::chrono::steady_clock::time_point instance_started_at,
                                     SnapshotProvider& provider, IpcServer& server)
    : instance_{instance}, instance_started_at_{instance_started_at}, provider_{&provider},
      server_{&server} {}

bool SnapshotPublisher::IsDue(const SubscriptionState& subscription,
                              std::chrono::steady_clock::time_point now) noexcept {
    return !subscription.next_deadline.has_value() || *subscription.next_deadline <= now;
}

bool SnapshotPublisher::IsValidSubscription(const ClientSubscription& subscription) noexcept {
    return subscription.client_id != 0U &&
           subscription.refresh_interval >= config::kMinimumRefreshInterval &&
           subscription.refresh_interval <= config::kMaximumRefreshInterval;
}

std::uint64_t SnapshotPublisher::UptimeMicroseconds(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point started_at) noexcept {
    if (now <= started_at) {
        return 0U;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - started_at);
    if (elapsed.count() <= 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(elapsed.count());
}

void SnapshotPublisher::SubscriptionChanged(ClientSubscription subscription) noexcept {
    const std::scoped_lock lock{mutex_};
    if (!IsValidSubscription(subscription)) {
        pending_error_ = PublisherError::InvalidSubscription;
        return;
    }
    try {
        subscriptions_.insert_or_assign(
            subscription.client_id,
            SubscriptionState{.refresh_interval = subscription.refresh_interval,
                              .next_deadline = std::nullopt});
    } catch (...) {
        pending_error_ = PublisherError::CaptureFailed;
    }
}

void SnapshotPublisher::ClientDisconnected(std::uint64_t client_id) noexcept {
    const std::scoped_lock lock{mutex_};
    subscriptions_.erase(client_id);
}

void SnapshotPublisher::ScheduleRetry(std::chrono::steady_clock::time_point now) noexcept {
    const auto retry_at = now + config::kMinimumRefreshInterval;
    for (auto& [client_id, subscription] : subscriptions_) {
        static_cast<void>(client_id);
        if (IsDue(subscription, now)) {
            subscription.next_deadline = retry_at;
        }
    }
}

void SnapshotPublisher::ScheduleCompleted(std::chrono::steady_clock::time_point now,
                                          std::span<const std::uint64_t> due_clients) noexcept {
    for (const auto client_id : due_clients) {
        const auto subscription = subscriptions_.find(client_id);
        if (subscription != subscriptions_.end()) {
            subscription->second.next_deadline = now + subscription->second.refresh_interval;
        }
    }
}

PublisherResult SnapshotPublisher::PublishStatus(
    const CollectorCapture& capture, std::chrono::steady_clock::time_point now) noexcept {
    const auto uptime = UptimeMicroseconds(now, instance_started_at_);
    if (uptime == 0U) {
        return PublisherFailure(PublisherError::UptimeOverflow);
    }
    auto status = std::make_shared<CollectorStatusMessage>(
        CollectorStatusMessage{.uptime_microseconds = uptime,
                               .collector_health = capture.health,
                               .ipc_health = server_->health()});
    try {
        if (!server_->PublishStatus(std::move(status)).ok()) {
            return PublisherFailure(PublisherError::ServerFailed);
        }
    } catch (...) {
        return PublisherFailure(PublisherError::ServerFailed);
    }
    return {};
}

PublisherResult SnapshotPublisher::RunDue(std::chrono::steady_clock::time_point now) noexcept {
    const std::scoped_lock lock{mutex_};
    if (pending_error_ != PublisherError::None) {
        const auto error = pending_error_;
        pending_error_ = PublisherError::None;
        return PublisherFailure(error);
    }
    if (last_run_.has_value() && now < *last_run_) {
        return PublisherFailure(PublisherError::TimeMovedBackward);
    }
    last_run_ = now;

    std::vector<std::uint64_t> due_clients;
    try {
        due_clients.reserve(subscriptions_.size());
        for (const auto& [client_id, subscription] : subscriptions_) {
            if (IsDue(subscription, now)) {
                due_clients.push_back(client_id);
            }
        }
    } catch (...) {
        return PublisherFailure(PublisherError::CaptureFailed);
    }
    if (due_clients.empty()) {
        return {};
    }

    const auto capture = provider_->Capture();
    if (capture.error != CaptureError::None) {
        const auto status_result = PublishStatus(capture.capture, now);
        ScheduleRetry(now);
        if (!status_result.ok()) {
            return status_result;
        }
        return PublisherFailure(PublisherError::CaptureFailed);
    }

    if (capture.capture.counters.captured_at == std::chrono::steady_clock::time_point{}) {
        ScheduleRetry(now);
        return PublisherFailure(PublisherError::CaptureFailed);
    }
    if (last_capture_.has_value() && capture.capture.counters.captured_at <= *last_capture_) {
        ScheduleRetry(now);
        return PublisherFailure(PublisherError::TimeMovedBackward);
    }

    const auto uptime = UptimeMicroseconds(now, instance_started_at_);
    if (uptime == 0U) {
        ScheduleRetry(now);
        return PublisherFailure(PublisherError::UptimeOverflow);
    }
    if (snapshot_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        ScheduleRetry(now);
        return PublisherFailure(PublisherError::SequenceExhausted);
    }

    try {
        auto candidate_catalog = catalog_;
        const auto catalog_result = candidate_catalog.Publish(capture.capture.counters);
        if (!catalog_result.accepted()) {
            ScheduleRetry(now);
            if (catalog_result.error == ApplicationCatalogError::SequenceExhausted) {
                return PublisherFailure(PublisherError::SequenceExhausted);
            }
            return PublisherFailure(PublisherError::CatalogFailed);
        }

        const auto next_sequence = snapshot_sequence_ + 1U;
        auto published = std::make_shared<PublishedSnapshot>();
        published->message =
            SnapshotMessage{.snapshot_sequence = next_sequence,
                            .required_catalog_sequence = catalog_result.published.catalog_sequence,
                            .uptime_microseconds = uptime,
                            .applications_aggregated_to_other =
                                capture.capture.health.aggregated_application_events != 0U,
                            .collector_health = capture.capture.health,
                            .ipc_health = server_->health(),
                            .applications = catalog_result.published.counters};
        published->latest_changes = catalog_result.published.changes;
        published->full_catalog = catalog_result.published.full_catalog;
        published->recipient_client_ids = due_clients;
        if (!server_->Publish(std::move(published)).ok()) {
            ScheduleRetry(now);
            return PublisherFailure(PublisherError::ServerFailed);
        }

        catalog_ = std::move(candidate_catalog);
        snapshot_sequence_ = next_sequence;
        last_capture_ = capture.capture.counters.captured_at;
        ScheduleCompleted(now, due_clients);
        return {};
    } catch (...) {
        ScheduleRetry(now);
        return PublisherFailure(PublisherError::CatalogFailed);
    }
}

std::optional<std::chrono::steady_clock::time_point> SnapshotPublisher::next_deadline()
    const noexcept {
    const std::scoped_lock lock{mutex_};
    if (subscriptions_.empty()) {
        return std::nullopt;
    }
    std::optional<std::chrono::steady_clock::time_point> result;
    for (const auto& [client_id, subscription] : subscriptions_) {
        static_cast<void>(client_id);
        if (!subscription.next_deadline.has_value()) {
            return std::chrono::steady_clock::time_point{};
        }
        if (!result.has_value() || *subscription.next_deadline < *result) {
            result = subscription.next_deadline;
        }
    }
    return result;
}

} // namespace nfm::ipc
