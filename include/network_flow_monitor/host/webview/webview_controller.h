#ifndef NETWORK_FLOW_MONITOR_HOST_WEBVIEW_WEBVIEW_CONTROLLER_H
#define NETWORK_FLOW_MONITOR_HOST_WEBVIEW_WEBVIEW_CONTROLLER_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace nfm::host::webview {

using WebMessageReceivedCallback = std::function<void(std::string_view message_json)>;
using NavigationCompletedCallback = std::function<void(bool success, int http_status)>;
using WebViewDiagnosticCallback = std::function<void(std::string_view diagnostic)>;

class IWebViewController {
  public:
    IWebViewController() = default;
    virtual ~IWebViewController() = default;
    IWebViewController(const IWebViewController&) = delete;
    IWebViewController& operator=(const IWebViewController&) = delete;
    IWebViewController(IWebViewController&&) = delete;
    IWebViewController& operator=(IWebViewController&&) = delete;

    virtual bool initialize(HWND parent_hwnd, const std::filesystem::path& static_assets_dir) = 0;
    virtual void resize(int width_px, int height_px) = 0;
    virtual void set_visible(bool visible) = 0;
    virtual void set_memory_target_low(bool low_memory) {
        static_cast<void>(low_memory);
    }
    virtual void notify_parent_window_position_changed() {}
    virtual void navigate(std::wstring_view url) = 0;
    virtual void post_web_message_json(std::string_view json) = 0;
    virtual void execute_script(std::wstring_view script) = 0;
    virtual void set_message_callback(WebMessageReceivedCallback callback) = 0;
    virtual void set_navigation_callback(NavigationCompletedCallback callback) = 0;
    virtual void set_diagnostic_callback(const WebViewDiagnosticCallback& /*callback*/) {}
    [[nodiscard]] virtual bool is_ready() const noexcept = 0;
    virtual bool ensure_alive() = 0;
};

class WebView2Controller : public IWebViewController {
  public:
    WebView2Controller();
    ~WebView2Controller() override;

    WebView2Controller(const WebView2Controller&) = delete;
    WebView2Controller& operator=(const WebView2Controller&) = delete;
    WebView2Controller(WebView2Controller&&) = delete;
    WebView2Controller& operator=(WebView2Controller&&) = delete;

    bool initialize(HWND parent_hwnd, const std::filesystem::path& static_assets_dir) override;
    void resize(int width_px, int height_px) override;
    void set_visible(bool visible) override;
    void set_memory_target_low(bool low_memory) override;
    void notify_parent_window_position_changed() override;
    void navigate(std::wstring_view url) override;
    void post_web_message_json(std::string_view json) override;
    void execute_script(std::wstring_view script) override;
    void set_message_callback(WebMessageReceivedCallback callback) override;
    void set_navigation_callback(NavigationCompletedCallback callback) override;
    void set_diagnostic_callback(const WebViewDiagnosticCallback& callback) override;
    [[nodiscard]] bool is_ready() const noexcept override;
    bool ensure_alive() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfm::host::webview

#endif // NETWORK_FLOW_MONITOR_HOST_WEBVIEW_WEBVIEW_CONTROLLER_H
