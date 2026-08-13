#include "network_flow_monitor/ipc/application_catalog.h"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace nfm::ipc {
namespace {

ApplicationCatalogError ValidateSnapshot(const ranking::CounterSnapshot& snapshot) noexcept {
    if (snapshot.applications.size() > kMaximumSnapshotApplications) {
        return ApplicationCatalogError::TooManyApplications;
    }
    const process::ApplicationId* previous_id{};
    for (const auto& application : snapshot.applications) {
        if (application.id.empty()) {
            return ApplicationCatalogError::EmptyApplicationId;
        }
        if (previous_id != nullptr && application.id == *previous_id) {
            return ApplicationCatalogError::DuplicateApplicationId;
        }
        if (previous_id != nullptr && application.id < *previous_id) {
            return ApplicationCatalogError::UnsortedApplicationId;
        }
        previous_id = &application.id;
    }
    return ApplicationCatalogError::None;
}

bool EnrichField(std::string& current, const std::string& observed) {
    if (!current.empty() || observed.empty()) {
        return false;
    }
    current = observed;
    return true;
}

bool EnrichMetadata(process::ApplicationMetadata& current,
                    const process::ApplicationMetadata& observed) {
    const bool display_enriched = EnrichField(current.display_name, observed.display_name);
    const bool icon_enriched = EnrichField(current.icon_key, observed.icon_key);
    return display_enriched || icon_enriched;
}

bool ContainsApplication(const ranking::CounterSnapshot& snapshot,
                         std::string_view application_id) {
    const auto found = std::ranges::lower_bound(snapshot.applications, application_id, {},
                                                &ranking::ApplicationCounters::id);
    return found != snapshot.applications.end() && found->id == application_id;
}

bool HandleOrder(const CatalogEntry& left, const CatalogEntry& right) noexcept {
    return left.handle < right.handle;
}

bool CounterHandleOrder(const ApplicationCounterWire& left,
                        const ApplicationCounterWire& right) noexcept {
    return left.handle < right.handle;
}

} // namespace

bool ApplicationCatalogResult::accepted() const noexcept {
    return error == ApplicationCatalogError::None;
}

ApplicationCatalog::ApplicationCatalog(ApplicationCatalogSeed seed)
    : next_handle_{seed.first_handle}, initial_revision_{seed.initial_revision} {}

void ApplicationCatalog::RemoveMissingApplications(const ranking::CounterSnapshot& snapshot,
                                                   RecordMap& candidate_records,
                                                   std::vector<CatalogEntry>& changes) {
    auto record = candidate_records.begin();
    while (record != candidate_records.end()) {
        if (!ContainsApplication(snapshot, record->first)) {
            changes.push_back({.handle = record->second.handle,
                               .metadata_revision = record->second.metadata_revision,
                               .removed = true,
                               .metadata = {}});
            record = candidate_records.erase(record);
        } else {
            ++record;
        }
    }
}

