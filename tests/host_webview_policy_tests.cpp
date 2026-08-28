#include "network_flow_monitor/host/platform/webview2_environment.h"

#include <iostream>
#include <string_view>

namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

void CheckNavigationPolicies(TestContext& context) {
    using namespace nfm::host::platform;

    // Allowed origins
    context.Check(WebView2Environment::is_allowed_origin("https://nfm.local/"),
                  "allow https://nfm.local/");
    context.Check(WebView2Environment::is_allowed_origin("https://nfm.local"),
                  "allow https://nfm.local");
    context.Check(WebView2Environment::is_allowed_origin("http://localhost:5173/"),
                  "allow localhost");
    context.Check(WebView2Environment::is_allowed_origin("http://127.0.0.1:3000/"),
                  "allow 127.0.0.1");

    // Rejected origins
    context.Check(!WebView2Environment::is_allowed_origin("https://evil.com/"), "reject evil.com");
    context.Check(!WebView2Environment::is_allowed_origin("http://example.org/"),
                  "reject example.org");

    // Allowed navigation URLs
    context.Check(WebView2Environment::is_allowed_navigation(L"https://nfm.local/index.html"),
                  "allow local index.html");
    context.Check(WebView2Environment::is_allowed_navigation(L"about:blank"), "allow about:blank");
    context.Check(!WebView2Environment::is_allowed_navigation(L"https://google.com"),
                  "reject external google.com");
}

void CheckRecoveryGuidance(TestContext& context) {
    using namespace nfm::host::platform;

    auto guidance = WebView2Environment::get_recovery_guidance();
    context.Check(!guidance.empty(), "recovery guidance is non-empty");
    context.Check(guidance.find(L"WebView2") != std::wstring::npos,
                  "recovery guidance mentions WebView2");
}

} // namespace

int main() {
    TestContext context;
    CheckNavigationPolicies(context);
    CheckRecoveryGuidance(context);
    return context.failures();
}
