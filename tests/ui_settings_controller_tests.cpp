#include "network_flow_monitor/ui/settings_controller.h"

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

struct FakeStore final : nfm::ui::SettingsStoreApi {
    nfm::config::ConfigLoadResult load_result{.config = nfm::config::DefaultAppConfig(),
                                              .status = nfm::config::ConfigLoadStatus::Missing,
                                              .error = std::nullopt};
    nfm::config::ConfigSaveResult save_result{.saved = true, .error = std::nullopt};
    std::size_t load_calls{};
    std::size_t save_calls{};
    nfm::config::AppConfig saved{};

    nfm::config::ConfigLoadResult Load(const std::filesystem::path&,
                                       nfm::config::ConfigDocumentKind) override {
        ++load_calls;
        return load_result;
    }
    nfm::config::ConfigSaveResult Save(const std::filesystem::path&,
                                       const nfm::config::AppConfig& value,
                                       nfm::config::ConfigDocumentKind) override {
        ++save_calls;
        saved = value;
        return save_result;
    }
};

struct FakeService final : nfm::ui::ServiceActionApi {
    nfm::ui::SettingsServiceAction action{nfm::ui::SettingsServiceAction::Query};
    nfm::config::ServiceStartType start_type{nfm::config::ServiceStartType::Manual};
    std::size_t calls{};

    nfm::ui::SettingsActionResult Execute(
        nfm::ui::SettingsServiceAction next_action,
        nfm::config::ServiceStartType next_start_type) noexcept override {
        action = next_action;
        start_type = next_start_type;
        ++calls;
        return {.succeeded = true,
                .state = nfm::service::ServiceState::Running,
                .win32_error = ERROR_SUCCESS};
    }
};

void CheckLoadAndSave(TestContext& context) {
    FakeStore store;
    FakeService service;
    nfm::ui::SettingsController controller{L"settings.json", nfm::config::ConfigDocumentKind::User,
                                           store, service};
    const auto loaded = controller.Load();
    context.Check(loaded.status == nfm::config::ConfigLoadStatus::Missing && store.load_calls == 1U,
                  "settings load delegates to store");

    auto value = loaded.config;
    value.ranking.top_n = 9U;
    const auto saved = controller.Save(value);
    context.Check(saved.saved && store.save_calls == 1U && controller.config() == value,
                  "valid settings are persisted and become current");
}

void CheckValidationAndService(TestContext& context) {
    FakeStore store;
    FakeService service;
    nfm::ui::SettingsController controller{L"settings.json", nfm::config::ConfigDocumentKind::User,
                                           store, service};
    auto invalid = nfm::config::DefaultAppConfig();
    invalid.ranking.top_n = 0U;
    const auto rejected = controller.Save(invalid);
    context.Check(!rejected.saved && rejected.error.has_value() && store.save_calls == 0U,
                  "invalid settings are rejected before writing");
    const auto action = controller.ExecuteService(nfm::ui::SettingsServiceAction::Install,
                                                  nfm::config::ServiceStartType::Automatic);
    context.Check(action.ok() && service.calls == 1U &&
                      service.action == nfm::ui::SettingsServiceAction::Install &&
                      service.start_type == nfm::config::ServiceStartType::Automatic,
                  "service actions preserve the selected start type");
}

} // namespace

int main() {
    TestContext context;
    CheckLoadAndSave(context);
    CheckValidationAndService(context);
    return context.failures();
}
