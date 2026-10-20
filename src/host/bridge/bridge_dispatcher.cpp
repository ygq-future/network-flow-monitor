#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"

#include <array>
#include <span>
#include <utility>
#include <yyjson.h>

namespace nfm::host::bridge {

namespace {

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

std::string write_mut_doc(yyjson_mut_doc* doc) {
    if (doc == nullptr) {
        return {};
    }
    size_t len = 0;
    char* json_str = yyjson_mut_write(doc, 0, &len);
    if (json_str == nullptr) {
        return {};
    }
    std::string result(json_str, len);
    free(json_str); // NOLINT(cppcoreguidelines-no-malloc)
    return result;
}

const char* to_speed_unit_str(config::SpeedUnit unit) noexcept {
    switch (unit) {
    case config::SpeedUnit::Auto:
        return "auto";
    case config::SpeedUnit::Bytes:
        return "bytes";
    case config::SpeedUnit::Bits:
        return "bits";
    }
    return "auto";
}

const char* to_language_str(config::LanguageMode lang) noexcept {
    switch (lang) {
    case config::LanguageMode::System:
        return "system";
    case config::LanguageMode::English:
        return "english";
    case config::LanguageMode::SimplifiedChinese:
        return "simplified_chinese";
    }
    return "system";
}

const char* to_theme_str(config::ThemeMode theme) noexcept {
    switch (theme) {
    case config::ThemeMode::System:
        return "system";
    case config::ThemeMode::Light:
        return "light";
    case config::ThemeMode::Dark:
        return "dark";
    }
    return "system";
}

const char* to_dock_side_str(config::DockSide side) noexcept {
    switch (side) {
    case config::DockSide::Left:
        return "left";
    case config::DockSide::Right:
        return "right";
    }
    return "right";
}

const char* to_log_level_str(config::LogLevel level) noexcept {
    switch (level) {
    case config::LogLevel::Off:
        return "off";
    case config::LogLevel::Error:
        return "error";
    case config::LogLevel::Info:
        return "info";
    case config::LogLevel::Debug:
        return "debug";
    }
    return "info";
}

} // namespace

BridgeDispatcher::BridgeDispatcher(std::shared_ptr<IBridgeCommandHandler> handler)
    : handler_(std::move(handler)) {}

bool BridgeDispatcher::parse_web_command(std::string_view raw_json, WebCommand* command_out,
                                         BridgeErrorDto* error_out) {
    if (raw_json.size() > kMaxBridgePayloadBytes) {
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::PayloadTooLarge);
            error_out->message = "Payload exceeds maximum allowed size of 256 KiB";
        }
        return false;
    }

    ScopedJsonDoc doc(raw_json);
    if (doc.get() == nullptr) {
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::InvalidJson);
            error_out->message = "Malformed JSON syntax";
        }
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(doc.get());
    std::string_view unescaped_json;
    if (root != nullptr && yyjson_is_str(root)) {
        unescaped_json = std::string_view{yyjson_get_str(root), yyjson_get_len(root)};
    }
    ScopedJsonDoc inner_doc(unescaped_json);
    if (!unescaped_json.empty() && inner_doc.get() != nullptr) {
        root = yyjson_doc_get_root(inner_doc.get());
    }

    if (root == nullptr || !yyjson_is_obj(root)) {
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::InvalidJson);
            error_out->message = "Expected JSON object at root";
        }
        return false;
    }

    // Check version
    yyjson_val* version_val = yyjson_obj_get(root, "version");
    std::uint32_t version = kCurrentBridgeVersion;
    if (version_val != nullptr) {
        if (!yyjson_is_uint(version_val)) {
            if (error_out != nullptr) {
                error_out->code = to_error_code_string(BridgeErrorCode::InvalidVersion);
                error_out->message = "'version' must be an unsigned integer";
            }
            return false;
        }
        version = static_cast<std::uint32_t>(yyjson_get_uint(version_val));
        if (version != kCurrentBridgeVersion) {
            if (error_out != nullptr) {
                error_out->code = to_error_code_string(BridgeErrorCode::InvalidVersion);
                error_out->message = "Unsupported bridge protocol version";
            }
            return false;
        }
    }

    // Check command type
    yyjson_val* type_val = yyjson_obj_get(root, "type");
    if (type_val == nullptr || !yyjson_is_str(type_val)) {
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::MissingField);
            error_out->message = "Missing or invalid string field 'type'";
        }
        return false;
    }

    std::string_view type_str = yyjson_get_str(type_val);
    NativeEventType native_type = parse_native_event_type(type_str);
    if (native_type != NativeEventType::Unknown) {
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::InvalidDirection);
            error_out->message = "Native event types cannot be sent from Web to Native";
        }
        return false;
    }

    WebCommandType cmd_type = parse_web_command_type(type_str);
    if (cmd_type == WebCommandType::Unknown) {
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::UnknownCommand);
            error_out->message = "Unknown web command type: " + std::string(type_str);
        }
        return false;
    }

    if (command_out != nullptr) {
        command_out->type = cmd_type;
        command_out->version = version;

        yyjson_val* id_val = yyjson_obj_get(root, "commandId");
        if (id_val != nullptr && yyjson_is_str(id_val)) {
            command_out->command_id = yyjson_get_str(id_val);
        }

        yyjson_val* payload_val = yyjson_obj_get(root, "payload");
        if (payload_val != nullptr && yyjson_is_obj(payload_val)) {
            switch (cmd_type) {
            case WebCommandType::SetPin: {
                yyjson_val* pin_val = yyjson_obj_get(payload_val, "pinned");
                if (pin_val != nullptr && yyjson_is_bool(pin_val)) {
                    command_out->pin_state = yyjson_get_bool(pin_val);
                }
                break;
            }
            case WebCommandType::OpenSettings: {
                yyjson_val* route_val = yyjson_obj_get(payload_val, "route");
                if (route_val != nullptr && yyjson_is_str(route_val)) {
                    command_out->route = yyjson_get_str(route_val);
                }
                break;
            }
            case WebCommandType::UpdateSetting: {
                yyjson_val* key_val = yyjson_obj_get(payload_val, "key");
                if (key_val != nullptr && yyjson_is_str(key_val)) {
                    command_out->setting_key = yyjson_get_str(key_val);
                }
                yyjson_val* val_val = yyjson_obj_get(payload_val, "value");
                if (val_val != nullptr) {
                    size_t val_len = 0;
                    char* val_str = yyjson_val_write(val_val, 0, &val_len);
                    if (val_str != nullptr) {
                        command_out->setting_value_json = std::string(val_str, val_len);
                        free(val_str); // NOLINT(cppcoreguidelines-no-malloc)
                    }
                }
                break;
            }
            case WebCommandType::SetTheme: {
                yyjson_val* theme_val = yyjson_obj_get(payload_val, "theme");
                if (theme_val != nullptr && yyjson_is_str(theme_val)) {
                    command_out->theme = yyjson_get_str(theme_val);
                }
                break;
            }
            case WebCommandType::SetLocale: {
                yyjson_val* loc_val = yyjson_obj_get(payload_val, "locale");
                if (loc_val != nullptr && yyjson_is_str(loc_val)) {
                    command_out->locale = yyjson_get_str(loc_val);
                }
                break;
            }
            case WebCommandType::OpenContextMenu: {
                yyjson_val* x_val = yyjson_obj_get(payload_val, "x");
                if (x_val != nullptr && yyjson_is_int(x_val)) {
                    command_out->screen_x = static_cast<int>(yyjson_get_sint(x_val));
                }
                yyjson_val* y_val = yyjson_obj_get(payload_val, "y");
                if (y_val != nullptr && yyjson_is_int(y_val)) {
                    command_out->screen_y = static_cast<int>(yyjson_get_sint(y_val));
                }
                break;
            }
            case WebCommandType::ResizePanel:
            case WebCommandType::ResizeSettingsWindow: {
                yyjson_val* h_val = yyjson_obj_get(payload_val, "heightDip");
                if (h_val != nullptr && yyjson_is_int(h_val)) {
                    command_out->panel_height_dip = static_cast<int>(yyjson_get_sint(h_val));
                }
                break;
            }
            default:
                break;
            }
        }
    }

    return true;
}

