#include "network_flow_monitor/ui/tray_controller.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <strsafe.h>

namespace nfm::ui {

HICON CreateNetworkFlowMonitorIcon(HINSTANCE instance) noexcept {
    static_cast<void>(instance);

    constexpr int kSize = 32;
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = kSize;
    bitmap_info.bmiHeader.biHeight = -kSize;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* raw_pixels = nullptr;
    const auto color_bitmap =
        ::CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS, &raw_pixels, nullptr, 0U);
    if (color_bitmap == nullptr || raw_pixels == nullptr) {
        if (color_bitmap != nullptr) {
            static_cast<void>(::DeleteObject(color_bitmap));
        }
        return nullptr;
    }

    auto* const pixels = static_cast<std::uint32_t*>(raw_pixels);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const auto dx = x - 16;
            const auto dy = y - 16;
            const auto distance = dx * dx + dy * dy;
            std::uint32_t color = 0U;
            if (distance <= 225) {
                color = 0xFF2563EBU;
                if (distance >= 196) {
                    color = 0xFF1E3A8AU;
                }
            }

            const auto up_arrow = (x >= 13 && x <= 18 && y >= 8 && y <= 21) ||
                                  (y >= 8 && y <= 12 && x >= 10 && x <= 21 && (x <= 13 || x >= 18));
            const auto down_arrow =
                (x >= 15 && x <= 20 && y >= 12 && y <= 24) ||
                (y >= 20 && y <= 24 && x >= 12 && x <= 23 && (x <= 15 || x >= 20));
            if (up_arrow) {
                color = 0xFFF8FAFCU;
            } else if (down_arrow) {
                color = 0xFF34D399U;
            }
            pixels[(y * kSize) + x] = color;
        }
    }

    const auto mask_bitmap = ::CreateBitmap(kSize, kSize, 1U, 1U, nullptr);
    if (mask_bitmap == nullptr) {
        static_cast<void>(::DeleteObject(color_bitmap));
        return nullptr;
    }

    ICONINFO icon_info{.fIcon = TRUE,
                       .xHotspot = 0U,
                       .yHotspot = 0U,
                       .hbmMask = mask_bitmap,
                       .hbmColor = color_bitmap};
    const auto icon = ::CreateIconIndirect(&icon_info);
    static_cast<void>(::DeleteObject(mask_bitmap));
    static_cast<void>(::DeleteObject(color_bitmap));
    return icon;
}

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
    data.uFlags = NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
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
    case WM_CONTEXTMENU:
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

    const auto open_added = api_->AppendMenu(menu, MF_STRING, kOpenCommand, L"Show/hide monitor");
    const auto retry_added =
        api_->AppendMenu(menu, MF_STRING, kRetryCollectorCommand, L"Retry collector");
    const auto settings_added = api_->AppendMenu(menu, MF_STRING, kSettingsCommand, L"Settings");
    const auto exit_added = api_->AppendMenu(menu, MF_STRING, kExitCommand, L"Exit");
    if (!open_added || !retry_added || !settings_added || !exit_added) {
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
    case kRetryCollectorCommand:
        callbacks_->OnTrayRetryCollector();
        break;
    case kOpenCommand:
        callbacks_->OnTrayLeftClick();
        break;
    case kExitCommand:
        callbacks_->OnTrayExit();
        break;
    default:
        break;
    }
}

} // namespace nfm::ui
