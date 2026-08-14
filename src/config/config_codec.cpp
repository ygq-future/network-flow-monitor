#include "network_flow_monitor/config/config_codec.h"

#include "network_flow_monitor/config/config_validation.h"

#include "yyjson.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nfm::config {
namespace {

struct JsonDocumentDeleter {
    void operator()(yyjson_doc* document) const noexcept {
        yyjson_doc_free(document);
    }
};

using UniqueJsonDocument = std::unique_ptr<yyjson_doc, JsonDocumentDeleter>;

struct MutableDocumentDeleter {
    void operator()(yyjson_mut_doc* document) const noexcept {
        yyjson_mut_doc_free(document);
    }
};

using UniqueMutableDocument = std::unique_ptr<yyjson_mut_doc, MutableDocumentDeleter>;

struct JsonBufferDeleter {
    void operator()(char* buffer) const noexcept {
        // yyjson's default writer allocator requires the matching C runtime release function.
        std::free(buffer); // NOLINT(cppcoreguidelines-no-malloc)
    }
};

using UniqueJsonBuffer = std::unique_ptr<char, JsonBufferDeleter>;

constexpr std::array<std::string_view, 9> kUserRootKeys{
    "schema_version", "ranking", "display",   "interaction", "floating",
    "appearance",     "startup", "collector", "diagnostics"};
constexpr std::array<std::string_view, 3> kServiceRootKeys{"schema_version", "service",
                                                           "diagnostics"};
constexpr std::array<std::string_view, 2> kRankingKeys{"top_n", "window_ms"};
constexpr std::array<std::string_view, 4> kDisplayKeys{"refresh_interval_ms", "speed_unit",
                                                       "show_upload", "show_download"};
constexpr std::array<std::string_view, 2> kInteractionKeys{"hover_delay_ms", "dismiss_delay_ms"};
constexpr std::array<std::string_view, 9> kFloatingKeys{
    "auto_hide_enabled", "auto_hide_delay_ms", "opacity_percent",
    "capsule_width_dip", "capsule_height_dip", "nfm_pinned",
    "anchor_monitor_id", "anchor_side",        "anchor_vertical_offset_dip"};
constexpr std::array<std::string_view, 11> kDevelopmentFloatingKeys{
    "snap_distance_dip", "auto_hide_enabled", "auto_hide_delay_ms",        "reveal_strip_dip",
    "opacity_percent",   "capsule_width_dip", "capsule_height_dip",        "nfm_pinned",
    "anchor_monitor_id", "anchor_side",       "anchor_vertical_offset_dip"};
constexpr std::array<std::string_view, 2> kAppearanceKeys{"language", "theme"};
constexpr std::array<std::string_view, 1> kStartupKeys{"ui_at_login"};
constexpr std::array<std::string_view, 1> kCollectorKeys{"preferred_mode"};
constexpr std::array<std::string_view, 1> kServiceKeys{"start_type"};
constexpr std::array<std::string_view, 1> kDiagnosticsKeys{"log_level"};

ConfigError MakeError(ConfigErrorCode code, std::string field_path, std::string detail) {
    return ConfigError{.code = code,
                       .field_path = std::move(field_path),
                       .detail = std::move(detail),
                       .system_error = 0U};
}

ConfigLoadResult MakeLoadFailure(ConfigLoadStatus status, ConfigError error) {
    return ConfigLoadResult{
        .config = DefaultAppConfig(), .status = status, .error = std::move(error)};
}

std::string JoinPath(std::string_view prefix, std::string_view key) {
    if (prefix.empty()) {
        return std::string{key};
    }
    return std::string{prefix} + '.' + std::string{key};
}

std::string_view JsonString(yyjson_val* value) noexcept {
    return {yyjson_get_str(value), yyjson_get_len(value)};
}

std::optional<ConfigError> FindDuplicateKeys(yyjson_val* object, std::string_view prefix) {
    if (object == nullptr) {
        return std::nullopt;
    }

    std::vector<std::pair<yyjson_val*, std::string>> pending;
    pending.emplace_back(object, prefix);
    while (!pending.empty()) {
        auto [current, current_path] = std::move(pending.back());
        pending.pop_back();
        std::unordered_set<std::string_view> keys;
        yyjson_obj_iter iterator{};
        if (!yyjson_obj_iter_init(current, &iterator)) {
            continue;
        }
        while (auto* key = yyjson_obj_iter_next(&iterator)) {
            auto* value = yyjson_obj_iter_get_val(key);
            const auto key_text = JsonString(key);
            if (!keys.insert(key_text).second) {
                return MakeError(ConfigErrorCode::DuplicateKey, JoinPath(current_path, key_text),
                                 "duplicate object key");
            }
            if (yyjson_is_obj(value)) {
                pending.emplace_back(value, JoinPath(current_path, key_text));
            }
        }
    }
    return std::nullopt;
}

bool Contains(std::span<const std::string_view> keys, std::string_view candidate) noexcept {
    return std::ranges::any_of(keys,
                               [candidate](std::string_view key) { return key == candidate; });
}

std::span<const std::string_view> GroupKeys(std::string_view group,
                                            bool development_schema) noexcept {
    if (group == "ranking") {
        return kRankingKeys;
    }
    if (group == "display") {
        return kDisplayKeys;
    }
    if (group == "interaction") {
        return kInteractionKeys;
    }
    if (group == "floating") {
        return development_schema ? std::span<const std::string_view>{kDevelopmentFloatingKeys}
                                  : std::span<const std::string_view>{kFloatingKeys};
    }
    if (group == "appearance") {
        return kAppearanceKeys;
    }
    if (group == "startup") {
        return kStartupKeys;
    }
    if (group == "collector") {
        return kCollectorKeys;
    }
    if (group == "service") {
        return kServiceKeys;
    }
    if (group == "diagnostics") {
        return kDiagnosticsKeys;
    }
    return {};
}

std::optional<ConfigError> ValidateGroupKeys(yyjson_val* object, std::string_view group,
                                             bool development_schema) {
    if (object == nullptr) {
        return MakeError(ConfigErrorCode::TypeMismatch, std::string{group},
                         "configuration group must be an object");
    }
    const auto allowed = GroupKeys(group, development_schema);
    yyjson_obj_iter iterator{};
    if (!yyjson_obj_iter_init(object, &iterator)) {
        return MakeError(ConfigErrorCode::TypeMismatch, std::string{group},
                         "configuration group must be an object");
    }
    while (auto* key = yyjson_obj_iter_next(&iterator)) {
        auto* value = yyjson_obj_iter_get_val(key);
        static_cast<void>(value);
        const auto key_text = JsonString(key);
        if (!Contains(allowed, key_text)) {
            return MakeError(ConfigErrorCode::UnknownKey, JoinPath(group, key_text),
                             "unknown configuration key");
        }
    }
    return std::nullopt;
}

std::optional<ConfigError> ValidateStructure(yyjson_val* root, ConfigDocumentKind kind,
                                             bool development_schema) {
    const std::span<const std::string_view> allowed_root =
        kind == ConfigDocumentKind::User ? std::span<const std::string_view>{kUserRootKeys}
                                         : std::span<const std::string_view>{kServiceRootKeys};

    yyjson_obj_iter iterator{};
    if (!yyjson_obj_iter_init(root, &iterator)) {
        return MakeError(ConfigErrorCode::InvalidRoot, {}, "configuration root must be an object");
    }
    while (auto* key = yyjson_obj_iter_next(&iterator)) {
        auto* value = yyjson_obj_iter_get_val(key);
        const auto key_text = JsonString(key);
        if (!Contains(allowed_root, key_text)) {
            return MakeError(ConfigErrorCode::UnknownKey, std::string{key_text},
                             "key is unknown or belongs to another document scope");
        }
        if (key_text == "schema_version") {
            continue;
        }
        if (!yyjson_is_obj(value)) {
            return MakeError(ConfigErrorCode::TypeMismatch, std::string{key_text},
                             "configuration group must be an object");
        }
        if (auto error = ValidateGroupKeys(value, key_text, development_schema)) {
            return error;
        }
    }
    return std::nullopt;
}

yyjson_val* FindGroup(yyjson_val* root, const char* name, bool* complete) noexcept {
    auto* group = yyjson_obj_get(root, name);
    if (group == nullptr) {
        *complete = false;
    }
    return group;
}

struct UnsignedField {
    const char* key;
    std::string_view path;
    std::uint64_t minimum;
    std::uint64_t maximum;
};

std::optional<ConfigError> ReadUnsigned(yyjson_val* object, const UnsignedField& field,
                                        std::uint64_t& output, bool* complete) {
    auto* value = yyjson_obj_get(object, field.key);
    if (value == nullptr) {
        *complete = false;
        return std::nullopt;
    }
    if (!yyjson_is_int(value)) {
        return MakeError(ConfigErrorCode::TypeMismatch, std::string{field.path},
                         "expected an integer");
    }
    if (!yyjson_is_uint(value)) {
        return MakeError(ConfigErrorCode::OutOfRange, std::string{field.path},
                         "integer is outside the allowed range");
    }
    const auto parsed = yyjson_get_uint(value);
    if (parsed < field.minimum || parsed > field.maximum) {
        return MakeError(ConfigErrorCode::OutOfRange, std::string{field.path},
                         "integer is outside the allowed range");
    }
    output = parsed;
    return std::nullopt;
}

std::optional<ConfigError> ReadBoolean(yyjson_val* object, const char* key, std::string_view path,
                                       bool& output, bool* complete) {
    auto* value = yyjson_obj_get(object, key);
    if (value == nullptr) {
        *complete = false;
        return std::nullopt;
    }
    if (!yyjson_is_bool(value)) {
        return MakeError(ConfigErrorCode::TypeMismatch, std::string{path}, "expected a boolean");
    }
    output = yyjson_get_bool(value);
    return std::nullopt;
}

template <typename Enum, std::size_t Size>
std::optional<ConfigError> ReadEnum(
    yyjson_val* object, const char* key, std::string_view path,
    const std::array<std::pair<std::string_view, Enum>, Size>& values, Enum& output,
    bool* complete) {
    auto* value = yyjson_obj_get(object, key);
    if (value == nullptr) {
        *complete = false;
        return std::nullopt;
    }
    if (!yyjson_is_str(value)) {
        return MakeError(ConfigErrorCode::TypeMismatch, std::string{path}, "expected a string");
    }
    const auto parsed = JsonString(value);
    for (const auto& [text, enum_value] : values) {
        if (parsed == text) {
            output = enum_value;
            return std::nullopt;
        }
    }
    return MakeError(ConfigErrorCode::InvalidEnum, std::string{path}, "unknown enumeration value");
}

constexpr std::array kSpeedUnits{
    std::pair{std::string_view{"auto"}, SpeedUnit::Auto},
    std::pair{std::string_view{"bytes"}, SpeedUnit::Bytes},
    std::pair{std::string_view{"bits"}, SpeedUnit::Bits},
};
constexpr std::array kCollectorModes{
    std::pair{std::string_view{"portable"}, CollectorMode::Portable},
    std::pair{std::string_view{"service"}, CollectorMode::Service},
};
constexpr std::array kServiceStartTypes{
    std::pair{std::string_view{"manual"}, ServiceStartType::Manual},
    std::pair{std::string_view{"automatic"}, ServiceStartType::Automatic},
    std::pair{std::string_view{"automatic_delayed"}, ServiceStartType::AutomaticDelayed},
};
constexpr std::array kLogLevels{
    std::pair{std::string_view{"off"}, LogLevel::Off},
    std::pair{std::string_view{"error"}, LogLevel::Error},
    std::pair{std::string_view{"info"}, LogLevel::Info},
    std::pair{std::string_view{"debug"}, LogLevel::Debug},
};
constexpr std::array kLanguageModes{
    std::pair{std::string_view{"system"}, LanguageMode::System},
    std::pair{std::string_view{"english"}, LanguageMode::English},
    std::pair{std::string_view{"simplified_chinese"}, LanguageMode::SimplifiedChinese},
};
constexpr std::array kThemeModes{
    std::pair{std::string_view{"system"}, ThemeMode::System},
    std::pair{std::string_view{"light"}, ThemeMode::Light},
    std::pair{std::string_view{"dark"}, ThemeMode::Dark},
};
constexpr std::array kDockSides{
    std::pair{std::string_view{"left"}, DockSide::Left},
    std::pair{std::string_view{"right"}, DockSide::Right},
};

std::optional<ConfigError> ParseRanking(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "ranking", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    std::uint64_t parsed{};
    if (auto error = ReadUnsigned(group,
                                  {.key = "top_n",
                                   .path = "ranking.top_n",
                                   .minimum = kMinimumTopN,
                                   .maximum = kMaximumTopN},
                                  parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "top_n") != nullptr) {
        config.ranking.top_n = static_cast<std::uint8_t>(parsed);
    }
    if (auto error =
            ReadUnsigned(group,
                         {.key = "window_ms",
                          .path = "ranking.window_ms",
                          .minimum = static_cast<std::uint64_t>(kMinimumRankingWindow.count()),
                          .maximum = static_cast<std::uint64_t>(kMaximumRankingWindow.count())},
                         parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "window_ms") != nullptr) {
        config.ranking.window = std::chrono::milliseconds{parsed};
    }
    return std::nullopt;
}

std::optional<ConfigError> ParseDisplay(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "display", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    std::uint64_t parsed{};
    if (auto error =
            ReadUnsigned(group,
                         {.key = "refresh_interval_ms",
                          .path = "display.refresh_interval_ms",
                          .minimum = static_cast<std::uint64_t>(kMinimumRefreshInterval.count()),
                          .maximum = static_cast<std::uint64_t>(kMaximumRefreshInterval.count())},
                         parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "refresh_interval_ms") != nullptr) {
        config.display.refresh_interval = std::chrono::milliseconds{parsed};
    }
    if (auto error = ReadEnum(group, "speed_unit", "display.speed_unit", kSpeedUnits,
                              config.display.speed_unit, &complete)) {
        return error;
    }
    if (auto error = ReadBoolean(group, "show_upload", "display.show_upload",
                                 config.display.show_upload, &complete)) {
        return error;
    }
    return ReadBoolean(group, "show_download", "display.show_download",
                       config.display.show_download, &complete);
}

std::optional<ConfigError> ParseInteraction(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "interaction", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    std::uint64_t parsed{};
    if (auto error =
            ReadUnsigned(group,
                         {.key = "hover_delay_ms",
                          .path = "interaction.hover_delay_ms",
                          .minimum = static_cast<std::uint64_t>(kMinimumHoverDelay.count()),
                          .maximum = static_cast<std::uint64_t>(kMaximumHoverDelay.count())},
                         parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "hover_delay_ms") != nullptr) {
        config.interaction.hover_delay = std::chrono::milliseconds{parsed};
    }
    if (auto error =
            ReadUnsigned(group,
                         {.key = "dismiss_delay_ms",
                          .path = "interaction.dismiss_delay_ms",
                          .minimum = static_cast<std::uint64_t>(kMinimumDismissDelay.count()),
                          .maximum = static_cast<std::uint64_t>(kMaximumDismissDelay.count())},
                         parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "dismiss_delay_ms") != nullptr) {
        config.interaction.dismiss_delay = std::chrono::milliseconds{parsed};
    }
    return std::nullopt;
}

std::optional<ConfigError> ParseFloating(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "floating", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    std::uint64_t parsed{};
    if (auto error = ReadBoolean(group, "auto_hide_enabled", "floating.auto_hide_enabled",
                                 config.floating.auto_hide_enabled, &complete)) {
        return error;
    }
    if (auto error =
            ReadUnsigned(group,
                         {.key = "auto_hide_delay_ms",
                          .path = "floating.auto_hide_delay_ms",
                          .minimum = static_cast<std::uint64_t>(kMinimumAutoHideDelay.count()),
                          .maximum = static_cast<std::uint64_t>(kMaximumAutoHideDelay.count())},
                         parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "auto_hide_delay_ms") != nullptr) {
        config.floating.auto_hide_delay = std::chrono::milliseconds{parsed};
    }
    if (auto error = ReadUnsigned(group,
                                  {.key = "opacity_percent",
                                   .path = "floating.opacity_percent",
                                   .minimum = kMinimumOpacityPercent,
                                   .maximum = kMaximumOpacityPercent},
                                  parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "opacity_percent") != nullptr) {
        config.floating.opacity_percent = static_cast<std::uint8_t>(parsed);
    }
    if (auto error = ReadUnsigned(group,
                                  {.key = "capsule_width_dip",
                                   .path = "floating.capsule_width_dip",
                                   .minimum = kMinimumCapsuleWidthDip,
                                   .maximum = kMaximumCapsuleWidthDip},
                                  parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "capsule_width_dip") != nullptr) {
        config.floating.capsule_width_dip = static_cast<std::int32_t>(parsed);
    }
    if (auto error = ReadUnsigned(group,
                                  {.key = "capsule_height_dip",
                                   .path = "floating.capsule_height_dip",
                                   .minimum = kMinimumCapsuleHeightDip,
                                   .maximum = kMaximumCapsuleHeightDip},
                                  parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "capsule_height_dip") != nullptr) {
        config.floating.capsule_height_dip = static_cast<std::int32_t>(parsed);
    }
    if (auto error = ReadBoolean(group, "nfm_pinned", "floating.nfm_pinned",
                                 config.floating.nfm_pinned, &complete)) {
        return error;
    }
    auto* anchor_monitor = yyjson_obj_get(group, "anchor_monitor_id");
    if (anchor_monitor == nullptr) {
        complete = false;
    } else if (!yyjson_is_str(anchor_monitor)) {
        return MakeError(ConfigErrorCode::TypeMismatch, "floating.anchor_monitor_id",
                         "expected a string");
    } else {
        const auto identifier = JsonString(anchor_monitor);
        if (identifier.size() > kMaximumMonitorIdentifierBytes) {
            return MakeError(ConfigErrorCode::OutOfRange, "floating.anchor_monitor_id",
                             "monitor identifier exceeds the supported length");
        }
        std::copy(identifier.begin(), identifier.end(), config.floating.anchor_monitor_id.begin());
    }
    if (auto error = ReadEnum(group, "anchor_side", "floating.anchor_side", kDockSides,
                              config.floating.anchor_side, &complete)) {
        return error;
    }
    if (auto error = ReadUnsigned(group,
                                  {.key = "anchor_vertical_offset_dip",
                                   .path = "floating.anchor_vertical_offset_dip",
                                   .minimum = 0U,
                                   .maximum = 100000U},
                                  parsed, &complete)) {
        return error;
    }
    if (yyjson_obj_get(group, "anchor_vertical_offset_dip") != nullptr) {
        config.floating.anchor_vertical_offset_dip = static_cast<std::int32_t>(parsed);
    }
    return std::nullopt;
}

std::optional<ConfigError> ParseAppearance(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "appearance", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    if (auto error = ReadEnum(group, "language", "appearance.language", kLanguageModes,
                              config.appearance.language, &complete)) {
        return error;
    }
    return ReadEnum(group, "theme", "appearance.theme", kThemeModes, config.appearance.theme,
                    &complete);
}

std::optional<ConfigError> ParseStartup(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "startup", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    return ReadBoolean(group, "ui_at_login", "startup.ui_at_login", config.startup.ui_at_login,
                       &complete);
}

std::optional<ConfigError> ParseCollector(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "collector", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    return ReadEnum(group, "preferred_mode", "collector.preferred_mode", kCollectorModes,
                    config.collector.preferred_mode, &complete);
}

std::optional<ConfigError> ParseService(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "service", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    return ReadEnum(group, "start_type", "service.start_type", kServiceStartTypes,
                    config.service.start_type, &complete);
}

std::optional<ConfigError> ParseDiagnostics(yyjson_val* root, AppConfig& config, bool& complete) {
    auto* group = FindGroup(root, "diagnostics", &complete);
    if (group == nullptr) {
        return std::nullopt;
    }
    return ReadEnum(group, "log_level", "diagnostics.log_level", kLogLevels,
                    config.diagnostics.log_level, &complete);
}

std::optional<ConfigError> ParseUserFields(yyjson_val* root, AppConfig& config, bool& complete) {
    if (auto error = ParseRanking(root, config, complete)) {
        return error;
    }
    if (auto error = ParseDisplay(root, config, complete)) {
        return error;
    }
    if (auto error = ParseInteraction(root, config, complete)) {
        return error;
    }
    if (auto error = ParseFloating(root, config, complete)) {
        return error;
    }
    if (auto error = ParseAppearance(root, config, complete)) {
        return error;
    }
    if (auto error = ParseStartup(root, config, complete)) {
        return error;
    }
    if (auto error = ParseCollector(root, config, complete)) {
        return error;
    }
    return ParseDiagnostics(root, config, complete);
}

std::optional<ConfigError> ParseServiceFields(yyjson_val* root, AppConfig& config, bool& complete) {
    if (auto error = ParseService(root, config, complete)) {
        return error;
    }
    return ParseDiagnostics(root, config, complete);
}

AppConfig SelectedScopeConfig(const AppConfig& config, ConfigDocumentKind kind) {
    auto selected = DefaultAppConfig();
    selected.schema_version = config.schema_version;
    selected.diagnostics = config.diagnostics;
    if (kind == ConfigDocumentKind::User) {
        selected.ranking = config.ranking;
        selected.display = config.display;
        selected.interaction = config.interaction;
        selected.floating = config.floating;
        selected.appearance = config.appearance;
        selected.startup = config.startup;
        selected.collector = config.collector;
    } else {
        selected.service = config.service;
    }
    return selected;
}

ConfigError ValidationError(const ConfigIssue& issue) {
    const auto code = issue.code == ConfigIssueCode::InvalidEnum ? ConfigErrorCode::InvalidEnum
                                                                 : ConfigErrorCode::OutOfRange;
    return MakeError(code, issue.field_path, "configuration value is invalid");
}

std::string_view SpeedUnitText(SpeedUnit value) noexcept {
    switch (value) {
    case SpeedUnit::Auto:
        return "auto";
    case SpeedUnit::Bytes:
        return "bytes";
    case SpeedUnit::Bits:
        return "bits";
    }
    return {};
}

std::string_view CollectorModeText(CollectorMode value) noexcept {
    switch (value) {
    case CollectorMode::Portable:
        return "portable";
    case CollectorMode::Service:
        return "service";
    }
    return {};
}

std::string_view ServiceStartTypeText(ServiceStartType value) noexcept {
    switch (value) {
    case ServiceStartType::Manual:
        return "manual";
    case ServiceStartType::Automatic:
        return "automatic";
    case ServiceStartType::AutomaticDelayed:
        return "automatic_delayed";
    }
    return {};
}

std::string_view LogLevelText(LogLevel value) noexcept {
    switch (value) {
    case LogLevel::Off:
        return "off";
    case LogLevel::Error:
        return "error";
    case LogLevel::Info:
        return "info";
    case LogLevel::Debug:
        return "debug";
    }
    return {};
}

std::string_view LanguageModeText(LanguageMode value) noexcept {
    switch (value) {
    case LanguageMode::System:
        return "system";
    case LanguageMode::English:
        return "english";
    case LanguageMode::SimplifiedChinese:
        return "simplified_chinese";
    }
    return {};
}

std::string_view ThemeModeText(ThemeMode value) noexcept {
    switch (value) {
    case ThemeMode::System:
        return "system";
    case ThemeMode::Light:
        return "light";
    case ThemeMode::Dark:
        return "dark";
    }
    return {};
}

std::string_view DockSideText(DockSide value) noexcept {
    switch (value) {
    case DockSide::Left:
        return "left";
    case DockSide::Right:
        return "right";
    }
    return {};
}

bool AddString(yyjson_mut_doc* document, yyjson_mut_val* object, const char* key,
               std::string_view value) noexcept {
    return yyjson_mut_obj_add_strn(document, object, key, value.data(), value.size());
}

bool AddRanking(yyjson_mut_doc* document, yyjson_mut_val* root, const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "ranking");
    return group != nullptr &&
           yyjson_mut_obj_add_uint(document, group, "top_n", config.ranking.top_n) &&
           yyjson_mut_obj_add_sint(document, group, "window_ms", config.ranking.window.count());
}

