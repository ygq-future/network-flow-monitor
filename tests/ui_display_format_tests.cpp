#include "network_flow_monitor/ui/display_format.h"

#include <iostream>
#include <string_view>

namespace {

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

void CheckRateFormatting(TestContext& context) {
    const auto bytes = nfm::ui::FormatRate(1'536.0, nfm::config::SpeedUnit::Bytes);
    const auto bits = nfm::ui::FormatRate(1'536.0, nfm::config::SpeedUnit::Bits);
    context.Check(bytes.find(L"KiB/s") != std::wstring::npos,
                  "byte format uses binary unit suffix");
    context.Check(bits.find(L"Kbit/s") != std::wstring::npos, "bit format uses bit suffix");
}

void CheckApplicationFormatting(TestContext& context) {
    const nfm::ranking::RankedApplication application{
        .id = "executable:C:\\Apps\\browser.exe",
        .metadata = {.display_name = "Browser", .icon_key = {}},
        .rate = {.upload_bytes_per_second = 512.0, .download_bytes_per_second = 2048.0}};
    const nfm::ui::WindowDisplayOptions options{.speed_unit = nfm::config::SpeedUnit::Bytes,
                                                .show_upload = true,
                                                .show_download = false,
                                                .opacity_percent = 92U,
                                                .top_n = 5U};
    const auto line = nfm::ui::FormatApplicationRate(application, options);
    context.Check(line.find(L"Browser") != std::wstring::npos,
                  "application line includes display name");
    context.Check(line.find(L'↑') != std::wstring::npos && line.find(L'↓') == std::wstring::npos,
                  "application line respects direction visibility");
}

} // namespace

int main() {
    TestContext context;
    CheckRateFormatting(context);
    CheckApplicationFormatting(context);
    return context.failures == 0 ? 0 : 1;
}
