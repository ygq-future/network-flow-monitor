#include "network_flow_monitor/host/app/product_host.h"

#include "network_flow_monitor/host/platform/webview2_environment.h"

#include <utility>

namespace nfm::host::app {

ProductHost::ProductHost(ProductHostOptions options) : options_(std::move(options)) {
    if (options_.static_assets_dir.empty()) {
        // Default to "ui" directory relative to executable directory
        options_.static_assets_dir = std::filesystem::current_path() / "ui";
    }
    if (options_.collector_executable_path.empty()) {
        options_.collector_executable_path = std::filesystem::current_path() / "nfm-collector.exe";
    }
}

ProductHost::~ProductHost() {
    shutdown();
}

bool ProductHost::initialize() {
    if (is_initialized_) {
        return true;
    }

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
        .initial_width = 720,
        .initial_height = 540,
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
                window_->post_ui_task([this, status]() noexcept {
                    try {
                        if (native_bridge_) {
                            native_bridge_->post_collector_status(status);
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
    if (!webview_->initialize(window_->hwnd(), options_.static_assets_dir)) {
        return false;
    }

    if (!options_.dev_server_url.empty()) {
        webview_->navigate(options_.dev_server_url);
    }

    window_->show();
    is_initialized_ = true;
    return true;
}

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

void ProductHost::on_update_setting(std::string_view /*key*/, std::string_view /*value_json*/) {}

void ProductHost::on_retry_collector() {
    if (collector_companion_) {
        collector_companion_->restart();
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
