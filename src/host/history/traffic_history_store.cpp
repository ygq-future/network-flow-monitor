#include "network_flow_monitor/host/history/traffic_history_store.h"

#include "network_flow_monitor/host/lifecycle/collector_companion.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <utility>
#include <yyjson.h>

namespace nfm::host::history {
namespace {

constexpr std::uint64_t kMaxHistoryFileBytes = 16ULL * 1024ULL * 1024ULL; // 16 MiB
constexpr std::uint32_t kMaxTempAttempts = 64U;

char ToLowerAscii(char c) noexcept {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c + ('a' - 'A'));
    }
    return c;
}

std::string NormalizeLowerPath(std::string_view input) {
    while (!input.empty() &&
           (input.front() == ' ' || input.front() == '\t' || input.front() == '"')) {
        input.remove_prefix(1U);
    }
    while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '"')) {
        input.remove_suffix(1U);
    }
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '/') {
            out.push_back('\\');
        } else {
            out.push_back(ToLowerAscii(c));
        }
    }
    return out;
}

bool IsValidDateYmd(std::string_view date) noexcept {
    if (date.size() != 10U || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (std::size_t idx : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U}) {
        if (date[idx] < '0' || date[idx] > '9') {
            return false;
        }
    }
    return true;
}

class UniqueHandle {
  public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_{handle} {}
    ~UniqueHandle() {
        Reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_{other.Release()} {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    void Reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

  private:
    [[nodiscard]] HANDLE Release() noexcept {
        auto* const released = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return released;
    }

    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class ScopedJsonDoc {
  public:
    explicit ScopedJsonDoc(std::string_view json)
        : doc_(yyjson_read(json.data(), json.size(), 0)) {}
    ~ScopedJsonDoc() {
        if (doc_ != nullptr) {
            yyjson_doc_free(doc_);
        }
    }
    ScopedJsonDoc(const ScopedJsonDoc&) = delete;
    ScopedJsonDoc& operator=(const ScopedJsonDoc&) = delete;
    ScopedJsonDoc(ScopedJsonDoc&&) = delete;
    ScopedJsonDoc& operator=(ScopedJsonDoc&&) = delete;

    [[nodiscard]] yyjson_doc* get() const noexcept {
        return doc_;
    }

  private:
    yyjson_doc* doc_{nullptr};
};

class ScopedJsonMutDoc {
  public:
    ScopedJsonMutDoc() : doc_(yyjson_mut_doc_new(nullptr)) {}
    ~ScopedJsonMutDoc() {
        if (doc_ != nullptr) {
            yyjson_mut_doc_free(doc_);
        }
    }
    ScopedJsonMutDoc(const ScopedJsonMutDoc&) = delete;
    ScopedJsonMutDoc& operator=(const ScopedJsonMutDoc&) = delete;
    ScopedJsonMutDoc(ScopedJsonMutDoc&&) = delete;
    ScopedJsonMutDoc& operator=(ScopedJsonMutDoc&&) = delete;

    [[nodiscard]] yyjson_mut_doc* get() const noexcept {
        return doc_;
    }

  private:
    yyjson_mut_doc* doc_{nullptr};
};

bool WriteFileAtomically(const std::filesystem::path& target, std::string_view contents) {
    if (target.empty()) {
        return false;
    }
    if (const auto parent = target.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::filesystem::path temp_path;
    UniqueHandle temp_handle;
    for (std::uint32_t attempt = 0U; attempt < kMaxTempAttempts; ++attempt) {
        auto candidate_name = target.filename().native();
        candidate_name += L".tmp.";
        candidate_name += std::to_wstring(GetCurrentProcessId());
        candidate_name += L".";
        candidate_name += std::to_wstring(GetTickCount64());
        candidate_name += L".";
        candidate_name += std::to_wstring(attempt);
        auto candidate = target.parent_path() / candidate_name;
        UniqueHandle h{CreateFileW(candidate.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (h.valid()) {
            temp_path = std::move(candidate);
            temp_handle = std::move(h);
            break;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    if (!temp_handle.valid()) {
        return false;
    }

    std::size_t offset = 0U;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20U));
        DWORD written = 0U;
        if (WriteFile(temp_handle.get(), contents.data() + offset, chunk, &written, nullptr) ==
                FALSE ||
            written == 0U) {
            temp_handle.Reset();
            DeleteFileW(temp_path.c_str());
            return false;
        }
        offset += written;
    }
    if (FlushFileBuffers(temp_handle.get()) == FALSE) {
        temp_handle.Reset();
        DeleteFileW(temp_path.c_str());
        return false;
    }
    temp_handle.Reset();

    const DWORD attrs = GetFileAttributesW(target.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            DeleteFileW(temp_path.c_str());
            return false;
        }
        if (ReplaceFileW(target.c_str(), temp_path.c_str(), nullptr, 0U, nullptr, nullptr) !=
            FALSE) {
            return true;
        }
    }
    if (MoveFileExW(temp_path.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    DeleteFileW(temp_path.c_str());
    return false;
}

std::optional<std::string> ReadFileUtf8(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!file.valid()) {
        return std::nullopt;
    }
    FILE_STANDARD_INFO info{};
    if (GetFileInformationByHandleEx(file.get(), FileStandardInfo, &info, sizeof(info)) == FALSE ||
        info.Directory != FALSE) {
        return std::nullopt;
    }
    const auto file_size = static_cast<std::uint64_t>(info.EndOfFile.QuadPart);
    if (file_size == 0U || file_size > kMaxHistoryFileBytes) {
        return std::nullopt;
    }
    std::string contents(static_cast<std::size_t>(file_size), '\0');
    std::size_t offset = 0U;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20U));
        DWORD bytes_read = 0U;
        if (ReadFile(file.get(), contents.data() + offset, chunk, &bytes_read, nullptr) == FALSE ||
            bytes_read == 0U) {
            return std::nullopt;
        }
        offset += bytes_read;
    }
    return contents;
}

} // namespace

std::string current_local_date_ymd() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::array<char, 16> buffer{};
    const int written = std::snprintf(
        buffer.data(), buffer.size(), "%04u-%02u-%02u", static_cast<unsigned>(st.wYear),
        static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay));
    if (written <= 0) {
        return "1970-01-01";
    }
    return std::string(buffer.data(), static_cast<std::size_t>(written));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::string normalize_history_app_key(std::string_view app_id, std::string_view executable_path,
                                      std::string_view display_name) {
    if (!executable_path.empty()) {
        return "executable:" + NormalizeLowerPath(executable_path);
    }
    if (app_id.starts_with("executable:") || app_id.starts_with("package-app:") ||
        app_id.starts_with("package-family:") || app_id.starts_with("system:")) {
        return NormalizeLowerPath(app_id);
    }
    if (!display_name.empty()) {
        const std::string exe_name = lifecycle::extract_executable_filename(display_name);
        return "app:" + NormalizeLowerPath(exe_name.empty() ? display_name : exe_name);
    }
    return NormalizeLowerPath(app_id);
}

