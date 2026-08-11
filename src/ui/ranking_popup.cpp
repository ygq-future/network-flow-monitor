#include "network_flow_monitor/ui/ranking_popup.h"

#include <array>

namespace nfm::ui {
namespace {

constexpr wchar_t kRankingPopupClass[] = L"NetworkFlowMonitor.RankingPopup";
constexpr wchar_t kRankingPopupTitle[] = L"Network Flow Monitor ranking";

[[nodiscard]] PixelRect FromNativeRect(const RECT& rect) noexcept {
    return PixelRect{
        .left = rect.left, .top = rect.top, .right = rect.right, .bottom = rect.bottom};
}

} // namespace

RankingPopup::~RankingPopup() {
    Destroy();
}

bool RankingPopup::RegisterWindowClass(HINSTANCE instance) noexcept {
    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = CS_HREDRAW | CS_VREDRAW,
                                   .lpfnWndProc = &RankingPopup::WindowProc,
                                   .cbClsExtra = 0,
                                   .cbWndExtra = 0,
                                   .hInstance = instance,
                                   .hIcon = nullptr,
                                   .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
                                   .hbrBackground = nullptr,
                                   .lpszMenuName = nullptr,
                                   .lpszClassName = kRankingPopupClass,
                                   .hIconSm = nullptr};
    if (::RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool RankingPopup::Create() noexcept {
    if (window_ != nullptr) {
        return true;
    }
    if (instance_ == nullptr) {
        instance_ = ::GetModuleHandleW(nullptr);
    }
    if (!RegisterWindowClass(instance_)) {
        return false;
    }
    window_ = ::CreateWindowExW(kExtendedStyle, kRankingPopupClass, kRankingPopupTitle, WS_POPUP,
                                rect_.left, rect_.top, rect_.Width(), rect_.Height(), nullptr,
                                nullptr, instance_, this);
    return window_ != nullptr;
}

void RankingPopup::ShowAt(PopupPlacementContext context) noexcept {
    anchor_ = context.anchor;
    const auto placement = ChoosePopupPlacement(anchor_, popup_size_, context.work_area);
    rect_ = placement.rect;
    if (Create()) {
        ::SetWindowPos(window_, HWND_TOPMOST, rect_.left, rect_.top, rect_.Width(), rect_.Height(),
                       SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        ::ShowWindow(window_, SW_SHOWNOACTIVATE);
    }
    Invalidate();
}

void RankingPopup::Hide() noexcept {
    if (window_ != nullptr) {
        ::ShowWindow(window_, SW_HIDE);
    }
}

void RankingPopup::Destroy() noexcept {
    target_.Reset();
    if (window_ != nullptr) {
        const auto window = window_;
        window_ = nullptr;
        ::DestroyWindow(window);
    }
}

void RankingPopup::SetSnapshot(RankingSnapshotPtr snapshot, std::uint64_t generation) noexcept {
    if (generation_ == generation && snapshot_.get() == snapshot.get()) {
        return;
    }
    snapshot_ = std::move(snapshot);
    generation_ = generation;
    Invalidate();
}

void RankingPopup::Reposition(PopupPlacementContext context) noexcept {
    anchor_ = context.anchor;
    const auto next = ChoosePopupPlacement(anchor_, popup_size_, context.work_area).rect;
    if (rect_ == next) {
        return;
    }
    rect_ = next;
    if (window_ != nullptr) {
        ::SetWindowPos(window_, HWND_TOPMOST, rect_.left, rect_.top, rect_.Width(), rect_.Height(),
                       SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    Invalidate();
}

bool RankingPopup::ShouldInvalidate(std::uint64_t old_generation,
                                    std::uint64_t new_generation) noexcept {
    return old_generation != new_generation;
}

PixelRect RankingPopup::ApplyDpiSuggestion(PixelRect suggested, PixelRect work_area) noexcept {
    return ClampRectToWorkArea(suggested, work_area);
}

void RankingPopup::Invalidate() noexcept {
    if (window_ != nullptr) {
        static_cast<void>(::InvalidateRect(window_, nullptr, FALSE));
    }
    observer_->OnWindowInvalidated(WindowKind::RankingPopup);
}

void RankingPopup::HandleDpiChanged(LPARAM lparam) noexcept {
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

void RankingPopup::Paint() noexcept {
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
        if (SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 0.94F),
                                                     brush.GetAddressOf()))) {
            target_->BeginDraw();
            target_->Clear(D2D1::ColorF(D2D1::ColorF::Black, 0.94F));
            const D2D1_RECT_F title_rect{10.0F, 8.0F, 300.0F, 30.0F};
            target_->DrawTextW(L"Top applications", 16U, resources_->text_format(), title_rect,
                               brush.Get());
            if (snapshot_ != nullptr) {
                const auto count = std::min<std::size_t>(snapshot_->applications.size(), 20U);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto y = 34.0F + static_cast<float>(index) * 22.0F;
                    const D2D1_RECT_F row_rect{10.0F, y, 310.0F, y + 20.0F};
                    target_->DrawTextW(L"Application", 11U, resources_->text_format(), row_rect,
                                       brush.Get());
                }
            }
            if (RenderResources::EndDraw(*target_.Get()) == DrawResult::RecreateTarget) {
                target_.Reset();
            }
        }
    }
    static_cast<void>(device_context);
    ::EndPaint(window_, &paint);
}

LRESULT CALLBACK RankingPopup::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): GWLP_USERDATA is the Win32 pointer storage ABI.
    auto* self = reinterpret_cast<RankingPopup*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 ABI.
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<RankingPopup*>(create->lpCreateParams);
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
    case WM_MOUSEMOVE:
        if (!self->tracking_mouse_) {
            TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                                     .dwFlags = TME_LEAVE,
                                     .hwndTrack = window,
                                     .dwHoverTime = 0U};
            static_cast<void>(::TrackMouseEvent(&tracking));
            self->tracking_mouse_ = true;
            self->observer_->OnWindowPointer(WindowKind::RankingPopup,
                                             InteractionEvent::PointerEnter, MouseTarget::Popup);
        }
        return 0;
    case WM_MOUSELEAVE:
        self->tracking_mouse_ = false;
        self->observer_->OnWindowPointer(WindowKind::RankingPopup, InteractionEvent::PointerLeave,
                                         MouseTarget::Popup);
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
