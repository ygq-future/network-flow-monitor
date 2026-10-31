#include "network_flow_monitor/host/app/product_host.h"

#include "network_flow_monitor/build/dependencies.h"
#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/config/config_validation.h"
#include "network_flow_monitor/host/platform/webview2_environment.h"

#include <cmath>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <utility>
#include <yyjson.h>
namespace nfm::host::app {

namespace {
constexpr int kSettingsSurfaceWidthDip = 520;
constexpr std::chrono::milliseconds kPanelCollapseBoundsDelay{260};
constexpr std::chrono::milliseconds kPanelShrinkBoundsDelay{240};
constexpr std::chrono::milliseconds kTrafficHistoryAutoSaveInterval{30'000};
constexpr std::chrono::milliseconds kPresentationRepairLogCooldown{30'000};
constexpr std::chrono::milliseconds kUiStallLogThreshold{750};
constexpr std::chrono::milliseconds kUiStallLogCooldown{5'000};
constexpr std::chrono::milliseconds kHistoryPushInterval{30'000};
} // namespace

ProductHost::ProductHost(ProductHostOptions options) : options_(std::move(options)) {
    const std::filesystem::path exe_path = platform::StartupRegistration::current_executable_path();
    const std::filesystem::path exe_dir = exe_path.parent_path();

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
        options_.config_file_path = config::PortableConfigPath(exe_path);
        const auto legacy_config_path = config::LegacyRootConfigPath(exe_path);
        const DWORD target_attrs = GetFileAttributesW(options_.config_file_path.c_str());
        const DWORD legacy_attrs = GetFileAttributesW(legacy_config_path.c_str());
        if (target_attrs == INVALID_FILE_ATTRIBUTES && legacy_attrs != INVALID_FILE_ATTRIBUTES &&
            (legacy_attrs & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
            static_cast<void>(SHCreateDirectoryExW(
                nullptr, options_.config_file_path.parent_path().c_str(), nullptr));
            static_cast<void>(MoveFileExW(legacy_config_path.c_str(),
                                          options_.config_file_path.c_str(),
                                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
        }
    }
    if (options_.traffic_history_file_path.empty()) {
        if (!options_.config_file_path.empty() &&
            !options_.config_file_path.parent_path().empty()) {
            options_.traffic_history_file_path =
                options_.config_file_path.parent_path() / L"traffic_history.json";
        } else {
            options_.traffic_history_file_path = config::PortableTrafficHistoryPath(exe_path);
        }
    }
    traffic_history_store_ =
        std::make_unique<history::TrafficHistoryStore>(options_.traffic_history_file_path);
    if (options_.log_directory_path.empty()) {
        const std::filesystem::path cfg_parent = options_.config_file_path.parent_path();
        if (cfg_parent.filename() == L"data" && !cfg_parent.parent_path().empty()) {
            options_.log_directory_path = cfg_parent.parent_path() / L"logs";
        } else if (!cfg_parent.empty()) {
            options_.log_directory_path = cfg_parent / L"logs";
        } else {
            options_.log_directory_path = config::PortableLogDirectory(exe_path);
        }
    }
    logger_.set_options(platform::RotatingFileLoggerOptions{
        .log_directory = options_.log_directory_path,
    });

    app_updater_ = std::make_unique<platform::AppUpdater>(platform::AppUpdaterOptions{
        .current_version = NFM_APP_VERSION,
        .github_repo = NFM_GITHUB_REPO,
        .app_root_dir = exe_dir,
        .host_executable_path = exe_path,
    });
    coordinator_ = std::make_unique<coordinator::DockCapsuleCoordinator>(
        current_config_, platform::Win32Window::enumerate_monitors());
    coordinator_->update_drag_threshold(platform::Win32Window::system_drag_threshold());
    native_bridge_ = std::make_shared<bridge::NativeBridge>([this](std::string_view json) {
        if (webview_ && webview_->is_ready()) {
            webview_->post_web_message_json(json);
        }
    });
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
    if (load_result.status == config::ConfigLoadStatus::Damaged ||
        load_result.status == config::ConfigLoadStatus::IoError) {
        std::string detail =
            load_result.error.has_value() ? load_result.error->detail : "unknown config error";
        if (load_result.error.has_value() && load_result.error->system_error != 0U) {
            detail += " (win32_error=" + std::to_string(load_result.error->system_error) + ")";
        }
        write_diagnostic_log(config::LogLevel::Error,
                             "Configuration load degraded to defaults: " + detail);
    }
    if (traffic_history_store_) {
        static_cast<void>(traffic_history_store_->load());
    }
    lifecycle::load_executable_icon_cache(options_.config_file_path.parent_path() /
                                          L"icon_cache.json");

    // Synchronize startup registry entry with authoritative config on launch
    const auto current_exe = platform::StartupRegistration::current_executable_path();
    if (!current_exe.empty()) {
        const auto startup_sync = platform::StartupRegistration::sync_login_startup(
            current_config_.startup.ui_at_login, current_exe, options_.startup_options);
        if (!startup_sync.success) {
            write_diagnostic_log(config::LogLevel::Error, "Startup registration sync failed: " +
                                                              startup_sync.diagnostic_summary);
        } else if (current_config_.startup.ui_at_login || startup_sync.removed_entries_count > 0U) {
            write_diagnostic_log(config::LogLevel::Info, startup_sync.diagnostic_summary);
        }
    }

    // 1. Detect WebView2 Evergreen Runtime
    auto rt_info = platform::WebView2Environment::detect_runtime();
    if (!rt_info.is_available) {
        write_diagnostic_log(config::LogLevel::Error,
                             "WebView2 Evergreen Runtime not detected on host");
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
        write_diagnostic_log(config::LogLevel::Error, "Frontend static bundle index.html missing");
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
    webview_->set_diagnostic_callback(
        [this](std::string_view diag) { write_diagnostic_log(config::LogLevel::Error, diag); });
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

    collector_companion_->set_excluded_programs(current_config_.ranking.excluded_programs);
    collector_companion_->set_traffic_tick_callback([this](const bridge::TrafficTickDto& tick) {
        if (window_ != nullptr) {
            window_->post_ui_task([this, tick]() noexcept {
                try {
                    if (native_bridge_ && coordinator_ &&
                        coordinator_->state().presentation_state !=
                            coordinator::PresentationState::TrayOnly &&
                        !coordinator_->state().is_fullscreen_hidden) {
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
                        if (settings_surface_visible_ && settings_bridge_ && collector_companion_) {
                            settings_bridge_->post_connected_applications(
                                collector_companion_->connected_applications());
                        }
                    } catch (...) {
                    }
                });
            }
        });
    collector_companion_->set_raw_ranking_snapshot_callback(
        [this](const ipc::RankingSnapshotMessage& raw_snapshot) {
            bool changed = false;
            if (traffic_history_store_) {
                changed = traffic_history_store_->record_snapshot(
                    raw_snapshot, current_config_.ranking.excluded_programs);
            }
            if (changed && window_ != nullptr) {
                window_->post_ui_task([this]() noexcept {
                    try {
                        // Snapshots change several times per second; publishing the full history at
                        // that rate makes the settings renderer re-parse and re-render it
                        // constantly.
                        if (!is_shutting_down_ && settings_surface_visible_) {
                            push_traffic_history_to_settings(false);
                        }
                    } catch (...) {
                    }
                });
            }
        });

    collector_companion_->set_diagnostic_callback([this](bool is_error, std::string_view message) {
        write_diagnostic_log(is_error ? config::LogLevel::Error : config::LogLevel::Info, message);
    });

    collector_companion_->set_collector_status_callback(
        [this](const bridge::CollectorStatusDto& status) {
            if (window_ != nullptr) {
                auto status_ptr = std::make_shared<bridge::CollectorStatusDto>(status);
                window_->post_ui_task([this, status_ptr]() noexcept {
                    try {
                        if (status_ptr) {
                            latest_collector_status_ = *status_ptr;
                            if (!status_ptr->last_error.empty() &&
                                status_ptr->last_error != last_logged_collector_error_) {
                                last_logged_collector_error_ = status_ptr->last_error;
                                write_diagnostic_log(config::LogLevel::Error,
                                                     "Collector error: " + status_ptr->last_error);
                            } else if (status_ptr->last_error.empty()) {
                                last_logged_collector_error_.clear();
                            }
                            if (status_ptr->dropped_packets > last_logged_dropped_packets_) {
                                const auto delta =
                                    status_ptr->dropped_packets - last_logged_dropped_packets_;
                                last_logged_dropped_packets_ = status_ptr->dropped_packets;
                                write_diagnostic_log(
                                    config::LogLevel::Info,
                                    "ETW packet drop detected (delta=" + std::to_string(delta) +
                                        ", total_dropped=" +
                                        std::to_string(status_ptr->dropped_packets) + ")");
                            }
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
        if (traffic_history_store_) {
            traffic_history_store_->on_session_reset(reset.collector_instance_id);
        }
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

    if (app_updater_) {
        app_updater_->set_diagnostic_callback([this](bool is_error, std::string_view message) {
            write_diagnostic_log(is_error ? config::LogLevel::Error : config::LogLevel::Info,
                                 message);
        });
        app_updater_->set_status_callback([this](const bridge::UpdateStatusDto& status) {
            if (window_ != nullptr) {
                auto status_ptr = std::make_shared<bridge::UpdateStatusDto>(status);
                window_->post_ui_task([this, status_ptr]() noexcept {
                    try {
                        if (!is_shutting_down_ && status_ptr) {
                            broadcast_to_all_bridges([&status_ptr](bridge::INativeBridge& b) {
                                b.post_update_status(*status_ptr);
                            });
                        }
                    } catch (...) {
                    }
                });
            } else {
                broadcast_to_all_bridges(
                    [&status](bridge::INativeBridge& b) { b.post_update_status(status); });
            }
        });
        app_updater_->set_shutdown_request_callback([this]() {
            if (window_ != nullptr) {
                window_->post_ui_task([this]() noexcept {
                    try {
                        shutdown();
                    } catch (...) {
                    }
                });
            } else {
                shutdown();
            }
        });
    }
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
    icon_warmup_thread_ = std::thread([this]() noexcept {
        try {
            warm_executable_icons();
        } catch (...) {
        }
    });
    is_initialized_ = true;
    int wv_len =
        WideCharToMultiByte(CP_UTF8, 0, rt_info.version.data(),
                            static_cast<int>(rt_info.version.size()), nullptr, 0, nullptr, nullptr);
    std::string wv_ver(wv_len > 0 ? static_cast<std::size_t>(wv_len) : 0U, '\0');
    if (wv_len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, rt_info.version.data(),
                            static_cast<int>(rt_info.version.size()), wv_ver.data(), wv_len,
                            nullptr, nullptr);
    }
    write_diagnostic_log(config::LogLevel::Info, "Host ready (webview2=" + wv_ver + ", monitors=" +
                                                     std::to_string(monitors.size()) + ")");
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
    icon_warmup_stop_.store(true);
    if (icon_warmup_thread_.joinable()) {
        icon_warmup_thread_.join();
    }
    if (app_updater_) {
        app_updater_->cancel_and_wait();
    }
    if (collector_companion_) {
        collector_companion_->stop();
    }
    if (traffic_history_store_) {
        static_cast<void>(traffic_history_store_->save_if_dirty());
    }
    static_cast<void>(lifecycle::save_executable_icon_cache_if_dirty());
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

void ProductHost::on_window_position_changed() {
    if (webview_) {
        webview_->notify_parent_window_position_changed();
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
void ProductHost::on_context_menu_traffic_history() {
    on_open_settings("history");
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
    const bool was_dragging = coordinator_->state().is_dragging;
    const auto before = coordinator_->state().geometry;
    coordinator_->on_pointer_move(point);
    if (!was_dragging && coordinator_->state().is_dragging && window_) {
        window_->perform_system_move();
        return;
    }
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
    // The surface clock is the only periodic work on the UI thread, so an unusually long gap means
    // the thread was blocked outside our control (shell, IME/TSF, WebView2 browser, DWM). Recording
    // the gap keeps those external stalls diagnosable while the 250 ms frame cap hides them from
    // the interaction timers.
    if (ui_stall_log_cooldown_.count() > 0) {
        ui_stall_log_cooldown_ -= elapsed;
    }
    if (history_push_cooldown_.count() > 0) {
        history_push_cooldown_ -= elapsed;
    }
    if (window_ != nullptr && ui_stall_log_cooldown_.count() <= 0) {
        const auto gap = window_->last_tick_gap();
        if (gap >= kUiStallLogThreshold) {
            ui_stall_log_cooldown_ = kUiStallLogCooldown;
            write_diagnostic_log(config::LogLevel::Info, "UI thread stalled for " +
                                                             std::to_string(gap.count()) +
                                                             " ms between surface ticks");
        }
    }
    const auto before = coordinator_->state().presentation_state;
    const bool was_topmost = coordinator_->state().is_topmost;
    const bool was_auto_hidden = coordinator_->state().is_auto_hidden;
    const bool was_fullscreen_hidden = coordinator_->state().is_fullscreen_hidden;
    const bool was_click_through = coordinator_->state().is_click_through;
    const bool was_pinned = coordinator_->state().is_pinned;
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
    bool collapse_finished = false;
    if (collapse_bounds_remaining_.count() > 0) {
        if (elapsed >= collapse_bounds_remaining_) {
            collapse_bounds_remaining_ = std::chrono::milliseconds{0};
            collapse_finished = true;
        } else {
            collapse_bounds_remaining_ -= elapsed;
        }
    }
    if (panel_shrink_bounds_remaining_.count() > 0) {
        if (elapsed >= panel_shrink_bounds_remaining_) {
            panel_shrink_bounds_remaining_ = std::chrono::milliseconds{0};
            collapse_finished = true;
        } else {
            panel_shrink_bounds_remaining_ -= elapsed;
        }
    }
    const bool coordinator_state_changed =
        (coordinator_->state().presentation_state != before ||
         coordinator_->state().is_topmost != was_topmost ||
         coordinator_->state().is_auto_hidden != was_auto_hidden ||
         coordinator_->state().is_fullscreen_hidden != was_fullscreen_hidden ||
         coordinator_->state().is_click_through != was_click_through ||
         coordinator_->state().is_pinned != was_pinned || was_iconic);
    if (coordinator_state_changed || collapse_finished) {
        apply_coordinator_state();
        if (coordinator_state_changed) {
            publish_surface_state();
        }
    }
    watchdog_elapsed_ += elapsed;
    if (watchdog_elapsed_ >= std::chrono::milliseconds{1000}) {
        watchdog_elapsed_ = std::chrono::milliseconds{0};
        if (presentation_repair_log_cooldown_.count() > 0) {
            presentation_repair_log_cooldown_ -= std::chrono::milliseconds{1000};
        }
        if (webview_) {
            static_cast<void>(webview_->ensure_alive());
        }
        if (settings_surface_visible_ && settings_webview_) {
            static_cast<void>(settings_webview_->ensure_alive());
        }
        ensure_window_presentation();
    }
    history_persist_elapsed_ += elapsed;
    if (history_persist_elapsed_ >= kTrafficHistoryAutoSaveInterval) {
        history_persist_elapsed_ = std::chrono::milliseconds{0};
        if (traffic_history_store_) {
            static_cast<void>(traffic_history_store_->save_if_dirty());
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
            void on_window_position_changed() override {
                if (host_->settings_webview_) {
                    host_->settings_webview_->notify_parent_window_position_changed();
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
            void on_surface_pointer_down(surface::ScreenPoint point) override {
                drag_origin_ = point;
                drag_armed_ = true;
            }
            void on_surface_pointer_move(surface::ScreenPoint point) override {
                if (!drag_armed_ || !host_->settings_window_) {
                    return;
                }
                const auto threshold = platform::Win32Window::system_drag_threshold();
                const int dx = std::abs(point.x_px - drag_origin_.x_px);
                const int dy = std::abs(point.y_px - drag_origin_.y_px);
                if (dx >= threshold.horizontal_px || dy >= threshold.vertical_px) {
                    drag_armed_ = false;
                    host_->settings_window_->perform_system_move();
                }
            }
            void on_surface_pointer_up(surface::ScreenPoint /*point*/) override {
                drag_armed_ = false;
            }
            void on_surface_pointer_cancel() override {
                drag_armed_ = false;
            }

          private:
            ProductHost* host_;
            surface::ScreenPoint drag_origin_{};
            bool drag_armed_{false};
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
        settings_webview_->set_memory_target_low(!settings_surface_visible_);
        settings_webview_->set_visible(settings_surface_visible_);
        settings_webview_->set_diagnostic_callback(
            [this](std::string_view diag) { write_diagnostic_log(config::LogLevel::Error, diag); });
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
                            if (settings_surface_visible_) {
                                if (settings_webview_) {
                                    settings_webview_->set_memory_target_low(false);
                                    settings_webview_->set_visible(true);
                                }
                            }
                            settings_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
                                .surface = bridge::SurfaceKind::Settings,
                                .pinned = coordinator_ ? coordinator_->state().is_pinned : false,
                                .dock_side = coordinator_ ? coordinator_->state().anchor_side
                                                          : config::DockSide::Right,
                                .auto_hidden = false,
                                .route = settings_active_route_,
                            });
                            settings_bridge_->post_config_changed(current_config_);
                            if (latest_collector_status_.has_value()) {
                                settings_bridge_->post_collector_status(*latest_collector_status_);
                            }
                            if (app_updater_) {
                                settings_bridge_->post_update_status(app_updater_->status());
                            }
                            if (collector_companion_) {
                                settings_bridge_->post_connected_applications(
                                    collector_companion_->connected_applications());
                            }
                            if (traffic_history_store_) {
                                settings_bridge_->post_traffic_history(
                                    traffic_history_store_->export_dto(
                                        current_config_.ranking.excluded_programs));
                            }
                            if (settings_surface_visible_) {
                                settings_window_->show(SW_SHOW);
                                SetForegroundWindow(settings_window_->hwnd());
                            } else if (settings_webview_) {
                                settings_webview_->set_memory_target_low(true);
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
            settings_webview_->set_memory_target_low(false);
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
                .route = settings_active_route_,
            });
            settings_bridge_->post_config_changed(current_config_);
            if (latest_collector_status_.has_value()) {
                settings_bridge_->post_collector_status(*latest_collector_status_);
            }
            if (app_updater_) {
                settings_bridge_->post_update_status(app_updater_->status());
            }
            if (collector_companion_) {
                settings_bridge_->post_connected_applications(
                    collector_companion_->connected_applications());
            }
            if (traffic_history_store_) {
                settings_bridge_->post_traffic_history(
                    traffic_history_store_->export_dto(current_config_.ranking.excluded_programs));
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
        settings_webview_->set_memory_target_low(true);
        settings_webview_->set_visible(false);
    }
    if (settings_window_) {
        settings_window_->hide();
    }
    if (coordinator_) {
        collapse_bounds_remaining_ = std::chrono::milliseconds{0};
        panel_shrink_bounds_remaining_ = std::chrono::milliseconds{0};
        apply_coordinator_state();
        publish_surface_state();
    }
    if (webview_) {
        webview_->set_visible(true);
        static_cast<void>(webview_->ensure_alive());
    }
}

void ProductHost::push_traffic_history_to_settings(bool force) {
    if (!traffic_history_store_ || !settings_bridge_) {
        return;
    }
    if (!force && history_push_cooldown_.count() > 0) {
        return;
    }
    history_push_cooldown_ = kHistoryPushInterval;
    settings_bridge_->post_traffic_history(
        traffic_history_store_->export_dto(current_config_.ranking.excluded_programs));
}

void ProductHost::warm_executable_icons() {
    std::vector<std::string> paths;
    if (traffic_history_store_) {
        paths = traffic_history_store_->collect_executable_paths();
    }
    if (collector_companion_) {
        for (const auto& app : collector_companion_->connected_applications().applications) {
            if (!app.executable_path.empty()) {
                paths.push_back(app.executable_path);
            }
        }
    }
    std::ranges::sort(paths);
    const auto unique_end = std::ranges::unique(paths).begin();
    paths.erase(unique_end, paths.end());

    std::size_t extracted_total = 0;
    std::size_t extracted_since_save = 0;
    for (const auto& path : paths) {
        if (icon_warmup_stop_.load()) {
            return;
        }
        if (!lifecycle::cached_executable_icon_data_uri(path).empty()) {
            continue;
        }
        if (!lifecycle::extract_executable_icon_data_uri(path).empty()) {
            ++extracted_total;
            ++extracted_since_save;
        }
        if (extracted_since_save >= 16U) {
            static_cast<void>(lifecycle::save_executable_icon_cache_if_dirty());
            extracted_since_save = 0;
        }
    }
    static_cast<void>(lifecycle::save_executable_icon_cache_if_dirty());
    if (!icon_warmup_stop_.load()) {
        write_diagnostic_log(config::LogLevel::Info,
                             "Icon cache warmed (" + std::to_string(extracted_total) + " new, " +
                                 std::to_string(lifecycle::executable_icon_cache_size()) +
                                 " cached)");
    }
    if (window_ != nullptr && !icon_warmup_stop_.load()) {
        window_->post_ui_task([this]() noexcept {
            if (!is_shutting_down_ && settings_surface_visible_) {
                push_traffic_history_to_settings(true);
            }
        });
    }
}

void ProductHost::ensure_window_presentation() {
    if (!coordinator_ || !window_ || window_->hwnd() == nullptr) {
        return;
    }
    bool monitors_changed = false;
    if (!coordinator_->state().is_dragging) {
        const auto live_monitors = platform::Win32Window::enumerate_monitors();
        if (live_monitors != coordinator_->monitors()) {
            coordinator_->update_monitors(live_monitors);
            monitors_changed = true;
        }
    }
    const auto& state = coordinator_->state();
    if (state.presentation_state == coordinator::PresentationState::TrayOnly ||
        state.is_fullscreen_hidden) {
        has_applied_window_geometry_ = false;
        return;
    }

    // The coordinator only pushes state on transitions, so an HWND hidden, minimized, moved or
    // de-topmosted by the shell, a fullscreen application, a DPI/display switch or a remote-desktop
    // tool would otherwise stay invisible until the next user interaction with the tray.
    std::string_view reason;
    if (!window_->is_presented()) {
        reason = "window hidden or minimized";
    } else if (state.is_topmost && !window_->is_topmost()) {
        reason = "topmost style lost";
    } else if (monitors_changed) {
        reason = "monitor geometry changed";
    } else if (has_applied_window_geometry_ && !state.is_dragging &&
               collapse_bounds_remaining_.count() == 0 &&
               panel_shrink_bounds_remaining_.count() == 0) {
        RECT actual{};
        const auto& expected = applied_window_geometry_;
        if (window_->get_bounds(&actual) &&
            (actual.left != expected.x_px || actual.top != expected.y_px ||
             (actual.right - actual.left) != expected.width_px ||
             (actual.bottom - actual.top) != expected.height_px)) {
            reason = "window bounds drifted";
        }
    }
    if (reason.empty()) {
        return;
    }

    apply_coordinator_state();
    if (state.is_topmost) {
        window_->set_topmost(true, true);
    }
    publish_surface_state();
    if (presentation_repair_log_cooldown_.count() == 0 ||
        reason != last_presentation_repair_reason_) {
        presentation_repair_log_cooldown_ = kPresentationRepairLogCooldown;
        last_presentation_repair_reason_.assign(reason);
        write_diagnostic_log(config::LogLevel::Info,
                             "Window presentation repaired: " + std::string(reason));
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
    if (state.presentation_state == coordinator::PresentationState::TrayOnly ||
        state.is_fullscreen_hidden) {
        collapse_bounds_remaining_ = std::chrono::milliseconds{0};
        panel_shrink_bounds_remaining_ = std::chrono::milliseconds{0};
        last_applied_presentation_state_ = state.presentation_state;
        has_applied_window_geometry_ = false;
        window_->clear_logical_hit_bounds();
        window_->set_click_through(false);
        if (webview_) {
            webview_->set_memory_target_low(true);
            webview_->set_visible(false);
        }
        window_->hide();
    } else {
        if (webview_) {
            webview_->set_memory_target_low(false);
            webview_->set_visible(true);
        }
        if (last_applied_presentation_state_ == coordinator::PresentationState::PanelVisible &&
            state.presentation_state == coordinator::PresentationState::CapsuleVisible &&
            !state.is_dragging) {
            collapse_bounds_remaining_ = kPanelCollapseBoundsDelay;
            panel_shrink_bounds_remaining_ = std::chrono::milliseconds{0};
        } else if (state.presentation_state != coordinator::PresentationState::CapsuleVisible ||
                   state.is_dragging) {
            collapse_bounds_remaining_ = std::chrono::milliseconds{0};
        }
        if (state.presentation_state != coordinator::PresentationState::PanelVisible ||
            state.is_dragging) {
            panel_shrink_bounds_remaining_ = std::chrono::milliseconds{0};
        }
        last_applied_presentation_state_ = state.presentation_state;
        window_->set_logical_hit_bounds(state.geometry.x_px, state.geometry.y_px,
                                        state.geometry.width_px, state.geometry.height_px);
        // Keep the floating HWND itself frameless & un-rounded so WebView2 renders
        // the morphing transition between capsule and panel without a rigid DWM border box.
        window_->configure_surface_chrome(false, dark_mode);
        int window_x = state.geometry.x_px;
        const int window_y = state.geometry.y_px;
        int window_w = state.geometry.width_px;
        const int window_h = state.geometry.height_px;
        if (state.presentation_state == coordinator::PresentationState::CapsuleVisible &&
            !state.is_dragging) {
            if (const auto* mon = coordinator_->active_monitor(); mon != nullptr) {
                const auto canvas_w = static_cast<int>(std::round(
                    coordinator::DockCapsuleCoordinator::kDefaultPanelWidthDip * mon->dpi_scale()));
                if (state.anchor_side == config::DockSide::Right) {
                    window_x = (state.geometry.x_px + state.geometry.width_px) - canvas_w;
                }
                window_w = canvas_w;
            }
        }
        const bool is_collapsing =
            (state.presentation_state == coordinator::PresentationState::CapsuleVisible &&
             collapse_bounds_remaining_.count() > 0 && !state.is_dragging);
        const bool is_shrinking_panel =
            (state.presentation_state == coordinator::PresentationState::PanelVisible &&
             panel_shrink_bounds_remaining_.count() > 0 && !state.is_dragging);
        if (!is_collapsing && !is_shrinking_panel) {
            window_->set_bounds(window_x, window_y, window_w, window_h);
            if (webview_) {
                webview_->resize(window_w, window_h);
            }
            applied_window_geometry_ = surface::WindowGeometry{
                .x_px = window_x,
                .y_px = window_y,
                .width_px = window_w,
                .height_px = window_h,
            };
            has_applied_window_geometry_ = true;
        }
        window_->set_click_through(state.is_click_through);
        window_->set_topmost(state.is_topmost);
        window_->show(SW_SHOWNOACTIVATE);
    }
    if (state.anchor_updated || state.pin_updated) {
        if (state.anchor_updated) {
            current_config_.floating.anchor_side = state.anchor_side;
            current_config_.floating.anchor_vertical_offset_dip = state.anchor_vertical_offset_dip;
            if (!state.anchor_monitor_id.empty()) {
                strncpy_s(current_config_.floating.anchor_monitor_id.data(),
                          current_config_.floating.anchor_monitor_id.size(),
                          state.anchor_monitor_id.c_str(), _TRUNCATE);
            }
        }
        if (state.pin_updated) {
            current_config_.floating.nfm_pinned = state.is_pinned;
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
    const bool was_panel = (last_published_surface_ == bridge::SurfaceKind::Panel);
    last_published_surface_ = surface;
    native_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
        .surface = surface,
        .pinned = coordinator_->state().is_pinned,
        .dock_side = coordinator_->state().anchor_side,
        .auto_hidden = coordinator_->state().is_auto_hidden,
    });
    if (!was_panel && surface == bridge::SurfaceKind::Panel &&
        latest_ranking_snapshot_.has_value()) {
        native_bridge_->post_ranking_snapshot(*latest_ranking_snapshot_);
    }
}

void ProductHost::on_show_monitor() {
    if (coordinator_) {
        collapse_bounds_remaining_ = std::chrono::milliseconds{0};
        panel_shrink_bounds_remaining_ = std::chrono::milliseconds{0};
        coordinator_->show_monitor();
        apply_coordinator_state();
        if (window_ && window_->hwnd() != nullptr) {
            SetForegroundWindow(window_->hwnd());
        }
        if (webview_) {
            webview_->set_visible(true);
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
        const auto& after = coordinator_->state().geometry;
        if (after != before) {
            if (coordinator_->state().presentation_state ==
                    coordinator::PresentationState::PanelVisible &&
                !coordinator_->state().is_dragging && after.height_px < before.height_px &&
                after.x_px == before.x_px && after.y_px == before.y_px &&
                after.width_px == before.width_px) {
                panel_shrink_bounds_remaining_ = kPanelShrinkBoundsDelay;
            } else {
                panel_shrink_bounds_remaining_ = std::chrono::milliseconds{0};
            }
            apply_coordinator_state();
        }
    }
}

void ProductHost::on_resize_settings_window(int height_dip) {
    const int clamped_dip = std::clamp(height_dip, 140, 680);
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
void ProductHost::on_open_settings(std::string_view route) {
    if (!route.empty()) {
        settings_active_route_ = std::string{route};
    } else if (settings_active_route_.empty()) {
        settings_active_route_ = "general";
    }
    settings_surface_visible_ = true;
    if (window_ && window_->hwnd() != nullptr) {
        ensure_settings_window();
    } else if (native_bridge_ && coordinator_) {
        native_bridge_->post_surface_changed(bridge::SurfaceChangedDto{
            .surface = bridge::SurfaceKind::Settings,
            .pinned = coordinator_->state().is_pinned,
            .dock_side = coordinator_->state().anchor_side,
            .auto_hidden = coordinator_->state().is_auto_hidden,
            .route = settings_active_route_,
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
            .route = settings_active_route_,
        });
        if (!settings_surface_visible_) {
            settings_bridge_->post_config_changed(current_config_);
            if (latest_collector_status_.has_value()) {
                settings_bridge_->post_collector_status(*latest_collector_status_);
            }
            if (app_updater_) {
                settings_bridge_->post_update_status(app_updater_->status());
            }
            if (collector_companion_) {
                settings_bridge_->post_connected_applications(
                    collector_companion_->connected_applications());
            }
            if (traffic_history_store_) {
                settings_bridge_->post_traffic_history(
                    traffic_history_store_->export_dto(current_config_.ranking.excluded_programs));
            }
        }
    }
    broadcast_to_all_bridges([this](bridge::INativeBridge& b) {
        b.post_config_changed(current_config_);
        if (latest_collector_status_.has_value()) {
            b.post_collector_status(*latest_collector_status_);
        }
        if (app_updater_) {
            b.post_update_status(app_updater_->status());
        }
        if (collector_companion_) {
            b.post_connected_applications(collector_companion_->connected_applications());
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
            const bool desired_startup = yyjson_get_bool(val);
            const auto exe_path = platform::StartupRegistration::current_executable_path();
            const auto sync_res = platform::StartupRegistration::sync_login_startup(
                desired_startup, exe_path, options_.startup_options);
            if (!sync_res.success) {
                err_msg = sync_res.diagnostic_summary;
                write_diagnostic_log(config::LogLevel::Error, "Failed to update login startup: " +
                                                                  sync_res.diagnostic_summary);
            } else {
                current_config_.startup.ui_at_login = desired_startup;
                updated = true;
                write_diagnostic_log(config::LogLevel::Info, sync_res.diagnostic_summary);
            }
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
    } else if (key == "floating.show_capsule_border" || key == "capsule.showCapsuleBorder" ||
               key == "showCapsuleBorder") {
        if (yyjson_is_bool(val)) {
            current_config_.floating.show_capsule_border = yyjson_get_bool(val);
            updated = true;
        } else {
            err_msg = "Expected boolean for show_capsule_border";
        }
    } else if (key == "floating.ignore_hover_when_fullscreen" ||
               key == "capsule.ignoreHoverWhenFullscreen" ||
               key == "monitor.ignoreHoverWhenFullscreen" || key == "ignoreHoverWhenFullscreen") {
        if (yyjson_is_bool(val)) {
            current_config_.floating.ignore_hover_when_fullscreen = yyjson_get_bool(val);
            updated = true;
        } else {
            err_msg = "Expected boolean for ignore_hover_when_fullscreen";
        }
    } else if (key == "floating.hide_capsule_when_fullscreen" ||
               key == "capsule.hideCapsuleWhenFullscreen" ||
               key == "monitor.hideCapsuleWhenFullscreen" || key == "hideCapsuleWhenFullscreen") {
        if (yyjson_is_bool(val)) {
            current_config_.floating.hide_capsule_when_fullscreen = yyjson_get_bool(val);
            updated = true;
        } else {
            err_msg = "Expected boolean for hide_capsule_when_fullscreen";
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
    } else if (key == "ranking.excluded_programs" || key == "ranking.excludedPrograms" ||
               key == "excludedPrograms") {
        if (yyjson_is_arr(val)) {
            const auto count = yyjson_arr_size(val);
            if (count > config::kMaximumExcludedPrograms) {
                err_msg = "excluded_programs exceeds maximum count";
            } else {
                std::vector<std::string> next_list;
                next_list.reserve(count);
                bool valid_items = true;
                for (std::size_t idx = 0U; idx < count; ++idx) {
                    auto* item = yyjson_arr_get(val, idx);
                    if (!yyjson_is_str(item)) {
                        err_msg = "Expected string items in excluded_programs";
                        valid_items = false;
                        break;
                    }
                    std::string_view item_str{yyjson_get_str(item), yyjson_get_len(item)};
                    while (!item_str.empty() &&
                           (item_str.front() == ' ' || item_str.front() == '\t')) {
                        item_str.remove_prefix(1U);
                    }
                    while (!item_str.empty() &&
                           (item_str.back() == ' ' || item_str.back() == '\t')) {
                        item_str.remove_suffix(1U);
                    }
                    if (item_str.empty()) {
                        continue;
                    }
                    if (item_str.size() > config::kMaximumExcludedProgramBytes) {
                        err_msg = "Excluded program item exceeds maximum length";
                        valid_items = false;
                        break;
                    }
                    const std::string entry{item_str};
                    const bool duplicate =
                        std::ranges::any_of(next_list, [&](const std::string& existing) {
                            return lifecycle::is_application_excluded(
                                existing, existing, existing, std::vector<std::string>{entry});
                        });
                    if (!duplicate) {
                        next_list.push_back(entry);
                    }
                }
                if (valid_items) {
                    current_config_.ranking.excluded_programs = std::move(next_list);
                    if (collector_companion_) {
                        collector_companion_->set_excluded_programs(
                            current_config_.ranking.excluded_programs);
                    }
                    if (latest_ranking_snapshot_.has_value()) {
                        auto& snap = *latest_ranking_snapshot_;
                        std::uint64_t excl_dl = 0U;
                        std::uint64_t excl_ul = 0U;
                        std::vector<bridge::RankingAppDto> kept;
                        kept.reserve(snap.applications.size());
                        for (const auto& app : snap.applications) {
                            if (lifecycle::is_application_excluded(
                                    app.id, app.executable_path, app.display_name,
                                    current_config_.ranking.excluded_programs)) {
                                excl_dl += app.download_bytes_per_sec;
                                excl_ul += app.upload_bytes_per_sec;
                            } else {
                                kept.push_back(app);
                            }
                        }
                        snap.total_download_bytes_per_sec =
                            snap.total_download_bytes_per_sec > excl_dl
                                ? snap.total_download_bytes_per_sec - excl_dl
                                : 0U;
                        snap.total_upload_bytes_per_sec =
                            snap.total_upload_bytes_per_sec > excl_ul
                                ? snap.total_upload_bytes_per_sec - excl_ul
                                : 0U;
                        snap.applications = std::move(kept);
                        if (native_bridge_ && coordinator_ &&
                            coordinator_->state().presentation_state ==
                                coordinator::PresentationState::PanelVisible) {
                            native_bridge_->post_ranking_snapshot(snap);
                        }
                    }
                    updated = true;
                    if (traffic_history_store_ && settings_bridge_) {
                        settings_bridge_->post_traffic_history(traffic_history_store_->export_dto(
                            current_config_.ranking.excluded_programs));
                    }
                }
            }
        } else {
            err_msg = "Expected array of strings for excluded_programs";
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
    const auto save_res = config::SaveConfigFile(options_.config_file_path, current_config_,
                                                 config::ConfigDocumentKind::User);
    if (!save_res.saved && save_res.error.has_value()) {
        write_diagnostic_log(config::LogLevel::Error,
                             "Failed to persist configuration: " + save_res.error->detail +
                                 " (win32_error=" + std::to_string(save_res.error->system_error) +
                                 ")");
    }

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
            settings_window_->begin_interactive_drag();
            if (settings_listener_) {
                settings_listener_->on_surface_pointer_down(
                    surface::ScreenPoint{.x_px = pt.x, .y_px = pt.y});
            }
            return;
        }
    }
    if (coordinator_) {
        if (collapse_bounds_remaining_.count() > 0) {
            collapse_bounds_remaining_ = std::chrono::milliseconds{0};
            apply_coordinator_state();
        }
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

std::filesystem::path ProductHost::log_directory_path() const {
    return logger_.log_directory();
}

void ProductHost::write_diagnostic_log(config::LogLevel level,
                                       std::string_view message) const noexcept {
    logger_.write(current_config_.diagnostics.log_level, level, message);
}

void ProductHost::on_open_log_directory() {
    const std::filesystem::path log_dir = log_directory_path();
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    const std::filesystem::path target_dir =
        std::filesystem::exists(log_dir, ec) ? log_dir : log_dir.parent_path();
    static_cast<void>(
        ShellExecuteW(nullptr, L"open", target_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
}

void ProductHost::on_check_for_update() {
    if (app_updater_) {
        app_updater_->check_for_updates_async();
    }
}

void ProductHost::on_download_and_apply_update() {
    if (app_updater_) {
        app_updater_->download_and_apply_async();
    }
}
void ProductHost::on_request_connected_applications() {
    if (!collector_companion_) {
        return;
    }
    const auto dto = collector_companion_->connected_applications();
    broadcast_to_all_bridges(
        [&dto](bridge::INativeBridge& b) { b.post_connected_applications(dto); });
}
void ProductHost::on_request_traffic_history() {
    if (!traffic_history_store_) {
        return;
    }
    const auto dto = traffic_history_store_->export_dto(current_config_.ranking.excluded_programs);
    broadcast_to_all_bridges([&dto](bridge::INativeBridge& b) { b.post_traffic_history(dto); });
}

void ProductHost::on_browse_excluded_program() {
    IFileOpenDialog* dialog = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        return;
    }
    DWORD flags = 0;
    if (SUCCEEDED(dialog->GetOptions(&flags))) {
        static_cast<void>(dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                             FOS_PATHMUSTEXIST));
    }
    const bool is_zh =
        (current_config_.appearance.language == config::LanguageMode::SimplifiedChinese);
    const COMDLG_FILTERSPEC filter_specs[] = {
        {is_zh ? L"可执行程序 (*.exe)" : L"Executable Files (*.exe)", L"*.exe"},
        {is_zh ? L"所有文件 (*.*)" : L"All Files (*.*)", L"*.*"},
    };
    static_cast<void>(dialog->SetFileTypes(2U, filter_specs));
    static_cast<void>(dialog->SetFileTypeIndex(1U));
    static_cast<void>(dialog->SetTitle(is_zh ? L"选择要排除的程序" : L"Select Program to Exclude"));

    HWND owner_hwnd = nullptr;
    if (settings_window_ && settings_window_->hwnd() != nullptr &&
        IsWindowVisible(settings_window_->hwnd()) != FALSE) {
        owner_hwnd = settings_window_->hwnd();
    } else if (window_) {
        owner_hwnd = window_->hwnd();
    }

    if (SUCCEEDED(dialog->Show(owner_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR file_path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &file_path)) &&
                file_path != nullptr) {
                const std::filesystem::path chosen_path(file_path);
                CoTaskMemFree(file_path);
                const auto u8_name = chosen_path.filename().u8string();
                std::string exe_name(reinterpret_cast<const char*>(u8_name.data()), u8_name.size());
                if (!exe_name.empty() && current_config_.ranking.excluded_programs.size() <
                                             config::kMaximumExcludedPrograms) {
                    const bool already = std::ranges::any_of(
                        current_config_.ranking.excluded_programs,
                        [&](const std::string& existing) {
                            return lifecycle::is_application_excluded(
                                existing, existing, existing, std::vector<std::string>{exe_name});
                        });
                    if (!already) {
                        current_config_.ranking.excluded_programs.push_back(exe_name);
                        if (collector_companion_) {
                            collector_companion_->set_excluded_programs(
                                current_config_.ranking.excluded_programs);
                        }
                        static_cast<void>(config::SaveConfigFile(options_.config_file_path,
                                                                 current_config_,
                                                                 config::ConfigDocumentKind::User));
                        broadcast_to_all_bridges([this](bridge::INativeBridge& b) {
                            b.post_config_changed(current_config_);
                        });
                        if (traffic_history_store_) {
                            const auto hist_dto = traffic_history_store_->export_dto(
                                current_config_.ranking.excluded_programs);
                            broadcast_to_all_bridges([&hist_dto](bridge::INativeBridge& b) {
                                b.post_traffic_history(hist_dto);
                            });
                        }
                    }
                }
            }
            item->Release();
        }
    }
    dialog->Release();
}
} // namespace nfm::host::app
