#include "network_flow_monitor/ui/floating_window.h"
#include "network_flow_monitor/ui/monitor_panel_layout.h"
#include "network_flow_monitor/ui/topmost_policy.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <dwmapi.h>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using nfm::ui::FloatingWindow;
using nfm::ui::InteractionState;
using nfm::ui::MonitorPanelSurface;
using nfm::ui::PixelRect;

struct TestContext {
    int failures{};

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
};

class RecordingObserver final : public nfm::ui::UiWindowObserver {
  public:
    void OnWindowInvalidated() noexcept override {
        ++invalidations;
    }
    void OnMonitorPanelEvent(nfm::ui::MonitorPanelEventKind event,
                             nfm::ui::PixelPoint) noexcept override {
        events.push_back(event);
    }
    void OnWindowContextMenu() noexcept override {
        ++context_menu_count;
    }

    std::vector<nfm::ui::MonitorPanelEventKind> events;
    int context_menu_count{};
    std::atomic<int> invalidations;
};

nfm::ui::RankingSnapshotPtr MakeRows(std::size_t count) {
    auto snapshot = std::make_shared<nfm::ranking::RankingSnapshot>();
    snapshot->status = nfm::ranking::RankingStatus::Ready;
    for (std::size_t index = 0; index < count; ++index) {
        snapshot->applications.push_back(
            {.id = "app-" + std::to_string(index),
             .metadata = {.display_name = "Application", .icon_key = {}},
             .rate = {.upload_bytes_per_second = static_cast<double>(index + 1),
                      .download_bytes_per_second = static_cast<double>(index + 2)}});
    }
    return snapshot;
}

void CheckDirtyDecisions(TestContext& context) {
    context.Check(!FloatingWindow::ShouldInvalidate(4U, 4U, InteractionState::Visible,
                                                    InteractionState::Visible),
                  "identical snapshot stays clean");
    context.Check(FloatingWindow::ShouldInvalidate(4U, 5U, InteractionState::Visible,
                                                   InteractionState::Visible),
                  "new snapshot invalidates");
    context.Check(FloatingWindow::ShouldInvalidate(4U, 4U, InteractionState::Visible,
                                                   InteractionState::EdgeHidden),
                  "interaction change invalidates");
}

void CheckDpiAndWorkArea(TestContext& context) {
    const PixelRect work_area{.left = -1920, .top = 0, .right = 0, .bottom = 1080};
    const PixelRect suggestion{.left = -2100, .top = 900, .right = -1700, .bottom = 1200};
    const auto floating = FloatingWindow::ApplyDpiSuggestion(suggestion, work_area);
    context.Check(floating.left >= work_area.left && floating.right <= work_area.right &&
                      floating.top >= work_area.top && floating.bottom <= work_area.bottom,
                  "DPI suggestion is clamped");
}

void CheckWindowStyles(TestContext& context) {
    static_assert(nfm::ui::MonitorPanelLayout::kWidthDip <= 340);
    static_assert(nfm::ui::MonitorPanelLayout::kIconSizeDip ==
                  static_cast<std::int32_t>(nfm::ui::kApplicationIconSize));
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_TOOLWINDOW) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_NOACTIVATE) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_TOPMOST) != 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_LAYERED) == 0U);
    static_assert((FloatingWindow::kExtendedStyle & WS_EX_NOREDIRECTIONBITMAP) == 0U);
    static_cast<void>(context);
}

void CheckTopmostPolicy(TestContext& context) {
    const nfm::ui::ForegroundWindowState fullscreen{
        .present = true,
        .visible = true,
        .bounds = {.left = 0, .top = 0, .right = 2560, .bottom = 1440},
        .monitor_bounds = {.left = 0, .top = 0, .right = 2560, .bottom = 1440}};
    context.Check(!nfm::ui::ShouldStayTopmost(true, fullscreen),
                  "configured monitor yields to a full-screen game or capture overlay");
    context.Check(nfm::ui::ShouldStayTopmost(false, fullscreen),
                  "disabling full-screen yielding preserves always-on-top behavior");
    auto windowed = fullscreen;
    windowed.bounds = {.left = 80, .top = 60, .right = 2480, .bottom = 1380};
    context.Check(nfm::ui::ShouldStayTopmost(true, windowed),
                  "ordinary foreground windows do not lower the monitor");
    auto own_fullscreen = fullscreen;
    own_fullscreen.belongs_to_current_process = true;
    context.Check(nfm::ui::ShouldStayTopmost(true, own_fullscreen),
                  "the monitor does not lower itself when its own UI is foreground");
}

