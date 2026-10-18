#include "network_flow_monitor/config/config_codec.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using nfm::config::ConfigDocumentKind;
using nfm::config::ConfigErrorCode;
using nfm::config::ConfigLoadStatus;

constexpr std::string_view kCompleteUserJson = R"json({
  "schema_version": 2,
  "ranking": {"top_n": 7, "window_ms": 1750},
  "display": {"refresh_interval_ms": 250, "speed_unit": "bits", "show_upload": false, "show_download": true},
  "interaction": {"hover_delay_ms": 100, "dismiss_delay_ms": 200},
  "floating": {"auto_hide_enabled": false, "auto_hide_delay_ms": 300, "opacity_percent": 85, "blur_strength_percent": 75, "capsule_width_dip": 12, "capsule_height_dip": 96, "capsule_end_rounding_dip": 6, "nfm_pinned": true, "ignore_hover_when_fullscreen": false, "hide_capsule_when_fullscreen": true, "anchor_monitor_id": "DISPLAY-1", "anchor_side": "left", "anchor_vertical_offset_dip": 72},
  "appearance": {"language": "simplified_chinese", "theme": "dark"},
  "startup": {"ui_at_login": true},
  "diagnostics": {"log_level": "debug"}
})json";

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    // NOLINTBEGIN(bugprone-easily-swappable-parameters) -- This test helper keeps the expected
    // field path next to the case label so failure assertions remain readable at every call site.
    void CheckError(std::string_view json, ConfigDocumentKind kind, ConfigLoadStatus status,
                    ConfigErrorCode code, std::string_view field_path, std::string_view name) {
        const auto result = nfm::config::ParseConfigJson(json, kind);
        Check(result.status == status, name);
        Check(result.error.has_value(), "error is present");
        if (result.error.has_value()) {
            Check(result.error->code == code, "error code");
            Check(result.error->field_path == field_path, "error field path");
        }
    }
    // NOLINTEND(bugprone-easily-swappable-parameters)

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

std::string FieldDocument(std::string_view group, std::string_view field, std::int64_t value) {
    return R"json({"schema_version":2,")json" + std::string{group} + R"json(":{")json" +
           std::string{field} + R"json(":)json" + std::to_string(value) + "}}";
}

void CheckStructuralFailures(TestContext& context) {
    context.CheckError("{}", ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::MissingSchemaVersion, "schema_version",
                       "missing schema version");
    context.CheckError("[]", ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::InvalidRoot, "", "array root");
    context.CheckError("{", ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::ParseFailed, "", "malformed JSON");

    auto invalid_utf8 = std::string{R"json({"schema_version":2,"diagnostics":{"log_level":")json"};
    invalid_utf8.push_back(static_cast<char>(0xFF));
    invalid_utf8 += "\"}}";
    context.CheckError(invalid_utf8, ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::ParseFailed, "", "invalid UTF-8");

    context.CheckError(R"json({"schema_version":2,"ranking":{"top_n":5,"top_n":6}})json",
                       ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::DuplicateKey, "ranking.top_n", "duplicate nested key");
    context.CheckError(R"json({"schema_version":2,"mystery":{}})json", ConfigDocumentKind::User,
                       ConfigLoadStatus::Damaged, ConfigErrorCode::UnknownKey, "mystery",
                       "unknown root key");
    context.CheckError(R"json({"schema_version":2,"ranking":{"mystery":5}})json",
                       ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::UnknownKey, "ranking.mystery", "unknown group key");
    context.CheckError(R"json({"schema_version":2,"service":{"start_type":"manual"}})json",
                       ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::UnknownKey, "service", "service group in user document");
    context.CheckError(R"json({"schema_version":2,"ranking":{"top_n":"5"}})json",
                       ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::TypeMismatch, "ranking.top_n", "wrong scalar type");
}

