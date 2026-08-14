#include "network_flow_monitor/ui/floating_window.h"

#include "network_flow_monitor/ui/tray_controller.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <windowsx.h>

namespace nfm::ui {
namespace {

constexpr auto kFloatingWindowClass = std::to_array(L"NetworkFlowMonitor.FloatingWindow");
constexpr auto kFloatingWindowTitle = std::to_array(L"Network Flow Monitor");

[[nodiscard]] RECT ToNativeRect(PixelRect rect) noexcept {
    return RECT{.left = rect.left, .top = rect.top, .right = rect.right, .bottom = rect.bottom};
}

[[nodiscard]] PixelRect FromNativeRect(const RECT& rect) noexcept {
    return PixelRect{
        .left = rect.left, .top = rect.top, .right = rect.right, .bottom = rect.bottom};
}

} // namespace

FloatingWindow::~FloatingWindow() {
    Destroy();
}

bool FloatingWindow::RegisterWindowClass(HINSTANCE instance) noexcept {
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (::GetClassInfoExW(instance, kFloatingWindowClass.data(), &existing) != FALSE) {
        return true;
    }
    const auto icon = CreateNetworkFlowMonitorIcon(instance);
    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = CS_HREDRAW | CS_VREDRAW,
                                   .lpfnWndProc = &FloatingWindow::WindowProc,
                                   .cbClsExtra = 0,
                                   .cbWndExtra = 0,
                                   .hInstance = instance,
                                   .hIcon = icon,
                                   .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
                                   .hbrBackground = nullptr,
                                   .lpszMenuName = nullptr,
                                   .lpszClassName = kFloatingWindowClass.data(),
                                   .hIconSm = icon};
    if (::RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool FloatingWindow::Create() noexcept {
    if (window_ != nullptr) {
        return true;
    }
    if (instance_ == nullptr) {
        instance_ = ::GetModuleHandleW(nullptr);
    }
    if (!RegisterWindowClass(instance_)) {
        return false;
    }
    if (!positioned_) {
        PositionOnFirstShow();
        positioned_ = true;
    }

    window_ = ::CreateWindowExW(kExtendedStyle, kFloatingWindowClass.data(),
                                kFloatingWindowTitle.data(), WS_POPUP, rect_.left, rect_.top,
                                rect_.Width(), rect_.Height(), nullptr, nullptr, instance_, this);
    if (window_ != nullptr) {
        static_cast<void>(::SetLayeredWindowAttributes(window_, RGB(0, 0, 0), 0U, LWA_COLORKEY));
        ApplyHitWindowRect();
    }
    return window_ != nullptr;
}

void FloatingWindow::Show() noexcept {
    if (Create()) {
        ::ShowWindow(window_, SW_SHOWNOACTIVATE);
        if (hit_window_ != nullptr) {
            ::ShowWindow(hit_window_, SW_SHOWNOACTIVATE);
        }
    }
}

void FloatingWindow::Hide() noexcept {
    if (window_ != nullptr) {
        ::ShowWindow(window_, SW_HIDE);
    }
    if (hit_window_ != nullptr) {
        ::ShowWindow(hit_window_, SW_HIDE);
    }
}

void FloatingWindow::Destroy() noexcept {
    target_.Reset();
    if (hit_window_ != nullptr) {
        auto* const hit_window = hit_window_;
        hit_window_ = nullptr;
        ::DestroyWindow(hit_window);
    }
    if (window_ != nullptr) {
        auto* const window = window_;
        window_ = nullptr;
        ::DestroyWindow(window);
    }
}

void FloatingWindow::SetSnapshot(RankingSnapshotPtr snapshot, std::uint64_t generation) noexcept {
    if (generation_ == generation && snapshot_.get() == snapshot.get()) {
        return;
    }
    snapshot_ = std::move(snapshot);
    generation_ = generation;
    Invalidate();
}

void FloatingWindow::SetInteractionState(InteractionState state) noexcept {
    if (interaction_state_ == state) {
        return;
    }
    interaction_state_ = state;
    Invalidate();
}

void FloatingWindow::SetDisplayOptions(WindowDisplayOptions options) noexcept {
    if (display_options_ == options) {
        return;
    }
    display_options_ = options;
    Invalidate();
}

void FloatingWindow::SetMonitorPanelPresentation(MonitorPanelSurface surface,
                                                 CapsulePresentation capsule,
                                                 PixelRect visual_bounds,
                                                 PixelRect hit_bounds) noexcept {
    panel_surface_ = surface;
    capsule_ = capsule;
    visual_bounds_ = visual_bounds;
    hit_bounds_ = hit_bounds;
    ApplyHitWindowRect();
    Invalidate();
}

void FloatingWindow::ApplyWindowRect(PixelRect rect) noexcept {
    const auto size_changed = rect.Width() != rect_.Width() || rect.Height() != rect_.Height();
    rect_ = rect;
    if (size_changed) {
        target_.Reset();
    }
    if (window_ != nullptr) {
        ::SetWindowPos(window_, HWND_TOPMOST, rect.left, rect.top, rect.Width(), rect.Height(),
                       SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    ApplyHitWindowRect();
    observer_->OnWindowMoved(WindowKind::Floating);
    Invalidate();
}

void FloatingWindow::ApplyHitWindowRect() noexcept {
    PixelRect extension{};
    if (panel_surface_ == MonitorPanelSurface::Capsule) {
        if (hit_bounds_.left < visual_bounds_.left) {
            extension = {.left = hit_bounds_.left,
                         .top = hit_bounds_.top,
                         .right = visual_bounds_.left,
                         .bottom = hit_bounds_.bottom};
        } else if (hit_bounds_.right > visual_bounds_.right) {
            extension = {.left = visual_bounds_.right,
                         .top = hit_bounds_.top,
                         .right = hit_bounds_.right,
                         .bottom = hit_bounds_.bottom};
        }
    }
    if (extension.Width() <= 0 || extension.Height() <= 0 || window_ == nullptr) {
        if (hit_window_ != nullptr) {
            ::ShowWindow(hit_window_, SW_HIDE);
        }
        return;
    }
    if (hit_window_ == nullptr) {
        hit_window_ = ::CreateWindowExW(kExtendedStyle, kFloatingWindowClass.data(), L"", WS_POPUP,
                                        extension.left, extension.top, extension.Width(),
                                        extension.Height(), nullptr, nullptr, instance_, this);
        if (hit_window_ != nullptr) {
            // A separate almost-transparent layered HWND provides a real hit-test surface without
            // making color-key-transparent pixels responsible for pointer delivery.
            static_cast<void>(::SetLayeredWindowAttributes(hit_window_, 0U, 1U, LWA_ALPHA));
        }
    } else {
        ::SetWindowPos(hit_window_, HWND_TOPMOST, extension.left, extension.top, extension.Width(),
                       extension.Height(), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (hit_window_ != nullptr && ::IsWindowVisible(window_) != FALSE) {
        ::ShowWindow(hit_window_, SW_SHOWNOACTIVATE);
    }
}

void FloatingWindow::Reposition(PixelRect rect) noexcept {
    if (rect_ == rect) {
        return;
    }
    ApplyWindowRect(rect);
}

bool FloatingWindow::ShouldInvalidate(std::uint64_t old_generation, std::uint64_t new_generation,
                                      InteractionState old_state,
                                      InteractionState new_state) noexcept {
    return old_generation != new_generation || old_state != new_state;
}

PixelRect FloatingWindow::ApplyDpiSuggestion(PixelRect suggested, PixelRect work_area) noexcept {
    return ClampRectToWorkArea(suggested, work_area);
}

void FloatingWindow::Invalidate() noexcept {
    if (window_ != nullptr) {
        static_cast<void>(::InvalidateRect(window_, nullptr, FALSE));
    }
    observer_->OnWindowInvalidated(WindowKind::Floating);
}

// NOLINTNEXTLINE(bugprone-exception-escape): formatting allocations are caught before drawing.
void FloatingWindow::Paint() noexcept {
    PAINTSTRUCT paint{};
    auto* const device_context = ::BeginPaint(window_, &paint);
    if (device_context == nullptr) {
        return;
    }

    RECT client_rect{};
    ::GetClientRect(window_, &client_rect);
    const auto size = PixelSize{.width = client_rect.right - client_rect.left,
                                .height = client_rect.bottom - client_rect.top};
    if (target_ == nullptr) {
        target_ = resources_->CreateHwndRenderTarget(window_, size);
    }
    if (target_ != nullptr && resources_->text_format() != nullptr) {
        std::wstring title = L"Network Flow Monitor";
        std::wstring status;
        try {
            if (interaction_state_ == InteractionState::DisconnectedOverlay) {
                status = L"Collector offline - right-click to retry";
            } else if (snapshot_ == nullptr) {
                status = L"Starting...";
            } else if (snapshot_->status == ranking::RankingStatus::WarmingUp) {
                status = L"Synchronizing...";
            } else {
                status = FormatTotalRate(snapshot_->total_rate, display_options_);
            }
        } catch (...) {
            status = L"Data unavailable";
        }

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush;
        const auto opacity = static_cast<float>(display_options_.opacity_percent) / 100.0F;
        if (SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.05F, 0.10F, 0.18F, opacity),
                                                     background_brush.GetAddressOf())) &&
            SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.94F, 0.97F, 1.0F, 1.0F),
                                                     text_brush.GetAddressOf()))) {
            target_->BeginDraw();
            target_->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 1.0F));
            const D2D1_RECT_F background{
                .left = static_cast<float>(visual_bounds_.left - rect_.left),
                .top = static_cast<float>(visual_bounds_.top - rect_.top),
                .right = static_cast<float>(visual_bounds_.right - rect_.left),
                .bottom = static_cast<float>(visual_bounds_.bottom - rect_.top)};
            const auto radius = panel_surface_ == MonitorPanelSurface::Capsule
                                    ? static_cast<float>(visual_bounds_.Width()) / 2.0F
                                    : 12.0F;
            target_->FillRoundedRectangle(D2D1::RoundedRect(background, radius, radius),
                                          background_brush.Get());
            if (panel_surface_ == MonitorPanelSurface::Capsule) {
                const auto color = [](std::uint32_t rgb) {
                    return D2D1::ColorF(static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F,
                                        static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F,
                                        static_cast<float>(rgb & 0xFFU) / 255.0F, 1.0F);
                };
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> download_brush;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> upload_brush;
                if (SUCCEEDED(target_->CreateSolidColorBrush(color(capsule_.download.color),
                                                             download_brush.GetAddressOf())) &&
                    SUCCEEDED(target_->CreateSolidColorBrush(color(capsule_.upload.color),
                                                             upload_brush.GetAddressOf()))) {
                    const auto half_height = (background.bottom - background.top) / 2.0F;
                    const auto download_height = half_height * capsule_.download.fill;
                    const auto upload_height = half_height * capsule_.upload.fill;
                    target_->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(background.left, background.top,
                                                      background.right,
                                                      background.top + download_height),
                                          radius, radius),
                        download_brush.Get());
                    target_->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(background.left,
                                                      background.bottom - upload_height,
                                                      background.right, background.bottom),
                                          radius, radius),
                        upload_brush.Get());
                }
            } else {
                const D2D1_RECT_F title_rect{
                    .left = 10.0F, .top = 7.0F, .right = 310.0F, .bottom = 28.0F};
                target_->DrawTextW(title.data(), static_cast<UINT32>(title.size()),
                                   resources_->text_format(), title_rect, text_brush.Get());
                const D2D1_RECT_F status_rect{
                    .left = 10.0F, .top = 34.0F, .right = 310.0F, .bottom = 58.0F};
                target_->DrawTextW(status.data(), static_cast<UINT32>(status.size()),
                                   resources_->text_format(), status_rect, text_brush.Get());
            }
            if (RenderResources::EndDraw(*target_.Get()) == DrawResult::RecreateTarget) {
                target_.Reset();
            }
        }
    }
    static_cast<void>(device_context);
    ::EndPaint(window_, &paint);
}