void CheckCapsuleHitAdapter(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "capsule adapter creates main HWND");
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule, {},
                                       {.left = 200, .top = 100, .right = 210, .bottom = 164},
                                       {.left = 200, .top = 100, .right = 210, .bottom = 164}, {});
    window.Reposition({.left = 200, .top = 100, .right = 210, .bottom = 164});
    window.Show();
    static_cast<void>(
        ::RedrawWindow(window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
    const auto fallback_style = static_cast<DWORD>(::GetWindowLongPtrW(window.hwnd(), GWL_EXSTYLE));
    context.Check((fallback_style & WS_EX_NOREDIRECTIONBITMAP) == 0U,
                  "readable HWND fallback retains its DWM redirection surface");
    auto* const hit = window.hit_test_hwnd();
    context.Check(hit == window.hwnd(),
                  "capsule inward hit target shares the transparent main HWND and cannot expose "
                  "a fixed-width background box");
    RECT capsule_window_bounds{};
    static_cast<void>(::GetWindowRect(window.hwnd(), &capsule_window_bounds));
    context.Check(capsule_window_bounds.left == 200 && capsule_window_bounds.right == 210,
                  "single capsule HWND matches capsule bounds exactly");
    if (hit != nullptr) {
        ::SendMessageW(hit, WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
        ::SendMessageW(hit, WM_MOUSELEAVE, 0U, 0);
        ::SendMessageW(hit, WM_DPICHANGED, 0U, 0);

        ::SendMessageW(hit, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(1, 1));
        context.Check(::GetCapture() == window.hwnd() && ::IsWindowVisible(hit) != FALSE,
                      "capsule drag captures the single transparent hit surface");
        ::SendMessageW(window.hwnd(), WM_LBUTTONUP, 0U, MAKELPARAM(5, 5));
        context.Check(::GetCapture() == nullptr, "capsule drag releases capture");
    }
    context.Check(observer.events.size() >= 3U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::CapsulePointerEnter &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::CapsulePointerLeave &&
                      observer.events[2] == nfm::ui::MonitorPanelEventKind::EnvironmentChanged,
                  "capsule hit area normalizes pointer and environment messages");
    window.Destroy();
    context.Check(window.hwnd() == nullptr && window.hit_test_hwnd() == nullptr,
                  "capsule adapter destroys its single HWND");
}

void CheckPanelUsesMonitorPanelSeam(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "panel adapter creates its single HWND");
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Panel, {},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292}, {});
    ::SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
    ::SendMessageW(window.hwnd(), WM_MOUSELEAVE, 0U, 0);
    ::SendMessageW(window.hwnd(), WM_RBUTTONUP, 0U, MAKELPARAM(1, 1));
    context.Check(observer.events.size() >= 2U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::PanelPointerEnter &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelPointerLeave,
                  "panel pointer enter and leave reach MonitorPanelBehavior");
    context.Check(observer.context_menu_count == 1,
                  "right-clicking the panel opens the application menu");
    window.Destroy();
}

void CheckSurfaceTransitionRetargetsPointerTracking(TestContext& context) {
    const PixelRect tracked_bounds{.left = 100, .top = 100, .right = 300, .bottom = 200};
    context.Check(
        !FloatingWindow::ShouldDispatchPointerLeave(tracked_bounds, true, {.x = 150, .y = 150}) &&
            FloatingWindow::ShouldDispatchPointerLeave(tracked_bounds, true, {.x = 99, .y = 150}) &&
            !FloatingWindow::ShouldDispatchPointerLeave(tracked_bounds, false, {.x = 0, .y = 0}),
        "stale leave is ignored unless a cursor query proves the tracked surface was "
        "actually exited");

    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "surface-transition adapter creates its HWNDs");
    POINT original_cursor{};
    const auto cursor_saved = ::GetCursorPos(&original_cursor) != FALSE;
    context.Check(cursor_saved && ::SetCursorPos(205, 110) != FALSE,
                  "surface-transition test positions the real cursor inside the capsule");
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule, {},
                                       {.left = 200, .top = 100, .right = 210, .bottom = 164},
                                       {.left = 200, .top = 100, .right = 216, .bottom = 164}, {});
    window.Reposition({.left = 200, .top = 100, .right = 210, .bottom = 164});
    ::SendMessageW(window.hit_test_hwnd(), WM_MOUSEMOVE, 0U, MAKELPARAM(1, 1));
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Panel, {},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292}, {});
    window.Reposition({.left = 200, .top = 100, .right = 600, .bottom = 292});
    context.Check(::SetCursorPos(0, 0) != FALSE,
                  "surface-transition test moves the real cursor outside the panel");
    ::SendMessageW(window.hwnd(), WM_MOUSELEAVE, 0U, 0);
    context.Check(observer.events.size() >= 2U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::CapsulePointerEnter &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelPointerLeave,
                  "capsule reveal retargets tracking to the panel so its leave can schedule "
                  "auto-hide");
    if (cursor_saved) {
        static_cast<void>(::SetCursorPos(original_cursor.x, original_cursor.y));
    }
    window.Destroy();
}

