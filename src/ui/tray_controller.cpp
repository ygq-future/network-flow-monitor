#include "network_flow_monitor/ui/tray_controller.h"

#include "network_flow_monitor/ui/resources.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <strsafe.h>

namespace nfm::ui {
namespace {

class ProcessIcon final {
  public:
    explicit ProcessIcon(HICON icon) noexcept : icon_{icon} {}
    ~ProcessIcon() {
        if (icon_ != nullptr) {
            static_cast<void>(::DestroyIcon(icon_));
        }
    }
    ProcessIcon(const ProcessIcon&) = delete;
    ProcessIcon& operator=(const ProcessIcon&) = delete;
    ProcessIcon(ProcessIcon&&) = delete;
    ProcessIcon& operator=(ProcessIcon&&) = delete;
    [[nodiscard]] HICON get() const noexcept {
        return icon_;
    }

  private:
    HICON icon_{};
};

} // namespace

const std::array<std::uint32_t, kNetworkFlowMonitorIconPixelCount>&
NetworkFlowMonitorIconPixels() noexcept {
    static const auto pixels = [] {
        std::array<std::uint32_t, kNetworkFlowMonitorIconPixelCount> result{};
        for (std::uint32_t y = 0; y < kNetworkFlowMonitorIconSize; ++y) {
            for (std::uint32_t x = 0; x < kNetworkFlowMonitorIconSize; ++x) {
                const auto dx = static_cast<std::int32_t>(x) - 16;
                const auto dy = static_cast<std::int32_t>(y) - 16;
                const auto distance = dx * dx + dy * dy;
                std::uint32_t color = 0U;
                if (distance <= 225) {
                    color = distance >= 196 ? 0xFF1E3A8AU : 0xFF2563EBU;
                }

                const auto up_arrow =
                    (x >= 13U && x <= 18U && y >= 8U && y <= 21U) ||
                    (y >= 8U && y <= 12U && x >= 10U && x <= 21U && (x <= 13U || x >= 18U));
                const auto down_arrow =
                    (x >= 15U && x <= 20U && y >= 12U && y <= 24U) ||
                    (y >= 20U && y <= 24U && x >= 12U && x <= 23U && (x <= 15U || x >= 20U));
                if (up_arrow) {
                    color = 0xFFF8FAFCU;
                } else if (down_arrow) {
                    color = 0xFF34D399U;
                }
                result[(static_cast<std::size_t>(y) * kNetworkFlowMonitorIconSize) + x] = color;
            }
        }
        return result;
    }();
    return pixels;
}

HICON CreateNetworkFlowMonitorIcon(HINSTANCE instance) noexcept {
    static_cast<void>(instance);

    constexpr auto kSize = static_cast<int>(kNetworkFlowMonitorIconSize);
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

    std::copy(NetworkFlowMonitorIconPixels().begin(), NetworkFlowMonitorIconPixels().end(),
              static_cast<std::uint32_t*>(raw_pixels));

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

HICON NetworkFlowMonitorClassIcon(HINSTANCE instance) noexcept {
    static const ProcessIcon icon{CreateNetworkFlowMonitorIcon(instance)};
    return icon.get();
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

bool WindowsTrayApi::PostWindowMessage(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept {
    return ::PostMessageW(window, message, wparam, lparam) != FALSE;
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

bool TrayController::ShowContextMenu(HWND owner_override) noexcept {
    POINT point{};
    if (!api_->GetCursorPos(&point)) {
        return false;
    }
    const auto menu = api_->CreatePopupMenu();
    if (menu == nullptr) {
        return false;
    }

    const auto open_added =
        api_->AppendMenu(menu, MF_STRING, kOpenCommand,
                         ResourceCString(ResourceKey::MenuShowHideMonitor, options_.language));
    auto pin_flags = MF_STRING;
    if (pinned_) {
        pin_flags |= MF_CHECKED;
    }
    if (!pin_enabled_) {
        pin_flags |= MF_GRAYED;
    }
    const auto pin_added = api_->AppendMenu(menu, pin_flags, kPinCommand,
                                            ResourceCString(ResourceKey::Pin, options_.language));
    const auto retry_added =
        api_->AppendMenu(menu, MF_STRING, kRetryCollectorCommand,
                         ResourceCString(ResourceKey::MenuRetryCollector, options_.language));
    const auto settings_added =
        api_->AppendMenu(menu, MF_STRING, kSettingsCommand,
                         ResourceCString(ResourceKey::MenuSettings, options_.language));
    const auto exit_added = api_->AppendMenu(
        menu, MF_STRING, kExitCommand, ResourceCString(ResourceKey::MenuExit, options_.language));
    if (!open_added || !pin_added || !retry_added || !settings_added || !exit_added) {
        static_cast<void>(api_->DestroyMenu(menu));
        return false;
    }

    const auto owner = owner_override != nullptr ? owner_override : owner_;
    if (owner == nullptr || !api_->SetForegroundWindow(owner)) {
        static_cast<void>(api_->DestroyMenu(menu));
        return false;
    }
    const auto command =
        api_->TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, owner);
    // Required by the TrackPopupMenu foreground-owner contract so repeated menus and outside
    // clicks complete their native menu loop instead of waiting for unrelated pointer traffic.
    static_cast<void>(api_->PostWindowMessage(owner, WM_NULL, 0U, 0));
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
    case kPinCommand:
        if (pin_enabled_) {
            callbacks_->OnTrayPinToggle();
        }
        break;
    case kExitCommand:
        callbacks_->OnTrayExit();
        break;
    default:
        break;
    }
}

} // namespace nfm::ui
