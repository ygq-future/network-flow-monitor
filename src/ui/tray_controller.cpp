#include "network_flow_monitor/ui/tray_controller.h"

#include <algorithm>
#include <iterator>
#include <strsafe.h>

namespace nfm::ui {

bool WindowsTrayApi::ShellNotifyIcon(DWORD action, NOTIFYICONDATAW& data) noexcept {
    return Shell_NotifyIconW(action, &data) != FALSE;
}

HMENU WindowsTrayApi::CreatePopupMenu() noexcept {
    return ::CreatePopupMenu();
}

bool WindowsTrayApi::AppendMenu(HMENU menu, UINT flags, UINT_PTR command, LPCWSTR text) noexcept {
    return ::AppendMenuW(menu, flags, command, text) != FALSE;
}

UINT WindowsTrayApi::TrackPopupMenu(HMENU menu, UINT flags, int x, int y, HWND owner) noexcept {
    return ::TrackPopupMenu(menu, flags, x, y, 0, owner, nullptr);
}

bool WindowsTrayApi::DestroyMenu(HMENU menu) noexcept {
    return ::DestroyMenu(menu) != FALSE;
}

bool WindowsTrayApi::GetCursorPos(POINT* point) noexcept {
    return ::GetCursorPos(point) != FALSE;
}

bool WindowsTrayApi::SetForegroundWindow(HWND window) noexcept {
    return ::SetForegroundWindow(window) != FALSE;
}

TrayController::~TrayController() {
    Shutdown();
}

NOTIFYICONDATAW TrayController::BuildIconData() const noexcept {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = options_.icon_id;
    data.uCallbackMessage = options_.callback_message;
    data.hIcon = options_.icon;
    data.uFlags = NIF_MESSAGE | NIF_TIP;
    if (options_.icon != nullptr) {
        data.uFlags |= NIF_ICON;
    }

    static_cast<void>(StringCchCopyW(data.szTip, std::size(data.szTip), options_.tooltip.c_str()));
    return data;
}

bool TrayController::Initialize() noexcept {
    if (installed_) {
        return true;
    }
    auto data = BuildIconData();
    if (!api_->ShellNotifyIcon(NIM_ADD, data)) {
        return false;
    }
    data.uVersion =
        NOTIFYICON_VERSION_4; // NOLINT(cppcoreguidelines-pro-type-union-access): the Shell API
                              // requires this union member for NIM_SETVERSION.
    static_cast<void>(api_->ShellNotifyIcon(NIM_SETVERSION, data));
    installed_ = true;
    return true;
}

void TrayController::Shutdown() noexcept {
    if (!installed_) {
        return;
    }
    auto data = BuildIconData();
    static_cast<void>(api_->ShellNotifyIcon(NIM_DELETE, data));
    installed_ = false;
}

void TrayController::SetOwner(HWND owner) noexcept {
    if (owner_ == owner) {
        return;
    }
    const auto was_installed = installed_;
    if (was_installed) {
        Shutdown();
    }
    owner_ = owner;
    if (was_installed) {
        static_cast<void>(Initialize());
    }
}

bool TrayController::HandleCallback(TrayNotification notification) noexcept {
    if (notification.icon_id != options_.icon_id) {
        return false;
    }

    switch (notification.message) {
    case WM_LBUTTONUP:
        callbacks_->OnTrayLeftClick();
        return true;
    case WM_RBUTTONUP:
        callbacks_->OnTrayRightClick();
        return true;
    case WM_MOUSEMOVE:
        callbacks_->OnTrayHover();
        return true;
    default:
        return false;
    }
}

bool TrayController::ShowContextMenu() noexcept {
    POINT point{};
    if (!api_->GetCursorPos(&point)) {
        return false;
    }
    const auto menu = api_->CreatePopupMenu();
    if (menu == nullptr) {
        return false;
    }

    const auto settings_added = api_->AppendMenu(menu, MF_STRING, kSettingsCommand, L"Settings");
    const auto exit_added = api_->AppendMenu(menu, MF_STRING, kExitCommand, L"Exit");
    if (!settings_added || !exit_added) {
        static_cast<void>(api_->DestroyMenu(menu));
        return false;
    }

    static_cast<void>(api_->SetForegroundWindow(owner_));
    const auto command = api_->TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                              point.x, point.y, owner_);
    static_cast<void>(api_->DestroyMenu(menu));
    RouteMenuCommand(command);
    return true;
}

void TrayController::RouteMenuCommand(UINT command) noexcept {
    switch (command) {
    case kSettingsCommand:
        callbacks_->OnTraySettings();
        break;
    case kExitCommand:
        callbacks_->OnTrayExit();
        break;
    default:
        break;
    }
}

} // namespace nfm::ui