void CheckPanelHasNoPinControl(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "pin gesture adapter creates its HWND");
    const auto dpi = static_cast<int>(::GetDpiForWindow(window.hwnd()));
    const auto width = nfm::ui::DipToPixels(400, dpi);
    const auto height = nfm::ui::DipToPixels(80, dpi);
    window.SetMonitorPanelPresentation(
        MonitorPanelSurface::Panel, {},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + height},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + height}, {});
    window.Reposition({.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + height});
    const auto pin_x = width - nfm::ui::DipToPixels(18, dpi);
    const auto pin_y = nfm::ui::DipToPixels(18, dpi);
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pin_x, pin_y));
    context.Check(observer.events.size() == 1U &&
                      observer.events.back() == nfm::ui::MonitorPanelEventKind::PanelPress,
                  "header press remains a panel drag gesture");
    ::SendMessageW(window.hwnd(), WM_LBUTTONUP, 0U, MAKELPARAM(pin_x, pin_y));
    context.Check(observer.events.size() == 2U &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PanelRelease,
                  "header release does not alter Pin");

    observer.events.clear();
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pin_x, pin_y));
    ::SendMessageW(window.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(pin_x - 40, pin_y + 40));
    ::SendMessageW(window.hwnd(), WM_LBUTTONUP, 0U, MAKELPARAM(pin_x, pin_y));
    context.Check(observer.events.size() == 3U &&
                      observer.events[0] == nfm::ui::MonitorPanelEventKind::PanelPress &&
                      observer.events[1] == nfm::ui::MonitorPanelEventKind::PointerMove &&
                      observer.events[2] == nfm::ui::MonitorPanelEventKind::PanelRelease,
                  "crossing the drag threshold cancels pin activation");

    window.Destroy();
}

constexpr UINT kTrafficMessage = WM_APP + 0x55U;

void PumpWindowMessagesFor(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    MSG message{};
    while (std::chrono::steady_clock::now() < deadline) {
        while (::PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        std::this_thread::yield();
    }
}

void PumpQueuedMessagesFor(std::chrono::milliseconds duration, std::atomic<int>& pending_traffic) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    MSG message{};
    while (std::chrono::steady_clock::now() < deadline) {
        int processed{};
        while (processed < 64 && ::PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
            if (message.message == kTrafficMessage) {
                --pending_traffic;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
            ++processed;
        }
        std::this_thread::yield();
    }
}

void CheckAnimationFramesSurviveMessageTraffic(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "animation adapter creates its HWND");
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Panel, {},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292},
                                       {.left = 200, .top = 100, .right = 600, .bottom = 292},
                                       {.left = 212, .top = 141, .right = 588, .bottom = 280});
    window.Reposition({.left = 200, .top = 100, .right = 600, .bottom = 292});
    window.SetSnapshot(MakeRows(4), 1U);
    window.Show();
    auto reordered = std::make_shared<nfm::ranking::RankingSnapshot>(*MakeRows(4));
    std::swap(reordered->applications.at(0), reordered->applications.at(1));
    observer.invalidations.store(0, std::memory_order_release);
    window.SetSnapshot(reordered, 2U);

    std::atomic<int> pending_traffic{};
    std::jthread traffic{[hwnd = window.hwnd(), &pending_traffic](const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            if (pending_traffic.load(std::memory_order_acquire) < 256) {
                ++pending_traffic;
                if (::PostMessageW(hwnd, kTrafficMessage, 0U, 0) == FALSE) {
                    --pending_traffic;
                }
            } else {
                std::this_thread::yield();
            }
        }
    }};
    PumpQueuedMessagesFor(std::chrono::milliseconds{220}, pending_traffic);
    traffic.request_stop();
    traffic.join();
    context.Check(observer.invalidations.load(std::memory_order_acquire) >= 6,
                  "ranking transition produces multiple frames even while ordinary window "
                  "messages keep the queue busy");
    window.Destroy();
}

