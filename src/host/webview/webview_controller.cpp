#include "network_flow_monitor/host/webview/webview_controller.h"

#include "network_flow_monitor/host/platform/webview2_environment.h"
#include "network_flow_monitor/host/platform/webview2_includes.h"
#include "network_flow_monitor/host/platform/win32_window.h"

#include <array>
#include <commctrl.h>
#include <string>
#include <utility>
#include <windowsx.h>
#include <wrl/client.h>
#include <wrl/event.h>

using namespace Microsoft::WRL;

namespace nfm::host::webview {

namespace {

constexpr UINT_PTR kWebViewChildSubclassId = 0x8899;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
LRESULT CALLBACK WebViewChildSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                          UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    HWND parent = GetAncestor(hwnd, GA_ROOT);
    if (parent != nullptr) {
        if (msg == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                                     .dwFlags = TME_LEAVE,
                                     .hwndTrack = hwnd,
                                     .dwHoverTime = HOVER_DEFAULT};
            TrackMouseEvent(&tracking);
            PostMessageW(parent, platform::kWebSurfacePointerMoveMessage, wparam, lparam);
        } else if (msg == WM_LBUTTONUP) {
            PostMessageW(parent, platform::kWebSurfacePointerUpMessage, wparam, lparam);
        } else if (msg == WM_MOUSELEAVE) {
            PostMessageW(parent, platform::kWebSurfacePointerLeaveMessage, 0, 0);
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, WebViewChildSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void SubclassWebViewChildren(HWND parent_hwnd) {
    if (parent_hwnd == nullptr) {
        return;
    }
    EnumChildWindows(
        parent_hwnd,
        [](HWND child, LPARAM) -> BOOL {
            SetWindowSubclass(child, WebViewChildSubclassProc, kWebViewChildSubclassId, 0);
            return TRUE;
        },
        0);
}
void UnsubclassWebViewChildren(HWND parent_hwnd) {
    if (parent_hwnd == nullptr) {
        return;
    }
    EnumChildWindows(
        parent_hwnd,
        [](HWND child, LPARAM) -> BOOL {
            RemoveWindowSubclass(child, WebViewChildSubclassProc, kWebViewChildSubclassId);
            return TRUE;
        },
        0);
}

} // namespace

static ComPtr<ICoreWebView2Environment> g_shared_env;

struct WebView2Controller::Impl {
    HWND parent_hwnd_{nullptr};
    std::filesystem::path static_assets_dir_;
    std::wstring last_navigated_url_{L"https://nfm.local/index.html"};
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;

    WebMessageReceivedCallback message_cb_;
    NavigationCompletedCallback navigation_cb_;
    WebViewDiagnosticCallback diagnostic_cb_;
    bool is_ready_{false};
    bool is_creating_{false};
    bool desired_visible_{true};
    bool desired_low_memory_{false};
    bool is_suspended_{false};
    int last_width_px_{-1};
    int last_height_px_{-1};
    EventRegistrationToken message_token_{};
    EventRegistrationToken navigation_token_{};
    EventRegistrationToken new_window_token_{};
    EventRegistrationToken process_failed_token_{};

    void reset_controller() {
        is_ready_ = false;
        is_creating_ = false;
        is_suspended_ = false;
        if (parent_hwnd_ != nullptr) {
            UnsubclassWebViewChildren(parent_hwnd_);
        }
        if (controller_.Get() != nullptr) {
            controller_->Close();
            controller_.Reset();
        }
        webview_.Reset();
        environment_.Reset();
    }

