#include "network_flow_monitor/app/collector_runtime.h"

#include <windows.h>

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

void CheckPrincipal(TestContext& context) {
    const auto portable =
        nfm::app::BuildCollectorPrincipal(nfm::ipc::CollectorEndpointMode::Portable);
    context.Check(portable.ok() && nfm::ipc::ValidateAuthorizedPrincipal(portable.principal) ==
                                       nfm::ipc::PipeSecurityError::None,
                  "portable runtime builds a valid owner-scoped principal");
    context.Check(portable.principal.mode == nfm::ipc::CollectorEndpointMode::Portable &&
                      !portable.principal.authorized_sid.empty() &&
                      !portable.principal.server_logon_sid.empty(),
                  "portable principal carries the current logon SID");

    const auto service =
        nfm::app::BuildCollectorPrincipal(nfm::ipc::CollectorEndpointMode::Service);
    context.Check(service.ok() && nfm::ipc::ValidateAuthorizedPrincipal(service.principal) ==
                                      nfm::ipc::PipeSecurityError::None,
                  "service runtime builds a valid users-scoped principal");
    context.Check(service.principal.mode == nfm::ipc::CollectorEndpointMode::Service &&
                      !service.principal.authorized_sid.empty() &&
                      service.principal.server_logon_sid.empty(),
                  "service principal does not require a logon SID");

    const auto invalid =
        nfm::app::BuildCollectorPrincipal(nfm::ipc::CollectorEndpointMode::Invalid);
    context.Check(!invalid.ok() && invalid.source_error == ERROR_INVALID_PARAMETER,
                  "invalid endpoint mode is classified before runtime construction");
}

void CheckInstanceIdentity(TestContext& context) {
    const auto first = nfm::app::CreateCollectorInstanceId();
    const auto second = nfm::app::CreateCollectorInstanceId();
    context.Check(first != nfm::ipc::CollectorInstanceId{} &&
                      second != nfm::ipc::CollectorInstanceId{},
                  "runtime instances are non-zero");
    context.Check(first != second, "successive runtime instances are distinct");
}

} // namespace

int main() {
    TestContext context;
    CheckPrincipal(context);
    CheckInstanceIdentity(context);
    return context.failures == 0 ? 0 : 1;
}
