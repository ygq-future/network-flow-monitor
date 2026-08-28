#include "network_flow_monitor/host/bridge/bridge_dispatcher.h"

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
    case WebCommandType::UpdateSetting:
        handler_->on_update_setting(cmd.setting_key.value_or(""),
                                    cmd.setting_value_json.value_or(""));
        break;
    case WebCommandType::RetryCollector:
        handler_->on_retry_collector();
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
