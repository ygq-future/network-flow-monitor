#include "network_flow_monitor/host/app/product_host.h"

#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/config/config_validation.h"
#include "network_flow_monitor/host/platform/webview2_environment.h"

#include <utility>
#include <yyjson.h>

namespace nfm::host::app {

ProductHost::ProductHost(ProductHostOptions options) : options_(std::move(options)) {
    std::array<wchar_t, MAX_PATH> exe_path_buf{};
    GetModuleFileNameW(nullptr, exe_path_buf.data(), MAX_PATH);
    std::filesystem::path exe_dir = std::filesystem::path(exe_path_buf.data()).parent_path();

    if (options_.static_assets_dir.empty() ||
        !std::filesystem::exists(options_.static_assets_dir / "index.html")) {
        const std::array<std::filesystem::path, 5> candidates = {
            exe_dir / "ui",
            exe_dir / "apps" / "nfm-ui" / "dist",
            exe_dir.parent_path() / "apps" / "nfm-ui" / "dist",
            exe_dir.parent_path().parent_path() / "apps" / "nfm-ui" / "dist",
            std::filesystem::current_path() / "ui",
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate / "index.html")) {
                options_.static_assets_dir = candidate;
                break;
            }
        }
    }
    if (options_.collector_executable_path.empty() ||
        !std::filesystem::exists(options_.collector_executable_path)) {
        const std::array<std::filesystem::path, 4> candidates = {
            exe_dir / "nfm-collector.exe",
            exe_dir.parent_path() / "nfm-collector.exe",
            std::filesystem::current_path() / "nfm-collector.exe",
            std::filesystem::current_path() / "build" / "windows-x64-debug" / "nfm-collector.exe",
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                options_.collector_executable_path = candidate;
                break;
            }
        }
    }
    if (options_.config_file_path.empty()) {
        options_.config_file_path = exe_dir / "config.json";
    }
}

ProductHost::~ProductHost() {
    shutdown();
}