void CheckBackdropAndOpacity(TestContext& context) {
    context.Check(nfm::ui::FrostedGlassHost::BlurStandardDeviation(0U) == 0.0F &&
                      nfm::ui::FrostedGlassHost::BlurStandardDeviation(50U) == 15.0F &&
                      nfm::ui::FrostedGlassHost::BlurStandardDeviation(100U) == 30.0F,
                  "public blur strength maps across the full Gaussian blur range");
    nfm::ui::RenderResources resources;
    context.Check(resources.Initialize(), "opacity test initializes Direct2D resources");
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "opacity test creates its HWND");
    if (!window.frosted_glass_active()) {
        std::cerr << "Frosted glass HRESULT: 0x" << std::hex
                  << static_cast<unsigned long>(window.frosted_glass_error()) << std::dec << '\n';
    }
    context.Check(window.frosted_glass_active(),
                  "Windows 11 creates the native adjustable backdrop composition visual");
    const PixelRect bounds{.left = 220, .top = 160, .right = 620, .bottom = 320};
    const auto create_background = [&bounds](std::int32_t left, std::int32_t right, DWORD style) {
        return ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
                                 WS_POPUP | WS_VISIBLE | style, left, bounds.top, right - left,
                                 bounds.Height(), nullptr, nullptr, ::GetModuleHandleW(nullptr),
                                 nullptr);
    };
    const auto midpoint = bounds.left + (bounds.Width() / 2);
    auto* const light_background = create_background(bounds.left, midpoint, SS_WHITERECT);
    auto* const dark_background = create_background(midpoint, bounds.right, SS_BLACKRECT);
    context.Check(light_background != nullptr && dark_background != nullptr,
                  "opacity test creates a two-tone controlled backdrop");
    static_cast<void>(::RedrawWindow(light_background, nullptr, nullptr,
                                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW));
    static_cast<void>(::RedrawWindow(dark_background, nullptr, nullptr,
                                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW));
    static_cast<void>(::DwmFlush());
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Panel, {}, bounds, bounds, {});
    window.Reposition(bounds);
    window.Show();
    static_cast<void>(::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));

    const auto sample = [&window, &bounds](std::uint8_t opacity) {
        window.SetDisplayOptions({.opacity_percent = opacity, .blur_strength_percent = 0U});
        // The first HostBackdrop commit after Show is asynchronous. Drive a bounded number of
        // presents before screen readback so this verifies the settled material instead of an
        // arbitrary pre-commit frame.
        for (int attempt = 0; attempt < 8; ++attempt) {
            static_cast<void>(
                ::RedrawWindow(window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
            static_cast<void>(::DwmFlush());
            PumpWindowMessagesFor(std::chrono::milliseconds{20});
        }
        static_cast<void>(::DwmFlush());
        std::vector<COLORREF> colors;
        auto* const screen = ::GetDC(nullptr);
        if (screen != nullptr) {
            for (int x = bounds.left + 24; x < bounds.right - 24; x += 32) {
                colors.push_back(::GetPixel(screen, x, bounds.bottom - 20));
            }
            static_cast<void>(::ReleaseDC(nullptr, screen));
        }
        return colors;
    };
    auto translucent = sample(0U);
    const auto contrast_fn = [](COLORREF first, COLORREF last) {
        return std::abs(static_cast<int>(GetRValue(first)) - static_cast<int>(GetRValue(last))) +
               std::abs(static_cast<int>(GetGValue(first)) - static_cast<int>(GetGValue(last))) +
               std::abs(static_cast<int>(GetBValue(first)) - static_cast<int>(GetBValue(last)));
    };
    int translucent_contrast =
        (translucent.size() >= 2U) ? contrast_fn(translucent.front(), translucent.back()) : 0;
    for (int settle = 0; settle < 10 && translucent_contrast < 20; ++settle) {
        PumpWindowMessagesFor(std::chrono::milliseconds{80});
        translucent = sample(0U);
        translucent_contrast =
            (translucent.size() >= 2U) ? contrast_fn(translucent.front(), translucent.back()) : 0;
    }
    const auto dpi = static_cast<int>(::GetDpiForWindow(window.hwnd()));
    const auto icon_left =
        bounds.left + nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kIconLeftDip, dpi);
    const auto header_offset_dip =
        static_cast<double>(nfm::ui::MonitorPanelLayout::kVerticalPaddingDip) +
        (static_cast<double>(nfm::ui::MonitorPanelLayout::kHeaderRowDip -
                             nfm::ui::MonitorPanelLayout::kIconSizeDip) /
         2.0);
    const auto icon_top = bounds.top + nfm::ui::DipToPixels(header_offset_dip, dpi);
    const auto icon_size = nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kIconSizeDip, dpi);
    const auto sample_content_component_range = [icon_left, icon_top, icon_size]() {
        unsigned minimum_component{255U};
        unsigned maximum_component{};
        auto* const screen = ::GetDC(nullptr);
        if (screen == nullptr) {
            return 0U;
        }
        for (int y = icon_top; y < icon_top + icon_size; ++y) {
            for (int x = icon_left; x < icon_left + icon_size; ++x) {
                const auto pixel = ::GetPixel(screen, x, y);
                minimum_component = std::min(minimum_component,
                                             std::min({static_cast<unsigned>(GetRValue(pixel)),
                                                       static_cast<unsigned>(GetGValue(pixel)),
                                                       static_cast<unsigned>(GetBValue(pixel))}));
                maximum_component = std::max(maximum_component,
                                             std::max({static_cast<unsigned>(GetRValue(pixel)),
                                                       static_cast<unsigned>(GetGValue(pixel)),
                                                       static_cast<unsigned>(GetBValue(pixel))}));
            }
        }
        static_cast<void>(::ReleaseDC(nullptr, screen));
        return maximum_component - minimum_component;
    };
    unsigned content_range_translucent = sample_content_component_range();
    for (int settle = 0; settle < 10 && content_range_translucent < 8U; ++settle) {
        if (settle > 0) {
            static_cast<void>(
                ::RedrawWindow(window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
            static_cast<void>(::DwmFlush());
            PumpWindowMessagesFor(std::chrono::milliseconds{80});
        }
        content_range_translucent = sample_content_component_range();
    }
    const auto opaque = sample(100U);
    if (!window.frosted_glass_active()) {
        std::cerr << "Frosted glass stopped during opacity sampling: 0x" << std::hex
                  << static_cast<unsigned long>(window.frosted_glass_error()) << std::dec << '\n';
    }
    // Composition surface commits are asynchronous and may lag behind DwmFlush.
    // Retry sampling with additional presents to accommodate compositor latency.
    unsigned content_component_range = sample_content_component_range();
    for (int settle = 0; settle < 10 && content_component_range < 8U; ++settle) {
        if (settle > 0) {
            static_cast<void>(
                ::RedrawWindow(window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
            static_cast<void>(::DwmFlush());
            PumpWindowMessagesFor(std::chrono::milliseconds{80});
        }
        content_component_range = sample_content_component_range();
    }
    const auto measured_content_range =
        std::max(content_range_translucent, content_component_range);
    context.Check(measured_content_range >= 8U,
                  "the composition backdrop never covers the opaque header content");
    unsigned long difference{};
    for (std::size_t index = 0; index < std::min(translucent.size(), opaque.size()); ++index) {
        difference +=
            static_cast<unsigned long>(std::abs(static_cast<int>(GetRValue(translucent.at(index))) -
                                                static_cast<int>(GetRValue(opaque.at(index)))) +
                                       std::abs(static_cast<int>(GetGValue(translucent.at(index))) -
                                                static_cast<int>(GetGValue(opaque.at(index)))) +
                                       std::abs(static_cast<int>(GetBValue(translucent.at(index))) -
                                                static_cast<int>(GetBValue(opaque.at(index)))));
    }
    if (difference < 120U) {
        std::cerr << "Opacity pixel difference: " << difference << " translucent:";
        for (const auto color : translucent) {
            std::cerr << ' ' << static_cast<unsigned>(GetRValue(color)) << ','
                      << static_cast<unsigned>(GetGValue(color)) << ','
                      << static_cast<unsigned>(GetBValue(color));
        }
        std::cerr << " opaque:";
        for (const auto color : opaque) {
            std::cerr << ' ' << static_cast<unsigned>(GetRValue(color)) << ','
                      << static_cast<unsigned>(GetGValue(color)) << ','
                      << static_cast<unsigned>(GetBValue(color));
        }
        std::cerr << '\n';
    }
    context.Check(translucent.size() == opaque.size() && !translucent.empty() && difference >= 120U,
                  "changing monitor opacity changes the composited background pixels");
    if (translucent.size() >= 2U && opaque.size() >= 2U) {
        if (translucent_contrast < 20) {
            std::cerr << "Zero-opacity backdrop contrast: " << translucent_contrast << '\n';
        }
        context.Check(translucent_contrast >= 20,
                      "zero opacity reveals the real two-tone backdrop instead of a flat tint");
    }

    window.SetDisplayOptions({.opacity_percent = 30U});
    static_cast<void>(
        ::RedrawWindow(window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
    static_cast<void>(::DwmFlush());
    ::Sleep(80U);
    static_cast<void>(::DwmFlush());
    // Creating a new Gaussian effect graph is asynchronous and may complete several DWM
    // presents after the property change, especially on the first use after boot. Exercise
    // that bounded transition before measuring steady repaints; a feedback loop between the
    // HWND surface and backdrop continues after this warm-up and is still caught below.
    for (int frame = 0; frame < 24; ++frame) {
        static_cast<void>(
            ::RedrawWindow(window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
        static_cast<void>(::DwmFlush());
    }
    std::vector<unsigned> stable_red_samples;
    std::vector<unsigned> stable_green_samples;
    std::vector<unsigned> stable_blue_samples;
    for (int frame = 0; frame < 24; ++frame) {
        static_cast<void>(
            ::RedrawWindow(window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
        static_cast<void>(::DwmFlush());
        auto* const screen = ::GetDC(nullptr);
        if (screen != nullptr) {
            const auto pixel = ::GetPixel(screen, bounds.left + 48, bounds.bottom - 20);
            stable_red_samples.push_back(static_cast<unsigned>(GetRValue(pixel)));
            stable_green_samples.push_back(static_cast<unsigned>(GetGValue(pixel)));
            stable_blue_samples.push_back(static_cast<unsigned>(GetBValue(pixel)));
            static_cast<void>(::ReleaseDC(nullptr, screen));
        }
    }
    const auto avoids_surface_alternation = [](const std::vector<unsigned>& samples) {
        int previous_direction{};
        int reversals{};
        unsigned maximum_step{};
        for (std::size_t index = 1; index < samples.size(); ++index) {
            const auto difference =
                static_cast<int>(samples.at(index)) - static_cast<int>(samples.at(index - 1U));
            maximum_step = std::max(maximum_step, static_cast<unsigned>(std::abs(difference)));
            if (std::abs(difference) <= 2) {
                continue;
            }
            const auto direction = difference > 0 ? 1 : -1;
            if (previous_direction != 0 && direction != previous_direction) {
                ++reversals;
            }
            previous_direction = direction;
        }
        // A newly committed material may converge monotonically from the previous brush with
        // one large first step. That is a state transition, not the old surface-feedback bug.
        // Once the samples reverse direction, keep the step bound so a dark/light flash or
        // repeated surface alternation still fails the regression.
        return reversals == 0 || (reversals == 1 && maximum_step <= 40U);
    };
    const auto stable_background = avoids_surface_alternation(stable_red_samples) &&
                                   avoids_surface_alternation(stable_green_samples) &&
                                   avoids_surface_alternation(stable_blue_samples);
    context.Check(stable_background,
                  "repainting never alternates between composition surfaces or flashes");

    const PixelRect capsule{.left = 610, .top = 160, .right = 620, .bottom = 224};
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule, {}, capsule, capsule, {});
    window.Reposition(capsule);
    DWM_SYSTEMBACKDROP_TYPE backdrop{};
    const auto backdrop_result = ::DwmGetWindowAttribute(window.hwnd(), DWMWA_SYSTEMBACKDROP_TYPE,
                                                         &backdrop, sizeof(backdrop));
    context.Check(SUCCEEDED(backdrop_result) && backdrop == DWMSBT_NONE,
                  "dock capsule does not expose a second Acrylic rectangle around its tracks");
    window.Destroy();
    if (light_background != nullptr) {
        ::DestroyWindow(light_background);
    }
    if (dark_background != nullptr) {
        ::DestroyWindow(dark_background);
    }
}

void CheckRankingScrollInput(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "scrollable panel creates its HWND");
    const auto dpi = static_cast<int>(::GetDpiForWindow(window.hwnd()));
    const auto width = nfm::ui::DipToPixels(400, dpi);
    const auto row = nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kRowDip, dpi);
    const auto header = nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kHeaderRowDip, dpi);
    const auto padding =
        nfm::ui::DipToPixels(nfm::ui::MonitorPanelLayout::kVerticalPaddingDip, dpi);
    const auto panel_height = padding + header + (2 * row) + padding;
    window.SetMonitorPanelPresentation(
        MonitorPanelSurface::Panel, {},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + panel_height},
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + panel_height},
        {.left = 200 + padding,
         .top = 100 + padding + header,
         .right = 200 + width - padding,
         .bottom = 100 + panel_height - padding});
    window.Reposition(
        {.left = 200, .top = 100, .right = 200 + width, .bottom = 100 + panel_height});
    window.Show();
    window.SetSnapshot(MakeRows(9), 1U);
    ::SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0U, MAKELPARAM(10, 70));
    context.Check(window.scrollbar_visible(), "hovering an overflowing ranking reveals scrollbar");
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 70));
    context.Check(!window.scrollbar_visible(), "dragging the panel suppresses its scrollbar");
    ::SendMessageW(window.hwnd(), WM_LBUTTONUP, 0U, MAKELPARAM(10, 70));
    context.Check(::GetFocus() == window.hwnd(), "clicking the panel assigns keyboard focus");
    ::SendMessageW(window.hwnd(), WM_KEYDOWN, VK_END, 0);
    context.Check(window.scroll_row_offset() > 0, "End scrolls to the last ranking viewport");
    ::SendMessageW(window.hwnd(), WM_KEYDOWN, VK_HOME, 0);
    context.Check(window.scroll_row_offset() == 0, "Home scrolls to the first ranking row");
    ::SendMessageW(window.hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    context.Check(window.scroll_row_offset() == 1,
                  "mouse wheel and touchpad-compatible wheel input scroll rows");
    auto reordered = std::make_shared<nfm::ranking::RankingSnapshot>(*MakeRows(9));
    std::swap(reordered->applications.at(0), reordered->applications.at(1));
    window.SetSnapshot(reordered, 2U);
    context.Check(window.row_animation_active(),
                  "a new snapshot retargets retained rows from their current positions");
    ::SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 70));
    context.Check(!window.row_animation_active(),
                  "starting a panel drag stops compositor-paced row frames from blocking input");
    auto reordered_during_drag = std::make_shared<nfm::ranking::RankingSnapshot>(*reordered);
    std::swap(reordered_during_drag->applications.at(1), reordered_during_drag->applications.at(2));
    window.SetSnapshot(reordered_during_drag, 3U);
    context.Check(!window.row_animation_active(),
                  "snapshots received during a panel drag cannot restart blocking row frames");
    window.SetSnapshot(nullptr, 4U);
    context.Check(window.scroll_row_offset() == 0,
                  "disconnect clears scroll state before a reconnect warm-up");
    window.Destroy();
}