    void apply_memory_and_suspension_state() {
        if (webview_.Get() == nullptr) {
            return;
        }
        ComPtr<ICoreWebView2_19> webview_v19;
        if (SUCCEEDED(webview_.As(&webview_v19)) && webview_v19.Get() != nullptr) {
            const COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL target_level =
                (desired_low_memory_ || !desired_visible_)
                    ? COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW
                    : COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL;
            static_cast<void>(webview_v19->put_MemoryUsageTargetLevel(target_level));
        }

        ComPtr<ICoreWebView2_3> webview_v3;
        if (SUCCEEDED(webview_.As(&webview_v3)) && webview_v3.Get() != nullptr) {
            if (desired_visible_) {
                if (is_suspended_) {
                    is_suspended_ = false;
                    static_cast<void>(webview_v3->Resume());
                }
            } else if (is_ready_ && !is_suspended_) {
                is_suspended_ = true;
                static_cast<void>(webview_v3->TrySuspend(
                    Callback<ICoreWebView2TrySuspendCompletedHandler>([this](HRESULT error_code,
                                                                             BOOL is_successful)
                                                                          -> HRESULT {
                        if (FAILED(error_code) || is_successful == FALSE) {
                            is_suspended_ = false;
                        }
                        return S_OK;
                    }).Get()));
            }
        }
    }
};
WebView2Controller::WebView2Controller() : impl_(std::make_unique<Impl>()) {}

WebView2Controller::~WebView2Controller() {
    if (impl_->parent_hwnd_ != nullptr) {
        UnsubclassWebViewChildren(impl_->parent_hwnd_);
    }
    if (impl_->controller_.Get() != nullptr) {
        impl_->controller_->Close();
        impl_->controller_.Reset();
    }
    impl_->webview_.Reset();
    impl_->environment_.Reset();
}

bool WebView2Controller::initialize(HWND parent_hwnd,
                                    const std::filesystem::path& static_assets_dir) {
    impl_->parent_hwnd_ = parent_hwnd;
    impl_->static_assets_dir_ = static_assets_dir;
    if (impl_->parent_hwnd_ == nullptr || IsWindow(impl_->parent_hwnd_) == FALSE) {
        return false;
    }
    if (impl_->is_creating_) {
        return true;
    }
    impl_->is_creating_ = true;

    static bool env_vars_set = false;
    if (!env_vars_set) {
        SetEnvironmentVariableW(
            L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
            L"--disable-background-timer-throttling "
            L"--disable-features=CalculateNativeWinOcclusion,IntensiveWakeUpThrottling "
            L"--js-flags=--lite-mode");
        env_vars_set = true;
    }

    auto create_controller_with_env = [this](ICoreWebView2Environment* env) -> HRESULT {
        if (impl_->parent_hwnd_ == nullptr || IsWindow(impl_->parent_hwnd_) == FALSE) {
            impl_->is_creating_ = false;
            return E_FAIL;
        }
        impl_->environment_ = env;
        return env->CreateCoreWebView2Controller(
            impl_->parent_hwnd_,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this](HRESULT ctrl_hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                    impl_->is_creating_ = false;
                    if (FAILED(ctrl_hr) || ctrl == nullptr) {
                        g_shared_env.Reset();
                        return ctrl_hr;
                    }
                    impl_->controller_ = ctrl;
                    ctrl->get_CoreWebView2(&impl_->webview_);

                    if (impl_->webview_.Get() == nullptr) {
                        return E_FAIL;
                    }

                    // Configure settings
                    ComPtr<ICoreWebView2Settings> settings;
                    if (SUCCEEDED(impl_->webview_->get_Settings(&settings)) &&
                        settings.Get() != nullptr) {
                        settings->put_IsScriptEnabled(TRUE);
                        settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                        settings->put_IsWebMessageEnabled(TRUE);
                        settings->put_AreDevToolsEnabled(TRUE);
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_IsZoomControlEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                    }

                    // Setup virtual host name mapping for static assets
                    ComPtr<ICoreWebView2_3> webview_v3;
                    if (SUCCEEDED(impl_->webview_.As(&webview_v3)) && webview_v3.Get() != nullptr &&
                        !impl_->static_assets_dir_.empty()) {
                        std::error_code ec;
                        auto abs_path = std::filesystem::absolute(impl_->static_assets_dir_, ec);
                        if (!ec && std::filesystem::exists(abs_path)) {
                            std::wstring folder_path = abs_path.wstring();
                            static_cast<void>(webview_v3->SetVirtualHostNameToFolderMapping(
                                L"nfm.local", folder_path.c_str(),
                                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW));
                        }
                    }

                    // Navigation starting policy filter
                    impl_->webview_->add_NavigationStarting(
                        Callback<ICoreWebView2NavigationStartingEventHandler>(
                            [](ICoreWebView2* /*sender*/,
                               ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                LPWSTR uri_raw = nullptr;
                                if (SUCCEEDED(args->get_Uri(&uri_raw)) && uri_raw != nullptr) {
                                    std::wstring uri = uri_raw;
                                    CoTaskMemFree(uri_raw);
                                    if (!platform::WebView2Environment::is_allowed_navigation(
                                            uri)) {
                                        args->put_Cancel(TRUE);
                                    }
                                }
                                return S_OK;
                            })
                            .Get(),
                        &impl_->navigation_token_);

                    // Block new window requests
                    impl_->webview_->add_NewWindowRequested(
                        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                            [](ICoreWebView2* /*sender*/,
                               ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                args->put_Handled(TRUE);
                                return S_OK;
                            })
                            .Get(),
                        &impl_->new_window_token_);

                    // Message received handler
                    impl_->webview_->add_WebMessageReceived(
                        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                            [this](ICoreWebView2* /*sender*/,
                                   ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                LPWSTR msg_raw = nullptr;
                                HRESULT str_hr = args->TryGetWebMessageAsString(&msg_raw);
                                if (FAILED(str_hr) || msg_raw == nullptr) {
                                    static_cast<void>(args->get_WebMessageAsJson(&msg_raw));
                                }
                                if (msg_raw != nullptr) {
                                    int size_needed = WideCharToMultiByte(
                                        CP_UTF8, 0, msg_raw, -1, nullptr, 0, nullptr, nullptr);
                                    if (size_needed > 1) {
                                        std::string utf8_msg(size_needed - 1, '\0');
                                        WideCharToMultiByte(CP_UTF8, 0, msg_raw, -1,
                                                            utf8_msg.data(), size_needed, nullptr,
                                                            nullptr);
                                        if (impl_->message_cb_) {
                                            impl_->message_cb_(utf8_msg);
                                        }
                                    }
                                    CoTaskMemFree(msg_raw);
                                }
                                return S_OK;
                            })
                            .Get(),
                        &impl_->message_token_);

                    // Navigation completed handler
                    impl_->webview_->add_NavigationCompleted(
                        Callback<ICoreWebView2NavigationCompletedEventHandler>(
                            [this](ICoreWebView2* /*sender*/,
                                   ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                BOOL is_success = FALSE;
                                args->get_IsSuccess(&is_success);
                                int http_status = 200;
                                SubclassWebViewChildren(impl_->parent_hwnd_);
                                if (impl_->navigation_cb_) {
                                    impl_->navigation_cb_(is_success == TRUE, http_status);
                                }
                                impl_->apply_memory_and_suspension_state();
                                return S_OK;
                            })
                            .Get(),
                        nullptr);

                    impl_->webview_->add_ProcessFailed(
                        Callback<ICoreWebView2ProcessFailedEventHandler>(
                            [this](ICoreWebView2* /*sender*/,
                                   ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                                COREWEBVIEW2_PROCESS_FAILED_KIND kind =
                                    COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                                if (args != nullptr) {
                                    static_cast<void>(args->get_ProcessFailedKind(&kind));
                                }
                                if (impl_->diagnostic_cb_) {
                                    impl_->diagnostic_cb_(
                                        "WebView2 ProcessFailed triggered (kind=" +
                                        std::to_string(static_cast<int>(kind)) +
                                        "); attempting automatic recovery");
                                }
                                if (kind ==
                                    COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
                                    g_shared_env.Reset();
                                    HWND hwnd = impl_->parent_hwnd_;
                                    auto assets = impl_->static_assets_dir_;
                                    impl_->reset_controller();
                                    static_cast<void>(initialize(hwnd, assets));
                                } else if (impl_->webview_.Get() != nullptr) {
                                    if (FAILED(impl_->webview_->Reload())) {
                                        g_shared_env.Reset();
                                        HWND hwnd = impl_->parent_hwnd_;
                                        auto assets = impl_->static_assets_dir_;
                                        impl_->reset_controller();
                                        static_cast<void>(initialize(hwnd, assets));
                                    }
                                }
                                return S_OK;
                            })
                            .Get(),
                        &impl_->process_failed_token_);

                    ComPtr<ICoreWebView2Controller2> ctrl2;
                    if (SUCCEEDED(impl_->controller_.As(&ctrl2)) && ctrl2.Get() != nullptr) {
                        COREWEBVIEW2_COLOR bg_color{.A = 0, .R = 0, .G = 0, .B = 0};
                        ctrl2->put_DefaultBackgroundColor(bg_color);
                    }

                    ComPtr<ICoreWebView2Controller3> ctrl3;
                    if (SUCCEEDED(impl_->controller_.As(&ctrl3)) && ctrl3.Get() != nullptr) {
                        ctrl3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
                        ctrl3->put_ShouldDetectMonitorScaleChanges(TRUE);
                    }

                    // Resize controller to parent HWND client rect
                    RECT client_rect{};
                    GetClientRect(impl_->parent_hwnd_, &client_rect);
                    ctrl->put_Bounds(client_rect);
                    impl_->last_width_px_ = client_rect.right - client_rect.left;
                    impl_->last_height_px_ = client_rect.bottom - client_rect.top;
                    ctrl->put_IsVisible(impl_->desired_visible_ ? TRUE : FALSE);
                    SubclassWebViewChildren(impl_->parent_hwnd_);
                    impl_->is_ready_ = true;
                    impl_->apply_memory_and_suspension_state();
                    // Navigate to target URL
                    navigate(impl_->last_navigated_url_);
                    return S_OK;
                })
                .Get());
    };

    if (g_shared_env.Get() != nullptr) {
        HRESULT ctrl_hr = create_controller_with_env(g_shared_env.Get());
        if (SUCCEEDED(ctrl_hr)) {
            return true;
        }
        g_shared_env.Reset();
        impl_->is_creating_ = true;
    }

    std::wstring user_data_dir = platform::WebView2Environment::get_user_data_directory();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data_dir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, create_controller_with_env](HRESULT env_hr,
                                               ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(env_hr) || env == nullptr) {
                    impl_->is_creating_ = false;
                    return env_hr;
                }
                g_shared_env = env;
                return create_controller_with_env(env);
            })
            .Get());

    if (FAILED(hr)) {
        impl_->is_creating_ = false;
    }
    return SUCCEEDED(hr);
}

