#include "network_flow_monitor/host/webview/webview_controller.h"

#include "network_flow_monitor/host/platform/webview2_environment.h"
#include "network_flow_monitor/host/platform/webview2_includes.h"

#include <string>
#include <utility>
#include <wrl/client.h>
#include <wrl/event.h>

using namespace Microsoft::WRL;

namespace nfm::host::webview {

struct WebView2Controller::Impl {
    HWND parent_hwnd_{nullptr};
    std::filesystem::path static_assets_dir_;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;

    WebMessageReceivedCallback message_cb_;
    NavigationCompletedCallback navigation_cb_;
    bool is_ready_{false};

    EventRegistrationToken message_token_{};
    EventRegistrationToken navigation_token_{};
    EventRegistrationToken new_window_token_{};
};

WebView2Controller::WebView2Controller() : impl_(std::make_unique<Impl>()) {}

WebView2Controller::~WebView2Controller() {
    if (impl_->controller_) {
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

    std::wstring user_data_dir = platform::WebView2Environment::get_user_data_directory();

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data_dir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT env_hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(env_hr) || env == nullptr) {
                    return env_hr;
                }
                impl_->environment_ = env;

                return env->CreateCoreWebView2Controller(
                    impl_->parent_hwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT ctrl_hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(ctrl_hr) || ctrl == nullptr) {
                                return ctrl_hr;
                            }
                            impl_->controller_ = ctrl;
                            ctrl->get_CoreWebView2(&impl_->webview_);

                            if (!impl_->webview_) {
                                return E_FAIL;
                            }

                            // Configure settings
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(impl_->webview_->get_Settings(&settings)) && settings) {
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
                            if (SUCCEEDED(impl_->webview_.As(&webview_v3)) && webview_v3 &&
                                !impl_->static_assets_dir_.empty()) {
                                std::error_code ec;
                                auto abs_path =
                                    std::filesystem::absolute(impl_->static_assets_dir_, ec);
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
                                        if (SUCCEEDED(args->get_Uri(&uri_raw)) &&
                                            uri_raw != nullptr) {
                                            std::wstring uri = uri_raw;
                                            CoTaskMemFree(uri_raw);
                                            if (!platform::WebView2Environment::
                                                    is_allowed_navigation(uri)) {
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
                                    [this](
                                        ICoreWebView2* /*sender*/,
                                        ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR msg_raw = nullptr;
                                        if (SUCCEEDED(args->get_WebMessageAsJson(&msg_raw)) &&
                                            msg_raw != nullptr) {
                                            int size_needed =
                                                WideCharToMultiByte(CP_UTF8, 0, msg_raw, -1,
                                                                    nullptr, 0, nullptr, nullptr);
                                            if (size_needed > 1) {
                                                std::string utf8_msg(size_needed - 1, '\0');
                                                WideCharToMultiByte(CP_UTF8, 0, msg_raw, -1,
                                                                    utf8_msg.data(), size_needed,
                                                                    nullptr, nullptr);
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
                                           ICoreWebView2NavigationCompletedEventArgs* args)
                                        -> HRESULT {
                                        BOOL is_success = FALSE;
                                        args->get_IsSuccess(&is_success);
                                        int http_status = 200;
                                        if (impl_->navigation_cb_) {
                                            impl_->navigation_cb_(is_success == TRUE, http_status);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);

                            // Resize controller to parent HWND client rect
                            RECT client_rect{};
                            GetClientRect(impl_->parent_hwnd_, &client_rect);
                            ctrl->put_Bounds(client_rect);
                            ctrl->put_IsVisible(TRUE);

                            impl_->is_ready_ = true;

                            // Default navigation
                            navigate(L"https://nfm.local/index.html");

                            return S_OK;
                        })
                        .Get());
            })
            .Get());

    return SUCCEEDED(hr);
}

void WebView2Controller::resize(int width_px, int height_px) {
    if (impl_->controller_) {
        RECT bounds{0, 0, width_px, height_px};
        impl_->controller_->put_Bounds(bounds);
    }
}

void WebView2Controller::navigate(std::wstring_view url) {
    if (impl_->webview_ && !url.empty()) {
        std::wstring url_str(url);
        impl_->webview_->Navigate(url_str.c_str());
    }
}

void WebView2Controller::post_web_message_json(std::string_view json) {
    if (impl_->webview_ && !json.empty()) {
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
    if (impl_->webview_ && !script.empty()) {
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

bool WebView2Controller::is_ready() const noexcept {
    return impl_->is_ready_;
}

} // namespace nfm::host::webview
