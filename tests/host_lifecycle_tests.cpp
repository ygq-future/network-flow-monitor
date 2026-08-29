#include "network_flow_monitor/host/lifecycle/collector_companion.h"

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

void CheckCollectorCompanionInjections(TestContext& context) {
    using namespace nfm::host::lifecycle;

    CollectorCompanion companion;

    // 1. Check HelloAck injection triggers session_reset_cb
    bool session_reset_called = false;
    std::string instance_id_received;
    companion.set_session_reset_callback([&](const nfm::host::bridge::SessionResetDto& reset) {
        session_reset_called = true;
        instance_id_received = reset.collector_instance_id;
    });

    nfm::ipc::HelloAckMessage ack{
        .protocol_version = 1,
        .collector_instance_id = "test_inst_999",
        .collector_pid = 1234,
        .capabilities = {"traffic_tick_4hz"},
    };
    companion.inject_hello_ack(ack);

    context.Check(session_reset_called, "hello_ack injection calls session_reset_cb");
    context.Check(instance_id_received == "test_inst_999", "instance id matches");
    context.Check(companion.is_running(), "companion state becomes Running after HelloAck");

    // 2. Check TrafficTick injection
    bool traffic_tick_called = false;
    std::uint64_t dl_received = 0;
    companion.set_traffic_tick_callback([&](const nfm::host::bridge::TrafficTickDto& tick) {
        traffic_tick_called = true;
        dl_received = tick.download_bytes_per_sec;
    });

    nfm::ipc::TrafficTickMessage tick{
        .seq = 42,
        .monotonic_ms = 1000,
        .download_bytes_per_sec = 204800,
        .upload_bytes_per_sec = 102400,
    };
    companion.inject_traffic_tick(tick);

    context.Check(traffic_tick_called, "traffic_tick injection calls traffic_tick_cb");
    context.Check(dl_received == 204800, "download rate matches");

    // 3. Check CollectorStatus injection
    bool status_called = false;
    companion.set_collector_status_callback(
        [&](const nfm::host::bridge::CollectorStatusDto& status) {
            status_called = true;
            context.Check(status.active_pids == 8, "active pids match");
        });

    nfm::ipc::CollectorStatusMessage status{
        .monotonic_ms = 1000,
        .etw_session_active = true,
        .dropped_packets = 0,
        .active_pids = 8,
    };
    companion.inject_collector_status(status);

    context.Check(status_called, "collector_status injection calls callback");

    // 4. Test stop and restart state
    companion.stop();
    context.Check(!companion.is_running(), "companion not running after stop");
    context.Check(companion.get_info().state == CollectorState::Stopped, "state is Stopped");
}

} // namespace

int main() {
    TestContext context;
    CheckCollectorCompanionInjections(context);
    return context.failures();
}