void WebView2Controller::resize(int width_px, int height_px) {
    if (impl_->controller_.Get() != nullptr) {
        if (impl_->last_width_px_ != width_px || impl_->last_height_px_ != height_px) {
            impl_->last_width_px_ = width_px;
            impl_->last_height_px_ = height_px;
            RECT bounds{.left = 0, .top = 0, .right = width_px, .bottom = height_px};
            impl_->controller_->put_Bounds(bounds);
        }
        if (impl_->desired_visible_) {
            impl_->controller_->put_IsVisible(TRUE);
        }
    }
}

void WebView2Controller::notify_parent_window_position_changed() {
    if (impl_->controller_.Get() != nullptr) {
        static_cast<void>(impl_->controller_->NotifyParentWindowPositionChanged());
    }
}

void WebView2Controller::set_visible(bool visible) {
    impl_->desired_visible_ = visible;
    if (impl_->controller_.Get() != nullptr) {
        if (visible) {
            impl_->apply_memory_and_suspension_state();
            impl_->controller_->put_IsVisible(TRUE);
        } else {
            impl_->controller_->put_IsVisible(FALSE);
            impl_->apply_memory_and_suspension_state();
        }
    }
}

void WebView2Controller::set_memory_target_low(bool low_memory) {
    impl_->desired_low_memory_ = low_memory;
    impl_->apply_memory_and_suspension_state();
}
void WebView2Controller::navigate(std::wstring_view url) {
    if (!url.empty()) {
        impl_->last_navigated_url_.assign(url);
    }
    if (impl_->webview_.Get() != nullptr && !url.empty()) {
        std::wstring url_str(url);
        impl_->webview_->Navigate(url_str.c_str());
    }
}

