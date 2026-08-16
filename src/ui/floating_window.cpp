#include "network_flow_monitor/ui/floating_window.h"

#include "network_flow_monitor/ui/monitor_panel_layout.h"
#include "network_flow_monitor/ui/resources.h"
#include "network_flow_monitor/ui/tray_controller.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <dwmapi.h>
#include <limits>
#include <string>
#include <windowsx.h>

namespace nfm::ui {
namespace {

constexpr auto kFloatingWindowClass = std::to_array(L"NetworkFlowMonitor.FloatingWindow");
constexpr auto kFloatingWindowTitle = std::to_array(L"Network Flow Monitor");
constexpr UINT kRowAnimationFrameMessage = WM_APP + 0x36U;
constexpr UINT kHeightAnimationFrameMessage = WM_APP + 0x37U;
constexpr UINT_PTR kCapsuleRateAnimationTimerId = 101U;
constexpr UINT kCapsuleRateAnimationFrameMilliseconds = 16U;
constexpr auto kRowAnimationDuration = std::chrono::milliseconds{200};
constexpr auto kHeightAnimationDuration = std::chrono::milliseconds{200};

constexpr float kPanelPaddingDip = 12.0F;
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

[[nodiscard]] D2D1::ColorF Rgb(std::uint32_t rgb, float alpha) noexcept {
    return D2D1::ColorF(static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F,
                        static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F,
                        static_cast<float>(rgb & 0xFFU) / 255.0F, alpha);
}

struct ColorRange {
    std::uint32_t from;
    std::uint32_t to;
};

[[nodiscard]] std::uint32_t InterpolateColor(ColorRange range, float progress) noexcept {
    const auto channel = [progress](std::uint32_t first, std::uint32_t second) noexcept {
        return static_cast<std::uint32_t>(
            std::lround(static_cast<float>(first) +
                        ((static_cast<float>(second) - static_cast<float>(first)) * progress)));
    };
    const auto red = channel((range.from >> 16U) & 0xFFU, (range.to >> 16U) & 0xFFU);
    const auto green = channel((range.from >> 8U) & 0xFFU, (range.to >> 8U) & 0xFFU);
    const auto blue = channel(range.from & 0xFFU, range.to & 0xFFU);
    return (red << 16U) | (green << 8U) | blue;
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
        ApplyDwmAttributes();
        if (redirection_alpha_supported_) {
            static_cast<void>(
                frosted_glass_.Initialize(window_, display_options_.blur_strength_percent));
        }
        frosted_glass_.SetTint(tokens_.background, tokens_.transparency_enabled
                                                       ? display_options_.opacity_percent
                                                       : 100U);
        UpdateFrostedGlassClip();
        frosted_glass_.SetVisible(tokens_.transparency_enabled);
        ApplyDwmAttributes();
        icon_cache_.SetNotifyWindow(window_);
    }
    return window_ != nullptr;
}

void FloatingWindow::Show() noexcept {
    if (Create()) {
        ::ShowWindow(window_, SW_SHOWNOACTIVATE);
    }
}

void FloatingWindow::Hide() noexcept {
    CancelPointerTracking();
    if (window_ != nullptr) {
        ::ShowWindow(window_, SW_HIDE);
    }
    FinishRowAnimation();
    FinishHeightAnimation();
    FinishCapsuleRateAnimation();
}

void FloatingWindow::Destroy() noexcept {
    icon_cache_.SetNotifyWindow(nullptr);
    target_.Reset();
    frosted_glass_.Reset();
    redirection_alpha_supported_ = false;
    FinishHeightAnimation();
    FinishCapsuleRateAnimation();
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
    const auto blur_changed =
        display_options_.blur_strength_percent != options.blur_strength_percent;
    display_options_ = options;
    if (blur_changed) {
        frosted_glass_.SetBlurStrength(display_options_.blur_strength_percent);
        if (!frosted_glass_.active()) {
            ApplyDwmAttributes();
        }
    }
    frosted_glass_.SetTint(tokens_.background,
                           tokens_.transparency_enabled ? display_options_.opacity_percent : 100U);
    Invalidate();
}

void FloatingWindow::SetAppearance(VisualTokens tokens, ResolvedLanguage language) noexcept {
    tokens_ = tokens;
    language_ = language;
    frosted_glass_.SetVisible(tokens_.transparency_enabled);
    frosted_glass_.SetTint(tokens_.background,
                           tokens_.transparency_enabled ? display_options_.opacity_percent : 100U);
    ApplyDwmAttributes();
    Invalidate();
}

void FloatingWindow::ApplyDwmAttributes() noexcept {
    if (window_ == nullptr) {
        return;
    }
    const BOOL dark = tokens_.dark ? TRUE : FALSE;
    static_cast<void>(
        ::DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
    const DWM_SYSTEMBACKDROP_TYPE backdrop = frosted_glass_.active() ||
                                                     !tokens_.transparency_enabled ||
                                                     panel_surface_ == MonitorPanelSurface::Capsule
                                                 ? DWMSBT_NONE
                                                 : DWMSBT_TRANSIENTWINDOW;
    static_cast<void>(
        ::DwmSetWindowAttribute(window_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)));
    const DWM_WINDOW_CORNER_PREFERENCE corner =
        panel_surface_ == MonitorPanelSurface::Capsule ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    static_cast<void>(
        ::DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)));
    const BOOL use_redirection_alpha = TRUE;
    redirection_alpha_supported_ =
        SUCCEEDED(::DwmSetWindowAttribute(window_, DWMWA_REDIRECTIONBITMAP_ALPHA,
                                          &use_redirection_alpha, sizeof(use_redirection_alpha)));
    const MARGINS full_client_frame{
        .cxLeftWidth = -1, .cxRightWidth = -1, .cyTopHeight = -1, .cyBottomHeight = -1};
    static_cast<void>(::DwmExtendFrameIntoClientArea(window_, &full_client_frame));
}