bool ProductHost::initialize() {
    if (is_initialized_) {
        return true;
    }

    // Load existing config or default
    auto load_result =
        config::LoadConfigFile(options_.config_file_path, config::ConfigDocumentKind::User);
    current_config_ = load_result.config;

    // 1. Detect WebView2 Evergreen Runtime
    auto rt_info = platform::WebView2Environment::detect_runtime();
    if (!rt_info.is_available) {
        MessageBoxW(nullptr, platform::WebView2Environment::get_recovery_guidance().c_str(),
                    L"Network Flow Monitor - Missing Requirement", MB_ICONERROR | MB_OK);
        return false;
    }

    // 2. Validate static assets existence if not using dev server
    if (options_.dev_server_url.empty() &&
        !std::filesystem::exists(options_.static_assets_dir / "index.html")) {
        std::wstring msg =
            L"Frontend UI static bundle was not found at:\n" +
            options_.static_assets_dir.wstring() +
            L"\n\nPlease ensure 'apps/nfm-ui/dist' is built or 'ui/' folder is present.";
        MessageBoxW(nullptr, msg.c_str(), L"Network Flow Monitor - Missing Assets",
                    MB_ICONERROR | MB_OK);
        return false;
    }

    // 2. Create Window
    platform::WindowConfig win_cfg{
        .title = L"Network Flow Monitor",
        .initial_width = 860,
        .initial_height = 620,
        .frameless = true,
    };
    window_ = std::make_unique<platform::Win32Window>(win_cfg);

    // 3. Initialize Bridge & Dispatcher
    bridge_dispatcher_ = std::make_shared<bridge::BridgeDispatcher>(
        std::shared_ptr<bridge::IBridgeCommandHandler>(this, [](bridge::IBridgeCommandHandler*) {
            // Non-owning deleter because ProductHost owns bridge_dispatcher_
        }));

    webview_ = std::make_shared<webview::WebView2Controller>();
    native_bridge_ = std::make_shared<bridge::NativeBridge>([this](std::string_view json) {
        if (webview_ && webview_->is_ready()) {
            webview_->post_web_message_json(json);
        }
    });

    webview_->set_message_callback([this](std::string_view json) {
        bridge::BridgeErrorDto err;
        bridge::CommandResponseDto resp;
        if (!bridge_dispatcher_->dispatch_web_message(json, &err, &resp)) {
            native_bridge_->post_error(err);
        } else if (!resp.command_id.empty()) {
            native_bridge_->post_command_response(resp);
        }
    });

    // 4. Initialize Collector Companion
    collector_companion_ =
        std::make_shared<lifecycle::CollectorCompanion>(options_.collector_executable_path);

    collector_companion_->set_traffic_tick_callback([this](const bridge::TrafficTickDto& tick) {
        if (window_ != nullptr) {
            window_->post_ui_task([this, tick]() noexcept {
                try {
                    if (native_bridge_) {
                        native_bridge_->post_traffic_tick(tick);
                    }
                } catch (...) {
                }
            });
        }
    });

    collector_companion_->set_collector_status_callback(
        [this](const bridge::CollectorStatusDto& status) {
            if (window_ != nullptr) {
                auto status_ptr = std::make_shared<bridge::CollectorStatusDto>(status);
                window_->post_ui_task([this, status_ptr]() noexcept {
                    try {
                        if (native_bridge_ && status_ptr) {
                            native_bridge_->post_collector_status(*status_ptr);
                        }
                    } catch (...) {
                    }
                });
            }
        });

    collector_companion_->set_session_reset_callback([this](const bridge::SessionResetDto& reset) {
        if (window_ != nullptr) {
            auto reset_ptr = std::make_shared<bridge::SessionResetDto>(reset);
            window_->post_ui_task([this, reset_ptr]() noexcept {
                try {
                    if (native_bridge_) {
                        native_bridge_->post_session_reset(*reset_ptr);
                    }
                } catch (...) {
                }
            });
        }
    });

    // 5. Create Win32 Window
    if (!window_->create(this)) {
        return false;
    }

    // 6. Start Collector Companion
    collector_companion_->start();

    // 7. Initialize WebView2
    webview_->set_navigation_callback([this](bool /*is_success*/, int /*http_status*/) {
        if (window_ != nullptr) {
            window_->post_ui_task([this]() noexcept {
                try {
                    if (window_) {
                        window_->show(SW_SHOW);
                        SetForegroundWindow(window_->hwnd());
                    }
                } catch (...) {
                }
            });
        }
    });

    if (!webview_->initialize(window_->hwnd(), options_.static_assets_dir)) {
        return false;
    }

    if (!options_.dev_server_url.empty()) {
        webview_->navigate(options_.dev_server_url);
    }

    if (native_bridge_) {
        native_bridge_->post_config_changed(current_config_);
    }

    window_->create_tray_icon();
    is_initialized_ = true;
    return true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
int ProductHost::run() {
    return platform::Win32Window::run_message_loop();
}

void ProductHost::shutdown() {
    if (is_shutting_down_) {
        return;
    }
    is_shutting_down_ = true;

    if (collector_companion_) {
        collector_companion_->stop();
    }

    if (window_) {
        window_->close();
    }
}

void ProductHost::on_window_created(HWND /*hwnd*/) {}

void ProductHost::on_window_size_changed(int width_px, int height_px) {
    if (webview_) {
        webview_->resize(width_px, height_px);
    }
}

void ProductHost::on_window_dpi_changed(std::uint32_t /*new_dpi*/, const RECT* /*suggested_rect*/) {
}

void ProductHost::on_window_close_requested() {
    shutdown();
}

void ProductHost::on_window_destroyed() {}

void ProductHost::on_tray_show_monitor() {
    on_show_monitor();
}

void ProductHost::on_tray_settings() {
    on_open_settings("");
}

void ProductHost::on_tray_retry_collector() {
    on_retry_collector();
}

void ProductHost::on_tray_exit() {
    on_exit_app();
}

void ProductHost::on_show_monitor() {
    if (window_) {
        window_->show();
    }
}

void ProductHost::on_hide_monitor() {
    if (window_) {
        window_->hide();
    }
}

void ProductHost::on_set_pin(bool /*pinned*/) {}

void ProductHost::on_open_settings(std::string_view /*route*/) {}

void ProductHost::on_request_config() {
    if (native_bridge_) {
        native_bridge_->post_config_changed(current_config_);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ProductHost::on_update_setting(std::string_view key, std::string_view value_json) {
    if (!native_bridge_) {
        return;
    }

    yyjson_doc* doc = yyjson_read(value_json.data(), value_json.size(), 0);
    if (doc == nullptr) {
        native_bridge_->post_error(
            bridge::BridgeErrorDto{.code = std::string(bridge::to_error_code_string(
                                       bridge::BridgeErrorCode::InvalidSettingValue)),
                                   .message = "Failed to parse setting value JSON"});
        return;
    }
    yyjson_val* val = yyjson_doc_get_root(doc);
    if (val == nullptr) {
        yyjson_doc_free(doc);
        native_bridge_->post_error(
            bridge::BridgeErrorDto{.code = std::string(bridge::to_error_code_string(
                                       bridge::BridgeErrorCode::InvalidSettingValue)),
                                   .message = "Empty setting value"});
        return;
    }

    bool updated = false;
    bool theme_changed = false;
    bool locale_changed = false;
    std::string err_msg;

    auto check_int_range = [&](std::string_view name, std::int64_t v, std::int64_t min_v,
                               std::int64_t max_v, auto& out) -> bool {
        if (v < min_v || v > max_v) {
            err_msg = "Value " + std::to_string(v) + " out of allowed range [" +
                      std::to_string(min_v) + ", " + std::to_string(max_v) + "] for " +
                      std::string(name);
            return false;
        }
        out = static_cast<std::decay_t<decltype(out)>>(v);
        return true;
    };

    if (key == "startup.ui_at_login" || key == "general.uiAtLogin" || key == "uiAtLogin") {
        if (yyjson_is_bool(val)) {
            current_config_.startup.ui_at_login = yyjson_get_bool(val);
            updated = true;
        } else {
            err_msg = "Expected boolean for ui_at_login";
        }
    } else if (key == "display.speed_unit" || key == "general.speedUnit" || key == "speedUnit") {
        if (yyjson_is_str(val)) {
            std::string_view str = yyjson_get_str(val);
            if (str == "auto") {
                current_config_.display.speed_unit = config::SpeedUnit::Auto;
                updated = true;
            } else if (str == "bytes") {
                current_config_.display.speed_unit = config::SpeedUnit::Bytes;
                updated = true;
            } else if (str == "bits") {
                current_config_.display.speed_unit = config::SpeedUnit::Bits;
                updated = true;
            } else {
                err_msg = "Invalid speed_unit: " + std::string(str);
            }
        } else {
            err_msg = "Expected string for speed_unit";
        }
    } else if (key == "appearance.theme" || key == "theme") {
        if (yyjson_is_str(val)) {
            std::string_view str = yyjson_get_str(val);
            if (str == "system") {
                current_config_.appearance.theme = config::ThemeMode::System;
                updated = true;
                theme_changed = true;
            } else if (str == "light") {
                current_config_.appearance.theme = config::ThemeMode::Light;
                updated = true;
                theme_changed = true;
            } else if (str == "dark") {
                current_config_.appearance.theme = config::ThemeMode::Dark;
                updated = true;
                theme_changed = true;
            } else {
                err_msg = "Invalid theme: " + std::string(str);
            }
        } else {
            err_msg = "Expected string for theme";
        }
    } else if (key == "appearance.language" || key == "language") {
        if (yyjson_is_str(val)) {
            std::string_view str = yyjson_get_str(val);
            if (str == "system") {
                current_config_.appearance.language = config::LanguageMode::System;
                updated = true;
                locale_changed = true;
            } else if (str == "english") {
                current_config_.appearance.language = config::LanguageMode::English;
                updated = true;
                locale_changed = true;
            } else if (str == "simplified_chinese") {
                current_config_.appearance.language = config::LanguageMode::SimplifiedChinese;
                updated = true;
                locale_changed = true;
            } else {
                err_msg = "Invalid language: " + std::string(str);
            }
        } else {
            err_msg = "Expected string for language";
        }
    } else if (key == "floating.capsule_width_dip" || key == "capsule.widthDip" ||
               key == "capsuleWidthDip") {
        if (yyjson_is_int(val)) {
            updated = check_int_range(key, yyjson_get_sint(val), config::kMinimumCapsuleWidthDip,
                                      config::kMaximumCapsuleWidthDip,
                                      current_config_.floating.capsule_width_dip);
        } else {
            err_msg = "Expected integer for capsule_width_dip";
        }
    } else if (key == "floating.capsule_height_dip" || key == "capsule.heightDip" ||
               key == "capsuleHeightDip") {
        if (yyjson_is_int(val)) {
            updated = check_int_range(key, yyjson_get_sint(val), config::kMinimumCapsuleHeightDip,
                                      config::kMaximumCapsuleHeightDip,
                                      current_config_.floating.capsule_height_dip);
        } else {
            err_msg = "Expected integer for capsule_height_dip";
        }
    } else if (key == "floating.capsule_end_rounding_dip" || key == "capsule.endRoundingDip" ||
               key == "capsuleEndRoundingDip") {
        if (yyjson_is_int(val)) {
            updated =
                check_int_range(key, yyjson_get_sint(val), config::kMinimumCapsuleEndRoundingDip,
                                config::kMaximumCapsuleEndRoundingDip,
                                current_config_.floating.capsule_end_rounding_dip);
        } else {
            err_msg = "Expected integer for capsule_end_rounding_dip";
        }
    } else if (key == "interaction.hover_delay_ms" || key == "capsule.hoverDelayMs" ||
               key == "hoverDelayMs") {
        if (yyjson_is_int(val)) {
            std::int64_t ms = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumHoverDelay.count(),
                                config::kMaximumHoverDelay.count(), ms)) {
                current_config_.interaction.hover_delay = std::chrono::milliseconds{ms};
                updated = true;
            }
        } else {
            err_msg = "Expected integer for hover_delay_ms";
        }
    } else if (key == "floating.anchor_side" || key == "capsule.anchorSide" ||
               key == "anchorSide") {
        if (yyjson_is_str(val)) {
            std::string_view str = yyjson_get_str(val);
            if (str == "left") {
                current_config_.floating.anchor_side = config::DockSide::Left;
                updated = true;
            } else if (str == "right") {
                current_config_.floating.anchor_side = config::DockSide::Right;
                updated = true;
            } else {
                err_msg = "Invalid anchor_side: " + std::string(str);
            }
        } else {
            err_msg = "Expected string for anchor_side";
        }
    } else if (key == "floating.anchor_vertical_offset_dip" ||
               key == "capsule.anchorVerticalOffsetDip" || key == "anchorVerticalOffsetDip") {
        if (yyjson_is_int(val)) {
            updated = check_int_range(key, yyjson_get_sint(val), 0, 100000,
                                      current_config_.floating.anchor_vertical_offset_dip);
        } else {
            err_msg = "Expected integer for anchor_vertical_offset_dip";
        }
    } else if (key == "floating.auto_hide_enabled" || key == "capsule.autoHideEnabled" ||
               key == "autoHideEnabled") {
        if (yyjson_is_bool(val)) {
            current_config_.floating.auto_hide_enabled = yyjson_get_bool(val);
            updated = true;
        } else {
            err_msg = "Expected boolean for auto_hide_enabled";
        }
    } else if (key == "floating.auto_hide_delay_ms" || key == "capsule.autoHideDelayMs" ||
               key == "autoHideDelayMs") {
        if (yyjson_is_int(val)) {
            std::int64_t ms = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumAutoHideDelay.count(),
                                config::kMaximumAutoHideDelay.count(), ms)) {
                current_config_.floating.auto_hide_delay = std::chrono::milliseconds{ms};
                updated = true;
            }
        } else {
            err_msg = "Expected integer for auto_hide_delay_ms";
        }
    } else if (key == "floating.nfm_pinned" || key == "monitor.pinned" || key == "pinned" ||
               key == "nfmPinned") {
        if (yyjson_is_bool(val)) {
            current_config_.floating.nfm_pinned = yyjson_get_bool(val);
            updated = true;
        } else {
            err_msg = "Expected boolean for nfm_pinned";
        }
    } else if (key == "interaction.dismiss_delay_ms" || key == "monitor.dismissDelayMs" ||
               key == "dismissDelayMs") {
        if (yyjson_is_int(val)) {
            std::int64_t ms = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumDismissDelay.count(),
                                config::kMaximumDismissDelay.count(), ms)) {
                current_config_.interaction.dismiss_delay = std::chrono::milliseconds{ms};
                updated = true;
            }
        } else {
            err_msg = "Expected integer for dismiss_delay_ms";
        }
    } else if (key == "floating.opacity_percent" || key == "monitor.opacityPercent" ||
               key == "opacityPercent") {
        if (yyjson_is_int(val)) {
            std::int64_t pct = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumOpacityPercent,
                                config::kMaximumOpacityPercent, pct)) {
                current_config_.floating.opacity_percent = static_cast<std::uint8_t>(pct);
                updated = true;
            }
        } else {
            err_msg = "Expected integer for opacity_percent";
        }
    } else if (key == "floating.blur_strength_percent" || key == "monitor.blurStrengthPercent" ||
               key == "blurStrengthPercent") {
        if (yyjson_is_int(val)) {
            std::int64_t pct = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumBlurStrengthPercent,
                                config::kMaximumBlurStrengthPercent, pct)) {
                current_config_.floating.blur_strength_percent = static_cast<std::uint8_t>(pct);
                updated = true;
            }
        } else {
            err_msg = "Expected integer for blur_strength_percent";
        }
    } else if (key == "floating.yield_topmost_when_fullscreen" ||
               key == "monitor.yieldTopmostWhenFullscreen" || key == "yieldTopmostWhenFullscreen") {
        if (yyjson_is_bool(val)) {
            current_config_.floating.yield_topmost_when_fullscreen = yyjson_get_bool(val);
            updated = true;
        } else {
            err_msg = "Expected boolean for yield_topmost_when_fullscreen";
        }
    } else if (key == "ranking.top_n" || key == "ranking.topN" || key == "topN") {
        if (yyjson_is_int(val)) {
            std::int64_t n = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumTopN,
                                config::kMaximumTopN, n)) {
                current_config_.ranking.top_n = static_cast<std::uint8_t>(n);
                updated = true;
            }
        } else {
            err_msg = "Expected integer for top_n";
        }
    } else if (key == "ranking.window_ms" || key == "ranking.windowMs" || key == "windowMs") {
        if (yyjson_is_int(val)) {
            std::int64_t ms = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumRankingWindow.count(),
                                config::kMaximumRankingWindow.count(), ms)) {
                current_config_.ranking.window = std::chrono::milliseconds{ms};
                updated = true;
            }
        } else {
            err_msg = "Expected integer for window_ms";
        }
    } else if (key == "display.refresh_interval_ms" || key == "ranking.refreshIntervalMs" ||
               key == "refreshIntervalMs") {
        if (yyjson_is_int(val)) {
            std::int64_t ms = 0;
            if (check_int_range(key, yyjson_get_sint(val), config::kMinimumRefreshInterval.count(),
                                config::kMaximumRefreshInterval.count(), ms)) {
                current_config_.display.refresh_interval = std::chrono::milliseconds{ms};
                updated = true;
            }
        } else {
            err_msg = "Expected integer for refresh_interval_ms";
        }
    } else if (key == "diagnostics.log_level" || key == "diagnostics.logLevel" ||
               key == "logLevel") {
        if (yyjson_is_str(val)) {
            std::string_view str = yyjson_get_str(val);
            if (str == "off") {
                current_config_.diagnostics.log_level = config::LogLevel::Off;
                updated = true;
            } else if (str == "error") {
                current_config_.diagnostics.log_level = config::LogLevel::Error;
                updated = true;
            } else if (str == "info") {
                current_config_.diagnostics.log_level = config::LogLevel::Info;
                updated = true;
            } else if (str == "debug") {
                current_config_.diagnostics.log_level = config::LogLevel::Debug;
                updated = true;
            } else {
                err_msg = "Invalid log_level: " + std::string(str);
            }
        } else {
            err_msg = "Expected string for log_level";
        }
    } else {
        err_msg = "Unknown or disallowed setting key: " + std::string(key);
    }

    yyjson_doc_free(doc);

    if (!updated) {
        native_bridge_->post_error(
            bridge::BridgeErrorDto{.code = std::string(bridge::to_error_code_string(
                                       bridge::BridgeErrorCode::InvalidSettingValue)),
                                   .message = err_msg});
        return;
    }

    // Persist configuration atomically
    static_cast<void>(config::SaveConfigFile(options_.config_file_path, current_config_,
                                             config::ConfigDocumentKind::User));

    if (theme_changed) {
        std::string theme_str = "System";
        if (current_config_.appearance.theme == config::ThemeMode::Light) {
            theme_str = "Light";
        } else if (current_config_.appearance.theme == config::ThemeMode::Dark) {
            theme_str = "Dark";
        }
        native_bridge_->post_theme_changed(bridge::ThemeChangedDto{.theme = std::move(theme_str)});
    }
    if (locale_changed) {
        std::string locale_str = "System";
        if (current_config_.appearance.language == config::LanguageMode::English) {
            locale_str = "English";
        } else if (current_config_.appearance.language == config::LanguageMode::SimplifiedChinese) {
            locale_str = "SimplifiedChinese";
        }
        native_bridge_->post_locale_changed(
            bridge::LocaleChangedDto{.locale = std::move(locale_str)});
    }

    // Broadcast authoritative snapshot
    native_bridge_->post_config_changed(current_config_);
}

