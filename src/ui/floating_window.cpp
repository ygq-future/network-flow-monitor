#include "network_flow_monitor/ui/floating_window.h"

#include "network_flow_monitor/ui/monitor_panel_layout.h"
#include "network_flow_monitor/ui/tray_controller.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <windowsx.h>

namespace nfm::ui {
namespace {

constexpr auto kFloatingWindowClass = std::to_array(L"NetworkFlowMonitor.FloatingWindow");
constexpr auto kFloatingWindowTitle = std::to_array(L"Network Flow Monitor");
constexpr UINT_PTR kRowAnimationTimerId = 3U;
constexpr auto kRowAnimationDuration = std::chrono::milliseconds{200};
constexpr auto kRowAnimationFrameMilliseconds = 16U;

constexpr float kPanelPaddingDip = 12.0F;
constexpr float kIconBoxDip = 16.0F;
constexpr float kPinWidthDip = 36.0F;
constexpr float kRateWidthDip = 72.0F;
constexpr float kColumnGapDip = 8.0F;

[[nodiscard]] PixelPoint ScreenPoint(HWND window, LPARAM lparam) noexcept {
    POINT point{.x = GET_X_LPARAM(lparam), .y = GET_Y_LPARAM(lparam)};
    static_cast<void>(::ClientToScreen(window, &point));
    return {.x = point.x, .y = point.y};
}

[[nodiscard]] int WindowDpi(HWND window) noexcept {
    const auto dpi = window == nullptr ? 0U : ::GetDpiForWindow(window);
    return dpi == 0U ? 96 : static_cast<int>(dpi);
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
    auto* const icon = NetworkFlowMonitorClassIcon(instance);
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
        icon_cache_.SetNotifyWindow(window_);
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
    CancelPointerTracking();
    if (window_ != nullptr) {
        static_cast<void>(::KillTimer(window_, kRowAnimationTimerId));
        ::ShowWindow(window_, SW_HIDE);
    }
    if (hit_window_ != nullptr) {
        ::ShowWindow(hit_window_, SW_HIDE);
    }
    row_animation_active_ = false;
    for (auto& row : row_visuals_) {
        row.from_position = row.target_position;
        row.from_opacity = row.target_opacity;
    }
    std::erase_if(row_visuals_, [](const RowVisual& row) { return row.exiting; });
}

void FloatingWindow::Destroy() noexcept {
    icon_cache_.SetNotifyWindow(nullptr);
    target_.Reset();
    if (hit_window_ != nullptr) {
        auto* const hit_window = hit_window_;
        hit_window_ = nullptr;
        ::DestroyWindow(hit_window);
    }
    if (window_ != nullptr) {
        static_cast<void>(::KillTimer(window_, kRowAnimationTimerId));
        auto* const window = window_;
        window_ = nullptr;
        ::DestroyWindow(window);
    }
}

void FloatingWindow::SetSnapshot(RankingSnapshotPtr snapshot, std::uint64_t generation) noexcept {
    if (generation_ == generation && snapshot_.get() == snapshot.get()) {
        return;
    }
    try {
        RetargetRows(snapshot.get());
        snapshot_ = std::move(snapshot);
        generation_ = generation;
        if (snapshot_ != nullptr) {
            const auto maximum = std::max(
                0, static_cast<std::int32_t>(snapshot_->applications.size()) - VisibleRowCount());
            scroll_row_offset_ = std::clamp(scroll_row_offset_, 0, maximum);
            for (const auto& application : snapshot_->applications) {
                icon_cache_.Request(application.metadata.icon_key);
            }
        } else {
            scroll_row_offset_ = 0;
            scrollbar_active_ = false;
        }
    } catch (...) {
        row_visuals_.clear();
        row_animation_active_ = false;
        snapshot_ = std::move(snapshot);
        generation_ = generation;
    }
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
                                                 PixelRect visual_bounds, PixelRect hit_bounds,
                                                 PixelRect viewport) noexcept {
    const auto surface_changed = panel_surface_ != surface;
    if (surface_changed && tracking_window_ != nullptr) {
        CancelPointerTracking();
    }
    panel_surface_ = surface;
    capsule_ = capsule;
    visual_bounds_ = visual_bounds;
    hit_bounds_ = hit_bounds;
    viewport_ = viewport;
    ApplyHitWindowRect();
    if (surface_changed && surface == MonitorPanelSurface::Panel) {
        TransferPanelTracking();
    }
    Invalidate();
}

void FloatingWindow::SetMonitorPanelControls(bool pinned, bool pin_enabled) noexcept {
    if (pinned_ == pinned && pin_enabled_ == pin_enabled) {
        return;
    }
    pinned_ = pinned;
    pin_enabled_ = pin_enabled;
    Invalidate();
}

void FloatingWindow::ApplyWindowRect(PixelRect rect) noexcept {
    const auto size_changed = rect.Width() != rect_.Width() || rect.Height() != rect_.Height();
    rect_ = rect;
    if (size_changed) {
        target_.Reset();
    }
    if (window_ != nullptr) {
        ::SetWindowPos(window_, nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
                       SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    ApplyHitWindowRect();
    observer_->OnWindowMoved();
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
        ::SetWindowPos(hit_window_, nullptr, extension.left, extension.top, extension.Width(),
                       extension.Height(), SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
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

void FloatingWindow::CancelPointerTracking() noexcept {
    if (tracking_window_ == nullptr) {
        return;
    }
    TRACKMOUSEEVENT cancel{.cbSize = sizeof(TRACKMOUSEEVENT),
                           .dwFlags = TME_LEAVE | TME_CANCEL,
                           .hwndTrack = tracking_window_,
                           .dwHoverTime = 0U};
    static_cast<void>(::TrackMouseEvent(&cancel));
    tracking_window_ = nullptr;
}

void FloatingWindow::TransferPanelTracking() noexcept {
    if (window_ == nullptr) {
        return;
    }
    POINT cursor{};
    if (::GetCursorPos(&cursor) == FALSE) {
        return;
    }
    const PixelPoint point{.x = cursor.x, .y = cursor.y};
    if (point.x < visual_bounds_.left || point.x >= visual_bounds_.right ||
        point.y < visual_bounds_.top || point.y >= visual_bounds_.bottom) {
        return;
    }
    TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                             .dwFlags = TME_LEAVE,
                             .hwndTrack = window_,
                             .dwHoverTime = 0U};
    static_cast<void>(::TrackMouseEvent(&tracking));
    tracking_window_ = window_;
    tracking_surface_ = MonitorPanelSurface::Panel;
    tracking_mouse_ = true;
}

MonitorPanelControl FloatingWindow::HitTestControl(PixelPoint client_point,
                                                   std::int32_t client_width_pixels,
                                                   int dpi) noexcept {
    const auto x = PixelsToDip(client_point.x, dpi);
    const auto y = PixelsToDip(client_point.y, dpi);
    const auto width = PixelsToDip(client_width_pixels, dpi);
    const auto right = width - kPanelPaddingDip;
    if (y < 0.0 || y >= static_cast<double>(MonitorPanelLayout::kHeaderRowDip) ||
        x < right - kPinWidthDip) {
        return MonitorPanelControl::None;
    }
    return MonitorPanelControl::Pin;
}

bool FloatingWindow::ShouldDispatchPointerLeave(PixelRect tracked_bounds,
                                                bool cursor_query_succeeded,
                                                PixelPoint cursor) noexcept {
    return cursor_query_succeeded &&
           (cursor.x < tracked_bounds.left || cursor.x >= tracked_bounds.right ||
            cursor.y < tracked_bounds.top || cursor.y >= tracked_bounds.bottom);
}

void FloatingWindow::Invalidate() noexcept {
    if (window_ != nullptr) {
        static_cast<void>(::InvalidateRect(window_, nullptr, FALSE));
    }
    observer_->OnWindowInvalidated();
}

void FloatingWindow::HandleEnvironmentChanged() noexcept {
    target_.Reset();
    observer_->OnMonitorPanelEvent(MonitorPanelEventKind::EnvironmentChanged, {});
}

void FloatingWindow::ScrollRows(std::int32_t delta) noexcept {
    if (snapshot_ == nullptr || delta == 0) {
        return;
    }
    const auto rows = static_cast<std::int32_t>(snapshot_->applications.size());
    const auto maximum = std::max(0, rows - VisibleRowCount());
    scroll_row_offset_ = std::clamp(scroll_row_offset_ + delta, 0, maximum);
    scrollbar_active_ = maximum > 0;
    Invalidate();
}

std::int32_t FloatingWindow::VisibleRowCount() const noexcept {
    const auto dpi = WindowDpi(window_);
    const auto row = std::max(1, DipToPixels(MonitorPanelLayout::kRowDip, dpi));
    return std::max(1, std::max(0, viewport_.Height()) / row);
}

float FloatingWindow::RowAnimationProgress() const noexcept {
    if (!row_animation_active_) {
        return 1.0F;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - row_animation_started_);
    const auto linear = std::clamp(static_cast<float>(elapsed.count()) /
                                       static_cast<float>(kRowAnimationDuration.count()),
                                   0.0F, 1.0F);
    return 1.0F - ((1.0F - linear) * (1.0F - linear));
}

void FloatingWindow::RetargetRows(const ranking::RankingSnapshot* snapshot) {
    if (snapshot == nullptr) {
        row_visuals_.clear();
        row_animation_active_ = false;
        if (window_ != nullptr) {
            static_cast<void>(::KillTimer(window_, kRowAnimationTimerId));
        }
        return;
    }

    const auto progress = RowAnimationProgress();
    const auto position_at = [progress](const RowVisual& row) noexcept {
        return row.from_position + ((row.target_position - row.from_position) * progress);
    };
    const auto opacity_at = [progress](const RowVisual& row) noexcept {
        return row.from_opacity + ((row.target_opacity - row.from_opacity) * progress);
    };
    std::vector<RowVisual> next;
    next.reserve(snapshot->applications.size() + row_visuals_.size());
    bool changed{};
    for (std::size_t index = 0; index < snapshot->applications.size(); ++index) {
        const auto& application = snapshot->applications.at(index);
        const auto found = std::find_if(
            row_visuals_.begin(), row_visuals_.end(),
            [&application](const RowVisual& row) { return row.application.id == application.id; });
        const auto target = static_cast<float>(index);
        if (found == row_visuals_.end()) {
            next.push_back({.application = application,
                            .from_position = target,
                            .target_position = target,
                            .from_opacity = 0.0F,
                            .target_opacity = 1.0F});
            changed = true;
        } else {
            const auto current_position = position_at(*found);
            const auto current_opacity = opacity_at(*found);
            next.push_back({.application = application,
                            .from_position = current_position,
                            .target_position = target,
                            .from_opacity = current_opacity,
                            .target_opacity = 1.0F});
            changed = changed || current_position != target || current_opacity != 1.0F;
        }
    }
    for (const auto& previous : row_visuals_) {
        const auto retained =
            std::any_of(snapshot->applications.begin(), snapshot->applications.end(),
                        [&previous](const ranking::RankedApplication& application) {
                            return application.id == previous.application.id;
                        });
        if (!retained && !previous.exiting) {
            const auto current_position = position_at(previous);
            next.push_back({.application = previous.application,
                            .from_position = current_position,
                            .target_position = current_position,
                            .from_opacity = opacity_at(previous),
                            .target_opacity = 0.0F,
                            .exiting = true});
            changed = true;
        }
    }

    row_visuals_ = std::move(next);
    const auto animate = changed && window_ != nullptr && ::IsWindowVisible(window_) != FALSE;
    row_animation_active_ = animate;
    if (animate) {
        row_animation_started_ = std::chrono::steady_clock::now();
        row_animation_active_ = ::SetTimer(window_, kRowAnimationTimerId,
                                           kRowAnimationFrameMilliseconds, nullptr) != 0U;
    }
    if (!row_animation_active_) {
        std::erase_if(row_visuals_, [](const RowVisual& row) { return row.exiting; });
        for (auto& row : row_visuals_) {
            row.from_position = row.target_position;
            row.from_opacity = row.target_opacity;
        }
    }
}

void FloatingWindow::TickRowAnimation() noexcept {
    if (!row_animation_active_) {
        return;
    }
    if (std::chrono::steady_clock::now() - row_animation_started_ >= kRowAnimationDuration) {
        row_animation_active_ = false;
        for (auto& row : row_visuals_) {
            row.from_position = row.target_position;
            row.from_opacity = row.target_opacity;
        }
        std::erase_if(row_visuals_, [](const RowVisual& row) { return row.exiting; });
        if (window_ != nullptr) {
            static_cast<void>(::KillTimer(window_, kRowAnimationTimerId));
        }
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
        const auto window_dpi = WindowDpi(window_);
        const auto dpi = static_cast<float>(window_dpi);
        target_->SetDpi(dpi, dpi);
        const auto to_dip = [window_dpi](std::int32_t pixels) {
            return static_cast<float>(PixelsToDip(pixels, window_dpi));
        };

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_brush;
        const auto opacity = static_cast<float>(display_options_.opacity_percent) / 100.0F;
        if (SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.05F, 0.10F, 0.18F, opacity),
                                                     background_brush.GetAddressOf())) &&
            SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.94F, 0.97F, 1.0F, 1.0F),
                                                     text_brush.GetAddressOf())) &&
            SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.15F, 0.39F, 0.92F, 1.0F),
                                                     accent_brush.GetAddressOf()))) {
            target_->BeginDraw();
            target_->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 1.0F));
            const D2D1_RECT_F background{.left = to_dip(visual_bounds_.left - rect_.left),
                                         .top = to_dip(visual_bounds_.top - rect_.top),
                                         .right = to_dip(visual_bounds_.right - rect_.left),
                                         .bottom = to_dip(visual_bounds_.bottom - rect_.top)};
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
                DrawPanel(*target_.Get(), *text_brush.Get(), window_dpi, *accent_brush.Get());
            }
            if (RenderResources::EndDraw(*target_.Get()) == DrawResult::RecreateTarget) {
                target_.Reset();
            }
        }
    }
    static_cast<void>(device_context);
    ::EndPaint(window_, &paint);
}