void FloatingWindow::PositionOnFirstShow() noexcept {
    RECT work{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) == FALSE) {
        return;
    }
    constexpr auto kEdgeMargin = 24;
    constexpr auto kTopMargin = 64;
    const auto width = rect_.Width();
    const auto height = rect_.Height();
    rect_.right = static_cast<std::int32_t>(work.right) - kEdgeMargin;
    rect_.left = rect_.right - width;
    rect_.top = static_cast<std::int32_t>(work.top) + kTopMargin;
    rect_.bottom = rect_.top + height;
}

LRESULT CALLBACK FloatingWindow::WindowProc(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): GWLP_USERDATA is the Win32 pointer storage ABI.
    auto* self = reinterpret_cast<FloatingWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 ABI.
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<FloatingWindow*>(create->lpCreateParams);
        // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 ABI.
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self->window_ == nullptr) {
            self->window_ = window;
        } else {
            self->hit_window_ = window;
        }
    }
    if (self == nullptr) {
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_LBUTTONDOWN: {
        POINT cursor{};
        if (::GetCursorPos(&cursor) != FALSE) {
            self->panel_pressing_ = true;
            static_cast<void>(::SetCapture(window));
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelPress,
                                                 {.x = cursor.x, .y = cursor.y});
        }
    }
        return 0;
    case WM_MOUSEMOVE:
        if (self->panel_pressing_ && (wparam & MK_LBUTTON) != 0U) {
            POINT cursor{};
            if (::GetCursorPos(&cursor) != FALSE) {
                self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PointerMove,
                                                     {.x = cursor.x, .y = cursor.y});
            }
            return 0;
        }
        if (!self->tracking_mouse_) {
            TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                                     .dwFlags = TME_LEAVE,
                                     .hwndTrack = window,
                                     .dwHoverTime = 0U};
            static_cast<void>(::TrackMouseEvent(&tracking));
            self->tracking_mouse_ = true;
            if (self->panel_surface_ == MonitorPanelSurface::Capsule) {
                self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::CapsulePointerEnter,
                                                     {});
            } else {
                self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelPointerEnter, {});
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (self->panel_pressing_) {
            POINT cursor{};
            static_cast<void>(::GetCursorPos(&cursor));
            self->panel_pressing_ = false;
            if (::GetCapture() == window) {
                ::ReleaseCapture();
            }
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelRelease,
                                                 {.x = cursor.x, .y = cursor.y});
        }
        return 0;
    case WM_RBUTTONUP:
        self->observer_->OnWindowContextMenu(WindowKind::Floating);
        return 0;
    case WM_CAPTURECHANGED:
        if (self->panel_pressing_) {
            self->panel_pressing_ = false;
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::DragCancelled, {});
        }
        return 0;
    case WM_MOUSELEAVE:
        self->tracking_mouse_ = false;
        if (self->panel_surface_ == MonitorPanelSurface::Capsule) {
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::CapsulePointerLeave, {});
        } else {
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelPointerLeave, {});
        }
        return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::EnvironmentChanged, {});
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (window == self->window_) {
            self->Paint();
        } else {
            PAINTSTRUCT paint{};
            static_cast<void>(::BeginPaint(window, &paint));
            ::EndPaint(window, &paint);
        }
        return 0;
    case WM_NCDESTROY:
        if (window == self->window_) {
            self->window_ = nullptr;
        } else if (window == self->hit_window_) {
            self->hit_window_ = nullptr;
        }
        self->tracking_mouse_ = false;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace nfm::ui
