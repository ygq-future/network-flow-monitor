#include "network_flow_monitor/ui/floating_window.h"

#include <array>

namespace nfm::ui {
namespace {

constexpr wchar_t kFloatingWindowClass[] = L"NetworkFlowMonitor.FloatingWindow";
constexpr wchar_t kFloatingWindowTitle[] = L"Network Flow Monitor";

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
                                   .lpszClassName = kFloatingWindowClass,
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

    window_ = ::CreateWindowExW(kExtendedStyle, kFloatingWindowClass, kFloatingWindowTitle,
                                WS_POPUP, rect_.left, rect_.top, rect_.Width(), rect_.Height(),
                                nullptr, nullptr, instance_, this);
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
        const auto window = window_;
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

void FloatingWindow::Reposition(PixelRect rect) noexcept {
    if (rect_ == rect) {
        return;
    }
    rect_ = rect;
    if (window_ != nullptr) {
        ::SetWindowPos(window_, HWND_TOPMOST, rect.left, rect.top, rect.Width(), rect.Height(),
                       SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    Invalidate();
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

void FloatingWindow::Paint() noexcept {
    PAINTSTRUCT paint{};
    const auto device_context = ::BeginPaint(window_, &paint);
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
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(target_->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::MidnightBlue, 0.92F), brush.GetAddressOf()))) {
            target_->BeginDraw();
            target_->Clear(D2D1::ColorF(D2D1::ColorF::MidnightBlue, 0.92F));
            const D2D1_RECT_F text_rect{8.0F, 6.0F, 260.0F, 28.0F};
            target_->DrawTextW(L"Network Flow Monitor", 20U, resources_->text_format(), text_rect,
                               brush.Get());
            if (snapshot_ != nullptr) {
                const D2D1_RECT_F status_rect{8.0F, 30.0F, 300.0F, 54.0F};
                target_->DrawTextW(L"Live", 4U, resources_->text_format(), status_rect,
                                   brush.Get());
            }
            if (RenderResources::EndDraw(*target_.Get()) == DrawResult::RecreateTarget) {
                target_.Reset();
            }
        }
    }
    static_cast<void>(device_context);
    ::EndPaint(window_, &paint);
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
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace nfm::ui
