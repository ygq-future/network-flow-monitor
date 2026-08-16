#include "network_flow_monitor/config/app_config.h"
#include "network_flow_monitor/config/config_validation.h"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

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
    context.Check(config.floating.yield_topmost_when_fullscreen,
                  "full-screen foreground windows are above the monitor by default");
    context.Check(config.floating.capsule_width_dip == 10, "default capsule width");
    context.Check(config.floating.capsule_height_dip == 64, "default capsule height");
    context.Check(config.appearance.language == nfm::config::LanguageMode::English,
                  "default language is English");
    context.Check(config.appearance.theme == nfm::config::ThemeMode::System,
                  "default theme is system");
    context.Check(!config.startup.ui_at_login, "default UI startup");
    context.Check(config.collector.preferred_mode == nfm::config::CollectorMode::Portable,
                  "default collector mode");
    context.Check(config.service.start_type == nfm::config::ServiceStartType::AutomaticDelayed,
                  "default service start type");
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
}

} // namespace

int main() {
    TestContext context;
    CheckDefaults(context);
    CheckRanges(context);
    return context.failures();
}