void ProductHost::on_retry_collector() {
    if (collector_companion_) {
        collector_companion_->restart();
    }
}

void ProductHost::on_restart_collector() {
    if (collector_companion_) {
        collector_companion_->restart();
    }
}

void ProductHost::on_minimize_window() {
    if (window_) {
        window_->show(SW_MINIMIZE);
    }
}

void ProductHost::on_toggle_maximize() {
    if (window_ != nullptr && window_->hwnd() != nullptr) {
        if (IsZoomed(window_->hwnd())) {
            ShowWindow(window_->hwnd(), SW_RESTORE);
        } else {
            ShowWindow(window_->hwnd(), SW_MAXIMIZE);
        }
    }
}

void ProductHost::on_start_window_drag() {
    if (window_ != nullptr && window_->hwnd() != nullptr) {
        ReleaseCapture();
        SendMessageW(window_->hwnd(), WM_SYSCOMMAND, 0xF012, 0);
    }
}

void ProductHost::on_exit_app() {
    shutdown();
}

void ProductHost::on_set_theme(std::string_view theme) {
    if (native_bridge_) {
        native_bridge_->post_theme_changed(bridge::ThemeChangedDto{.theme = std::string(theme)});
    }
}

void ProductHost::on_set_locale(std::string_view locale) {
    if (native_bridge_) {
        native_bridge_->post_locale_changed(
            bridge::LocaleChangedDto{.locale = std::string(locale)});
    }
}

} // namespace nfm::host::app