bool AddDisplay(yyjson_mut_doc* document, yyjson_mut_val* root, const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "display");
    return group != nullptr &&
           yyjson_mut_obj_add_sint(document, group, "refresh_interval_ms",
                                   config.display.refresh_interval.count()) &&
           AddString(document, group, "speed_unit", SpeedUnitText(config.display.speed_unit)) &&
           yyjson_mut_obj_add_bool(document, group, "show_upload", config.display.show_upload) &&
           yyjson_mut_obj_add_bool(document, group, "show_download", config.display.show_download);
}

bool AddInteraction(yyjson_mut_doc* document, yyjson_mut_val* root,
                    const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "interaction");
    return group != nullptr &&
           yyjson_mut_obj_add_sint(document, group, "hover_delay_ms",
                                   config.interaction.hover_delay.count()) &&
           yyjson_mut_obj_add_sint(document, group, "dismiss_delay_ms",
                                   config.interaction.dismiss_delay.count());
}

bool AddFloating(yyjson_mut_doc* document, yyjson_mut_val* root, const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "floating");
    return group != nullptr &&
           yyjson_mut_obj_add_bool(document, group, "auto_hide_enabled",
                                   config.floating.auto_hide_enabled) &&
           yyjson_mut_obj_add_sint(document, group, "auto_hide_delay_ms",
                                   config.floating.auto_hide_delay.count()) &&
           yyjson_mut_obj_add_uint(document, group, "opacity_percent",
                                   config.floating.opacity_percent) &&
           yyjson_mut_obj_add_sint(document, group, "capsule_width_dip",
                                   config.floating.capsule_width_dip) &&
           yyjson_mut_obj_add_sint(document, group, "capsule_height_dip",
                                   config.floating.capsule_height_dip) &&
           yyjson_mut_obj_add_bool(document, group, "nfm_pinned", config.floating.nfm_pinned) &&
           AddString(document, group, "anchor_monitor_id",
                     std::string_view{config.floating.anchor_monitor_id.data()}) &&
           AddString(document, group, "anchor_side", DockSideText(config.floating.anchor_side)) &&
           yyjson_mut_obj_add_sint(document, group, "anchor_vertical_offset_dip",
                                   config.floating.anchor_vertical_offset_dip);
}

