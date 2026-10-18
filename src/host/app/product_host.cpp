#include "network_flow_monitor/host/app/product_host.h"

#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/config/config_validation.h"
#include "network_flow_monitor/host/platform/webview2_environment.h"

#include <cmath>
#include <utility>
#include <yyjson.h>

namespace nfm::host::app {

namespace {
constexpr int kSettingsSurfaceWidthDip = 520;
} // namespace

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

    // 2. Initialize Coordinator & Window
    auto monitors = platform::Win32Window::enumerate_monitors();
    coordinator_ = std::make_unique<coordinator::DockCapsuleCoordinator>(current_config_, monitors);
    coordinator_->update_drag_threshold(platform::Win32Window::system_drag_threshold());
    const auto& geom = coordinator_->state().geometry;

    platform::WindowConfig win_cfg{
        .title = L"Network Flow Monitor",
        .initial_x = geom.x_px,
        .initial_y = geom.y_px,
        .initial_width = geom.width_px > 0 ? geom.width_px : 860,
        .initial_height = geom.height_px > 0 ? geom.height_px : 620,
        .frameless = true,
        .topmost = coordinator_->state().is_topmost,
        .tool_window = true,
        .transparent_backdrop = true,
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
        if (window_ != nullptr) {
            std::string msg(json);
            window_->post_ui_task([this, m = std::move(msg)]() noexcept {
                try {
                    if (!is_shutting_down_ && bridge_dispatcher_) {
                        bridge::BridgeErrorDto err;
                        bridge::CommandResponseDto resp;
                        if (!bridge_dispatcher_->dispatch_web_message(m, &err, &resp)) {
                            if (native_bridge_) {
                                native_bridge_->post_error(err);
                            }
                        } else if (!resp.command_id.empty() && native_bridge_) {
                            native_bridge_->post_command_response(resp);
                        }
                    }
                } catch (...) {
                }
            });
        }
    });

    // 4. Initialize Collector Companion
    collector_companion_ =
        std::make_shared<lifecycle::CollectorCompanion>(options_.collector_executable_path);

    collector_companion_->set_traffic_tick_callback([this](const bridge::TrafficTickDto& tick) {
        if (window_ != nullptr) {
            window_->post_ui_task([this, tick]() noexcept {
                try {
                    if (native_bridge_ && coordinator_ &&
                        coordinator_->state().presentation_state !=
                            coordinator::PresentationState::TrayOnly) {
                        native_bridge_->post_traffic_tick(tick);
                    }
                } catch (...) {
                }
            });
        }
    });

    collector_companion_->set_ranking_snapshot_callback(
        [this](const bridge::RankingSnapshotDto& snapshot) {
            if (window_ != nullptr) {
                auto snap_ptr = std::make_shared<bridge::RankingSnapshotDto>(snapshot);
                window_->post_ui_task([this, snap_ptr]() noexcept {
                    try {
                        if (snap_ptr) {
                            latest_ranking_snapshot_ = *snap_ptr;
                            if (native_bridge_ && coordinator_ &&
                                coordinator_->state().presentation_state ==
                                    coordinator::PresentationState::PanelVisible) {
                                native_bridge_->post_ranking_snapshot(*snap_ptr);
                            }
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
                        if (status_ptr) {
                            latest_collector_status_ = *status_ptr;
                            broadcast_to_all_bridges([&status_ptr](bridge::INativeBridge& b) {
                                b.post_collector_status(*status_ptr);
                            });
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
                    if (reset_ptr && native_bridge_) {
                        native_bridge_->post_session_reset(*reset_ptr);
                    }
                } catch (...) {
                }
            });
        }
    });

    // 5. Create Win32 Window and show initial geometry before WebView2 controller creation
    if (!window_->create(this)) {
        return false;
    }
    apply_coordinator_state();

    // 6. Start Collector Companion
    collector_companion_->start();

    // 7. Initialize WebView2
    webview_->set_navigation_callback([this](bool /*is_success*/, int /*http_status*/) {
        if (window_ != nullptr) {
            window_->post_ui_task([this]() noexcept {
                try {
                    if (window_) {
                        apply_coordinator_state();
                        publish_surface_state();
                        if (native_bridge_) {
                            native_bridge_->post_config_changed(current_config_);
                            if (latest_collector_status_.has_value()) {
                                native_bridge_->post_collector_status(*latest_collector_status_);
                            }
                            if (latest_ranking_snapshot_.has_value()) {
                                native_bridge_->post_ranking_snapshot(*latest_ranking_snapshot_);
                            }
                        }
                        ensure_settings_window();
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
    settings_bridge_.reset();
    settings_webview_.reset();
    if (settings_window_) {
        settings_window_->close();
    }

    native_bridge_.reset();
    webview_.reset();
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
    if (coordinator_) {
        coordinator_->update_monitors(platform::Win32Window::enumerate_monitors());
        apply_coordinator_state();
    }
}

void ProductHost::on_display_change() {
    if (coordinator_) {
        coordinator_->update_monitors(platform::Win32Window::enumerate_monitors());
        apply_coordinator_state();
    }
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

void ProductHost::on_tray_context_menu(int screen_x, int screen_y) {
    on_open_context_menu(screen_x, screen_y);
}

void ProductHost::on_context_menu_show_monitor() {
    if (coordinator_ &&
        coordinator_->state().presentation_state == coordinator::PresentationState::PanelVisible) {
        on_hide_monitor();
    } else {
        on_show_monitor();
    }
}

void ProductHost::on_context_menu_toggle_pin() {
    if (coordinator_) {
        on_set_pin(!coordinator_->state().is_pinned);
    }
}

void ProductHost::on_context_menu_settings() {
    on_open_settings("");
}

void ProductHost::on_context_menu_retry_collector() {
    on_retry_collector();
}

void ProductHost::on_context_menu_exit() {
    on_exit_app();
}

void ProductHost::on_context_menu_opened() {
    if (coordinator_) {
        coordinator_->on_context_menu_opened();
    }
}

void ProductHost::on_context_menu_closed(bool cursor_inside) {
    if (coordinator_) {
        coordinator_->on_context_menu_closed(cursor_inside);
        apply_coordinator_state();
        publish_surface_state();
    }
}

void ProductHost::on_surface_pointer_enter() {
    if (!coordinator_) {
        return;
    }
    if (coordinator_->state().presentation_state == coordinator::PresentationState::PanelVisible) {
        coordinator_->on_pointer_enter_panel();
    } else {
        coordinator_->on_pointer_enter_capsule();
        apply_coordinator_state();
        publish_surface_state();
    }
}

void ProductHost::on_surface_pointer_leave() {
    if (!coordinator_) {
        return;
    }
    if (coordinator_->state().presentation_state == coordinator::PresentationState::PanelVisible) {
        coordinator_->on_pointer_leave_panel();
    } else {
        coordinator_->on_pointer_leave_capsule();
    }
}

void ProductHost::on_surface_pointer_down(surface::ScreenPoint point) {
    if (coordinator_ && (coordinator_->state().presentation_state ==
                             coordinator::PresentationState::CapsuleVisible ||
                         coordinator_->state().presentation_state ==
                             coordinator::PresentationState::PanelVisible)) {
        coordinator_->on_pointer_down_capsule(point);
    }
}
void ProductHost::on_surface_pointer_move(surface::ScreenPoint point) {
    if (!coordinator_) {
        return;
    }
    const auto before = coordinator_->state().geometry;
    coordinator_->on_pointer_move(point);
    if (coordinator_->state().geometry != before) {
        apply_coordinator_state();
    }
}

void ProductHost::on_surface_pointer_up(surface::ScreenPoint point) {
    if (!coordinator_) {
        return;
    }
    coordinator_->on_pointer_up(point);
    apply_coordinator_state();
    publish_surface_state();
}

void ProductHost::on_surface_pointer_cancel() {
    if (!coordinator_) {
        return;
    }
    coordinator_->on_cancel_drag();
    apply_coordinator_state();
}

void ProductHost::on_surface_clock_tick(std::chrono::milliseconds elapsed) {
    if (!coordinator_) {
        return;
    }
    const auto before = coordinator_->state().presentation_state;
    const bool was_topmost = coordinator_->state().is_topmost;
    const bool was_auto_hidden = coordinator_->state().is_auto_hidden;
    const bool was_iconic =
        (window_ != nullptr && window_->hwnd() != nullptr && IsIconic(window_->hwnd()) != 0);
    if (coordinator_->state().is_dragging && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        POINT pt{};
        if (GetCursorPos(&pt) != FALSE) {
            coordinator_->on_pointer_up(surface::ScreenPoint{.x_px = pt.x, .y_px = pt.y});
        } else {
            coordinator_->on_cancel_drag();
        }
        apply_coordinator_state();
        publish_surface_state();
    }
    coordinator_->set_fullscreen_active_on_same_monitor(
        window_ &&
        platform::Win32Window::is_foreground_fullscreen_on_same_monitor(window_->hwnd()));
    coordinator_->advance_time(elapsed);
    if (coordinator_->state().presentation_state != before ||
        coordinator_->state().is_topmost != was_topmost ||
        coordinator_->state().is_auto_hidden != was_auto_hidden || was_iconic) {
        apply_coordinator_state();
        publish_surface_state();
    }
    watchdog_elapsed_ += elapsed;
    if (watchdog_elapsed_ >= std::chrono::milliseconds{1000}) {
        watchdog_elapsed_ = std::chrono::milliseconds{0};
        if (webview_) {
            static_cast<void>(webview_->ensure_alive());
        }
        if (settings_surface_visible_ && settings_webview_) {
            static_cast<void>(settings_webview_->ensure_alive());
        }
        if (window_ && coordinator_->state().is_topmost &&
            coordinator_->state().presentation_state != coordinator::PresentationState::TrayOnly) {
            window_->set_topmost(true);
        }
    }
}

void ProductHost::on_surface_outside_click() {
    if (!coordinator_) {
        return;
    }
    const auto& state = coordinator_->state();
    if (state.presentation_state == coordinator::PresentationState::PanelVisible &&
        !state.is_pinned && !state.is_context_menu_open && !state.is_dragging) {
        coordinator_->show_capsule();
        apply_coordinator_state();
        publish_surface_state();
    }
}
void ProductHost::broadcast_to_all_bridges(const std::function<void(bridge::INativeBridge&)>& fn) {
    if (native_bridge_) {
        fn(*native_bridge_);
    }
    if (settings_surface_visible_ && settings_bridge_) {
        fn(*settings_bridge_);
    }
}

void ProductHost::apply_settings_window_chrome() {
    if (!settings_window_) {
        return;
    }
    bool dark_mode = true;
    if (current_config_.appearance.theme == config::ThemeMode::Light) {
        dark_mode = false;
    } else if (current_config_.appearance.theme == config::ThemeMode::Dark) {
        dark_mode = true;
    } else {
        dark_mode = platform::Win32Window::is_system_dark_mode();
    }
    settings_window_->configure_surface_chrome(true, dark_mode);
}
void ProductHost::ensure_settings_window() {
    const auto* monitor = coordinator_ ? coordinator_->active_monitor() : nullptr;
    const double scale = monitor != nullptr ? monitor->dpi_scale() : 1.0;
    const int width = static_cast<int>(std::round(kSettingsSurfaceWidthDip * scale));
    const int height = static_cast<int>(std::round(settings_height_dip_ * scale));
    const int x =
        monitor != nullptr ? monitor->work_left_px + ((monitor->work_width_px() - width) / 2) : 120;
    const int y = monitor != nullptr
                      ? monitor->work_top_px + ((monitor->work_height_px() - height) / 2)
                      : 120;
    if (!settings_window_) {
        class SettingsWindowListener : public platform::IWindowEventListener {
          public:
            explicit SettingsWindowListener(ProductHost* host) : host_(host) {}
            void on_window_created(HWND /*hwnd*/) override {}
            void on_window_size_changed(int width_px, int height_px) override {
                if (host_->settings_webview_) {
                    host_->settings_webview_->resize(width_px, height_px);
                }
            }
            void on_window_dpi_changed(std::uint32_t /*new_dpi*/,
                                       const RECT* suggested_rect) override {
                if (host_->settings_window_ && suggested_rect != nullptr) {
                    host_->settings_window_->set_bounds(
                        suggested_rect->left, suggested_rect->top,
                        suggested_rect->right - suggested_rect->left,
                        suggested_rect->bottom - suggested_rect->top);
                }
            }
            void on_window_close_requested() override {
                host_->close_settings_window();
            }
            void on_window_destroyed() override {}

          private:
            ProductHost* host_;
        };

        settings_listener_ = std::make_unique<SettingsWindowListener>(this);
        platform::WindowConfig cfg{
            .title = L"Network Flow Monitor - Settings",
            .class_name = L"NetworkFlowMonitor_SettingsWindow",
            .initial_x = x,
            .initial_y = y,
            .initial_width = width,
            .initial_height = height,
            .frameless = true,
            .topmost = false,
            .tool_window = false,
            .transparent_backdrop = true,
        };
        settings_window_ = std::make_unique<platform::Win32Window>(cfg);
        if (!settings_window_->create(settings_listener_.get())) {
            settings_window_.reset();
            settings_listener_.reset();
            return;
        }
        settings_window_->set_taskbar_visibility(true);
        apply_settings_window_chrome();
        settings_webview_ = std::make_shared<webview::WebView2Controller>();
        settings_webview_->set_visible(settings_surface_visible_);
        settings_bridge_ = std::make_shared<bridge::NativeBridge>([this](std::string_view json) {
            if (settings_webview_ && settings_webview_->is_ready()) {
                settings_webview_->post_web_message_json(json);
            }
        });
        settings_webview_->set_message_callback([this](std::string_view json) {
            if (window_ != nullptr) {
                std::string msg(json);
                window_->post_ui_task([this, m = std::move(msg)]() noexcept {
                    try {
                        if (!is_shutting_down_ && bridge_dispatcher_) {
                            bridge::BridgeErrorDto err;
                            bridge::CommandResponseDto resp;
                            if (!bridge_dispatcher_->dispatch_web_message(m, &err, &resp)) {
                                if (settings_bridge_) {
                                    settings_bridge_->post_error(err);
                                }
                            } else if (!resp.command_id.empty() && settings_bridge_) {
                                settings_bridge_->post_command_response(resp);
                            }
                        }
                    } catch (...) {
                    }
                });
            }
        });
        settings_webview_->set_navigation_callback([this](bool /*is_success*/,
                                                          int /*http_status*/) {
            if (window_ != nullptr) {
                window_->post_ui_task([this]() noexcept {
                    try {
                        if (settings_window_ && settings_bridge_) {
                            settings_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
                                .surface = bridge::SurfaceKind::Settings,
                                .pinned = coordinator_ ? coordinator_->state().is_pinned : false,
                                .dock_side = coordinator_ ? coordinator_->state().anchor_side
                                                          : config::DockSide::Right,
                                .auto_hidden = false,
                            });
                            settings_bridge_->post_config_changed(current_config_);
                            if (latest_collector_status_.has_value()) {
                                settings_bridge_->post_collector_status(*latest_collector_status_);
                            }
                            if (settings_surface_visible_) {
                                if (settings_webview_) {
                                    settings_webview_->set_visible(true);
                                }
                                settings_window_->show(SW_SHOW);
                                SetForegroundWindow(settings_window_->hwnd());
                            } else if (settings_webview_) {
                                settings_webview_->set_visible(false);
                            }
                        }
                    } catch (...) {
                    }
                });
            }
        });
        static_cast<void>(
            settings_webview_->initialize(settings_window_->hwnd(), options_.static_assets_dir));
        if (!options_.dev_server_url.empty()) {
            settings_webview_->navigate(options_.dev_server_url);
        }
    } else if (settings_surface_visible_) {
        apply_settings_window_chrome();
        RECT rc{};
        if (GetWindowRect(settings_window_->hwnd(), &rc) == FALSE ||
            IsWindowVisible(settings_window_->hwnd()) == FALSE) {
            settings_window_->set_bounds(x, y, width, height);
        }
        if (settings_webview_) {
            settings_webview_->set_visible(true);
            if (settings_webview_->is_ready()) {
                static_cast<void>(settings_webview_->ensure_alive());
            }
        }
        if (settings_bridge_) {
            settings_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
                .surface = bridge::SurfaceKind::Settings,
                .pinned = coordinator_ ? coordinator_->state().is_pinned : false,
                .dock_side =
                    coordinator_ ? coordinator_->state().anchor_side : config::DockSide::Right,
                .auto_hidden = false,
            });
            settings_bridge_->post_config_changed(current_config_);
            if (latest_collector_status_.has_value()) {
                settings_bridge_->post_collector_status(*latest_collector_status_);
            }
        }
        if (!settings_webview_ || settings_webview_->is_ready()) {
            settings_window_->show(SW_SHOW);
            SetForegroundWindow(settings_window_->hwnd());
        }
    }
}

void ProductHost::close_settings_window() {
    settings_surface_visible_ = false;
    if (settings_webview_) {
        settings_webview_->set_visible(false);
    }
    if (settings_window_) {
        settings_window_->hide();
    }
}

void ProductHost::apply_coordinator_state() {
    if (!coordinator_ || !window_) {
        return;
    }
    const auto& state = coordinator_->state();
    bool dark_mode = true;
    if (current_config_.appearance.theme == config::ThemeMode::Light) {
        dark_mode = false;
    } else if (current_config_.appearance.theme == config::ThemeMode::Dark) {
        dark_mode = true;
    } else {
        dark_mode = platform::Win32Window::is_system_dark_mode();
    }
    window_->set_taskbar_visibility(false);
    if (state.presentation_state == coordinator::PresentationState::TrayOnly) {
        window_->hide();
    } else {
        const bool is_panel =
            (state.presentation_state == coordinator::PresentationState::PanelVisible);
        window_->configure_surface_chrome(is_panel, dark_mode);
        window_->set_bounds(state.geometry.x_px, state.geometry.y_px, state.geometry.width_px,
                            state.geometry.height_px);
        window_->set_topmost(state.is_topmost);
        window_->show(SW_SHOWNOACTIVATE);
        if (webview_) {
            webview_->resize(state.geometry.width_px, state.geometry.height_px);
        }
    }
    if (state.anchor_updated) {
        current_config_.floating.anchor_side = state.anchor_side;
        current_config_.floating.anchor_vertical_offset_dip = state.anchor_vertical_offset_dip;
        if (!state.anchor_monitor_id.empty()) {
            strncpy_s(current_config_.floating.anchor_monitor_id.data(),
                      current_config_.floating.anchor_monitor_id.size(),
                      state.anchor_monitor_id.c_str(), _TRUNCATE);
        }
        coordinator_->clear_emitted_updates();
        static_cast<void>(config::SaveConfigFile(options_.config_file_path, current_config_,
                                                 config::ConfigDocumentKind::User));
        broadcast_to_all_bridges(
            [this](bridge::INativeBridge& b) { b.post_config_changed(current_config_); });
    }
}

void ProductHost::publish_surface_state() {
    if (!native_bridge_ || !coordinator_) {
        return;
    }
    bridge::SurfaceKind surface = bridge::SurfaceKind::Capsule;
    switch (coordinator_->state().presentation_state) {
    case coordinator::PresentationState::TrayOnly:
        surface = bridge::SurfaceKind::Tray;
        break;
    case coordinator::PresentationState::PanelVisible:
        surface = bridge::SurfaceKind::Panel;
        break;
    case coordinator::PresentationState::CapsuleVisible:
    default:
        surface = bridge::SurfaceKind::Capsule;
        break;
    }
    native_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
        .surface = surface,
        .pinned = coordinator_->state().is_pinned,
        .dock_side = coordinator_->state().anchor_side,
        .auto_hidden = coordinator_->state().is_auto_hidden,
    });
}

void ProductHost::on_show_monitor() {
    if (coordinator_) {
        coordinator_->show_monitor();
        apply_coordinator_state();
        if (webview_) {
            static_cast<void>(webview_->ensure_alive());
        }
        publish_surface_state();
        if (native_bridge_) {
            if (latest_collector_status_.has_value()) {
                native_bridge_->post_collector_status(*latest_collector_status_);
            }
            if (latest_ranking_snapshot_.has_value()) {
                native_bridge_->post_ranking_snapshot(*latest_ranking_snapshot_);
            }
        }
    } else if (window_) {
        window_->show();
    }
}

void ProductHost::on_hide_monitor() {
    if (settings_surface_visible_) {
        close_settings_window();
        return;
    }
    if (coordinator_) {
        coordinator_->show_capsule();
        apply_coordinator_state();
        publish_surface_state();
    } else if (window_) {
        window_->hide();
    }
}

void ProductHost::on_set_pin(bool pinned) {
    if (coordinator_) {
        current_config_.floating.nfm_pinned = pinned;
        coordinator_->set_pin(pinned);
        static_cast<void>(config::SaveConfigFile(options_.config_file_path, current_config_,
                                                 config::ConfigDocumentKind::User));
        apply_coordinator_state();
        broadcast_to_all_bridges(
            [this](bridge::INativeBridge& b) { b.post_config_changed(current_config_); });
        publish_surface_state();
    }
}

void ProductHost::on_open_context_menu(int screen_x, int screen_y) {
    if (!window_ || !coordinator_) {
        return;
    }
    const auto& state = coordinator_->state();
    bool is_chinese =
        (current_config_.appearance.language == config::LanguageMode::SimplifiedChinese);
    bool is_running = collector_companion_ && collector_companion_->is_running();
    std::wstring status;
    if (is_running) {
        status = is_chinese ? L"运行中" : L"Running";
    } else {
        status = is_chinese ? L"已停止" : L"Stopped";
    }

    platform::ContextMenuOptions opts{
        .screen_x = screen_x,
        .screen_y = screen_y,
        .is_pinned = state.is_pinned,
        .is_panel_visible =
            (state.presentation_state == coordinator::PresentationState::PanelVisible),
        .collector_status = std::move(status),
        .can_retry_collector = !is_running,
        .is_chinese = is_chinese,
    };
    window_->show_context_menu(opts);
}

void ProductHost::on_resize_panel(int height_dip) {
    if (coordinator_) {
        const auto before = coordinator_->state().geometry;
        coordinator_->set_panel_height_dip(height_dip);
        if (coordinator_->state().geometry != before) {
            apply_coordinator_state();
        }
    }
}

void ProductHost::on_resize_settings_window(int height_dip) {
    const int clamped_dip = std::clamp(height_dip, 140, 560);
    if (settings_height_dip_ == clamped_dip && !settings_window_) {
        return;
    }
    settings_height_dip_ = clamped_dip;
    if (settings_window_ && settings_window_->hwnd() != nullptr) {
        const double scale = settings_window_->dpi_scale();
        const int width_px = static_cast<int>(std::round(kSettingsSurfaceWidthDip * scale));
        const int height_px = static_cast<int>(std::round(settings_height_dip_ * scale));
        RECT rc{};
        if (GetWindowRect(settings_window_->hwnd(), &rc) != FALSE) {
            if ((rc.right - rc.left) != width_px || (rc.bottom - rc.top) != height_px) {
                settings_window_->set_bounds(rc.left, rc.top, width_px, height_px);
                if (settings_webview_) {
                    settings_webview_->resize(width_px, height_px);
                }
            }
        }
    }
}
void ProductHost::on_open_settings(std::string_view /*route*/) {
    settings_surface_visible_ = true;
    if (window_ && window_->hwnd() != nullptr) {
        ensure_settings_window();
    } else if (native_bridge_ && coordinator_) {
        native_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
            .surface = bridge::SurfaceKind::Settings,
            .pinned = coordinator_->state().is_pinned,
            .dock_side = coordinator_->state().anchor_side,
            .auto_hidden = coordinator_->state().is_auto_hidden,
        });
    }
}

void ProductHost::on_request_config() {
    publish_surface_state();
    if (settings_bridge_) {
        settings_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
            .surface = bridge::SurfaceKind::Settings,
            .pinned = coordinator_ ? coordinator_->state().is_pinned : false,
            .dock_side = coordinator_ ? coordinator_->state().anchor_side : config::DockSide::Right,
            .auto_hidden = false,
        });
        if (!settings_surface_visible_) {
            settings_bridge_->post_config_changed(current_config_);
            if (latest_collector_status_.has_value()) {
                settings_bridge_->post_collector_status(*latest_collector_status_);
            }
        }
    }
    broadcast_to_all_bridges([this](bridge::INativeBridge& b) {
        b.post_config_changed(current_config_);
        if (latest_collector_status_.has_value()) {
            b.post_collector_status(*latest_collector_status_);
        }
    });
    if (native_bridge_ && latest_ranking_snapshot_.has_value()) {
        native_bridge_->post_ranking_snapshot(*latest_ranking_snapshot_);
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
            current_config_.floating.auto_hide_delay = std::chrono::milliseconds{500};
            updated = true;
        } else {
            err_msg = "Expected integer for auto_hide_delay_ms";
        }
    } else if (key == "floating.download_color_hex" || key == "capsule.downloadColorHex" ||
               key == "downloadColorHex") {
        if (yyjson_is_str(val)) {
            std::string_view str = yyjson_get_str(val);
            if (str.size() == 7U && str.front() == '#') {
                current_config_.floating.download_color_hex.fill('\0');
                std::ranges::copy(str, current_config_.floating.download_color_hex.begin());
                updated = true;
            } else {
                err_msg = "Invalid download_color_hex";
            }
        } else {
            err_msg = "Expected string for download_color_hex";
        }
    } else if (key == "floating.upload_color_hex" || key == "capsule.uploadColorHex" ||
               key == "uploadColorHex") {
        if (yyjson_is_str(val)) {
            std::string_view str = yyjson_get_str(val);
            if (str.size() == 7U && str.front() == '#') {
                current_config_.floating.upload_color_hex.fill('\0');
                std::ranges::copy(str, current_config_.floating.upload_color_hex.begin());
                updated = true;
            } else {
                err_msg = "Invalid upload_color_hex";
            }
        } else {
            err_msg = "Expected string for upload_color_hex";
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
        broadcast_to_all_bridges([&theme_str](bridge::INativeBridge& b) {
            b.post_theme_changed(bridge::ThemeChangedDto{.theme = theme_str});
        });
    }
    if (locale_changed) {
        std::string locale_str = "System";
        if (current_config_.appearance.language == config::LanguageMode::English) {
            locale_str = "English";
        } else if (current_config_.appearance.language == config::LanguageMode::SimplifiedChinese) {
            locale_str = "SimplifiedChinese";
        }
        broadcast_to_all_bridges([&locale_str](bridge::INativeBridge& b) {
            b.post_locale_changed(bridge::LocaleChangedDto{.locale = locale_str});
        });
    }

    if (coordinator_) {
        coordinator_->update_config(current_config_);
        apply_coordinator_state();
        publish_surface_state();
    }
    apply_settings_window_chrome();

    // Broadcast authoritative snapshot
    broadcast_to_all_bridges(
        [this](bridge::INativeBridge& b) { b.post_config_changed(current_config_); });
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
        if (IsZoomed(window_->hwnd()) != 0) {
            ShowWindow(window_->hwnd(), SW_RESTORE);
        } else {
            ShowWindow(window_->hwnd(), SW_MAXIMIZE);
        }
    }
}

