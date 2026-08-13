#ifndef NETWORK_FLOW_MONITOR_IPC_APPLICATION_CATALOG_H
#define NETWORK_FLOW_MONITOR_IPC_APPLICATION_CATALOG_H

#include "network_flow_monitor/ipc/snapshot_codec.h"
#include "network_flow_monitor/ranking/counter_types.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace nfm::ipc {

struct PublishedApplicationSet {
    std::uint64_t catalog_sequence{};
    std::vector<CatalogEntry> changes;
    std::vector<CatalogEntry> full_catalog;
    std::vector<ApplicationCounterWire> counters;
};

enum class ApplicationCatalogError : std::uint8_t {
    None,
    EmptyApplicationId,
    DuplicateApplicationId,
    UnsortedApplicationId,
    HandleExhausted,
    RevisionExhausted,
    SequenceExhausted,
    TooManyApplications,
    CounterDecreased,
};

struct ApplicationCatalogResult {
    ApplicationCatalogError error{ApplicationCatalogError::None};
    PublishedApplicationSet published;

    [[nodiscard]] bool accepted() const noexcept;
};

struct ApplicationCatalogSeed {
    std::uint64_t first_handle{1U};
    std::uint32_t initial_revision{1U};
};

class ApplicationCatalog final {
  public:
    explicit ApplicationCatalog(ApplicationCatalogSeed seed = {});

    [[nodiscard]] ApplicationCatalogResult Publish(const ranking::CounterSnapshot& snapshot);

  private:
    struct ActiveRecord {
        ApplicationHandle handle;
        std::uint32_t metadata_revision{};
        process::ApplicationMetadata metadata;
        std::uint64_t upload_bytes{};
        std::uint64_t download_bytes{};
    };

    using RecordMap = std::unordered_map<process::ApplicationId, ActiveRecord>;

    static void RemoveMissingApplications(const ranking::CounterSnapshot& snapshot,
                                          RecordMap& candidate_records,
                                          std::vector<CatalogEntry>& changes);
    [[nodiscard]] ApplicationCatalogError MergeApplications(
        const ranking::CounterSnapshot& snapshot, RecordMap& candidate_records,
        std::uint64_t& candidate_next_handle, std::vector<CatalogEntry>& changes) const;
    [[nodiscard]] static ApplicationCatalogError AdvanceSequence(
        bool catalog_changed, std::uint64_t& candidate_sequence) noexcept;
    [[nodiscard]] static PublishedApplicationSet BuildPublishedSet(
        const RecordMap& candidate_records, std::uint64_t candidate_sequence,
        std::vector<CatalogEntry> changes);

    RecordMap records_;
    std::uint64_t next_handle_{};
    std::uint32_t initial_revision_{};
    std::uint64_t catalog_sequence_{};
};

} // namespace nfm::ipc

#endif // NETWORK_FLOW_MONITOR_IPC_APPLICATION_CATALOG_H