void WebView2Controller::post_web_message_json(std::string_view json) {
    if (impl_->webview_.Get() != nullptr && !json.empty()) {
        int w_size =
            MultiByteToWideChar(CP_UTF8, 0, json.data(), static_cast<int>(json.size()), nullptr, 0);
        if (w_size > 0) {
            std::wstring w_json(w_size, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, json.data(), static_cast<int>(json.size()),
                                w_json.data(), w_size);
            impl_->webview_->PostWebMessageAsJson(w_json.c_str());
        }
    }
}

void WebView2Controller::execute_script(std::wstring_view script) {
    if (impl_->webview_.Get() != nullptr && !script.empty()) {
        std::wstring script_str(script);
        impl_->webview_->ExecuteScript(script_str.c_str(), nullptr);
    }
}

void WebView2Controller::set_message_callback(WebMessageReceivedCallback callback) {
    impl_->message_cb_ = std::move(callback);
}

void WebView2Controller::set_navigation_callback(NavigationCompletedCallback callback) {
    impl_->navigation_cb_ = std::move(callback);
}
void WebView2Controller::set_diagnostic_callback(const WebViewDiagnosticCallback& callback) {
    impl_->diagnostic_cb_ = callback;
}

bool WebView2Controller::is_ready() const noexcept {
    return impl_->is_ready_;
}

bool WebView2Controller::ensure_alive() {
    if (impl_->parent_hwnd_ == nullptr || IsWindow(impl_->parent_hwnd_) == FALSE) {
        return false;
    }
    if (impl_->is_creating_) {
        return true;
    }
    HWND child = GetWindow(impl_->parent_hwnd_, GW_CHILD);
    if (impl_->is_ready_ && impl_->controller_.Get() != nullptr &&
        impl_->webview_.Get() != nullptr && child != nullptr && IsWindow(child) != FALSE) {
        RECT parent_rc{};
        if (GetClientRect(impl_->parent_hwnd_, &parent_rc) != FALSE) {
            const int w = parent_rc.right - parent_rc.left;
            const int h = parent_rc.bottom - parent_rc.top;
            if (w > 0 && h > 0) {
                RECT ctrl_bounds{};
                if (SUCCEEDED(impl_->controller_->get_Bounds(&ctrl_bounds))) {
                    const int cw = ctrl_bounds.right - ctrl_bounds.left;
                    const int ch = ctrl_bounds.bottom - ctrl_bounds.top;
                    if (cw != w || ch != h) {
                        resize(w, h);
                    }
                }
            }
        }
        return true;
    }
    HWND hwnd = impl_->parent_hwnd_;
    auto assets = impl_->static_assets_dir_;
    impl_->reset_controller();
    return initialize(hwnd, assets);
}

} // namespace nfm::host::webview