void FloatingWindow::UpdateFrostedGlassClip() noexcept {
    if (!frosted_glass_.active() || window_ == nullptr) {
        return;
    }
    const auto dpi = WindowDpi(window_);
    if (panel_surface_ == MonitorPanelSurface::Capsule) {
        frosted_glass_.SetVisible(false);
        const PixelRect local{
            .left = 0, .top = 0, .right = rect_.Width(), .bottom = rect_.Height()};
        frosted_glass_.SetClip(local, static_cast<float>(rect_.Width()) / 2.0F);
        ApplyDwmAttributes();
        return;
    }
    frosted_glass_.SetVisible(tokens_.transparency_enabled);
    const PixelRect local{.left = 0, .top = 0, .right = rect_.Width(), .bottom = rect_.Height()};
    frosted_glass_.SetClip(
        local, static_cast<float>(DipToPixels(static_cast<int>(tokens_.corner_radius_dip), dpi)));
    ApplyDwmAttributes();
}

void FloatingWindow::SetMonitorPanelPresentation(MonitorPanelSurface surface,
                                                 CapsulePresentation capsule,
                                                 PixelRect visual_bounds, PixelRect hit_bounds,
                                                 PixelRect viewport) noexcept {
    const auto surface_changed = panel_surface_ != surface;
    const auto displayed_before = displayed_capsule();
    if (surface_changed && tracking_window_ != nullptr) {
        CancelPointerTracking();
    }
    if (surface_changed) {
        FinishHeightAnimation();
    }
    panel_surface_ = surface;
    capsule_ = capsule;
    visual_bounds_ = visual_bounds;
    hit_bounds_ = hit_bounds;
    viewport_ = viewport;
    RetargetCapsuleRates(displayed_before);
    UpdateFrostedGlassClip();
    if (surface_changed) {
        ApplyDwmAttributes();
    }
    if (surface_changed && surface == MonitorPanelSurface::Panel) {
        TransferPanelTracking();
    }
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
        UpdateFrostedGlassClip();
    }
    observer_->OnWindowMoved();
    Invalidate();
}