// The single panel draws header and ranking rows in one surface; formatting allocations are
// caught internally and the remaining COM calls return HRESULT rather than throwing.
// NOLINTNEXTLINE(readability-function-cognitive-complexity, bugprone-exception-escape)
void FloatingWindow::DrawPanel(ID2D1RenderTarget& target, ID2D1SolidColorBrush& text_brush,
                               int window_dpi, ID2D1SolidColorBrush& accent_brush) noexcept {
    const auto to_dip = [window_dpi](std::int32_t pixels) {
        return static_cast<float>(PixelsToDip(pixels, window_dpi));
    };
    const auto logical_width = to_dip(rect_.Width());
    const auto right = logical_width - kPanelPaddingDip;
    const auto pin_left = right - kPinWidthDip;
    const auto upload_right = pin_left - kColumnGapDip;
    const auto upload_left = upload_right - kRateWidthDip;
    const auto download_right = upload_left - kColumnGapDip;
    const auto download_left = download_right - kRateWidthDip;
    const auto name_right = download_left - kColumnGapDip;
    constexpr auto name_left = 12.0F + kIconBoxDip + 8.0F;

    const auto header_top = static_cast<float>(MonitorPanelLayout::kVerticalPaddingDip);
    const auto header_bottom = header_top + static_cast<float>(MonitorPanelLayout::kHeaderRowDip);
    const auto header_center = (header_top + header_bottom) / 2.0F;

    target.FillEllipse(D2D1::Ellipse(D2D1::Point2F(16.0F, header_center), 7.0F, 7.0F),
                       &accent_brush);
    target.DrawLine(D2D1::Point2F(14.0F, header_center + 3.0F),
                    D2D1::Point2F(14.0F, header_center - 7.0F), &text_brush, 1.5F);
    target.DrawLine(D2D1::Point2F(14.0F, header_center - 7.0F),
                    D2D1::Point2F(11.0F, header_center - 4.0F), &text_brush, 1.5F);
    target.DrawLine(D2D1::Point2F(14.0F, header_center - 7.0F),
                    D2D1::Point2F(17.0F, header_center - 4.0F), &text_brush, 1.5F);
    target.DrawLine(D2D1::Point2F(20.0F, header_center - 5.0F),
                    D2D1::Point2F(20.0F, header_center + 5.0F), &text_brush, 1.5F);
    target.DrawLine(D2D1::Point2F(20.0F, header_center + 5.0F),
                    D2D1::Point2F(17.0F, header_center + 2.0F), &text_brush, 1.5F);
    target.DrawLine(D2D1::Point2F(20.0F, header_center + 5.0F),
                    D2D1::Point2F(23.0F, header_center + 2.0F), &text_brush, 1.5F);

    constexpr auto brand = std::wstring_view{L"Network Flow Monitor"};
    target.DrawTextW(brand.data(), static_cast<UINT32>(brand.size()), resources_->row_name_format(),
                     D2D1::RectF(name_left, header_top, name_right, header_bottom), &text_brush);

    std::wstring download_text;
    std::wstring upload_text;
    std::wstring_view status = L"";
    bool show_status = false;
    try {
        download_text = L"↓ —";
        upload_text = L"↑ —";
        if (interaction_state_ == InteractionState::DisconnectedOverlay) {
            status = L"Collector offline";
            show_status = true;
        } else if (snapshot_ == nullptr || snapshot_->status == ranking::RankingStatus::WarmingUp) {
            status = L"Collecting data…";
            show_status = true;
        } else if (snapshot_->applications.empty()) {
            status = L"No network activity";
            show_status = true;
        } else {
            download_text = L"↓ " + FormatRate(snapshot_->total_rate.download_bytes_per_second,
                                               display_options_.speed_unit);
            upload_text = L"↑ " + FormatRate(snapshot_->total_rate.upload_bytes_per_second,
                                             display_options_.speed_unit);
        }
    } catch (...) {
        download_text = L"↓ —";
        upload_text = L"↑ —";
        status = L"Data unavailable";
        show_status = true;
    }

    target.DrawTextW(
        download_text.data(), static_cast<UINT32>(download_text.size()), resources_->rate_format(),
        D2D1::RectF(download_left, header_top, download_right, header_bottom), &text_brush);
    target.DrawTextW(
        upload_text.data(), static_cast<UINT32>(upload_text.size()), resources_->rate_format(),
        D2D1::RectF(upload_left, header_top, upload_right, header_bottom), &text_brush);

    const auto pin_label = !pin_enabled_ ? std::wstring_view{L"Pin off"}
                           : pinned_     ? std::wstring_view{L"Pinned"}
                                         : std::wstring_view{L"Pin"};
    target.DrawTextW(pin_label.data(), static_cast<UINT32>(pin_label.size()),
                     resources_->row_name_format(),
                     D2D1::RectF(pin_left, header_top, right, header_bottom), &text_brush);

    if (show_status) {
        const auto status_top = header_bottom;
        const auto status_bottom = status_top + static_cast<float>(MonitorPanelLayout::kRowDip);
        target.DrawTextW(status.data(), static_cast<UINT32>(status.size()),
                         resources_->row_name_format(),
                         D2D1::RectF(name_left, status_top, right, status_bottom), &text_brush);
        return;
    }

    const auto viewport_top = to_dip(viewport_.top - rect_.top);
    const auto viewport_bottom = to_dip(viewport_.bottom - rect_.top);
    const auto progress = RowAnimationProgress();
    const auto visible_rows = static_cast<std::size_t>(VisibleRowCount());
    struct RowText {
        std::string icon_key;
        std::wstring name;
        std::wstring download;
        std::wstring upload;
        float position{};
        float opacity{1.0F};
    };
    std::vector<RowText> rows;
    try {
        rows.reserve(row_visuals_.size());
        for (const auto& visual : row_visuals_) {
            const auto& application = visual.application;
            rows.push_back(
                {.icon_key = application.metadata.icon_key,
                 .name = ApplicationLabel(application),
                 .download = FormatRate(application.rate.download_bytes_per_second,
                                        display_options_.speed_unit),
                 .upload = FormatRate(application.rate.upload_bytes_per_second,
                                      display_options_.speed_unit),
                 .position = visual.from_position +
                             ((visual.target_position - visual.from_position) * progress),
                 .opacity = visual.from_opacity +
                            ((visual.target_opacity - visual.from_opacity) * progress)});
        }
    } catch (...) {
        rows.clear();
    }
    const auto row_height = static_cast<float>(MonitorPanelLayout::kRowDip);
    for (const auto& row : rows) {
        const auto y =
            viewport_top + ((row.position - static_cast<float>(scroll_row_offset_)) * row_height);
        if (y + row_height <= viewport_top || y >= viewport_bottom || row.opacity <= 0.0F) {
            continue;
        }
        const auto row_center = y + (row_height / 2.0F);
        const auto icon = icon_cache_.Lookup(row.icon_key);
        Microsoft::WRL::ComPtr<ID2D1Bitmap> icon_bitmap;
        const auto bitmap_properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (icon != nullptr && SUCCEEDED(target.CreateBitmap(
                                   D2D1::SizeU(kApplicationIconSize, kApplicationIconSize),
                                   icon->bgra.data(), kApplicationIconSize * sizeof(std::uint32_t),
                                   bitmap_properties, icon_bitmap.GetAddressOf()))) {
            const auto icon_top = row_center - (static_cast<float>(kApplicationIconSize) / 2.0F);
            target.DrawBitmap(icon_bitmap.Get(),
                              D2D1::RectF(12.0F, icon_top, 12.0F + kApplicationIconSize,
                                          icon_top + kApplicationIconSize),
                              row.opacity);
        }
        text_brush.SetOpacity(row.opacity);
        target.DrawTextW(row.name.data(), static_cast<UINT32>(row.name.size()),
                         resources_->row_name_format(),
                         D2D1::RectF(name_left, y, name_right, y + row_height), &text_brush);
        target.DrawTextW(row.download.data(), static_cast<UINT32>(row.download.size()),
                         resources_->rate_format(),
                         D2D1::RectF(download_left, y, download_right, y + row_height),
                         &text_brush);
        target.DrawTextW(row.upload.data(), static_cast<UINT32>(row.upload.size()),
                         resources_->rate_format(),
                         D2D1::RectF(upload_left, y, upload_right, y + row_height), &text_brush);
    }
    text_brush.SetOpacity(1.0F);

    const auto ranked_row_count = static_cast<std::size_t>(snapshot_->applications.size());
    if (ranked_row_count > visible_rows && (tracking_mouse_ || scrollbar_active_)) {
        const auto track_bottom = viewport_bottom;
        const auto track_top = viewport_top;
        const auto track_height = std::max(1.0F, track_bottom - track_top);
        const auto thumb_height = std::max(20.0F, track_height * static_cast<float>(visible_rows) /
                                                      static_cast<float>(ranked_row_count));
        const auto maximum_offset = std::max<std::size_t>(1U, ranked_row_count - visible_rows);
        const auto thumb_top = track_top + ((track_height - thumb_height) *
                                            static_cast<float>(std::max(0, scroll_row_offset_)) /
                                            static_cast<float>(maximum_offset));
        target.FillRectangle(D2D1::RectF(logical_width - 5.0F, thumb_top, logical_width - 2.0F,
                                         thumb_top + thumb_height),
                             &text_brush);
    }
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
                                            LPARAM lparam) noexcept {
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
    case kApplicationIconReadyMessage:
        self->Invalidate();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_LBUTTONDOWN: {
        const auto point = ScreenPoint(window, lparam);
        self->pressed_control_ = MonitorPanelControl::None;
        if (self->panel_surface_ == MonitorPanelSurface::Panel) {
            const auto x = static_cast<std::int32_t>(static_cast<std::int16_t>(LOWORD(lparam)));
            const auto y = static_cast<std::int32_t>(static_cast<std::int16_t>(HIWORD(lparam)));
            const auto control = HitTestControl({.x = x, .y = y}, self->rect_.Width(),
                                                static_cast<int>(::GetDpiForWindow(window)));
            if (control == MonitorPanelControl::Pin && self->pin_enabled_) {
                self->pressed_control_ = control;
            }
        }
        static_cast<void>(::SetFocus(window));
        self->panel_press_point_ = point;
        self->panel_pressing_ = true;
        static_cast<void>(::SetCapture(window));
        self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelPress, point);
    }
        return 0;
    case WM_MOUSEMOVE:
        if (self->panel_pressing_ && (wparam & MK_LBUTTON) != 0U) {
            const auto point = ScreenPoint(window, lparam);
            if (std::abs(point.x - self->panel_press_point_.x) >= ::GetSystemMetrics(SM_CXDRAG) ||
                std::abs(point.y - self->panel_press_point_.y) >= ::GetSystemMetrics(SM_CYDRAG)) {
                self->pressed_control_ = MonitorPanelControl::None;
            }
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PointerMove, point);
            return 0;
        }
        if (self->tracking_window_ != window) {
            TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                                     .dwFlags = TME_LEAVE,
                                     .hwndTrack = window,
                                     .dwHoverTime = 0U};
            static_cast<void>(::TrackMouseEvent(&tracking));
            self->tracking_window_ = window;
            self->tracking_surface_ = self->panel_surface_;
            self->tracking_mouse_ = true;
            self->Invalidate();
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
            const auto point = ScreenPoint(window, lparam);
            const auto x = GET_X_LPARAM(lparam);
            const auto y = GET_Y_LPARAM(lparam);
            const auto released_control = HitTestControl(
                {.x = x, .y = y}, self->rect_.Width(), static_cast<int>(::GetDpiForWindow(window)));
            const auto activated = self->pressed_control_ != MonitorPanelControl::None &&
                                   self->pressed_control_ == released_control;
            self->panel_pressing_ = false;
            self->pressed_control_ = MonitorPanelControl::None;
            if (::GetCapture() == window) {
                ::ReleaseCapture();
            }
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelRelease, point);
            if (activated) {
                self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PinTogglePressed,
                                                     point);
            }
        }
        return 0;
    case WM_RBUTTONUP:
        self->observer_->OnWindowContextMenu();
        return 0;
    case WM_CAPTURECHANGED:
        if (self->panel_pressing_) {
            self->panel_pressing_ = false;
            self->pressed_control_ = MonitorPanelControl::None;
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::DragCancelled, {});
        }
        return 0;
    case WM_MOUSELEAVE: {
        if (self->tracking_window_ != window) {
            return 0;
        }
        self->tracking_window_ = nullptr;
        self->tracking_mouse_ = false;
        self->scrollbar_active_ = false;
        self->Invalidate();
        POINT cursor{};
        const auto& tracked_bounds = self->tracking_surface_ == MonitorPanelSurface::Capsule
                                         ? self->hit_bounds_
                                         : self->visual_bounds_;
        if (!ShouldDispatchPointerLeave(tracked_bounds, ::GetCursorPos(&cursor) != FALSE,
                                        {.x = cursor.x, .y = cursor.y})) {
            return 0;
        }
        if (self->tracking_surface_ == MonitorPanelSurface::Capsule) {
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::CapsulePointerLeave, {});
        } else {
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelPointerLeave, {});
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == kRowAnimationTimerId) {
            self->TickRowAnimation();
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        self->ScrollRows(GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1);
        return 0;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_UP:
            self->ScrollRows(-1);
            break;
        case VK_DOWN:
            self->ScrollRows(1);
            break;
        case VK_PRIOR:
            self->ScrollRows(-5);
            break;
        case VK_NEXT:
            self->ScrollRows(5);
            break;
        case VK_HOME:
            self->scroll_row_offset_ = 0;
            self->Invalidate();
            break;
        case VK_END:
            self->ScrollRows(std::numeric_limits<std::int32_t>::max());
            break;
        default:
            break;
        }
        return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        self->HandleEnvironmentChanged();
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
        if (self->tracking_window_ == window) {
            self->tracking_window_ = nullptr;
        }
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace nfm::ui