TrafficHistoryStore::TrafficHistoryStore(std::filesystem::path file_path,
                                         LocalDateProvider date_provider)
    : file_path_(std::move(file_path)),
      date_provider_(date_provider ? std::move(date_provider) : current_local_date_ymd) {}

void TrafficHistoryStore::set_file_path(std::filesystem::path file_path) {
    const std::scoped_lock lock(mutex_);
    file_path_ = std::move(file_path);
}

std::filesystem::path TrafficHistoryStore::file_path() const {
    const std::scoped_lock lock(mutex_);
    return file_path_;
}

void TrafficHistoryStore::set_date_provider(LocalDateProvider date_provider) {
    const std::scoped_lock lock(mutex_);
    if (date_provider) {
        date_provider_ = std::move(date_provider);
    }
}

void TrafficHistoryStore::on_session_reset(std::string_view collector_instance_id) {
    const std::scoped_lock lock(mutex_);
    if (active_collector_instance_id_ != collector_instance_id) {
        active_collector_instance_id_ = std::string{collector_instance_id};
        session_baselines_.clear();
    }
}

bool TrafficHistoryStore::load() {
    std::filesystem::path path_copy;
    {
        const std::scoped_lock lock(mutex_);
        path_copy = file_path_;
    }
    const auto raw_json = ReadFileUtf8(path_copy);
    if (!raw_json.has_value()) {
        return false;
    }

    ScopedJsonDoc doc(*raw_json);
    if (doc.get() == nullptr) {
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc.get());
    if (root == nullptr || !yyjson_is_obj(root)) {
        return false;
    }

    yyjson_val* days_val = yyjson_obj_get(root, "days");
    if (days_val == nullptr || !yyjson_is_arr(days_val)) {
        return false;
    }

    std::map<std::string, std::map<std::string, bridge::TrafficHistoryAppEntryDto>> loaded_days;
    const std::size_t day_count = yyjson_arr_size(days_val);
    for (std::size_t d_idx = 0U; d_idx < day_count; ++d_idx) {
        yyjson_val* day_obj = yyjson_arr_get(days_val, d_idx);
        if (day_obj == nullptr || !yyjson_is_obj(day_obj)) {
            continue;
        }
        yyjson_val* date_val = yyjson_obj_get(day_obj, "date");
        if (date_val == nullptr || !yyjson_is_str(date_val)) {
            continue;
        }
        const std::string_view date_sv{yyjson_get_str(date_val), yyjson_get_len(date_val)};
        if (!IsValidDateYmd(date_sv)) {
            continue;
        }
        yyjson_val* apps_val = yyjson_obj_get(day_obj, "applications");
        if (apps_val == nullptr || !yyjson_is_arr(apps_val)) {
            continue;
        }

        auto& app_map = loaded_days[std::string{date_sv}];
        const std::size_t app_count = yyjson_arr_size(apps_val);
        for (std::size_t a_idx = 0U; a_idx < app_count; ++a_idx) {
            if (app_map.size() >= kMaximumAppsPerDay) {
                break;
            }
            yyjson_val* app_obj = yyjson_arr_get(apps_val, a_idx);
            if (app_obj == nullptr || !yyjson_is_obj(app_obj)) {
                continue;
            }
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            auto get_str = [&](const char* key, const char* alt_key = nullptr) -> std::string {
                yyjson_val* v = yyjson_obj_get(app_obj, key);
                if ((v == nullptr || !yyjson_is_str(v)) && alt_key != nullptr) {
                    v = yyjson_obj_get(app_obj, alt_key);
                }
                if (v != nullptr && yyjson_is_str(v)) {
                    return std::string{yyjson_get_str(v), yyjson_get_len(v)};
                }
                return {};
            };
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            auto get_u64 = [&](const char* key, const char* alt_key = nullptr) -> std::uint64_t {
                yyjson_val* v = yyjson_obj_get(app_obj, key);
                if ((v == nullptr || !yyjson_is_uint(v)) && alt_key != nullptr) {
                    v = yyjson_obj_get(app_obj, alt_key);
                }
                if (v != nullptr && yyjson_is_uint(v)) {
                    return yyjson_get_uint(v);
                }
                return 0U;
            };

            const std::string app_id = get_str("app_id", "appId");
            const std::string exe_path = get_str("executable_path", "executablePath");
            std::string exe_name = get_str("executable_name", "executableName");
            std::string display_name = get_str("display_name", "displayName");
            const std::uint64_t dl_bytes = get_u64("download_bytes", "downloadBytes");
            const std::uint64_t ul_bytes = get_u64("upload_bytes", "uploadBytes");

            if (dl_bytes == 0U && ul_bytes == 0U) {
                continue;
            }
            const std::string norm_key = normalize_history_app_key(app_id, exe_path, display_name);
            if (norm_key.empty()) {
                continue;
            }
            if (exe_name.empty()) {
                exe_name = lifecycle::extract_executable_filename(
                    !exe_path.empty() ? exe_path : (!display_name.empty() ? display_name : app_id));
            }
            if (display_name.empty()) {
                display_name = !exe_name.empty() ? exe_name : norm_key;
            }

            auto& existing = app_map[norm_key];
            if (existing.app_id.empty()) {
                existing.app_id = norm_key;
            }
            if (!exe_path.empty()) {
                existing.executable_path = exe_path;
            }
            if (!exe_name.empty()) {
                existing.executable_name = exe_name;
            }
            if (!display_name.empty()) {
                existing.display_name = display_name;
            }
            existing.download_bytes += dl_bytes;
            existing.upload_bytes += ul_bytes;
        }
        if (app_map.empty()) {
            loaded_days.erase(std::string{date_sv});
        }
    }

    while (loaded_days.size() > kMaximumTrafficHistoryDays) {
        loaded_days.erase(loaded_days.begin());
    }

    {
        const std::scoped_lock lock(mutex_);
        days_ = std::move(loaded_days);
        dirty_ = false;
    }
    return true;
}

