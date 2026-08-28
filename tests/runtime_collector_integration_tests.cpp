#include "network_flow_monitor/app/collector_runtime.h"

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

} // namespace

int main() {
    TestContext context;
    const auto principal = nfm::app::BuildCollectorPrincipal();
    context.Check(principal.ok() && nfm::ipc::ValidateAuthorizedPrincipal(principal.principal) ==
                                        nfm::ipc::PipeSecurityError::None,
                  "runtime builds a valid owner-scoped principal");
    context.Check(!principal.principal.authorized_sid.empty() &&
                      !principal.principal.server_logon_sid.empty(),
                  "principal carries the current logon SID");

    const auto first = nfm::app::CreateCollectorInstanceId();
    const auto second = nfm::app::CreateCollectorInstanceId();
    context.Check(!first.empty() && !second.empty(), "runtime instances are non-empty");
    context.Check(first != second, "successive runtime instances are distinct");
    return context.failures == 0 ? 0 : 1;
}
