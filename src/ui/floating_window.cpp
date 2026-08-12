#include "network_flow_monitor/ui/floating_window.h"

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
    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = CS_HREDRAW | CS_VREDRAW,
                                   .lpfnWndProc = &FloatingWindow::WindowProc,
                                   .cbClsExtra = 0,
                                   .cbWndExtra = 0,
                                   .hInstance = instance,
                                   .hIcon = nullptr,
                                   .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
                                   .hbrBackground = nullptr,
                                   .lpszMenuName = nullptr,
                                   .lpszClassName = kFloatingWindowClass.data(),
                                   .hIconSm = nullptr};
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

    window_ = ::CreateWindowExW(kExtendedStyle, kFloatingWindowClass.data(),
                                kFloatingWindowTitle.data(), WS_POPUP, rect_.left, rect_.top,
                                rect_.Width(), rect_.Height(), nullptr, nullptr, instance_, this);
    return window_ != nullptr;
}

void FloatingWindow::Show() noexcept {
    if (Create()) {
        ::ShowWindow(window_, SW_SHOWNOACTIVATE);
    }
}

void FloatingWindow::Hide() noexcept {
    if (window_ != nullptr) {
        ::ShowWindow(window_, SW_HIDE);
    }
}

void FloatingWindow::Destroy() noexcept {
    target_.Reset();
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

void FloatingWindow::SetSnapDistance(std::int32_t snap_distance_dip) noexcept {
    snap_distance_dip_ = std::max<std::int32_t>(0, snap_distance_dip);
}

void FloatingWindow::ApplyWindowRect(PixelRect rect) noexcept {
    rect_ = rect;
    if (window_ != nullptr) {
        ::SetWindowPos(window_, HWND_TOPMOST, rect.left, rect.top, rect.Width(), rect.Height(),
                       SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    Invalidate();
}

void FloatingWindow::SetEdgeHidden(bool hidden, std::int32_t reveal_strip_dip,
                                   PixelRect work_area) noexcept {
    if (hidden == edge_hidden_ && hidden) {
        return;
    }
    if (!hidden) {
        if (!edge_hidden_) {
            return;
        }
        edge_hidden_ = false;
        ApplyWindowRect(normal_rect_);
        return;
    }

    normal_rect_ = rect_;
    const auto dpi = window_ == nullptr ? 96U : ::GetDpiForWindow(window_);
    const auto strip = std::max<std::int32_t>(
        1,
        static_cast<std::int32_t>(
            (static_cast<long long>(std::max<std::int32_t>(1, reveal_strip_dip)) * dpi + 95) / 96));
    const auto hidden_rect = ComputeEdgeHiddenRect(normal_rect_, work_area, strip);
    edge_hidden_ = true;
    ApplyWindowRect(hidden_rect);
}

PixelRect FloatingWindow::ComputeEdgeHiddenRect(PixelRect normal_rect, PixelRect work_area,
                                                std::int32_t reveal_strip_pixels) noexcept {
    const auto left_distance = std::llabs(static_cast<long long>(normal_rect.left) -
                                          static_cast<long long>(work_area.left));
    const auto right_distance = std::llabs(static_cast<long long>(work_area.right) -
                                           static_cast<long long>(normal_rect.right));
    const auto top_distance =
        std::llabs(static_cast<long long>(normal_rect.top) - static_cast<long long>(work_area.top));
    const auto bottom_distance = std::llabs(static_cast<long long>(work_area.bottom) -
                                            static_cast<long long>(normal_rect.bottom));
    const auto strip = std::max<std::int32_t>(1, reveal_strip_pixels);
    PixelRect hidden_rect = normal_rect;
    if (left_distance <= right_distance && left_distance <= top_distance &&
        left_distance <= bottom_distance) {
        hidden_rect.left = work_area.left;
        hidden_rect.right = std::min(work_area.right, work_area.left + strip);
    } else if (right_distance <= top_distance && right_distance <= bottom_distance) {
        hidden_rect.left = std::max(work_area.left, work_area.right - strip);
        hidden_rect.right = work_area.right;
    } else if (top_distance <= bottom_distance) {
        hidden_rect.top = work_area.top;
        hidden_rect.bottom = std::min(work_area.bottom, work_area.top + strip);
    } else {
        hidden_rect.top = std::max(work_area.top, work_area.bottom - strip);
        hidden_rect.bottom = work_area.bottom;
    }
    return hidden_rect;
}

void FloatingWindow::Reposition(PixelRect rect) noexcept {
    if (rect_ == rect) {
        return;
    }
    if (!edge_hidden_) {
        normal_rect_ = rect;
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

void FloatingWindow::HandleDpiChanged(LPARAM lparam) noexcept {
    if (lparam == 0) {
        return;
    }
    const auto* suggested =
        reinterpret_cast<const RECT*>(lparam); // NOLINT(performance-no-int-to-ptr): WM_DPICHANGED
                                               // carries a RECT pointer in LPARAM.
    rect_ = FromNativeRect(*suggested);
    if (window_ != nullptr) {
        ::SetWindowPos(window_, nullptr, rect_.left, rect_.top, rect_.Width(), rect_.Height(),
                       SWP_NOACTIVATE | SWP_NOZORDER);
    }
    Invalidate();
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
            if (snapshot_ == nullptr ||
                interaction_state_ == InteractionState::DisconnectedOverlay) {
                status = L"Disconnected";
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
            target_->Clear(D2D1::ColorF(0.02F, 0.04F, 0.08F, 1.0F));
            const D2D1_RECT_F background{.left = 0.0F,
                                         .top = 0.0F,
                                         .right = static_cast<float>(size.width),
                                         .bottom = static_cast<float>(size.height)};
            target_->FillRectangle(background, background_brush.Get());
            const D2D1_RECT_F title_rect{
                .left = 10.0F, .top = 7.0F, .right = 310.0F, .bottom = 28.0F};
            target_->DrawTextW(title.data(), static_cast<UINT32>(title.size()),
                               resources_->text_format(), title_rect, text_brush.Get());
            const D2D1_RECT_F status_rect{
                .left = 10.0F, .top = 34.0F, .right = 310.0F, .bottom = 58.0F};
            target_->DrawTextW(status.data(), static_cast<UINT32>(status.size()),
                               resources_->text_format(), status_rect, text_brush.Get());
            if (RenderResources::EndDraw(*target_.Get()) == DrawResult::RecreateTarget) {
                target_.Reset();
            }
        }
    }
    static_cast<void>(device_context);
    ::EndPaint(window_, &paint);
}

void FloatingWindow::HandleMouseMove(LPARAM /*lparam*/) noexcept {
    if (!dragging_) {
        return;
    }
    POINT cursor{};
    if (::GetCursorPos(&cursor) == FALSE) {
        return;
    }
    const auto dx = cursor.x - drag_anchor_.x;
    const auto dy = cursor.y - drag_anchor_.y;
    ApplyWindowRect(PixelRect{.left = drag_origin_.left + dx,
                              .top = drag_origin_.top + dy,
                              .right = drag_origin_.right + dx,
                              .bottom = drag_origin_.bottom + dy});
    normal_rect_ = rect_;
}

void FloatingWindow::SnapToWorkArea() noexcept {
    auto* const monitor = ::MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{.cbSize = sizeof(MONITORINFO)};
    if (monitor == nullptr || ::GetMonitorInfoW(monitor, &monitor_info) == FALSE) {
        return;
    }
    const auto work_area = PixelRect{.left = monitor_info.rcWork.left,
                                     .top = monitor_info.rcWork.top,
                                     .right = monitor_info.rcWork.right,
                                     .bottom = monitor_info.rcWork.bottom};
    const auto dpi = window_ == nullptr ? 96U : ::GetDpiForWindow(window_);
    const auto snap = std::max<std::int32_t>(
        0,
        static_cast<std::int32_t>(((static_cast<long long>(snap_distance_dip_) * dpi) + 95) / 96));
    auto snapped = rect_;
    if (std::abs(snapped.left - work_area.left) <= snap) {
        snapped.left = work_area.left;
        snapped.right = snapped.left + rect_.Width();
    }
    if (std::abs(work_area.right - snapped.right) <= snap) {
        snapped.right = work_area.right;
        snapped.left = snapped.right - rect_.Width();
    }
    if (std::abs(snapped.top - work_area.top) <= snap) {
        snapped.top = work_area.top;
        snapped.bottom = snapped.top + rect_.Height();
    }
    if (std::abs(work_area.bottom - snapped.bottom) <= snap) {
        snapped.bottom = work_area.bottom;
        snapped.top = snapped.bottom - rect_.Height();
    }
    normal_rect_ = snapped;
    ApplyWindowRect(snapped);
}

void FloatingWindow::FinishDrag() noexcept {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    if (::GetCapture() == window_) {
        ::ReleaseCapture();
    }
    SnapToWorkArea();
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
        self->window_ = window;
    }
    if (self == nullptr) {
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_LBUTTONDOWN:
        if (!self->edge_hidden_) {
            self->dragging_ = true;
            self->drag_origin_ = self->rect_;
            static_cast<void>(::GetCursorPos(&self->drag_anchor_));
            static_cast<void>(::SetCapture(window));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self->dragging_ && (wparam & MK_LBUTTON) != 0U) {
            self->HandleMouseMove(lparam);
            return 0;
        }
        if (!self->tracking_mouse_) {
            TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                                     .dwFlags = TME_LEAVE,
                                     .hwndTrack = window,
                                     .dwHoverTime = 0U};
            static_cast<void>(::TrackMouseEvent(&tracking));
            self->tracking_mouse_ = true;
            self->observer_->OnWindowPointer(WindowKind::Floating, InteractionEvent::PointerEnter,
                                             MouseTarget::Anchor);
        }
        return 0;
    case WM_LBUTTONUP:
        self->FinishDrag();
        return 0;
    case WM_CAPTURECHANGED:
        self->dragging_ = false;
        return 0;
    case WM_MOUSELEAVE:
        self->tracking_mouse_ = false;
        self->observer_->OnWindowPointer(WindowKind::Floating, InteractionEvent::PointerLeave,
                                         MouseTarget::Anchor);
        return 0;
    case WM_DPICHANGED:
        self->HandleDpiChanged(lparam);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        self->Invalidate();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        self->Paint();
        return 0;
    case WM_NCDESTROY:
        self->window_ = nullptr;
        self->tracking_mouse_ = false;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace nfm::ui