void FloatingWindow::Reposition(PixelRect rect) noexcept {
    visual_bounds_ = rect;
    const auto window_bounds = rect;
    if (rect_ == window_bounds && !height_animation_active_) {
        return;
    }
    if (height_animation_active_ && height_target_rect_ == window_bounds) {
        return;
    }
    // Animate panel rect changes when the window is visible.
    if (window_ != nullptr && ::IsWindowVisible(window_) != FALSE &&
        panel_surface_ == MonitorPanelSurface::Panel) {
        const auto was_animating = height_animation_active_;
        height_from_rect_ = rect_;
        height_target_rect_ = window_bounds;
        height_animation_started_ = std::chrono::steady_clock::now();
        height_animation_active_ = true;
        if (!was_animating) {
            static_cast<void>(::PostMessageW(window_, kHeightAnimationFrameMessage, 0U, 0));
        }
        return;
    }
    FinishHeightAnimation();
    ApplyWindowRect(window_bounds);
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
    const auto animate =
        changed && !panel_pressing_ && window_ != nullptr && ::IsWindowVisible(window_) != FALSE;
    const auto frame_already_scheduled = row_animation_active_;
    row_animation_active_ = animate;
    if (animate) {
        row_animation_started_ = std::chrono::steady_clock::now();
        if (!frame_already_scheduled) {
            static_cast<void>(::PostMessageW(window_, kRowAnimationFrameMessage, 0U, 0));
        }
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
    }
    Invalidate();
}

void FloatingWindow::FinishRowAnimation() noexcept {
    row_animation_active_ = false;
    for (auto& row : row_visuals_) {
        row.from_position = row.target_position;
        row.from_opacity = row.target_opacity;
    }
    std::erase_if(row_visuals_, [](const RowVisual& row) { return row.exiting; });
}

void FloatingWindow::TickHeightAnimation() noexcept {
    if (!height_animation_active_) {
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - height_animation_started_);
    const auto linear = std::clamp(static_cast<float>(elapsed.count()) /
                                       static_cast<float>(kHeightAnimationDuration.count()),
                                   0.0F, 1.0F);
    if (linear >= 1.0F) {
        height_animation_active_ = false;
        ApplyWindowRect(height_target_rect_);
        return;
    }
    // Ease-out: 1 − (1 − t)².
    const auto progress = 1.0F - ((1.0F - linear) * (1.0F - linear));
    const auto lerp = [](std::int32_t from, std::int32_t to, float t) {
        return from + static_cast<std::int32_t>(static_cast<float>(to - from) * t);
    };
    const PixelRect interpolated{
        .left = lerp(height_from_rect_.left, height_target_rect_.left, progress),
        .top = lerp(height_from_rect_.top, height_target_rect_.top, progress),
        .right = lerp(height_from_rect_.right, height_target_rect_.right, progress),
        .bottom = lerp(height_from_rect_.bottom, height_target_rect_.bottom, progress)};
    ApplyWindowRect(interpolated);
}

void FloatingWindow::FinishHeightAnimation() noexcept {
    if (height_animation_active_) {
        height_animation_active_ = false;
        ApplyWindowRect(height_target_rect_);
    }
}

float FloatingWindow::CapsuleRateProgress() const noexcept {
    if (!capsule_animation_active_) {
        return 1.0F;
    }
    if (capsule_animation_duration_.count() <= 0) {
        return 1.0F;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - capsule_animation_started_);
    const auto linear = std::clamp(static_cast<float>(elapsed.count()) /
                                       static_cast<float>(capsule_animation_duration_.count()),
                                   0.0F, 1.0F);
    // Fluent cubic ease-out: 1 − (1 − t)³ for natural deceleration.
    const auto inverted = 1.0F - linear;
    return 1.0F - (inverted * inverted * inverted);
}

CapsulePresentation FloatingWindow::displayed_capsule() const noexcept {
    if (!capsule_animation_active_) {
        return capsule_;
    }
    const auto progress = CapsuleRateProgress();
    const auto lerp = [](float from, float to, float t) noexcept {
        return from + ((to - from) * t);
    };
    return {
        .download = {.fill = lerp(capsule_from_.download.fill, capsule_.download.fill, progress),
                     .color = InterpolateColor(
                         {.from = capsule_from_.download.color, .to = capsule_.download.color},
                         progress)},
        .upload = {
            .fill = lerp(capsule_from_.upload.fill, capsule_.upload.fill, progress),
            .color = InterpolateColor(
                {.from = capsule_from_.upload.color, .to = capsule_.upload.color}, progress)}};
}