void CheckCapsuleRateAnimation(TestContext& context) {
    nfm::ui::RenderResources resources;
    RecordingObserver observer;
    FloatingWindow window{::GetModuleHandleW(nullptr), resources, observer};
    context.Check(window.Create(), "capsule-rate adapter creates its HWND");
    const PixelRect capsule{.left = 200, .top = 100, .right = 210, .bottom = 164};
    const auto presentation = [](float download, float upload, std::uint32_t dl_color = 0x19D8E6U,
                                 std::uint32_t ul_color = 0xF4D03FU) {
        return nfm::ui::CapsulePresentation{.download = {.fill = download, .color = dl_color},
                                            .upload = {.fill = upload, .color = ul_color}};
    };

    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule,
                                       presentation(0.0F, 0.0F, 0x19D8E6U, 0xF4D03FU), capsule,
                                       capsule, {});
    window.Reposition(capsule);
    window.Show();
    context.Check(!window.capsule_animation_active(),
                  "the initial capsule fill settles without a transition");

    // A rate increase retargets the fill from its current visual value instead of snapping.
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule,
                                       presentation(1.0F, 0.5F, 0x8146B8U, 0xF39C32U), capsule,
                                       capsule, {});
    context.Check(window.capsule_animation_active(),
                  "a download-rate increase starts a capsule-fill transition");
    context.Check(window.displayed_capsule().download.fill < 1.0F &&
                      window.displayed_capsule().upload.fill < 0.5F,
                  "the fill is still approaching its target instead of teleporting");
    context.Check(window.displayed_capsule().download.color != 0x8146B8U,
                  "the gradient color is smoothly interpolating across channels");

    PumpWindowMessagesFor(std::chrono::milliseconds{250});
    context.Check(!window.capsule_animation_active() &&
                      window.displayed_capsule().download.fill == 1.0F &&
                      window.displayed_capsule().upload.fill == 0.5F &&
                      window.displayed_capsule().download.color == 0x8146B8U &&
                      window.displayed_capsule().upload.color == 0xF39C32U,
                  "the capsule-fill transition settles exactly on the new rate and color");

    // A rate decrease also smoothly transitions downward.
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule,
                                       presentation(0.2F, 0.1F, 0x2878D0U, 0xF4D03FU), capsule,
                                       capsule, {});
    context.Check(window.capsule_animation_active(),
                  "a rate decrease starts a decreasing capsule-fill transition");
    context.Check(window.displayed_capsule().download.fill > 0.2F,
                  "the decreasing fill approaches the lower target smoothly");

    PumpWindowMessagesFor(std::chrono::milliseconds{350});
    context.Check(!window.capsule_animation_active() &&
                      window.displayed_capsule().download.fill == 0.2F &&
                      window.displayed_capsule().upload.fill == 0.1F &&
                      window.displayed_capsule().download.color == 0x2878D0U,
                  "the decrease settles on the lower target");

    // Under reduced motion (0 ms duration token), transitions complete immediately.
    auto tokens = nfm::ui::ResolveVisualTokens(nfm::ui::ResolvedTheme::Dark, false);
    tokens.capsule_rate_interpolation = std::chrono::milliseconds{0};
    window.SetAppearance(tokens, nfm::ui::ResolvedLanguage::English);
    window.SetMonitorPanelPresentation(MonitorPanelSurface::Capsule,
                                       presentation(0.8F, 0.9F, 0x8146B8U, 0xD93636U), capsule,
                                       capsule, {});
    context.Check(!window.capsule_animation_active() &&
                      window.displayed_capsule().download.fill == 0.8F &&
                      window.displayed_capsule().upload.fill == 0.9F &&
                      window.displayed_capsule().download.color == 0x8146B8U,
                  "reduced motion completes capsule rate changes immediately");

    window.Destroy();
}

