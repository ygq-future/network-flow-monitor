#ifndef NETWORK_FLOW_MONITOR_UI_TRAY_CONTROLLER_H
#define NETWORK_FLOW_MONITOR_UI_TRAY_CONTROLLER_H

#include <windows.h>

#include <shellapi.h>
#include <string>
#include <utility>

namespace nfm::ui {

inline constexpr UINT kTrayCallbackMessage = WM_APP + 0x31U;

struct TrayIconOptions {
    UINT callback_message{kTrayCallbackMessage};
    UINT icon_id{1U};
    HICON icon{};
    std::wstring tooltip{L"Network Flow Monitor"};
};

class TrayCallbacks {
  public:
    TrayCallbacks() = default;
    virtual ~TrayCallbacks() = default;
    TrayCallbacks(const TrayCallbacks&) = delete;
    TrayCallbacks& operator=(const TrayCallbacks&) = delete;
    TrayCallbacks(TrayCallbacks&&) = delete;
    TrayCallbacks& operator=(TrayCallbacks&&) = delete;
    virtual void OnTrayLeftClick() noexcept = 0;
    virtual void OnTrayRightClick() noexcept = 0;
    virtual void OnTrayHover() noexcept = 0;
    virtual void OnTraySettings() noexcept = 0;
    virtual void OnTrayExit() noexcept = 0;
};

class TrayApi {
  public:
    TrayApi() = default;
    virtual ~TrayApi() = default;
    TrayApi(const TrayApi&) = delete;
    TrayApi& operator=(const TrayApi&) = delete;
    TrayApi(TrayApi&&) = delete;
    TrayApi& operator=(TrayApi&&) = delete;
    virtual bool ShellNotifyIcon(DWORD action, NOTIFYICONDATAW& data) noexcept = 0;
    virtual HMENU CreatePopupMenu() noexcept = 0;
    virtual bool AppendMenu(HMENU menu, UINT flags, UINT_PTR command, LPCWSTR text) noexcept = 0;
    virtual UINT TrackPopupMenu(HMENU menu, UINT flags, int x, int y, HWND owner) noexcept = 0;
    virtual bool DestroyMenu(HMENU menu) noexcept = 0;
    virtual bool GetCursorPos(POINT* point) noexcept = 0;
    virtual bool SetForegroundWindow(HWND window) noexcept = 0;
};

class WindowsTrayApi final : public TrayApi {
  public:
    bool ShellNotifyIcon(DWORD action, NOTIFYICONDATAW& data) noexcept override;
    HMENU CreatePopupMenu() noexcept override;
    bool AppendMenu(HMENU menu, UINT flags, UINT_PTR command, LPCWSTR text) noexcept override;
    UINT TrackPopupMenu(HMENU menu, UINT flags, int x, int y, HWND owner) noexcept override;
    bool DestroyMenu(HMENU menu) noexcept override;
    bool GetCursorPos(POINT* point) noexcept override;
    bool SetForegroundWindow(HWND window) noexcept override;
};

struct TrayNotification {
    UINT icon_id{};
    UINT message{};
};

class TrayController final {
  public:
    static constexpr UINT kSettingsCommand = 1U;
    static constexpr UINT kExitCommand = 2U;

    TrayController(HWND owner, TrayIconOptions options, TrayApi& api, TrayCallbacks& callbacks)
        : owner_{owner}, options_{std::move(options)}, api_{&api}, callbacks_{&callbacks} {}
    ~TrayController();
    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;
    TrayController(TrayController&&) = delete;
    TrayController& operator=(TrayController&&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool initialized() const noexcept {
        return installed_;
    }
    void SetOwner(HWND owner) noexcept;
    [[nodiscard]] bool HandleCallback(TrayNotification notification) noexcept;
    [[nodiscard]] bool ShowContextMenu() noexcept;

  private:
    [[nodiscard]] NOTIFYICONDATAW BuildIconData() const noexcept;
    void RouteMenuCommand(UINT command) noexcept;

    HWND owner_{};
    TrayIconOptions options_;
    TrayApi* api_;
    TrayCallbacks* callbacks_;
    bool installed_{};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_TRAY_CONTROLLER_H