void FloatingWindow::RetargetCapsuleRates(const CapsulePresentation& displayed_before) noexcept {
    const auto fill_changed = displayed_before.download.fill != capsule_.download.fill ||
                              displayed_before.upload.fill != capsule_.upload.fill;
    const auto can_animate = panel_surface_ == MonitorPanelSurface::Capsule &&
                             tokens_.capsule_rate_interpolation.count() > 0 && window_ != nullptr &&
                             ::IsWindowVisible(window_) != FALSE;
    if (!fill_changed || !can_animate) {
        FinishCapsuleRateAnimation();
        return;
    }
    const auto is_growth = capsule_.download.fill >= displayed_before.download.fill &&
                           capsule_.upload.fill >= displayed_before.upload.fill;
    const auto base_duration = tokens_.capsule_rate_interpolation;
    capsule_animation_duration_ = is_growth ? base_duration : (base_duration * 3 / 2);
    capsule_from_ = displayed_before;
    capsule_animation_started_ = std::chrono::steady_clock::now();
    capsule_animation_active_ = true;
    static_cast<void>(::SetTimer(window_, kCapsuleRateAnimationTimerId,
                                 kCapsuleRateAnimationFrameMilliseconds, nullptr));
    Invalidate();
}

void FloatingWindow::TickCapsuleRateAnimation() noexcept {
    if (!capsule_animation_active_) {
        if (window_ != nullptr) {
            static_cast<void>(::KillTimer(window_, kCapsuleRateAnimationTimerId));
        }
        return;
    }
    if (std::chrono::steady_clock::now() - capsule_animation_started_ >=
        capsule_animation_duration_) {
        FinishCapsuleRateAnimation();
    }
    Invalidate();
}

