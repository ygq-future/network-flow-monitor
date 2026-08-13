#include "network_flow_monitor/ipc/application_catalog.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using nfm::ipc::ApplicationCatalog;
using nfm::ipc::ApplicationCatalogError;
using nfm::ipc::ApplicationCatalogSeed;
using nfm::ranking::ApplicationCounters;
using nfm::ranking::CounterSnapshot;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

ApplicationCounters Application(std::string id, std::uint64_t upload, std::uint64_t download,
                                std::string display = {}, std::string icon = {}) {
    return {.id = std::move(id),
            .metadata = {.display_name = std::move(display), .icon_key = std::move(icon)},
            .upload_bytes = upload,
            .download_bytes = download};
}

CounterSnapshot Snapshot(std::vector<ApplicationCounters> applications) {
    return {.captured_at = std::chrono::steady_clock::time_point{},
            .applications = std::move(applications)};
}

std::string IndexedId(std::size_t index) {
    auto suffix = std::to_string(index);
    suffix.insert(suffix.begin(), 6U - suffix.size(), '0');
    return "app." + suffix;
}

void CheckDeterministicAssignment(TestContext& context) {
    ApplicationCatalog catalog;
    const auto unsorted =
        catalog.Publish(Snapshot({Application("app.b", 2U, 4U), Application("app.a", 1U, 3U)}));
    context.Check(unsorted.error == ApplicationCatalogError::UnsortedApplicationId,
                  "unsorted input rejected explicitly");

    const auto first = catalog.Publish(
        Snapshot({Application("app.a", 1U, 3U, "A"), Application("app.b", 2U, 4U, "B")}));
    context.Check(first.accepted() && first.published.catalog_sequence == 1U &&
                      first.published.changes.size() == 2U &&
                      first.published.full_catalog.size() == 2U &&
                      first.published.counters.size() == 2U,
                  "first publication creates baseline and changes");
    if (first.published.full_catalog.size() == 2U && first.published.counters.size() == 2U) {
        context.Check(first.published.full_catalog.front().handle.value == 1U &&
                          first.published.full_catalog.front().metadata.display_name == "A" &&
                          first.published.full_catalog.back().handle.value == 2U &&
                          first.published.full_catalog.back().metadata.display_name == "B",
                      "sorted IDs receive deterministic consecutive handles");
        context.Check(first.published.counters.front().handle.value == 1U &&
                          first.published.counters.front().upload_bytes == 1U &&
                          first.published.counters.back().handle.value == 2U &&
                          first.published.counters.back().download_bytes == 4U,
                      "counters map to canonical handles");
    }
}

void CheckStabilityEnrichmentAndRemoval(TestContext& context) {
    ApplicationCatalog catalog;
    const auto initial = catalog.Publish(Snapshot({Application("app", 10U, 20U)}));
    context.Check(initial.accepted(), "initial sparse application accepted");

    const auto stable = catalog.Publish(Snapshot({Application("app", 11U, 21U)}));
    context.Check(stable.accepted() && stable.published.catalog_sequence == 1U &&
                      stable.published.changes.empty() &&
                      stable.published.full_catalog.front().metadata_revision == 1U,
                  "same ID and metadata preserve handle and revision");

    const auto enriched =
        catalog.Publish(Snapshot({Application("app", 12U, 22U, "Application", "app.exe")}));
    context.Check(enriched.accepted() && enriched.published.catalog_sequence == 2U &&
                      enriched.published.changes.size() == 1U &&
                      enriched.published.changes.front().handle.value == 1U &&
                      enriched.published.changes.front().metadata_revision == 2U &&
                      enriched.published.changes.front().metadata.display_name == "Application" &&
                      enriched.published.changes.front().metadata.icon_key == "app.exe",
                  "metadata enrichment increments revision on the same handle");

    const auto conflict =
        catalog.Publish(Snapshot({Application("app", 13U, 23U, "Conflict", "other.exe")}));
    context.Check(conflict.accepted() && conflict.published.changes.empty() &&
                      conflict.published.full_catalog.front().metadata_revision == 2U &&
                      conflict.published.full_catalog.front().metadata.display_name ==
                          "Application",
                  "first non-empty metadata remains stable on conflict");

    const auto removed = catalog.Publish(Snapshot({}));
    context.Check(removed.accepted() && removed.published.catalog_sequence == 3U &&
                      removed.published.changes.size() == 1U &&
                      removed.published.changes.front().removed &&
                      removed.published.changes.front().handle.value == 1U &&
                      removed.published.changes.front().metadata_revision == 2U &&
                      removed.published.full_catalog.empty() && removed.published.counters.empty(),
                  "disappearance emits removal and clears baseline");

    const auto reappeared = catalog.Publish(Snapshot({Application("app", 1U, 2U, "New")}));
    context.Check(reappeared.accepted() && reappeared.published.catalog_sequence == 4U &&
                      reappeared.published.changes.front().handle.value == 2U &&
                      reappeared.published.changes.front().metadata_revision == 1U,
                  "removed ID reappears with a new never-reused handle");
}

