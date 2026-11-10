#ifndef NETWORK_FLOW_MONITOR_TESTS_TEST_HEADLESS_RUNTIME_H
#define NETWORK_FLOW_MONITOR_TESTS_TEST_HEADLESS_RUNTIME_H

#include <windows.h>

// Every test process runs on a dedicated desktop created before main() so that test windows and
// popup menus are never presented on the interactive desktop. Threads created by the test runner
// do not inherit the creator's desktop, so any thread that creates a window or a popup menu must
// call enter_headless_test_desktop() first.
[[nodiscard]] HDESK headless_test_desktop() noexcept;
[[nodiscard]] bool is_on_headless_test_desktop() noexcept;
[[nodiscard]] bool enter_headless_test_desktop() noexcept;

#endif // NETWORK_FLOW_MONITOR_TESTS_TEST_HEADLESS_RUNTIME_H