void CheckSchemaStates(TestContext& context) {
    context.CheckError(R"json({"schema_version":0})json", ConfigDocumentKind::User,
                       ConfigLoadStatus::UnsupportedLegacyVersion,
                       ConfigErrorCode::InvalidSchemaVersion, "schema_version", "legacy schema");
    context.CheckError(
        R"json({"schema_version":1,"floating":{"snap_distance_dip":20,"reveal_strip_dip":6}})json",
        ConfigDocumentKind::User, ConfigLoadStatus::ReplaceableDevelopmentVersion,
        ConfigErrorCode::InvalidSchemaVersion, "schema_version", "development schema");
    context.CheckError(R"json({"schema_version":3})json", ConfigDocumentKind::User,
                       ConfigLoadStatus::FutureVersion, ConfigErrorCode::InvalidSchemaVersion,
                       "schema_version", "future schema");
    context.CheckError(R"json({"schema_version":1.0})json", ConfigDocumentKind::User,
                       ConfigLoadStatus::Damaged, ConfigErrorCode::InvalidSchemaVersion,
                       "schema_version", "non-integer schema");
    context.CheckError(R"json({"schema_version":4294967296})json", ConfigDocumentKind::User,
                       ConfigLoadStatus::FutureVersion, ConfigErrorCode::InvalidSchemaVersion,
                       "schema_version", "schema exceeds uint32");

    const auto partial = nfm::config::ParseConfigJson(
        R"json({"schema_version":2,"ranking":{"top_n":6}})json", ConfigDocumentKind::User);
    context.Check(partial.status == ConfigLoadStatus::LoadedWithDefaults,
                  "missing ordinary fields use defaults");
    context.Check(partial.config.ranking.top_n == 6U, "present field overlays default");
    context.Check(partial.config.ranking.window == std::chrono::milliseconds{2000},
                  "missing field retains default");
}

void CheckEnums(TestContext& context) {
    constexpr std::array speed_units{"auto", "bytes", "bits"};
    for (const auto* value : speed_units) {
        const auto json = std::string{R"json({"schema_version":2,"display":{"speed_unit":")json"} +
                          value + R"json("}})json";
        context.Check(!nfm::config::ParseConfigJson(json, ConfigDocumentKind::User).error,
                      "valid speed unit");
    }

    constexpr std::array log_levels{"off", "error", "info", "debug"};
    for (const auto* value : log_levels) {
        const auto json =
            std::string{R"json({"schema_version":2,"diagnostics":{"log_level":")json"} + value +
            R"json("}})json";
        context.Check(!nfm::config::ParseConfigJson(json, ConfigDocumentKind::User).error,
                      "valid log level");
    }

    context.CheckError(R"json({"schema_version":2,"display":{"speed_unit":"packets"}})json",
                       ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                       ConfigErrorCode::InvalidEnum, "display.speed_unit", "invalid enum");
}

struct NumericCase {
    std::string_view group;
    std::string_view field;
    std::int64_t minimum;
    std::int64_t maximum;
    std::string_view path;
};

void CheckNumericRanges(TestContext& context) {
    constexpr std::array cases{
        NumericCase{.group = "ranking",
                    .field = "top_n",
                    .minimum = 1,
                    .maximum = 20,
                    .path = "ranking.top_n"},
        NumericCase{.group = "ranking",
                    .field = "window_ms",
                    .minimum = 500,
                    .maximum = 10000,
                    .path = "ranking.window_ms"},
        NumericCase{.group = "display",
                    .field = "refresh_interval_ms",
                    .minimum = 100,
                    .maximum = 5000,
                    .path = "display.refresh_interval_ms"},
        NumericCase{.group = "interaction",
                    .field = "hover_delay_ms",
                    .minimum = 0,
                    .maximum = 2000,
                    .path = "interaction.hover_delay_ms"},
        NumericCase{.group = "interaction",
                    .field = "dismiss_delay_ms",
                    .minimum = 0,
                    .maximum = 2000,
                    .path = "interaction.dismiss_delay_ms"},
        NumericCase{.group = "floating",
                    .field = "auto_hide_delay_ms",
                    .minimum = 0,
                    .maximum = 5000,
                    .path = "floating.auto_hide_delay_ms"},
        NumericCase{.group = "floating",
                    .field = "opacity_percent",
                    .minimum = 0,
                    .maximum = 100,
                    .path = "floating.opacity_percent"},
        NumericCase{.group = "floating",
                    .field = "blur_strength_percent",
                    .minimum = 0,
                    .maximum = 100,
                    .path = "floating.blur_strength_percent"},
        NumericCase{.group = "floating",
                    .field = "capsule_width_dip",
                    .minimum = 8,
                    .maximum = 24,
                    .path = "floating.capsule_width_dip"},
        NumericCase{.group = "floating",
                    .field = "capsule_height_dip",
                    .minimum = 40,
                    .maximum = 160,
                    .path = "floating.capsule_height_dip"},
        NumericCase{.group = "floating",
                    .field = "capsule_end_rounding_dip",
                    .minimum = 0,
                    .maximum = 12,
                    .path = "floating.capsule_end_rounding_dip"},
    };

    for (const auto& item : cases) {
        context.Check(
            !nfm::config::ParseConfigJson(FieldDocument(item.group, item.field, item.minimum),
                                          ConfigDocumentKind::User)
                 .error,
            "numeric minimum");
        context.Check(
            !nfm::config::ParseConfigJson(FieldDocument(item.group, item.field, item.maximum),
                                          ConfigDocumentKind::User)
                 .error,
            "numeric maximum");
        context.CheckError(FieldDocument(item.group, item.field, item.minimum - 1),
                           ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                           ConfigErrorCode::OutOfRange, item.path, "below numeric minimum");
        context.CheckError(FieldDocument(item.group, item.field, item.maximum + 1),
                           ConfigDocumentKind::User, ConfigLoadStatus::Damaged,
                           ConfigErrorCode::OutOfRange, item.path, "above numeric maximum");
    }

    context.CheckError(FieldDocument("ranking", "top_n", 256), ConfigDocumentKind::User,
                       ConfigLoadStatus::Damaged, ConfigErrorCode::OutOfRange, "ranking.top_n",
                       "uint8 narrowing rejected");
}