ApplicationCatalogError ApplicationCatalog::MergeApplications(
    const ranking::CounterSnapshot& snapshot, RecordMap& candidate_records,
    std::uint64_t& candidate_next_handle, std::vector<CatalogEntry>& changes) const {
    for (const auto& application : snapshot.applications) {
        const auto existing = candidate_records.find(application.id);
        if (existing == candidate_records.end()) {
            if (candidate_next_handle == 0U) {
                return ApplicationCatalogError::HandleExhausted;
            }
            if (initial_revision_ == 0U) {
                return ApplicationCatalogError::RevisionExhausted;
            }
            ActiveRecord record{.handle = ApplicationHandle{candidate_next_handle},
                                .metadata_revision = initial_revision_,
                                .metadata = application.metadata,
                                .upload_bytes = application.upload_bytes,
                                .download_bytes = application.download_bytes};
            changes.push_back({.handle = record.handle,
                               .metadata_revision = record.metadata_revision,
                               .removed = false,
                               .metadata = record.metadata});
            candidate_records.emplace(application.id, std::move(record));
            candidate_next_handle =
                candidate_next_handle == std::numeric_limits<std::uint64_t>::max()
                    ? 0U
                    : candidate_next_handle + 1U;
            continue;
        }

        auto& record = existing->second;
        if (application.upload_bytes < record.upload_bytes ||
            application.download_bytes < record.download_bytes) {
            return ApplicationCatalogError::CounterDecreased;
        }
        const bool metadata_enriched = EnrichMetadata(record.metadata, application.metadata);
        if (metadata_enriched) {
            if (record.metadata_revision == std::numeric_limits<std::uint32_t>::max()) {
                return ApplicationCatalogError::RevisionExhausted;
            }
            ++record.metadata_revision;
            changes.push_back({.handle = record.handle,
                               .metadata_revision = record.metadata_revision,
                               .removed = false,
                               .metadata = record.metadata});
        }
        record.upload_bytes = application.upload_bytes;
        record.download_bytes = application.download_bytes;
    }
    return ApplicationCatalogError::None;
}

ApplicationCatalogError ApplicationCatalog::AdvanceSequence(
    bool catalog_changed, std::uint64_t& candidate_sequence) noexcept {
    if (catalog_changed || candidate_sequence == 0U) {
        if (candidate_sequence == std::numeric_limits<std::uint64_t>::max()) {
            return ApplicationCatalogError::SequenceExhausted;
        }
        ++candidate_sequence;
    }
    return ApplicationCatalogError::None;
}

PublishedApplicationSet ApplicationCatalog::BuildPublishedSet(const RecordMap& candidate_records,
                                                              std::uint64_t candidate_sequence,
                                                              std::vector<CatalogEntry> changes) {
    PublishedApplicationSet published{.catalog_sequence = candidate_sequence,
                                      .changes = std::move(changes)};
    published.full_catalog.reserve(candidate_records.size());
    published.counters.reserve(candidate_records.size());
    for (const auto& [application_id, record] : candidate_records) {
        static_cast<void>(application_id);
        published.full_catalog.push_back({.handle = record.handle,
                                          .metadata_revision = record.metadata_revision,
                                          .removed = false,
                                          .metadata = record.metadata});
        published.counters.push_back({.handle = record.handle,
                                      .upload_bytes = record.upload_bytes,
                                      .download_bytes = record.download_bytes});
    }
    std::ranges::sort(published.changes, HandleOrder);
    std::ranges::sort(published.full_catalog, HandleOrder);
    std::ranges::sort(published.counters, CounterHandleOrder);
    return published;
}

ApplicationCatalogResult ApplicationCatalog::Publish(const ranking::CounterSnapshot& snapshot) {
    const auto validation_error = ValidateSnapshot(snapshot);
    if (validation_error != ApplicationCatalogError::None) {
        return {.error = validation_error};
    }

    auto candidate_records = records_;
    auto candidate_next_handle = next_handle_;
    auto candidate_sequence = catalog_sequence_;
    std::vector<CatalogEntry> changes;
    changes.reserve(records_.size() + snapshot.applications.size());

    RemoveMissingApplications(snapshot, candidate_records, changes);
    const auto merge_error =
        MergeApplications(snapshot, candidate_records, candidate_next_handle, changes);
    if (merge_error != ApplicationCatalogError::None) {
        return {.error = merge_error};
    }
    const auto sequence_error = AdvanceSequence(!changes.empty(), candidate_sequence);
    if (sequence_error != ApplicationCatalogError::None) {
        return {.error = sequence_error};
    }
    auto published = BuildPublishedSet(candidate_records, candidate_sequence, std::move(changes));

    records_ = std::move(candidate_records);
    next_handle_ = candidate_next_handle;
    catalog_sequence_ = candidate_sequence;
    return {.published = std::move(published)};
}

} // namespace nfm::ipc