bool BridgeDispatcher::dispatch_web_message(std::string_view raw_json, BridgeErrorDto* error_out,
                                            CommandResponseDto* response_out) {
    WebCommand cmd;
    if (!parse_web_command(raw_json, &cmd, error_out)) {
        return false;
    }

    if (handler_ == nullptr) {
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::ExecutionFailed);
            error_out->message = "No command handler registered";
        }
        return false;
    }

    if (response_out != nullptr) {
        response_out->command_id = cmd.command_id;
        response_out->success = true;
    }

    switch (cmd.type) {
    case WebCommandType::ShowMonitor:
        handler_->on_show_monitor();
        break;
    case WebCommandType::HideMonitor:
        handler_->on_hide_monitor();
        break;
    case WebCommandType::SetPin:
        handler_->on_set_pin(cmd.pin_state.value_or(false));
        break;
    case WebCommandType::OpenSettings:
        handler_->on_open_settings(cmd.route.value_or(""));
        break;
    case WebCommandType::RequestConfig:
        handler_->on_request_config();
        break;
    case WebCommandType::UpdateSetting:
        handler_->on_update_setting(cmd.setting_key.value_or(""),
                                    cmd.setting_value_json.value_or(""));
        break;
    case WebCommandType::RetryCollector:
        handler_->on_retry_collector();
        break;
    case WebCommandType::RestartCollector:
        handler_->on_restart_collector();
        break;
    case WebCommandType::MinimizeWindow:
        handler_->on_minimize_window();
        break;
    case WebCommandType::ToggleMaximizeWindow:
        handler_->on_toggle_maximize();
        break;
    case WebCommandType::StartWindowDrag:
        handler_->on_start_window_drag();
        break;
    case WebCommandType::ExitApp:
        handler_->on_exit_app();
        break;
    case WebCommandType::SetTheme:
        handler_->on_set_theme(cmd.theme.value_or("System"));
        break;
    case WebCommandType::SetLocale:
        handler_->on_set_locale(cmd.locale.value_or("System"));
        break;
    case WebCommandType::OpenContextMenu:
        handler_->on_open_context_menu(cmd.screen_x.value_or(-1), cmd.screen_y.value_or(-1));
        break;
    case WebCommandType::ResizePanel:
        if (cmd.panel_height_dip.has_value()) {
            handler_->on_resize_panel(*cmd.panel_height_dip);
        }
        break;
    case WebCommandType::ResizeSettingsWindow:
        if (cmd.panel_height_dip.has_value()) {
            handler_->on_resize_settings_window(*cmd.panel_height_dip);
        }
        break;
    case WebCommandType::OpenLogDirectory:
        handler_->on_open_log_directory();
        break;
    case WebCommandType::CheckForUpdate:
        handler_->on_check_for_update();
        break;
    case WebCommandType::DownloadAndApplyUpdate:
        handler_->on_download_and_apply_update();
        break;
    case WebCommandType::RequestConnectedApplications:
        handler_->on_request_connected_applications();
        break;
    case WebCommandType::BrowseExcludedProgram:
        handler_->on_browse_excluded_program();
        break;
    case WebCommandType::RequestTrafficHistory:
        handler_->on_request_traffic_history();
        break;
    default:
        if (error_out != nullptr) {
            error_out->code = to_error_code_string(BridgeErrorCode::UnknownCommand);
            error_out->message = "Unhandled command type";
        }
        return false;
    }

    return true;
}

