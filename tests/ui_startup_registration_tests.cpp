#include "network_flow_monitor/ui/startup_registration.h"

#include <windows.h>

#include <iostream>
#include <string>
#include <string_view>

namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures_;
        }
    }
    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

struct FakeRunKeyStore final : nfm::ui::StartupRunKeyStoreApi {
    std::wstring value_name;
    std::wstring command;
    bool removed{};
    bool fail{};

    [[nodiscard]] nfm::ui::StartupRegistrationResult Set(
        std::wstring_view next_name, std::wstring_view next_command) noexcept override {
        value_name = next_name;
        command = next_command;
        return fail ? nfm::ui::StartupRegistrationResult{.succeeded = false,
                                                         .win32_error = ERROR_ACCESS_DENIED}
                    : nfm::ui::StartupRegistrationResult{.succeeded = true,
                                                         .win32_error = ERROR_SUCCESS};
    }

    [[nodiscard]] nfm::ui::StartupRegistrationResult Remove(
        std::wstring_view next_name) noexcept override {
        value_name = next_name;
        removed = true;
        return fail ? nfm::ui::StartupRegistrationResult{.succeeded = false,
                                                         .win32_error = ERROR_ACCESS_DENIED}
                    : nfm::ui::StartupRegistrationResult{.succeeded = true,
                                                         .win32_error = ERROR_SUCCESS};
    }
};

void CheckEnableDisable(TestContext& context) {
    FakeRunKeyStore store;
    nfm::ui::StartupRegistration registration{store};
    const auto enabled = registration.Apply(true);
    context.Check(enabled.ok() && store.value_name == nfm::ui::kStartupRunValueName,
                  "startup registration writes the stable value name");
    context.Check(store.command.size() > 2U && store.command.front() == L'"' &&
                      store.command.back() == L'"' &&
                      store.command.find(L"ui_startup_registration_tests.exe") !=
                          std::wstring::npos,
                  "startup registration stores a quoted executable path");

    const auto disabled = registration.Apply(false);
    context.Check(disabled.ok() && store.removed &&
                      store.value_name == nfm::ui::kStartupRunValueName,
                  "disabling startup removes the same value");
}

void CheckFailure(TestContext& context) {
    FakeRunKeyStore store;
    store.fail = true;
    nfm::ui::StartupRegistration registration{store};
    const auto result = registration.Apply(true);
    context.Check(!result.ok() && result.win32_error == ERROR_ACCESS_DENIED,
                  "startup registration propagates registry errors");
}

} // namespace

int main() {
    TestContext context;
    CheckEnableDisable(context);
    CheckFailure(context);
    return context.failures();
}
