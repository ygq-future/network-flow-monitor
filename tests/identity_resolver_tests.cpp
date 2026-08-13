#include "network_flow_monitor/process/identity_resolver.h"

#include <iostream>
#include <optional>
#include <string>
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

using nfm::process::IdentityKind;
using nfm::process::IdentityResolutionError;
using nfm::process::ProcessInstanceId;
using nfm::process::ProcessProbeData;

nfm::process::IdentityResolutionResult Resolve(ProcessInstanceId instance,
                                               const ProcessProbeData& data) {
    return nfm::process::ResolveApplicationIdentity(instance, data);
}

void CheckIdentityPriority(TestContext& context) {
    const ProcessInstanceId instance{.pid = 100U, .generation = 7U};
    const auto packaged = Resolve(instance, {.application_user_model_id = "Family!App",
                                             .package_family_name = "Family",
                                             .image_path = "C:\\App\\app.exe",
                                             .image_name_hint = "app.exe"});
    context.Check(packaged.ok(), "packaged identity resolves");
    context.Check(packaged.identity.kind == IdentityKind::PackageApplication, "AUMID kind wins");
    context.Check(packaged.identity.id == "package-app:Family!App", "AUMID value wins");
    context.Check(packaged.identity.metadata.display_name == "app.exe",
                  "image hint supplies display name");
    context.Check(packaged.identity.metadata.icon_key == "C:\\App\\app.exe",
                  "resolved image path supplies icon key");

    const auto family = Resolve(instance, {.package_family_name = "Family"});
    context.Check(family.ok(), "package family resolves");
    context.Check(family.identity.kind == IdentityKind::PackageFamily,
                  "package family is second priority");
    context.Check(family.identity.id == "package-family:Family", "package family value");

    const auto executable = Resolve(instance, {.image_path = "C:/App/app.exe"});
    context.Check(executable.ok(), "executable identity resolves");
    context.Check(executable.identity.kind == IdentityKind::Executable,
                  "executable is third priority");
    context.Check(executable.identity.id == "executable:C:\\App\\app.exe",
                  "executable path is normalized");
    context.Check(executable.identity.metadata.display_name == "app.exe",
                  "path basename supplies display name");
}

void CheckSystemAndUnresolvedIdentity(TestContext& context) {
    const auto idle = Resolve({.pid = 0U, .generation = 1U}, {});
    context.Check(idle.identity.kind == IdentityKind::System, "idle is system identity");
    context.Check(idle.identity.id == "system:idle", "idle identity is stable");

    const auto kernel = Resolve({.pid = 4U, .generation = 2U}, {.image_name_hint = "System"});
    context.Check(kernel.identity.kind == IdentityKind::System,
                  "verified PID 4 is system identity");
    context.Check(kernel.identity.id == "system:kernel", "kernel identity is stable");

    const auto unverified_kernel = Resolve({.pid = 4U, .generation = 3U}, {});
    context.Check(unverified_kernel.identity.kind == IdentityKind::Unresolved,
                  "PID 4 without System hint is unresolved");

    const auto first = Resolve({.pid = 42U, .generation = 8U}, {});
    const auto second = Resolve({.pid = 42U, .generation = 9U}, {});
    context.Check(first.identity.id == "unresolved:42:8", "unresolved identity has instance");
    context.Check(first.identity.id != second.identity.id,
                  "unresolved identity does not merge generations");
}

void CheckPathIdentitySeparation(TestContext& context) {
    const ProcessInstanceId first{.pid = 1U, .generation = 1U};
    const ProcessInstanceId second{.pid = 2U, .generation = 2U};
    const auto one = Resolve(first, {.image_path = "C:\\One\\same.exe"});
    const auto two = Resolve(second, {.image_path = "D:\\Two\\same.exe"});
    context.Check(one.identity.id != two.identity.id,
                  "same filename in different paths stays separate");

    const auto same_one = Resolve(first, {.image_path = "C:\\One\\same.exe"});
    const auto same_two = Resolve(second, {.image_path = "C:\\One\\same.exe"});
    context.Check(same_one.identity.id == same_two.identity.id,
                  "same executable path merges processes");

    const auto different_case = Resolve(second, {.image_path = "c:\\one\\same.exe"});
    context.Check(same_one.identity.id != different_case.identity.id,
                  "path case is preserved to avoid false merge");

    const auto first_packaged = Resolve(first, {.application_user_model_id = "Family!First"});
    const auto second_packaged = Resolve(second, {.application_user_model_id = "Family!Second"});
    context.Check(first_packaged.identity.id != second_packaged.identity.id,
                  "different AUMIDs stay separate");
}

void CheckPathNormalization(TestContext& context) {
    using nfm::process::NormalizeWindowsImagePath;

    context.Check(NormalizeWindowsImagePath(R"(\\?\C:\App\app.exe)") == R"(C:\App\app.exe)",
                  "extended DOS path normalized");
    context.Check(NormalizeWindowsImagePath(R"(\\?\UNC\server\share\app.exe)") ==
                      R"(\\server\share\app.exe)",
                  "extended UNC path normalized");
    context.Check(NormalizeWindowsImagePath("C:\\App\\") == "C:\\App",
                  "non-root trailing separator removed");
    context.Check(NormalizeWindowsImagePath("C:\\") == "C:\\", "drive root separator retained");
    context.Check(!NormalizeWindowsImagePath("relative\\app.exe").has_value(),
                  "relative path rejected");
    context.Check(!NormalizeWindowsImagePath("").has_value(), "empty path rejected");
    const std::string embedded_null{"C:\\App\0evil.exe", 15U};
    context.Check(!NormalizeWindowsImagePath(embedded_null).has_value(), "embedded NUL rejected");

    const auto invalid =
        Resolve({.pid = 8U, .generation = 8U}, {.image_path = "relative\\app.exe"});
    context.Check(invalid.error == IdentityResolutionError::InvalidImagePath,
                  "invalid supplied path is diagnostic");
}

} // namespace

int main() {
    TestContext context;
    CheckIdentityPriority(context);
    CheckSystemAndUnresolvedIdentity(context);
    CheckPathIdentitySeparation(context);
    CheckPathNormalization(context);
    return context.failures();
}