bool AddAppearance(yyjson_mut_doc* document, yyjson_mut_val* root,
                   const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "appearance");
    return group != nullptr &&
           AddString(document, group, "language", LanguageModeText(config.appearance.language)) &&
           AddString(document, group, "theme", ThemeModeText(config.appearance.theme));
}

bool AddStartup(yyjson_mut_doc* document, yyjson_mut_val* root, const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "startup");
    return group != nullptr &&
           yyjson_mut_obj_add_bool(document, group, "ui_at_login", config.startup.ui_at_login);
}

bool AddCollector(yyjson_mut_doc* document, yyjson_mut_val* root,
                  const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "collector");
    return group != nullptr && AddString(document, group, "preferred_mode",
                                         CollectorModeText(config.collector.preferred_mode));
}

bool AddService(yyjson_mut_doc* document, yyjson_mut_val* root, const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "service");
    return group != nullptr && AddString(document, group, "start_type",
                                         ServiceStartTypeText(config.service.start_type));
}

bool AddDiagnostics(yyjson_mut_doc* document, yyjson_mut_val* root,
                    const AppConfig& config) noexcept {
    auto* group = yyjson_mut_obj_add_obj(document, root, "diagnostics");
    return group != nullptr &&
           AddString(document, group, "log_level", LogLevelText(config.diagnostics.log_level));
}

