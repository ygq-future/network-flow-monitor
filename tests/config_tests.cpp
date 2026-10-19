#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/config/config_codec.h"
#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/config/config_store.h"
#include "network_flow_monitor/config/config_validation.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// === config_model_suite ===
namespace config_model_suite {
namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    void CheckValid(const nfm::config::AppConfig& config, std::string_view name) {
        const auto result = nfm::config::ValidateAppConfig(config);
        Check(result.ok(), name);
    }

    void CheckInvalid(const nfm::config::AppConfig& config,
                      const nfm::config::ConfigIssue& expected, std::string_view name) {
        const auto result = nfm::config::ValidateAppConfig(config);
        Check(!result.ok(), name);
        if (!result.issues.empty()) {
            Check(result.issues.front() == expected, "validation issue");
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

void CheckDefaults(TestContext& context) {
    using namespace std::chrono_literals;
    const auto config = nfm::config::DefaultAppConfig();

    context.Check(config.schema_version == nfm::config::kCurrentSchemaVersion, "schema version");
    context.Check(config.ranking.top_n == 5U, "default top N");
    context.Check(config.ranking.window == 2000ms, "default ranking window");
    context.Check(config.display.refresh_interval == 500ms, "default refresh interval");
    context.Check(config.display.speed_unit == nfm::config::SpeedUnit::Auto, "default speed unit");
    context.Check(config.display.show_upload, "default show upload");
    context.Check(config.display.show_download, "default show download");
    context.Check(config.interaction.hover_delay == 350ms, "default hover delay");
    context.Check(config.interaction.dismiss_delay == 250ms, "default dismiss delay");
    context.Check(config.floating.auto_hide_enabled, "default auto hide");
    context.Check(config.floating.auto_hide_delay == 500ms, "default auto hide delay");
    context.Check(config.floating.opacity_percent == 92U, "default opacity");
    context.Check(config.floating.blur_strength_percent == 60U, "default blur strength");
    context.Check(config.floating.ignore_hover_when_fullscreen,
                  "ignore hover on fullscreen enabled by default");
    context.Check(!config.floating.hide_capsule_when_fullscreen,
                  "hide capsule on fullscreen disabled by default");
    context.Check(config.floating.capsule_width_dip == 10, "default capsule width");
    context.Check(config.floating.capsule_height_dip == 64, "default capsule height");
    context.Check(config.floating.capsule_end_rounding_dip == 5, "default capsule end rounding");
    context.Check(nfm::config::EffectiveCapsuleEndRoundingDip(config.floating) == 5,
                  "default effective capsule end rounding");
    context.Check(config.appearance.language == nfm::config::LanguageMode::English,
                  "default language is English");
    context.Check(config.appearance.theme == nfm::config::ThemeMode::System,
                  "default theme is system");
    context.Check(!config.startup.ui_at_login, "default UI startup");
    context.Check(config.diagnostics.log_level == nfm::config::LogLevel::Info, "default log level");
    context.CheckValid(config, "defaults are valid");
}

void CheckInvalidRange(TestContext& context, const nfm::config::AppConfig& config,
                       std::string field_path, std::string_view name) {
    context.CheckInvalid(
        config,
        {.code = nfm::config::ConfigIssueCode::OutOfRange, .field_path = std::move(field_path)},
        name);
}

void CheckRanges(TestContext& context) {
    using namespace std::chrono_literals;

    auto config = nfm::config::DefaultAppConfig();
    config.ranking.top_n = 1U;
    context.CheckValid(config, "top N minimum");
    config.ranking.top_n = 20U;
    context.CheckValid(config, "top N maximum");
    config.ranking.top_n = 0U;
    CheckInvalidRange(context, config, "ranking.top_n", "top N below minimum");
    config.ranking.top_n = 21U;
    CheckInvalidRange(context, config, "ranking.top_n", "top N above maximum");

    config = nfm::config::DefaultAppConfig();
    config.ranking.window = 500ms;
    config.display.refresh_interval = 5000ms;
    context.CheckValid(config, "window may be shorter than refresh interval");
    config.ranking.window = 499ms;
    CheckInvalidRange(context, config, "ranking.window_ms", "window below minimum");
    config.ranking.window = 10001ms;
    CheckInvalidRange(context, config, "ranking.window_ms", "window above maximum");

    config = nfm::config::DefaultAppConfig();
    config.display.refresh_interval = 100ms;
    context.CheckValid(config, "refresh minimum");
    config.display.refresh_interval = 5000ms;
    context.CheckValid(config, "refresh maximum");
    config.display.refresh_interval = 99ms;
    CheckInvalidRange(context, config, "display.refresh_interval_ms", "refresh below minimum");
    config.display.refresh_interval = 5001ms;
    CheckInvalidRange(context, config, "display.refresh_interval_ms", "refresh above maximum");

    config = nfm::config::DefaultAppConfig();
    config.interaction.hover_delay = 0ms;
    context.CheckValid(config, "hover minimum");
    config.interaction.hover_delay = 2000ms;
    context.CheckValid(config, "hover maximum");
    config.interaction.hover_delay = -1ms;
    CheckInvalidRange(context, config, "interaction.hover_delay_ms", "hover below minimum");
    config.interaction.hover_delay = 2001ms;
    CheckInvalidRange(context, config, "interaction.hover_delay_ms", "hover above maximum");

    config = nfm::config::DefaultAppConfig();
    config.interaction.dismiss_delay = 0ms;
    context.CheckValid(config, "dismiss minimum");
    config.interaction.dismiss_delay = 2000ms;
    context.CheckValid(config, "dismiss maximum");
    config.interaction.dismiss_delay = -1ms;
    CheckInvalidRange(context, config, "interaction.dismiss_delay_ms", "dismiss below minimum");
    config.interaction.dismiss_delay = 2001ms;
    CheckInvalidRange(context, config, "interaction.dismiss_delay_ms", "dismiss above maximum");

    config = nfm::config::DefaultAppConfig();
    config.floating.auto_hide_delay = 0ms;
    context.CheckValid(config, "auto hide minimum");
    config.floating.auto_hide_delay = 5000ms;
    context.CheckValid(config, "auto hide maximum");
    config.floating.auto_hide_delay = -1ms;
    CheckInvalidRange(context, config, "floating.auto_hide_delay_ms", "auto hide below minimum");
    config.floating.auto_hide_delay = 5001ms;
    CheckInvalidRange(context, config, "floating.auto_hide_delay_ms", "auto hide above maximum");

    config = nfm::config::DefaultAppConfig();
    config.floating.opacity_percent = 0U;
    context.CheckValid(config, "opacity minimum");
    config.floating.opacity_percent = 100U;
    context.CheckValid(config, "opacity maximum");
    config.floating.opacity_percent = 101U;
    CheckInvalidRange(context, config, "floating.opacity_percent", "opacity above maximum");

    config = nfm::config::DefaultAppConfig();
    config.floating.blur_strength_percent = 0U;
    context.CheckValid(config, "blur strength minimum");
    config.floating.blur_strength_percent = 100U;
    context.CheckValid(config, "blur strength maximum");
    config.floating.blur_strength_percent = 101U;
    CheckInvalidRange(context, config, "floating.blur_strength_percent",
                      "blur strength above maximum");

    config = nfm::config::DefaultAppConfig();
    config.floating.capsule_width_dip = 8;
    context.CheckValid(config, "capsule width minimum");
    config.floating.capsule_width_dip = 24;
    context.CheckValid(config, "capsule width maximum");
    config.floating.capsule_width_dip = 7;
    CheckInvalidRange(context, config, "floating.capsule_width_dip", "capsule width below minimum");

    config = nfm::config::DefaultAppConfig();
    config.floating.capsule_height_dip = 40;
    context.CheckValid(config, "capsule height minimum");
    config.floating.capsule_height_dip = 160;
    context.CheckValid(config, "capsule height maximum");
    config.floating.capsule_height_dip = 161;
    CheckInvalidRange(context, config, "floating.capsule_height_dip",
                      "capsule height above maximum");

    config = nfm::config::DefaultAppConfig();
    config.floating.capsule_end_rounding_dip = 0;
    context.CheckValid(config, "capsule end rounding minimum");
    config.floating.capsule_end_rounding_dip = 12;
    context.CheckValid(config, "capsule end rounding maximum");
    config.floating.capsule_end_rounding_dip = -1;
    CheckInvalidRange(context, config, "floating.capsule_end_rounding_dip",
                      "capsule end rounding below minimum");
    config.floating.capsule_end_rounding_dip = 13;
    CheckInvalidRange(context, config, "floating.capsule_end_rounding_dip",
                      "capsule end rounding above maximum");

    // Test EffectiveCapsuleEndRoundingDip capping at width / 2
    config = nfm::config::DefaultAppConfig();
    config.floating.capsule_width_dip = 8;
    config.floating.capsule_end_rounding_dip = 10;
    context.Check(nfm::config::EffectiveCapsuleEndRoundingDip(config.floating) == 4,
                  "effective capsule end rounding capped at width / 2");
}

} // namespace

int Run() {
    TestContext context;
    CheckDefaults(context);
    CheckRanges(context);
    return context.failures();
}
} // namespace config_model_suite

// === config_codec_suite ===
namespace config_codec_suite {
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

int Run() {
    TestContext context;
    CheckStructuralFailures(context);
    CheckSchemaStates(context);
    CheckEnums(context);
    CheckNumericRanges(context);
    CheckCompleteDocuments(context);
    CheckSerialization(context);
    return context.failures();
}
} // namespace config_codec_suite

// === config_store_suite ===
namespace config_store_suite {
namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

class TestDirectory {
  public:
    TestDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        const auto identity =
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        for (std::uint32_t attempt = 0; attempt < 100U; ++attempt) {
            path_ = base / (L"nfm-config-tests-" + identity + L"-" + std::to_wstring(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        path_.clear();
    }

    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

class UniqueHandle {
  public:
    explicit UniqueHandle(HANDLE handle) noexcept : handle_{handle} {}

    ~UniqueHandle() {
        if (valid()) {
            CloseHandle(handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

bool WriteAll(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return stream.good();
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

std::size_t EntryCount(const std::filesystem::path& directory) {
    std::size_t count{};
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator{directory}) {
        ++count;
    }
    return count;
}

void CheckPaths(TestContext& context) {
    context.Check(nfm::config::PortableDataDirectory(LR"(C:\Tools\NetworkFlowMonitor.exe)") ==
                      LR"(C:\Tools\data)",
                  "portable data directory");
    context.Check(nfm::config::PortableLogDirectory(LR"(C:\Tools\NetworkFlowMonitor.exe)") ==
                      LR"(C:\Tools\logs)",
                  "portable log directory");
    context.Check(nfm::config::PortableWebView2UserDataDirectory(
                      LR"(C:\Tools\NetworkFlowMonitor.exe)") == LR"(C:\Tools\data\webview2)",
                  "portable webview2 user data directory");
    context.Check(nfm::config::PortableConfigPath(LR"(C:\Tools\NetworkFlowMonitor.exe)") ==
                      LR"(C:\Tools\data\config.json)",
                  "portable config path");
    context.Check(nfm::config::LegacyRootConfigPath(LR"(C:\Tools\NetworkFlowMonitor.exe)") ==
                      LR"(C:\Tools\config.json)",
                  "legacy root config path");
}

void CheckLoading(TestContext& context, const TestDirectory& directory) {
    const auto missing = directory.path() / L"missing.json";
    const auto missing_result =
        nfm::config::LoadConfigFile(missing, nfm::config::ConfigDocumentKind::User);
    context.Check(missing_result.status == nfm::config::ConfigLoadStatus::Missing,
                  "missing file status");
    context.Check(missing_result.config == nfm::config::DefaultAppConfig(),
                  "missing file returns defaults");

    const auto damaged = directory.path() / L"damaged.json";
    constexpr std::string_view damaged_text{"{ definitely not JSON"};
    context.Check(WriteAll(damaged, damaged_text), "prepare damaged file");
    const auto damaged_result =
        nfm::config::LoadConfigFile(damaged, nfm::config::ConfigDocumentKind::User);
    context.Check(damaged_result.status == nfm::config::ConfigLoadStatus::Damaged,
                  "damaged file status");
    context.Check(damaged_result.error &&
                      damaged_result.error->code == nfm::config::ConfigErrorCode::ParseFailed,
                  "damaged file parse error");
    context.Check(ReadAll(damaged) == damaged_text, "damaged load preserves original file");

    const auto development = directory.path() / L"development.json";
    constexpr std::string_view development_text{
        R"json({"schema_version":1,"floating":{"snap_distance_dip":20,"reveal_strip_dip":6}})json"};
    context.Check(WriteAll(development, development_text), "prepare development config");
    const auto development_result =
        nfm::config::LoadConfigFile(development, nfm::config::ConfigDocumentKind::User);
    context.Check(development_result.status == nfm::config::ConfigLoadStatus::Loaded,
                  "recognized development config is replaced");
    context.Check(development_result.config == nfm::config::DefaultAppConfig(),
                  "development replacement uses current defaults");
    const auto replaced_text = ReadAll(development);
    context.Check(replaced_text.find("\"schema_version\": 2") != std::string::npos,
                  "development replacement writes current schema");
    context.Check(replaced_text.find("snap_distance_dip") == std::string::npos &&
                      replaced_text.find("reveal_strip_dip") == std::string::npos,
                  "development replacement removes obsolete concepts");

    const auto oversized = directory.path() / L"oversized.json";
    const std::string oversized_text(1024U * 1024U + 1U, 'x');
    context.Check(WriteAll(oversized, oversized_text), "prepare oversized file");
    const auto oversized_result =
        nfm::config::LoadConfigFile(oversized, nfm::config::ConfigDocumentKind::User);
    context.Check(oversized_result.status == nfm::config::ConfigLoadStatus::Damaged,
                  "oversized file status");
    context.Check(oversized_result.error &&
                      oversized_result.error->code == nfm::config::ConfigErrorCode::FileTooLarge,
                  "oversized file error");
}

void CheckSaving(TestContext& context, const TestDirectory& directory) {
    const auto target = directory.path() / L"config.json";
    const auto entries_before = EntryCount(directory.path());
    auto config = nfm::config::DefaultAppConfig();
    config.ranking.top_n = 8U;

    const auto first =
        nfm::config::SaveConfigFile(target, config, nfm::config::ConfigDocumentKind::User);
    context.Check(first.saved, "first save succeeds");
    context.Check(!first.error, "first save has no error");
    const auto first_load =
        nfm::config::LoadConfigFile(target, nfm::config::ConfigDocumentKind::User);
    context.Check(first_load.status == nfm::config::ConfigLoadStatus::Loaded,
                  "first save creates complete document");
    context.Check(first_load.config.ranking.top_n == 8U, "first save persists value");
    context.Check(EntryCount(directory.path()) == entries_before + 1U,
                  "first save leaves no temporary sibling");

    config.ranking.top_n = 11U;
    const auto replacement =
        nfm::config::SaveConfigFile(target, config, nfm::config::ConfigDocumentKind::User);
    context.Check(replacement.saved, "replacement save succeeds");
    const auto replacement_load =
        nfm::config::LoadConfigFile(target, nfm::config::ConfigDocumentKind::User);
    context.Check(replacement_load.config.ranking.top_n == 11U, "replacement save persists value");
    context.Check(EntryCount(directory.path()) == entries_before + 1U,
                  "replacement leaves no temporary sibling");
}

void CheckLockedReplacement(TestContext& context, const TestDirectory& directory) {
    const auto target = directory.path() / L"locked.json";
    const auto initial = nfm::config::SerializeConfigJson(nfm::config::DefaultAppConfig(),
                                                          nfm::config::ConfigDocumentKind::User);
    context.Check(initial.ok() && WriteAll(target, initial.json), "prepare locked target");
    const auto original_text = ReadAll(target);
    const auto entries_before = EntryCount(directory.path());

    const UniqueHandle lock{CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    context.Check(lock.valid(), "open replacement lock");

    auto replacement = nfm::config::DefaultAppConfig();
    replacement.ranking.top_n = 9U;
    const auto result =
        nfm::config::SaveConfigFile(target, replacement, nfm::config::ConfigDocumentKind::User);
    context.Check(!result.saved, "locked target rejects replacement");
    context.Check(result.error && result.error->code == nfm::config::ConfigErrorCode::ReplaceFailed,
                  "locked target reports replacement error");
    context.Check(result.error && result.error->system_error != 0U,
                  "locked target exposes Win32 error");
    context.Check(ReadAll(target) == original_text, "failed replacement preserves original");
    context.Check(EntryCount(directory.path()) == entries_before,
                  "failed replacement removes temporary sibling");
}

} // namespace

int Run() {
    TestContext context;
    const TestDirectory directory;
    context.Check(!directory.path().empty(), "create unique test directory");
    if (!directory.path().empty()) {
        CheckPaths(context);
        CheckLoading(context, directory);
        CheckSaving(context, directory);
        CheckLockedReplacement(context, directory);
    }
    return context.failures();
}
} // namespace config_store_suite

int main() {
    int failures = 0;
    failures += (config_model_suite::Run() != 0 ? 1 : 0);
    failures += (config_codec_suite::Run() != 0 ? 1 : 0);
    failures += (config_store_suite::Run() != 0 ? 1 : 0);
    return failures;
}