std::string BridgeDispatcher::serialize_traffic_tick(const TrafficTickDto& tick) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "trafficTick");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_uint(doc.get(), payload, "seq", tick.seq);
    yyjson_mut_obj_add_uint(doc.get(), payload, "monotonicMs", tick.monotonic_ms);
    yyjson_mut_obj_add_uint(doc.get(), payload, "downloadBytesPerSec", tick.download_bytes_per_sec);
    yyjson_mut_obj_add_uint(doc.get(), payload, "uploadBytesPerSec", tick.upload_bytes_per_sec);

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_ranking_snapshot(const RankingSnapshotDto& snapshot) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "rankingSnapshot");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_uint(doc.get(), payload, "seq", snapshot.seq);
    yyjson_mut_obj_add_uint(doc.get(), payload, "monotonicMs", snapshot.monotonic_ms);
    yyjson_mut_obj_add_str(doc.get(), payload, "status", snapshot.status.c_str());
    yyjson_mut_obj_add_uint(doc.get(), payload, "totalDownloadBytesPerSec",
                            snapshot.total_download_bytes_per_sec);
    yyjson_mut_obj_add_uint(doc.get(), payload, "totalUploadBytesPerSec",
                            snapshot.total_upload_bytes_per_sec);

    yyjson_mut_val* apps = yyjson_mut_arr(doc.get());
    yyjson_mut_obj_add_val(doc.get(), payload, "applications", apps);
    for (const auto& app : snapshot.applications) {
        yyjson_mut_val* item = yyjson_mut_obj(doc.get());
        yyjson_mut_obj_add_str(doc.get(), item, "id", app.id.c_str());
        yyjson_mut_obj_add_str(doc.get(), item, "executablePath", app.executable_path.c_str());
        yyjson_mut_obj_add_str(doc.get(), item, "displayName", app.display_name.c_str());
        yyjson_mut_obj_add_str(doc.get(), item, "iconKey", app.icon_key.c_str());
        yyjson_mut_obj_add_uint(doc.get(), item, "downloadBytesPerSec", app.download_bytes_per_sec);
        yyjson_mut_obj_add_uint(doc.get(), item, "uploadBytesPerSec", app.upload_bytes_per_sec);
        yyjson_mut_arr_append(apps, item);
    }

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_collector_status(const CollectorStatusDto& status) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "collectorStatus");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_uint(doc.get(), payload, "monotonicMs", status.monotonic_ms);
    yyjson_mut_obj_add_bool(doc.get(), payload, "etwSessionActive", status.etw_session_active);
    yyjson_mut_obj_add_uint(doc.get(), payload, "droppedPackets", status.dropped_packets);
    yyjson_mut_obj_add_uint(doc.get(), payload, "activePids", status.active_pids);
    yyjson_mut_obj_add_str(doc.get(), payload, "state", status.state.c_str());
    yyjson_mut_obj_add_uint(doc.get(), payload, "processId", status.process_id);
    yyjson_mut_obj_add_str(doc.get(), payload, "instanceId", status.instance_id.c_str());
    yyjson_mut_obj_add_bool(doc.get(), payload, "isElevated", status.is_elevated);
    if (!status.last_error.empty()) {
        yyjson_mut_obj_add_str(doc.get(), payload, "lastError", status.last_error.c_str());
    }

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_session_reset(const SessionResetDto& reset) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "sessionReset");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_str(doc.get(), payload, "collectorInstanceId",
                           reset.collector_instance_id.c_str());
    yyjson_mut_obj_add_uint(doc.get(), payload, "collectorPid", reset.collector_pid);

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_theme_changed(const ThemeChangedDto& theme) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "themeChanged");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_str(doc.get(), payload, "theme", theme.theme.c_str());

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_locale_changed(const LocaleChangedDto& locale) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "localeChanged");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_str(doc.get(), payload, "locale", locale.locale.c_str());

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_config_changed(const config::AppConfig& config) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "configChanged");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    // 1. config snapshot
    yyjson_mut_val* cfg = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), payload, "config", cfg);

    // general
    yyjson_mut_val* gen = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), cfg, "general", gen);
    yyjson_mut_obj_add_bool(doc.get(), gen, "uiAtLogin", config.startup.ui_at_login);
    yyjson_mut_obj_add_str(doc.get(), gen, "speedUnit",
                           to_speed_unit_str(config.display.speed_unit));

    // appearance
    yyjson_mut_val* app = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), cfg, "appearance", app);
    yyjson_mut_obj_add_str(doc.get(), app, "theme", to_theme_str(config.appearance.theme));
    yyjson_mut_obj_add_str(doc.get(), app, "language", to_language_str(config.appearance.language));

    // capsule
    yyjson_mut_val* cap = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), cfg, "capsule", cap);
    yyjson_mut_obj_add_sint(doc.get(), cap, "widthDip", config.floating.capsule_width_dip);
    yyjson_mut_obj_add_sint(doc.get(), cap, "heightDip", config.floating.capsule_height_dip);
    yyjson_mut_obj_add_sint(doc.get(), cap, "endRoundingDip",
                            config.floating.capsule_end_rounding_dip);
    yyjson_mut_obj_add_sint(doc.get(), cap, "effectiveEndRoundingDip",
                            config::EffectiveCapsuleEndRoundingDip(config.floating));
    yyjson_mut_obj_add_sint(doc.get(), cap, "hoverDelayMs", config.interaction.hover_delay.count());
    yyjson_mut_obj_add_str(doc.get(), cap, "anchorSide",
                           to_dock_side_str(config.floating.anchor_side));
    yyjson_mut_obj_add_str(doc.get(), cap, "anchorMonitorId",
                           config.floating.anchor_monitor_id.data());
    yyjson_mut_obj_add_sint(doc.get(), cap, "anchorVerticalOffsetDip",
                            config.floating.anchor_vertical_offset_dip);
    yyjson_mut_obj_add_bool(doc.get(), cap, "autoHideEnabled", config.floating.auto_hide_enabled);
    yyjson_mut_obj_add_sint(doc.get(), cap, "autoHideDelayMs", 500);
    yyjson_mut_obj_add_str(doc.get(), cap, "downloadColorHex",
                           config.floating.download_color_hex.data());
    yyjson_mut_obj_add_str(doc.get(), cap, "uploadColorHex",
                           config.floating.upload_color_hex.data());
    yyjson_mut_obj_add_bool(doc.get(), cap, "showCapsuleBorder",
                            config.floating.show_capsule_border);
    yyjson_mut_obj_add_bool(doc.get(), cap, "ignoreHoverWhenFullscreen",
                            config.floating.ignore_hover_when_fullscreen);
    yyjson_mut_obj_add_bool(doc.get(), cap, "hideCapsuleWhenFullscreen",
                            config.floating.hide_capsule_when_fullscreen);
    yyjson_mut_val* mon = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), cfg, "monitor", mon);
    yyjson_mut_obj_add_bool(doc.get(), mon, "pinned", config.floating.nfm_pinned);
    yyjson_mut_obj_add_sint(doc.get(), mon, "dismissDelayMs",
                            config.interaction.dismiss_delay.count());
    yyjson_mut_obj_add_uint(doc.get(), mon, "opacityPercent", config.floating.opacity_percent);
    yyjson_mut_obj_add_uint(doc.get(), mon, "blurStrengthPercent",
                            config.floating.blur_strength_percent);

    yyjson_mut_val* rnk = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), cfg, "ranking", rnk);
    yyjson_mut_obj_add_uint(doc.get(), rnk, "topN", config.ranking.top_n);
    yyjson_mut_obj_add_sint(doc.get(), rnk, "windowMs", config.ranking.window.count());
    yyjson_mut_obj_add_sint(doc.get(), rnk, "refreshIntervalMs",
                            config.display.refresh_interval.count());

    yyjson_mut_val* excluded_arr = yyjson_mut_arr(doc.get());
    yyjson_mut_obj_add_val(doc.get(), rnk, "excludedPrograms", excluded_arr);
    for (const auto& prog : config.ranking.excluded_programs) {
        yyjson_mut_arr_add_str(doc.get(), excluded_arr, prog.c_str());
    }
    // diagnostics
    yyjson_mut_val* diag = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), cfg, "diagnostics", diag);
    yyjson_mut_obj_add_str(doc.get(), diag, "logLevel",
                           to_log_level_str(config.diagnostics.log_level));

    // 2. schema projection
    yyjson_mut_val* schema = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), payload, "schema", schema);

    auto add_range = [&](const char* key, int min_val, int max_val, int step, const char* unit,
                         int def_val) {
        yyjson_mut_val* item = yyjson_mut_obj(doc.get());
        yyjson_mut_obj_add_val(doc.get(), schema, key, item);
        yyjson_mut_obj_add_int(doc.get(), item, "min", min_val);
        yyjson_mut_obj_add_int(doc.get(), item, "max", max_val);
        yyjson_mut_obj_add_int(doc.get(), item, "step", step);
        yyjson_mut_obj_add_str(doc.get(), item, "unit", unit);
        yyjson_mut_obj_add_int(doc.get(), item, "default", def_val);
    };

    add_range("capsuleWidth", config::kMinimumCapsuleWidthDip, config::kMaximumCapsuleWidthDip, 1,
              "DIP", 10);
    add_range("capsuleHeight", config::kMinimumCapsuleHeightDip, config::kMaximumCapsuleHeightDip,
              1, "DIP", 64);
    add_range("capsuleEndRounding", config::kMinimumCapsuleEndRoundingDip,
              config::kMaximumCapsuleEndRoundingDip, 1, "DIP",
              config::kDefaultCapsuleEndRoundingDip);
    add_range("hoverDelay", static_cast<int>(config::kMinimumHoverDelay.count()),
              static_cast<int>(config::kMaximumHoverDelay.count()), 50, "ms", 350);
    add_range("autoHideDelay", static_cast<int>(config::kMinimumAutoHideDelay.count()),
              static_cast<int>(config::kMaximumAutoHideDelay.count()), 100, "ms", 500);
    add_range("dismissDelay", static_cast<int>(config::kMinimumDismissDelay.count()),
              static_cast<int>(config::kMaximumDismissDelay.count()), 50, "ms", 250);
    add_range("opacity", config::kMinimumOpacityPercent, config::kMaximumOpacityPercent, 1, "%",
              92);
    add_range("blurStrength", config::kMinimumBlurStrengthPercent,
              config::kMaximumBlurStrengthPercent, 1, "%", 60);
    add_range("topN", config::kMinimumTopN, config::kMaximumTopN, 1, "", 5);
    add_range("rankingWindow", static_cast<int>(config::kMinimumRankingWindow.count()),
              static_cast<int>(config::kMaximumRankingWindow.count()), 250, "ms", 2000);
    add_range("refreshInterval", static_cast<int>(config::kMinimumRefreshInterval.count()),
              static_cast<int>(config::kMaximumRefreshInterval.count()), 50, "ms", 500);

    auto add_enum_array = [&](const char* key, std::span<const char* const> values) {
        yyjson_mut_val* arr = yyjson_mut_arr(doc.get());
        yyjson_mut_obj_add_val(doc.get(), schema, key, arr);
        for (const auto* v : values) {
            yyjson_mut_arr_add_str(doc.get(), arr, v);
        }
    };

    constexpr std::array speed_units_arr = {"auto", "bytes", "bits"};
    add_enum_array("speedUnits", speed_units_arr);

    constexpr std::array themes_arr = {"system", "light", "dark"};
    add_enum_array("themes", themes_arr);

    constexpr std::array languages_arr = {"system", "english", "simplified_chinese"};
    add_enum_array("languages", languages_arr);

    constexpr std::array log_levels_arr = {"off", "error", "info", "debug"};
    add_enum_array("logLevels", log_levels_arr);

    constexpr std::array dock_sides_arr = {"left", "right"};
    add_enum_array("dockSides", dock_sides_arr);

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_surface_changed(const SurfaceChangedDto& surface) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "surfaceChanged");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);
    const std::string surface_name{to_string(surface.surface)};
    yyjson_mut_obj_add_str(doc.get(), payload, "surface", surface_name.c_str());
    yyjson_mut_obj_add_bool(doc.get(), payload, "pinned", surface.pinned);
    yyjson_mut_obj_add_str(doc.get(), payload, "dockSide",
                           surface.dock_side == config::DockSide::Left ? "left" : "right");
    yyjson_mut_obj_add_bool(doc.get(), payload, "autoHidden", surface.auto_hidden);
    if (!surface.route.empty()) {
        yyjson_mut_obj_add_str(doc.get(), payload, "route", surface.route.c_str());
    }
    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_command_response(const CommandResponseDto& response) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "commandResponse");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_str(doc.get(), payload, "commandId", response.command_id.c_str());
    yyjson_mut_obj_add_bool(doc.get(), payload, "success", response.success);
    if (!response.message.empty()) {
        yyjson_mut_obj_add_str(doc.get(), payload, "message", response.message.c_str());
    }

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_update_status(const UpdateStatusDto& status) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "updateStatus");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_str(doc.get(), payload, "state", status.state.c_str());
    yyjson_mut_obj_add_str(doc.get(), payload, "currentVersion", status.current_version.c_str());
    yyjson_mut_obj_add_str(doc.get(), payload, "latestVersion", status.latest_version.c_str());
    yyjson_mut_obj_add_uint(doc.get(), payload, "downloadProgressPercent",
                            status.download_progress_percent);
    yyjson_mut_obj_add_str(doc.get(), payload, "errorMessage", status.error_message.c_str());

    return write_mut_doc(doc.get());
}
std::string BridgeDispatcher::serialize_connected_applications(
    const ConnectedApplicationsDto& apps) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "connectedApplications");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_val* arr = yyjson_mut_arr(doc.get());
    yyjson_mut_obj_add_val(doc.get(), payload, "applications", arr);
    for (const auto& app : apps.applications) {
        yyjson_mut_val* item = yyjson_mut_obj(doc.get());
        yyjson_mut_obj_add_str(doc.get(), item, "id", app.id.c_str());
        yyjson_mut_obj_add_str(doc.get(), item, "executablePath", app.executable_path.c_str());
        yyjson_mut_obj_add_str(doc.get(), item, "executableName", app.executable_name.c_str());
        yyjson_mut_obj_add_str(doc.get(), item, "displayName", app.display_name.c_str());
        yyjson_mut_obj_add_str(doc.get(), item, "iconKey", app.icon_key.c_str());
        yyjson_mut_obj_add_uint(doc.get(), item, "totalDownloadBytes", app.total_download_bytes);
        yyjson_mut_obj_add_uint(doc.get(), item, "totalUploadBytes", app.total_upload_bytes);
        yyjson_mut_arr_append(arr, item);
    }

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_traffic_history(const TrafficHistoryDto& history) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "trafficHistoryUpdated");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_uint(doc.get(), payload, "schemaVersion", history.schema_version);

    yyjson_mut_val* days_arr = yyjson_mut_arr(doc.get());
    yyjson_mut_obj_add_val(doc.get(), payload, "days", days_arr);
    for (const auto& day : history.days) {
        yyjson_mut_val* day_obj = yyjson_mut_obj(doc.get());
        yyjson_mut_obj_add_str(doc.get(), day_obj, "date", day.date.c_str());
        yyjson_mut_obj_add_uint(doc.get(), day_obj, "totalDownloadBytes", day.total_download_bytes);
        yyjson_mut_obj_add_uint(doc.get(), day_obj, "totalUploadBytes", day.total_upload_bytes);

        yyjson_mut_val* apps_arr = yyjson_mut_arr(doc.get());
        yyjson_mut_obj_add_val(doc.get(), day_obj, "applications", apps_arr);
        for (const auto& app : day.applications) {
            yyjson_mut_val* app_obj = yyjson_mut_obj(doc.get());
            yyjson_mut_obj_add_str(doc.get(), app_obj, "appId", app.app_id.c_str());
            yyjson_mut_obj_add_str(doc.get(), app_obj, "executablePath",
                                   app.executable_path.c_str());
            yyjson_mut_obj_add_str(doc.get(), app_obj, "executableName",
                                   app.executable_name.c_str());
            yyjson_mut_obj_add_str(doc.get(), app_obj, "displayName", app.display_name.c_str());
            yyjson_mut_obj_add_str(doc.get(), app_obj, "iconKey", app.icon_key.c_str());
            yyjson_mut_obj_add_uint(doc.get(), app_obj, "downloadBytes", app.download_bytes);
            yyjson_mut_obj_add_uint(doc.get(), app_obj, "uploadBytes", app.upload_bytes);
            yyjson_mut_arr_append(apps_arr, app_obj);
        }
        yyjson_mut_arr_append(days_arr, day_obj);
    }

    return write_mut_doc(doc.get());
}

std::string BridgeDispatcher::serialize_error(const BridgeErrorDto& error) {
    ScopedJsonMutDoc doc;
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_str(doc.get(), root, "type", "error");
    yyjson_mut_obj_add_uint(doc.get(), root, "version", kCurrentBridgeVersion);

    yyjson_mut_val* payload = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_val(doc.get(), root, "payload", payload);

    yyjson_mut_obj_add_str(doc.get(), payload, "code", error.code.c_str());
    yyjson_mut_obj_add_str(doc.get(), payload, "message", error.message.c_str());

    return write_mut_doc(doc.get());
}

} // namespace nfm::host::bridge