void FloatingWindow::FinishCapsuleRateAnimation() noexcept {
    capsule_animation_active_ = false;
    capsule_from_ = capsule_;
    if (window_ != nullptr) {
        static_cast<void>(::KillTimer(window_, kCapsuleRateAnimationTimerId));
    }
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
    const auto window_dpi = WindowDpi(window_);
    auto composition_context = frosted_glass_.BeginDraw(size, window_dpi);
    const auto composition_drawing = composition_context != nullptr;
    if (!composition_drawing && target_ == nullptr) {
        target_ = resources_->CreateHwndRenderTarget(window_, size);
    }
    auto* const render_target = composition_drawing
                                    ? static_cast<ID2D1RenderTarget*>(composition_context.Get())
                                    : static_cast<ID2D1RenderTarget*>(target_.Get());
    if (render_target != nullptr && resources_->text_format() != nullptr) {
        const auto dpi = static_cast<float>(window_dpi);
        render_target->SetDpi(dpi, dpi);
        const auto to_dip = [window_dpi](std::int32_t pixels) {
            return static_cast<float>(PixelsToDip(pixels, window_dpi));
        };

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush;
        const auto opacity = static_cast<float>(display_options_.opacity_percent) / 100.0F;
        if (SUCCEEDED(render_target->CreateSolidColorBrush(Rgb(tokens_.text, 1.0F),
                                                           text_brush.GetAddressOf()))) {
            if (!composition_drawing) {
                render_target->BeginDraw();
            }
            // Composition surfaces and the supported HWND fallback both preserve premultiplied
            // alpha. Degraded paths keep the tint opaque so content remains readable.
            const auto transparent_material =
                tokens_.transparency_enabled &&
                (composition_drawing || redirection_alpha_supported_) &&
                (panel_surface_ == MonitorPanelSurface::Panel || frosted_glass_.active());
            const auto background_opacity = transparent_material ? opacity : 1.0F;
            if (!composition_drawing) {
                render_target->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
            }
            if (panel_surface_ == MonitorPanelSurface::Capsule) {
                const auto radius = to_dip(rect_.Width()) / 2.0F;
                const D2D1_RECT_F background{.left = 0.0F,
                                             .top = 0.0F,
                                             .right = to_dip(rect_.Width()),
                                             .bottom = to_dip(rect_.Height())};
                const auto half_height = background.bottom / 2.0F;
                const auto capsule = displayed_capsule();
                const auto download_height = half_height * capsule.download.fill;
                const auto upload_height = half_height * capsule.upload.fill;

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush;
                Microsoft::WRL::ComPtr<ID2D1Factory> factory;
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> capsule_geometry;
                Microsoft::WRL::ComPtr<ID2D1Layer> capsule_layer;
                render_target->GetFactory(factory.GetAddressOf());

                if (factory != nullptr &&
                    SUCCEEDED(factory->CreateRoundedRectangleGeometry(
                        D2D1::RoundedRect(background, radius, radius),
                        capsule_geometry.GetAddressOf())) &&
                    SUCCEEDED(render_target->CreateLayer(nullptr, capsule_layer.GetAddressOf())) &&
                    SUCCEEDED(render_target->CreateSolidColorBrush(
                        Rgb(tokens_.background, 1.0F), background_brush.GetAddressOf()))) {
                    render_target->PushLayer(
                        D2D1::LayerParameters(D2D1::InfiniteRect(), capsule_geometry.Get()),
                        capsule_layer.Get());
                    render_target->FillRectangle(background, background_brush.Get());

                    if (download_height > 0.0F) {
                        const auto three_stops = capsule.download.fill > 0.5F;
                        const auto mid_ratio = three_stops ? (0.5F / capsule.download.fill) : 0.5F;
                        const std::array download_stops{
                            D2D1_GRADIENT_STOP{.position = 0.0F, .color = Rgb(0x19D8E6U, 1.0F)},
                            D2D1_GRADIENT_STOP{.position = mid_ratio,
                                               .color = Rgb(0x2878D0U, 1.0F)},
                            D2D1_GRADIENT_STOP{.position = 1.0F,
                                               .color = Rgb(capsule.download.color, 1.0F)}};
                        const std::array download_two_stops{
                            D2D1_GRADIENT_STOP{.position = 0.0F, .color = Rgb(0x19D8E6U, 1.0F)},
                            D2D1_GRADIENT_STOP{.position = 1.0F,
                                               .color = Rgb(capsule.download.color, 1.0F)}};
                        const auto download_stop_count = three_stops ? 3U : 2U;
                        const auto* const stops_ptr =
                            three_stops ? download_stops.data() : download_two_stops.data();

                        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection>
                            download_stop_collection;
                        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> download_brush;
                        if (SUCCEEDED(render_target->CreateGradientStopCollection(
                                stops_ptr, download_stop_count, D2D1_GAMMA_2_2,
                                D2D1_EXTEND_MODE_CLAMP, download_stop_collection.GetAddressOf())) &&
                            SUCCEEDED(render_target->CreateLinearGradientBrush(
                                D2D1::LinearGradientBrushProperties(
                                    D2D1::Point2F(background.left, background.top),
                                    D2D1::Point2F(background.left,
                                                  background.top + download_height)),
                                download_stop_collection.Get(), download_brush.GetAddressOf()))) {
                            render_target->FillRectangle(
                                D2D1::RectF(background.left, background.top, background.right,
                                            background.top + download_height),
                                download_brush.Get());
                        }
                    }
                    if (upload_height > 0.0F) {
                        const auto three_stops = capsule.upload.fill > 0.5F;
                        const auto mid_ratio = three_stops ? (0.5F / capsule.upload.fill) : 0.5F;
                        const std::array upload_stops{
                            D2D1_GRADIENT_STOP{.position = 0.0F, .color = Rgb(0xF4D03FU, 1.0F)},
                            D2D1_GRADIENT_STOP{.position = mid_ratio,
                                               .color = Rgb(0xF39C32U, 1.0F)},
                            D2D1_GRADIENT_STOP{.position = 1.0F,
                                               .color = Rgb(capsule.upload.color, 1.0F)}};
                        const std::array upload_two_stops{
                            D2D1_GRADIENT_STOP{.position = 0.0F, .color = Rgb(0xF4D03FU, 1.0F)},
                            D2D1_GRADIENT_STOP{.position = 1.0F,
                                               .color = Rgb(capsule.upload.color, 1.0F)}};
                        const auto upload_stop_count = three_stops ? 3U : 2U;
                        const auto* const stops_ptr =
                            three_stops ? upload_stops.data() : upload_two_stops.data();

                        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> upload_stop_collection;
                        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> upload_brush;
                        if (SUCCEEDED(render_target->CreateGradientStopCollection(
                                stops_ptr, upload_stop_count, D2D1_GAMMA_2_2,
                                D2D1_EXTEND_MODE_CLAMP, upload_stop_collection.GetAddressOf())) &&
                            SUCCEEDED(render_target->CreateLinearGradientBrush(
                                D2D1::LinearGradientBrushProperties(
                                    D2D1::Point2F(background.left, background.bottom),
                                    D2D1::Point2F(background.left,
                                                  background.bottom - upload_height)),
                                upload_stop_collection.Get(), upload_brush.GetAddressOf()))) {
                            render_target->FillRectangle(
                                D2D1::RectF(background.left, background.bottom - upload_height,
                                            background.right, background.bottom),
                                upload_brush.Get());
                        }
                    }
                    render_target->PopLayer();
                }
            } else {
                if (!composition_drawing) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush;
                    if (SUCCEEDED(render_target->CreateSolidColorBrush(
                            Rgb(tokens_.background, background_opacity),
                            background_brush.GetAddressOf()))) {
                        render_target->FillRectangle(
                            D2D1::RectF(0.0F, 0.0F, to_dip(size.width), to_dip(size.height)),
                            background_brush.Get());
                    }
                }
                DrawPanel(*render_target, *text_brush.Get(), window_dpi);
            }
            static_cast<void>(render_target->Flush());
            if (composition_drawing) {
                if (FAILED(frosted_glass_.EndDraw())) {
                    Invalidate();
                }
            } else {
                const auto draw_result = RenderResources::EndDraw(*render_target);
                if (draw_result == DrawResult::RecreateTarget) {
                    target_.Reset();
                }
            }
        } else if (composition_drawing) {
            static_cast<void>(frosted_glass_.EndDraw());
        }
    }
    static_cast<void>(device_context);
    ::EndPaint(window_, &paint);
}