void CheckInvalidInputRollback(TestContext& context) {
    ApplicationCatalog catalog;
    context.Check(catalog.Publish(Snapshot({Application("", 1U, 1U)})).error ==
                      ApplicationCatalogError::EmptyApplicationId,
                  "empty application ID rejected");
    context.Check(
        catalog.Publish(Snapshot({Application("a", 1U, 1U), Application("a", 2U, 2U)})).error ==
            ApplicationCatalogError::DuplicateApplicationId,
        "duplicate application ID rejected");

    const auto accepted = catalog.Publish(Snapshot({Application("a", 1U, 1U)}));
    context.Check(accepted.accepted() && accepted.published.catalog_sequence == 1U &&
                      accepted.published.full_catalog.front().handle.value == 1U,
                  "input failures leave handle and sequence state unchanged");

    const auto decreased = catalog.Publish(Snapshot({Application("a", 0U, 2U, "ignored")}));
    context.Check(decreased.error == ApplicationCatalogError::CounterDecreased,
                  "same-handle counter decrease rejected");
    const auto recovered = catalog.Publish(Snapshot({Application("a", 2U, 2U)}));
    context.Check(recovered.accepted() && recovered.published.catalog_sequence == 1U &&
                      recovered.published.changes.empty() &&
                      recovered.published.counters.front().upload_bytes == 2U,
                  "counter failure rolls back metadata, sequence and counters");
}

void CheckCapacityAndRollback(TestContext& context) {
    ApplicationCatalog catalog;
    std::vector<ApplicationCounters> maximum;
    maximum.reserve(nfm::ipc::kMaximumSnapshotApplications);
    for (std::size_t index = 0U; index < nfm::ipc::kMaximumSnapshotApplications; ++index) {
        maximum.push_back(Application(IndexedId(index), index, index + 1U));
    }
    const auto accepted = catalog.Publish(Snapshot(maximum));
    context.Check(accepted.accepted() &&
                      accepted.published.full_catalog.size() ==
                          nfm::ipc::kMaximumSnapshotApplications &&
                      accepted.published.full_catalog.back().handle.value ==
                          nfm::ipc::kMaximumSnapshotApplications,
                  "exactly 4096 applications accepted");

    maximum.push_back(Application("app.999999", 1U, 1U));
    context.Check(catalog.Publish(Snapshot(maximum)).error ==
                      ApplicationCatalogError::TooManyApplications,
                  "application 4097 rejected");
    maximum.pop_back();
    for (auto& application : maximum) {
        ++application.upload_bytes;
        ++application.download_bytes;
    }
    const auto after_failure = catalog.Publish(Snapshot(maximum));
    context.Check(after_failure.accepted() && after_failure.published.catalog_sequence == 1U &&
                      after_failure.published.changes.empty() &&
                      after_failure.published.full_catalog.back().handle.value ==
                          nfm::ipc::kMaximumSnapshotApplications,
                  "capacity failure preserves all prior mappings");
}

void CheckExhaustionSeams(TestContext& context) {
    ApplicationCatalog handles{
        ApplicationCatalogSeed{.first_handle = std::numeric_limits<std::uint64_t>::max()}};
    const auto failed_pair =
        handles.Publish(Snapshot({Application("a", 1U, 1U), Application("b", 1U, 1U)}));
    context.Check(failed_pair.error == ApplicationCatalogError::HandleExhausted,
                  "handle exhaustion detected inside a candidate publication");
    const auto final_handle = handles.Publish(Snapshot({Application("a", 1U, 1U)}));
    context.Check(final_handle.accepted() &&
                      final_handle.published.full_catalog.front().handle.value ==
                          std::numeric_limits<std::uint64_t>::max(),
                  "handle exhaustion failure rolls back the last available handle");
    context.Check(handles.Publish(Snapshot({Application("b", 1U, 1U)})).error ==
                      ApplicationCatalogError::HandleExhausted,
                  "removed maximum handle is never reused");

    ApplicationCatalog revisions{ApplicationCatalogSeed{
        .first_handle = 1U, .initial_revision = std::numeric_limits<std::uint32_t>::max()}};
    context.Check(revisions.Publish(Snapshot({Application("a", 1U, 1U, "A")})).accepted(),
                  "maximum initial revision accepted through test seam");
    const auto exhausted = revisions.Publish(Snapshot({Application("a", 2U, 2U, "A", "a.exe")}));
    context.Check(exhausted.error == ApplicationCatalogError::RevisionExhausted,
                  "metadata enrichment detects revision exhaustion");
    const auto recovered = revisions.Publish(Snapshot({Application("a", 2U, 2U, "A")}));
    context.Check(recovered.accepted() && recovered.published.catalog_sequence == 1U &&
                      recovered.published.changes.empty() &&
                      recovered.published.full_catalog.front().metadata.icon_key.empty(),
                  "revision failure preserves metadata and catalog sequence");
}

} // namespace

int main() {
    TestContext context;
    CheckDeterministicAssignment(context);
    CheckStabilityEnrichmentAndRemoval(context);
    CheckInvalidInputRollback(context);
    CheckCapacityAndRollback(context);
    CheckExhaustionSeams(context);

    if (context.failures == 0) {
        std::cout << "ipc application catalog tests passed\n";
    }
    return context.failures == 0 ? 0 : 1;
}