bool TrafficHistoryStore::save() {
    std::filesystem::path target_path;
    std::string serialized_json;
    {
        const std::scoped_lock lock(mutex_);
        target_path = file_path_;
        if (target_path.empty()) {
            return false;
        }

        ScopedJsonMutDoc doc;
        yyjson_mut_val* root = yyjson_mut_obj(doc.get());
        yyjson_mut_doc_set_root(doc.get(), root);

        yyjson_mut_obj_add_uint(doc.get(), root, "schema_version", kTrafficHistorySchemaVersion);
        yyjson_mut_val* days_arr = yyjson_mut_arr(doc.get());
        yyjson_mut_obj_add_val(doc.get(), root, "days", days_arr);

        // Write days from newest to oldest
        for (auto it = days_.rbegin(); it != days_.rend(); ++it) {
            const auto& [date_str, app_map] = *it;
            if (app_map.empty()) {
                continue;
            }
            yyjson_mut_val* day_obj = yyjson_mut_obj(doc.get());
            yyjson_mut_obj_add_str(doc.get(), day_obj, "date", date_str.c_str());

            std::vector<const bridge::TrafficHistoryAppEntryDto*> sorted_apps;
            sorted_apps.reserve(app_map.size());
            std::uint64_t day_dl = 0U;
            std::uint64_t day_ul = 0U;
            for (const auto& [_, entry] : app_map) {
                if (entry.download_bytes == 0U && entry.upload_bytes == 0U) {
                    continue;
                }
                day_dl += entry.download_bytes;
                day_ul += entry.upload_bytes;
                sorted_apps.push_back(&entry);
            }
            if (sorted_apps.empty()) {
                continue;
            }
            std::ranges::sort(sorted_apps, [](const auto* a, const auto* b) {
                const auto total_a = a->download_bytes + a->upload_bytes;
                const auto total_b = b->download_bytes + b->upload_bytes;
                if (total_a != total_b) {
                    return total_a > total_b;
                }
                return a->app_id < b->app_id;
            });

            yyjson_mut_obj_add_uint(doc.get(), day_obj, "total_download_bytes", day_dl);
            yyjson_mut_obj_add_uint(doc.get(), day_obj, "total_upload_bytes", day_ul);

            yyjson_mut_val* apps_arr = yyjson_mut_arr(doc.get());
            yyjson_mut_obj_add_val(doc.get(), day_obj, "applications", apps_arr);
            for (const auto* entry : sorted_apps) {
                yyjson_mut_val* app_obj = yyjson_mut_obj(doc.get());
                yyjson_mut_obj_add_str(doc.get(), app_obj, "app_id", entry->app_id.c_str());
                yyjson_mut_obj_add_str(doc.get(), app_obj, "executable_path",
                                       entry->executable_path.c_str());
                yyjson_mut_obj_add_str(doc.get(), app_obj, "executable_name",
                                       entry->executable_name.c_str());
                yyjson_mut_obj_add_str(doc.get(), app_obj, "display_name",
                                       entry->display_name.c_str());
                yyjson_mut_obj_add_uint(doc.get(), app_obj, "download_bytes",
                                        entry->download_bytes);
                yyjson_mut_obj_add_uint(doc.get(), app_obj, "upload_bytes", entry->upload_bytes);
                yyjson_mut_arr_append(apps_arr, app_obj);
            }
            yyjson_mut_arr_append(days_arr, day_obj);
        }

        std::size_t len = 0U;
        char* raw = yyjson_mut_write(doc.get(), YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
        if (raw == nullptr) {
            return false;
        }
        serialized_json.assign(raw, len);
        free(raw); // NOLINT(cppcoreguidelines-no-malloc)
    }

    if (!WriteFileAtomically(target_path, serialized_json)) {
        return false;
    }

    {
        const std::scoped_lock lock(mutex_);
        dirty_ = false;
    }
    return true;
}

bool TrafficHistoryStore::save_if_dirty() {
    {
        const std::scoped_lock lock(mutex_);
        if (!dirty_) {
            return true;
        }
    }
    return save();
}

bool TrafficHistoryStore::record_snapshot(const ipc::RankingSnapshotMessage& snapshot,
                                          const std::vector<std::string>& excluded_programs) {
    const std::scoped_lock lock(mutex_);
    const std::string today = date_provider_ ? date_provider_() : current_local_date_ymd();
    if (!IsValidDateYmd(today)) {
        return false;
    }

    bool recorded_any = false;
    for (const auto& app : snapshot.applications) {
        if (app.id.empty() || app.id.starts_with("collector:")) {
            continue;
        }

        // Always update baseline so if an app is later un-excluded or excluded, we only
        // count deltas from that moment forward and never retroactively dump past bytes.
        std::uint64_t delta_dl = 0U;
        std::uint64_t delta_ul = 0U;
        auto base_it = session_baselines_.find(app.id);
        if (base_it == session_baselines_.end()) {
            delta_dl = app.total_download_bytes;
            delta_ul = app.total_upload_bytes;
            session_baselines_.emplace(app.id, AppCumulativeBaseline{
                                                   .total_download_bytes = app.total_download_bytes,
                                                   .total_upload_bytes = app.total_upload_bytes,
                                               });
        } else {
            if (app.total_download_bytes >= base_it->second.total_download_bytes) {
                delta_dl = app.total_download_bytes - base_it->second.total_download_bytes;
            } else {
                // Counter reset for this app id
                delta_dl = app.total_download_bytes;
            }
            if (app.total_upload_bytes >= base_it->second.total_upload_bytes) {
                delta_ul = app.total_upload_bytes - base_it->second.total_upload_bytes;
            } else {
                delta_ul = app.total_upload_bytes;
            }
            base_it->second.total_download_bytes = app.total_download_bytes;
            base_it->second.total_upload_bytes = app.total_upload_bytes;
        }

        if (delta_dl == 0U && delta_ul == 0U) {
            continue;
        }

        if (lifecycle::is_application_excluded(app.id, app.executable_path, app.display_name,
                                               excluded_programs)) {
            continue;
        }

        const std::string norm_key =
            normalize_history_app_key(app.id, app.executable_path, app.display_name);
        if (norm_key.empty()) {
            continue;
        }

        auto& day_apps = days_[today];
        auto app_it = day_apps.find(norm_key);
        if (app_it == day_apps.end() && day_apps.size() >= kMaximumAppsPerDay) {
            continue;
        }

        std::string exe_name = lifecycle::extract_executable_filename(
            !app.executable_path.empty() ? app.executable_path
                                         : (!app.display_name.empty() ? app.display_name : app.id));
        if (exe_name.empty()) {
            exe_name = app.display_name.empty() ? norm_key : app.display_name;
        }
        const std::string display = app.display_name.empty() ? exe_name : app.display_name;

        auto& entry = day_apps[norm_key];
        if (entry.app_id.empty()) {
            entry.app_id = norm_key;
        }
        if (!app.executable_path.empty()) {
            entry.executable_path = app.executable_path;
        }
        if (!exe_name.empty()) {
            entry.executable_name = std::move(exe_name);
        }
        if (!display.empty()) {
            entry.display_name = display;
        }
        entry.download_bytes += delta_dl;
        entry.upload_bytes += delta_ul;
        recorded_any = true;
    }

    if (recorded_any) {
        while (days_.size() > kMaximumTrafficHistoryDays) {
            days_.erase(days_.begin());
        }
        dirty_ = true;
    }
    return recorded_any;
}

bridge::TrafficHistoryDto TrafficHistoryStore::export_dto(
    const std::vector<std::string>& excluded_programs, bool resolve_icons) const {
    bridge::TrafficHistoryDto dto{
        .schema_version = kTrafficHistorySchemaVersion,
    };

    const std::scoped_lock lock(mutex_);
    dto.days.reserve(days_.size());
    for (auto it = days_.rbegin(); it != days_.rend(); ++it) {
        const auto& [date_str, app_map] = *it;
        bridge::TrafficHistoryDayDto day_dto{
            .date = date_str,
        };
        day_dto.applications.reserve(app_map.size());

        for (const auto& [_, entry] : app_map) {
            if (entry.download_bytes == 0U && entry.upload_bytes == 0U) {
                continue;
            }
            if (lifecycle::is_application_excluded(entry.app_id, entry.executable_path,
                                                   entry.display_name, excluded_programs)) {
                continue;
            }
            bridge::TrafficHistoryAppEntryDto item = entry;
            if (resolve_icons && !item.executable_path.empty() &&
                (item.icon_key.empty() || !item.icon_key.starts_with("data:"))) {
                std::string resolved_icon =
                    lifecycle::extract_executable_icon_data_uri(item.executable_path);
                if (!resolved_icon.empty()) {
                    item.icon_key = std::move(resolved_icon);
                }
            }
            day_dto.total_download_bytes += item.download_bytes;
            day_dto.total_upload_bytes += item.upload_bytes;
            day_dto.applications.push_back(std::move(item));
        }

        if (day_dto.applications.empty()) {
            continue;
        }

        std::ranges::sort(day_dto.applications, [](const auto& a, const auto& b) {
            const auto total_a = a.download_bytes + a.upload_bytes;
            const auto total_b = b.download_bytes + b.upload_bytes;
            if (total_a != total_b) {
                return total_a > total_b;
            }
            return a.app_id < b.app_id;
        });

        dto.days.push_back(std::move(day_dto));
    }

    return dto;
}

bool TrafficHistoryStore::is_dirty() const noexcept {
    const std::scoped_lock lock(mutex_);
    return dirty_;
}

} // namespace nfm::host::history