// The single panel draws header and ranking rows in one surface; formatting allocations are
// caught internally and the remaining COM calls return HRESULT rather than throwing.
// NOLINTNEXTLINE(readability-function-cognitive-complexity, bugprone-exception-escape)
void FloatingWindow::DrawPanel(ID2D1RenderTarget& target, ID2D1SolidColorBrush& text_brush,
                               int window_dpi) noexcept {
    const auto to_dip = [window_dpi](std::int32_t pixels) {
        return static_cast<float>(PixelsToDip(pixels, window_dpi));
    };
    const auto logical_width = to_dip(rect_.Width());
    const auto right = logical_width - kPanelPaddingDip;
    const auto upload_right = right;
    const auto upload_left = upload_right - kRateWidthDip;
    const auto download_right = upload_left - kColumnGapDip;
    const auto download_left = download_right - kRateWidthDip;
    const auto name_right = download_left - kColumnGapDip;
    constexpr auto name_left =
        static_cast<float>(MonitorPanelLayout::kIconLeftDip + MonitorPanelLayout::kIconSizeDip +
                           MonitorPanelLayout::kIconTextGapDip);

    const auto header_top = static_cast<float>(MonitorPanelLayout::kVerticalPaddingDip);
    const auto header_bottom = header_top + static_cast<float>(MonitorPanelLayout::kHeaderRowDip);
    const auto header_center = (header_top + header_bottom) / 2.0F;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> product_icon;
    const auto product_icon_properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const auto& product_pixels = NetworkFlowMonitorIconPixels();
    if (SUCCEEDED(target.CreateBitmap(
            D2D1::SizeU(kNetworkFlowMonitorIconSize, kNetworkFlowMonitorIconSize),
            product_pixels.data(), kNetworkFlowMonitorIconSize * sizeof(std::uint32_t),
            product_icon_properties, product_icon.GetAddressOf()))) {
        constexpr auto icon_left = static_cast<float>(MonitorPanelLayout::kIconLeftDip);
        constexpr auto icon_size = static_cast<float>(MonitorPanelLayout::kIconSizeDip);
        const auto icon_top = header_center - (icon_size / 2.0F);
        target.DrawBitmap(
            product_icon.Get(),
            D2D1::RectF(icon_left, icon_top, icon_left + icon_size, icon_top + icon_size), 1.0F,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

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
            status = ResourceText(ResourceKey::StatusCollectorOffline, language_);
            show_status = true;
        } else if (snapshot_ == nullptr || snapshot_->status == ranking::RankingStatus::WarmingUp) {
            status = ResourceText(ResourceKey::StatusCollectingData, language_);
            show_status = true;
        } else if (snapshot_->applications.empty()) {
            status = ResourceText(ResourceKey::StatusNoNetworkActivity, language_);
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
        status = ResourceText(ResourceKey::StatusDataUnavailable, language_);
        show_status = true;
    }

    auto download_layout = resources_->CreateRateLayout(
        download_text, download_right - download_left, header_bottom - header_top);
    if (download_layout != nullptr) {
        target.DrawTextLayout(D2D1::Point2F(download_left, header_top), download_layout.Get(),
                              &text_brush);
    }
    auto upload_layout = resources_->CreateRateLayout(upload_text, upload_right - upload_left,
                                                      header_bottom - header_top);
    if (upload_layout != nullptr) {
        target.DrawTextLayout(D2D1::Point2F(upload_left, header_top), upload_layout.Get(),
                              &text_brush);
    }

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
            constexpr auto icon_left = static_cast<float>(MonitorPanelLayout::kIconLeftDip);
            target.DrawBitmap(icon_bitmap.Get(),
                              D2D1::RectF(icon_left, icon_top, icon_left + kApplicationIconSize,
                                          icon_top + kApplicationIconSize),
                              row.opacity);
        }
        text_brush.SetOpacity(row.opacity);
        target.DrawTextW(row.name.data(), static_cast<UINT32>(row.name.size()),
                         resources_->row_name_format(),
                         D2D1::RectF(name_left, y, name_right, y + row_height), &text_brush);
        auto row_download_layout =
            resources_->CreateRateLayout(row.download, download_right - download_left, row_height);
        if (row_download_layout != nullptr) {
            target.DrawTextLayout(D2D1::Point2F(download_left, y), row_download_layout.Get(),
                                  &text_brush);
        }
        auto row_upload_layout =
            resources_->CreateRateLayout(row.upload, upload_right - upload_left, row_height);
        if (row_upload_layout != nullptr) {
            target.DrawTextLayout(D2D1::Point2F(upload_left, y), row_upload_layout.Get(),
                                  &text_brush);
        }
    }
    text_brush.SetOpacity(1.0F);

    const auto ranked_row_count = static_cast<std::size_t>(snapshot_->applications.size());
    if (scrollbar_visible()) {
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
        self->window_ = window;
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
        static_cast<void>(::SetFocus(window));
        self->panel_press_point_ = point;
        self->panel_pressing_ = true;
        self->scrollbar_active_ = false;
        self->FinishRowAnimation();
        static_cast<void>(::SetCapture(window));
        self->Invalidate();
        self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelPress, point);
    }
        return 0;
    case WM_MOUSEMOVE:
        if (self->panel_pressing_ && (wparam & MK_LBUTTON) != 0U) {
            const auto point = ScreenPoint(window, lparam);
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
            self->panel_pressing_ = false;
            if (::GetCapture() == window) {
                ::ReleaseCapture();
            }
            self->observer_->OnMonitorPanelEvent(MonitorPanelEventKind::PanelRelease, point);
        }
        return 0;
    case WM_RBUTTONUP:
        self->observer_->OnWindowContextMenu();
        return 0;
    case WM_CAPTURECHANGED:
        if (self->panel_pressing_) {
            self->panel_pressing_ = false;
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
    case kRowAnimationFrameMessage:
        self->TickRowAnimation();
        static_cast<void>(::UpdateWindow(window));
        if (self->row_animation_active_) {
            static_cast<void>(::DwmFlush());
            static_cast<void>(::PostMessageW(window, kRowAnimationFrameMessage, 0U, 0));
        }
        return 0;
    case kHeightAnimationFrameMessage:
        self->TickHeightAnimation();
        static_cast<void>(::UpdateWindow(window));
        if (self->height_animation_active_) {
            static_cast<void>(::DwmFlush());
            static_cast<void>(::PostMessageW(window, kHeightAnimationFrameMessage, 0U, 0));
        }
        return 0;
    case WM_TIMER:
        if (wparam == kCapsuleRateAnimationTimerId) {
            self->TickCapsuleRateAnimation();
            static_cast<void>(::UpdateWindow(window));
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
        self->HandleEnvironmentChanged();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        self->HandleEnvironmentChanged();
        self->observer_->OnSystemAppearanceChanged();
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
        self->window_ = nullptr;
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