void CheckApplicationIconFallback(TestContext& context) {
    nfm::ui::ApplicationIconCache cache;
    const auto fallback = cache.Lookup({});
    context.Check(fallback != nullptr && fallback->fallback,
                  "empty/system identities receive the stable application icon fallback");
    cache.Request("Z:\\path-that-does-not-exist\\missing.exe");
    const auto pending = cache.Lookup("Z:\\path-that-does-not-exist\\missing.exe");
    context.Check(pending == fallback,
                  "icon extraction requests return fallback immediately instead of blocking UI");
}

} // namespace

int main() {
    TestContext context;
    CheckDirtyDecisions(context);
    CheckDpiAndWorkArea(context);
    CheckWindowStyles(context);
    CheckTopmostPolicy(context);
    CheckCapsuleHitAdapter(context);
    CheckPanelUsesMonitorPanelSeam(context);
    CheckSurfaceTransitionRetargetsPointerTracking(context);
    CheckPanelHasNoPinControl(context);
    CheckRankingScrollInput(context);
    CheckAnimationFramesSurviveMessageTraffic(context);
    CheckBackdropAndOpacity(context);
    CheckCapsuleRateAnimation(context);
    CheckApplicationIconFallback(context);
    return context.failures == 0 ? 0 : 1;
}