bool AddUserDocument(yyjson_mut_doc* document, yyjson_mut_val* root,
                     const AppConfig& config) noexcept {
    return AddRanking(document, root, config) && AddDisplay(document, root, config) &&
           AddInteraction(document, root, config) && AddFloating(document, root, config) &&
           AddAppearance(document, root, config) && AddStartup(document, root, config) &&
           AddCollector(document, root, config) && AddDiagnostics(document, root, config);
}

bool AddServiceDocument(yyjson_mut_doc* document, yyjson_mut_val* root,
                        const AppConfig& config) noexcept {
    return AddService(document, root, config) && AddDiagnostics(document, root, config);
}

} // namespace

bool ConfigJsonResult::ok() const noexcept {
    return !error.has_value();
}

ConfigLoadResult ParseConfigJson(std::string_view json, ConfigDocumentKind kind) {
    const UniqueJsonDocument document{yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG)};
    if (!document) {
        return MakeLoadFailure(
            ConfigLoadStatus::Damaged,
            MakeError(ConfigErrorCode::ParseFailed, {}, "JSON is malformed or is not valid UTF-8"));
    }

    auto* root = yyjson_doc_get_root(document.get());
    if (!yyjson_is_obj(root)) {
        return MakeLoadFailure(
            ConfigLoadStatus::Damaged,
            MakeError(ConfigErrorCode::InvalidRoot, {}, "configuration root must be an object"));
    }
    if (auto error = FindDuplicateKeys(root, {})) {
        return MakeLoadFailure(ConfigLoadStatus::Damaged, std::move(*error));
    }
    auto* schema = yyjson_obj_get(root, "schema_version");
    if (schema == nullptr) {
        return MakeLoadFailure(ConfigLoadStatus::Damaged,
                               MakeError(ConfigErrorCode::MissingSchemaVersion, "schema_version",
                                         "schema_version is required"));
    }
    if (!yyjson_is_uint(schema)) {
        return MakeLoadFailure(ConfigLoadStatus::Damaged,
                               MakeError(ConfigErrorCode::InvalidSchemaVersion, "schema_version",
                                         "schema_version must be an unsigned integer"));
    }

    const auto schema_version = yyjson_get_uint(schema);
    if (schema_version == 0U) {
        return MakeLoadFailure(ConfigLoadStatus::UnsupportedLegacyVersion,
                               MakeError(ConfigErrorCode::InvalidSchemaVersion, "schema_version",
                                         "schema version 0 has no supported migration"));
    }
    if (schema_version > kCurrentSchemaVersion) {
        return MakeLoadFailure(ConfigLoadStatus::FutureVersion,
                               MakeError(ConfigErrorCode::InvalidSchemaVersion, "schema_version",
                                         "configuration was written by a newer version"));
    }
    const auto development_schema = schema_version == 1U;
    if (auto error = ValidateStructure(root, kind, development_schema)) {
        return MakeLoadFailure(ConfigLoadStatus::Damaged, std::move(*error));
    }
    if (development_schema) {
        return MakeLoadFailure(
            ConfigLoadStatus::ReplaceableDevelopmentVersion,
            MakeError(ConfigErrorCode::InvalidSchemaVersion, "schema_version",
                      "recognized pre-release schema must be replaced with current defaults"));
    }

    auto config = DefaultAppConfig();
    bool complete = true;
    auto field_error = kind == ConfigDocumentKind::User
                           ? ParseUserFields(root, config, complete)
                           : ParseServiceFields(root, config, complete);
    if (field_error) {
        return MakeLoadFailure(ConfigLoadStatus::Damaged, std::move(*field_error));
    }
    return ConfigLoadResult{.config = config,
                            .status = complete ? ConfigLoadStatus::Loaded
                                               : ConfigLoadStatus::LoadedWithDefaults,
                            .error = std::nullopt};
}