void CheckCompleteDocuments(TestContext& context) {
    const auto user = nfm::config::ParseConfigJson(kCompleteUserJson, ConfigDocumentKind::User);
    context.Check(user.status == ConfigLoadStatus::Loaded, "complete user document loaded");
    context.Check(!user.error, "complete user document has no error");
    context.Check(user.config.ranking.top_n == 7U, "user top N decoded");
    context.Check(user.config.floating.capsule_width_dip == 12 &&
                      user.config.floating.anchor_side == nfm::config::DockSide::Left,
                  "capsule configuration and anchor decoded");
    context.Check(user.config.floating.blur_strength_percent == 75U &&
                      !user.config.floating.ignore_hover_when_fullscreen &&
                      user.config.floating.hide_capsule_when_fullscreen,
                  "glass blur and full-screen policies decoded");
    context.Check(user.config.appearance.language == nfm::config::LanguageMode::SimplifiedChinese &&
                      user.config.appearance.theme == nfm::config::ThemeMode::Dark,
                  "appearance configuration decoded");
}

void CheckSerialization(TestContext& context) {
    const auto parsed_user =
        nfm::config::ParseConfigJson(kCompleteUserJson, ConfigDocumentKind::User);
    const auto first =
        nfm::config::SerializeConfigJson(parsed_user.config, ConfigDocumentKind::User);
    const auto second =
        nfm::config::SerializeConfigJson(parsed_user.config, ConfigDocumentKind::User);
    context.Check(first.ok(), "user serialization succeeds");
    context.Check(first.json == second.json, "serialization is deterministic");
    context.Check(!first.json.empty() && first.json.back() == '\n',
                  "serialization has final newline");
    context.Check(first.json.find("\"service\":") == std::string::npos,
                  "serialization excludes unsupported groups");
    context.Check(first.json.find("snap_distance_dip") == std::string::npos &&
                      first.json.find("reveal_strip_dip") == std::string::npos,
                  "obsolete development settings are not serialized");
    const auto user_round_trip = nfm::config::ParseConfigJson(first.json, ConfigDocumentKind::User);
    context.Check(user_round_trip.status == ConfigLoadStatus::Loaded,
                  "serialized user document is complete");
    context.Check(user_round_trip.config == parsed_user.config, "user document round trip");

    auto invalid = nfm::config::DefaultAppConfig();
    invalid.ranking.top_n = 0U;
    const auto invalid_json = nfm::config::SerializeConfigJson(invalid, ConfigDocumentKind::User);
    context.Check(!invalid_json.ok(), "invalid selected scope is not serialized");
    context.Check(invalid_json.error && invalid_json.error->code == ConfigErrorCode::OutOfRange,
                  "serialization reports range error");
}

} // namespace

int main() {
    TestContext context;
    CheckStructuralFailures(context);
    CheckSchemaStates(context);
    CheckEnums(context);
    CheckNumericRanges(context);
    CheckCompleteDocuments(context);
    CheckSerialization(context);
    return context.failures();
}