void ProductHost::on_start_window_drag() {
    if (window_ == nullptr || window_->hwnd() == nullptr) {
        return;
    }
    POINT pt{};
    if (GetCursorPos(&pt) == FALSE) {
        return;
    }
    if (settings_window_ && settings_window_->hwnd() != nullptr &&
        IsWindowVisible(settings_window_->hwnd()) != FALSE) {
        RECT settings_rc{};
        if (GetWindowRect(settings_window_->hwnd(), &settings_rc) != FALSE &&
            PtInRect(&settings_rc, pt) != FALSE) {
            ReleaseCapture();
            SendMessageW(settings_window_->hwnd(), WM_SYSCOMMAND, 0xF012, 0);
            return;
        }
    }
    if (coordinator_) {
        window_->begin_interactive_drag();
        coordinator_->on_pointer_down_capsule(surface::ScreenPoint{.x_px = pt.x, .y_px = pt.y});
    }
}

void ProductHost::on_exit_app() {
    shutdown();
}

void ProductHost::on_set_theme(std::string_view theme) {
    const std::string t_str(theme);
    broadcast_to_all_bridges([&t_str](bridge::INativeBridge& b) {
        b.post_theme_changed(bridge::ThemeChangedDto{.theme = t_str});
    });
}

void ProductHost::on_set_locale(std::string_view locale) {
    const std::string l_str(locale);
    broadcast_to_all_bridges([&l_str](bridge::INativeBridge& b) {
        b.post_locale_changed(bridge::LocaleChangedDto{.locale = l_str});
    });
}

} // namespace nfm::host::app
