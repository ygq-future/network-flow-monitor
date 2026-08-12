#include "network_flow_monitor/ui/ranking_popup.h"

#include "network_flow_monitor/ui/tray_controller.h"

#include <algorithm>
#include <array>
#include <vector>

namespace nfm::ui {
namespace {

constexpr auto kRankingPopupClass = std::to_array(L"NetworkFlowMonitor.RankingPopup");
constexpr auto kRankingPopupTitle = std::to_array(L"Network Flow Monitor ranking");

[[nodiscard]] PixelRect FromNativeRect(const RECT& rect) noexcept {
    return PixelRect{
        .left = rect.left, .top = rect.top, .right = rect.right, .bottom = rect.bottom};
}

} // namespace

RankingPopup::~RankingPopup() {
    Destroy();
}

bool RankingPopup::RegisterWindowClass(HINSTANCE instance) noexcept {
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (::GetClassInfoExW(instance, kRankingPopupClass.data(), &existing) != FALSE) {
        return true;
    }
    const auto icon = CreateNetworkFlowMonitorIcon(instance);
    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = CS_HREDRAW | CS_VREDRAW,
                                   .lpfnWndProc = &RankingPopup::WindowProc,
                                   .cbClsExtra = 0,
                                   .cbWndExtra = 0,
                                   .hInstance = instance,
                                   .hIcon = icon,
                                   .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
                                   .hbrBackground = nullptr,
                                   .lpszMenuName = nullptr,
                                   .lpszClassName = kRankingPopupClass.data(),
                                   .hIconSm = icon};
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
    window_ = ::CreateWindowExW(kExtendedStyle, kRankingPopupClass.data(),
                                kRankingPopupTitle.data(), WS_POPUP, rect_.left, rect_.top,
                                rect_.Width(), rect_.Height(), nullptr, nullptr, instance_, this);
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
        auto* const window = window_;
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

void RankingPopup::SetDisplayOptions(WindowDisplayOptions options) noexcept {
    if (display_options_ == options) {
        return;
    }
    display_options_ = options;
    popup_size_ = PixelSize{
        .width = 460,
        .height = std::max<std::int32_t>(180, 76 + static_cast<std::int32_t>(options.top_n) * 24)};
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

// NOLINTNEXTLINE(bugprone-exception-escape): formatting allocations are caught before drawing.
void RankingPopup::Paint() noexcept {
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
        std::wstring title = L"Top applications";
        std::wstring total = L"Collector offline - right-click to retry";
        std::vector<std::wstring> rows;
        try {
            if (snapshot_ != nullptr) {
                total = snapshot_->status == ranking::RankingStatus::WarmingUp
                            ? L"Synchronizing..."
                            : FormatTotalRate(snapshot_->total_rate, display_options_);
                const auto count =
                    std::min<std::size_t>(snapshot_->applications.size(), display_options_.top_n);
                rows.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    rows.push_back(
                        FormatApplicationRate(snapshot_->applications.at(index), display_options_));
                }
            }
        } catch (...) {
            rows.clear();
            total = L"Data unavailable";
        }

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush;
        const auto opacity = static_cast<float>(display_options_.opacity_percent) / 100.0F;
        if (SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.03F, 0.04F, 0.07F, opacity),
                                                     background_brush.GetAddressOf())) &&
            SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.95F, 0.97F, 1.0F, 1.0F),
                                                     text_brush.GetAddressOf()))) {
            target_->BeginDraw();
            target_->Clear(D2D1::ColorF(0.01F, 0.01F, 0.02F, 1.0F));
            const D2D1_RECT_F background{.left = 0.0F,
                                         .top = 0.0F,
                                         .right = static_cast<float>(size.width),
                                         .bottom = static_cast<float>(size.height)};
            target_->FillRectangle(background, background_brush.Get());
            const D2D1_RECT_F title_rect{
                .left = 12.0F, .top = 8.0F, .right = 440.0F, .bottom = 30.0F};
            target_->DrawTextW(title.data(), static_cast<UINT32>(title.size()),
                               resources_->text_format(), title_rect, text_brush.Get());
            const D2D1_RECT_F total_rect{
                .left = 12.0F, .top = 32.0F, .right = 440.0F, .bottom = 54.0F};
            target_->DrawTextW(total.data(), static_cast<UINT32>(total.size()),
                               resources_->text_format(), total_rect, text_brush.Get());
            for (std::size_t index = 0; index < rows.size(); ++index) {
                const auto y = 60.0F + (static_cast<float>(index) * 24.0F);
                const D2D1_RECT_F row_rect{
                    .left = 12.0F, .top = y, .right = 448.0F, .bottom = y + 22.0F};
                target_->DrawTextW(rows.at(index).data(),
                                   static_cast<UINT32>(rows.at(index).size()),
                                   resources_->text_format(), row_rect, text_brush.Get());
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
    case WM_RBUTTONUP:
        self->observer_->OnWindowContextMenu(WindowKind::RankingPopup);
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