ConfigJsonResult SerializeConfigJson(const AppConfig& config, ConfigDocumentKind kind) {
    const auto validation = ValidateAppConfig(SelectedScopeConfig(config, kind));
    if (!validation.ok()) {
        return ConfigJsonResult{.json = {}, .error = ValidationError(validation.issues.front())};
    }

    const UniqueMutableDocument document{yyjson_mut_doc_new(nullptr)};
    if (!document) {
        return ConfigJsonResult{
            .json = {},
            .error = MakeError(ConfigErrorCode::WriteFailed, {}, "cannot allocate JSON document")};
    }
    auto* root = yyjson_mut_obj(document.get());
    if (root == nullptr) {
        return ConfigJsonResult{
            .json = {},
            .error = MakeError(ConfigErrorCode::WriteFailed, {}, "cannot allocate JSON root")};
    }
    yyjson_mut_doc_set_root(document.get(), root);

    const bool built =
        yyjson_mut_obj_add_uint(document.get(), root, "schema_version", config.schema_version) &&
        (kind == ConfigDocumentKind::User ? AddUserDocument(document.get(), root, config)
                                          : AddServiceDocument(document.get(), root, config));
    if (!built) {
        return ConfigJsonResult{
            .json = {},
            .error = MakeError(ConfigErrorCode::WriteFailed, {}, "cannot build JSON document")};
    }

    std::size_t length{};
    const UniqueJsonBuffer buffer{
        yyjson_mut_write(document.get(), YYJSON_WRITE_PRETTY_TWO_SPACES, &length)};
    if (!buffer) {
        return ConfigJsonResult{
            .json = {},
            .error = MakeError(ConfigErrorCode::WriteFailed, {}, "cannot encode JSON document")};
    }
    std::string json{buffer.get(), length};
    if (json.empty() || json.back() != '\n') {
        json.push_back('\n');
    }
    return ConfigJsonResult{.json = std::move(json), .error = std::nullopt};
}

} // namespace nfm::config
