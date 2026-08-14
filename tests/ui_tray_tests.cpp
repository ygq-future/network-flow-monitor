#include "network_flow_monitor/ui/render_resources.h"
#include "network_flow_monitor/ui/tray_controller.h"

#include <windows.h>

#include <iostream>
#include <string_view>

namespace {

using nfm::ui::CreateNetworkFlowMonitorIcon;
using nfm::ui::DrawResult;
using nfm::ui::RenderResources;
using nfm::ui::TrayApi;
using nfm::ui::TrayCallbacks;
using nfm::ui::TrayController;
using nfm::ui::TrayIconOptions;

struct FakeTrayApi final : TrayApi {
    bool add_result{true};
    bool delete_result{true};
    UINT menu_command{};
    int shell_add_calls{};
    int shell_delete_calls{};
    int menu_create_calls{};
    int menu_destroy_calls{};
    int track_menu_calls{};
    int foreground_calls{};
    UINT track_menu_flags{};
    HWND foreground_window{};
    HWND track_menu_owner{};
    HWND posted_window{};
    UINT posted_message{};

    bool ShellNotifyIcon(DWORD action, NOTIFYICONDATAW&) noexcept override {
        if (action == NIM_ADD) {
            ++shell_add_calls;
            return add_result;
        }
        if (action == NIM_DELETE) {
            ++shell_delete_calls;
            return delete_result;
        }
        return false;
    }

    HMENU CreatePopupMenu() noexcept override {
        ++menu_create_calls;
        return reinterpret_cast<HMENU>(this);
    }

    bool AppendMenu(HMENU, UINT, UINT_PTR, LPCWSTR) noexcept override {
        return true;
    }

    UINT TrackPopupMenu(HMENU, UINT flags, int, int, HWND owner) noexcept override {
        ++track_menu_calls;
        track_menu_flags = flags;
        track_menu_owner = owner;
        return menu_command;
    }

    bool PostWindowMessage(HWND window, UINT message, WPARAM, LPARAM) noexcept override {
        posted_window = window;
        posted_message = message;
        return true;
    }

    bool DestroyMenu(HMENU) noexcept override {
        ++menu_destroy_calls;
        return true;
    }

    bool GetCursorPos(POINT* point) noexcept override {
        point->x = 100;
        point->y = 200;
        return true;
    }

    bool SetForegroundWindow(HWND window) noexcept override {
        ++foreground_calls;
        foreground_window = window;
        return true;
    }
};

struct FakeCallbacks final : TrayCallbacks {
    int left_clicks{};
    int right_clicks{};
    int hover_events{};
    int settings_commands{};
    int exit_commands{};
    int retry_commands{};

    void OnTrayLeftClick() noexcept override {
        ++left_clicks;
    }
    void OnTrayRightClick() noexcept override {
        ++right_clicks;
    }
    void OnTrayHover() noexcept override {
        ++hover_events;
    }
    void OnTraySettings() noexcept override {
        ++settings_commands;
    }
    void OnTrayRetryCollector() noexcept override {
        ++retry_commands;
    }
    void OnTrayExit() noexcept override {
        ++exit_commands;
    }
};

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

TrayController MakeController(FakeTrayApi& api, FakeCallbacks& callbacks,
                              const TrayIconOptions& options = {}) {
    return TrayController{nullptr, TrayIconOptions{options}, api, callbacks};
}

void CheckTrayLifecycle(TestContext& context) {
    FakeTrayApi api;
    FakeCallbacks callbacks;
    auto controller = MakeController(api, callbacks);
    context.Check(controller.Initialize(), "tray initialize succeeds");
    context.Check(controller.Initialize(), "tray initialize is idempotent");
    context.Check(api.shell_add_calls == 1, "one shell add call");
    controller.Shutdown();
    controller.Shutdown();
    context.Check(api.shell_delete_calls == 1, "one shell delete call");
}

void CheckTrayCallbacks(TestContext& context) {
    FakeTrayApi api;
    FakeCallbacks callbacks;
    auto controller = MakeController(api, callbacks);
    static_cast<void>(controller.Initialize());
    context.Check(controller.HandleCallback({.icon_id = 1U, .message = WM_LBUTTONUP}),
                  "left callback consumed");
    context.Check(controller.HandleCallback({.icon_id = 1U, .message = WM_RBUTTONUP}),
                  "right callback consumed");
    context.Check(controller.HandleCallback({.icon_id = 1U, .message = WM_MOUSEMOVE}),
                  "hover callback consumed");
    context.Check(callbacks.left_clicks == 1 && callbacks.right_clicks == 1 &&
                      callbacks.hover_events == 1,
                  "tray callbacks routed");
    context.Check(!controller.HandleCallback({.icon_id = 2U, .message = WM_LBUTTONUP}),
                  "other icon ignored");
}

void CheckTrayMenu(TestContext& context) {
    FakeTrayApi api;
    api.menu_command = TrayController::kSettingsCommand;
    FakeCallbacks callbacks;
    auto controller = MakeController(api, callbacks);
    const auto surface_owner = reinterpret_cast<HWND>(&context);
    context.Check(controller.ShowContextMenu(surface_owner), "context menu shown");
    context.Check(callbacks.settings_commands == 1 && callbacks.exit_commands == 0,
                  "settings command routed");
    context.Check(api.menu_create_calls == 1 && api.track_menu_calls == 1 &&
                      api.menu_destroy_calls == 1,
                  "menu lifecycle completed");
    context.Check((api.track_menu_flags & TPM_RIGHTBUTTON) == 0U,
                  "right click cannot activate a context-menu command");
    context.Check(api.foreground_window == surface_owner && api.track_menu_owner == surface_owner &&
                      api.posted_window == surface_owner && api.posted_message == WM_NULL,
                  "application menus use the clicked visible surface as foreground owner and "
                  "complete with the documented benign message");

    api.menu_command = TrayController::kRetryCollectorCommand;
    context.Check(controller.ShowContextMenu(surface_owner), "retry context menu shown");
    context.Check(callbacks.retry_commands == 1, "retry command routed");

    api.menu_command = TrayController::kOpenCommand;
    context.Check(controller.ShowContextMenu(surface_owner), "open context menu shown");
    context.Check(callbacks.left_clicks == 1, "open command routes to left click");
}

void CheckFailuresAndDrawClassification(TestContext& context) {
    FakeTrayApi api;
    api.add_result = false;
    FakeCallbacks callbacks;
    auto controller = MakeController(api, callbacks);
    context.Check(!controller.Initialize(), "unsupported shell failure is reported");
    context.Check(RenderResources::ClassifyEndDraw(S_OK) == DrawResult::Complete,
                  "successful draw classified");
    context.Check(RenderResources::ClassifyEndDraw(D2DERR_RECREATE_TARGET) ==
                      DrawResult::RecreateTarget,
                  "recreate-target draw classified");
    context.Check(RenderResources::ClassifyEndDraw(E_FAIL) == DrawResult::Failed,
                  "failed draw classified");
}

void CheckApplicationIcon(TestContext& context) {
    const auto icon = CreateNetworkFlowMonitorIcon(nullptr);
    context.Check(icon != nullptr, "network monitor icon is created");
    if (icon != nullptr) {
        static_cast<void>(DestroyIcon(icon));
    }
}

} // namespace

int main() noexcept {
    TestContext context;
    CheckTrayLifecycle(context);
    CheckTrayCallbacks(context);
    CheckTrayMenu(context);
    CheckFailuresAndDrawClassification(context);
    CheckApplicationIcon(context);
    return context.failures == 0 ? 0 : 1;
}
