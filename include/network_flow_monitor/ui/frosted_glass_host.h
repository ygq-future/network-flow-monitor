#ifndef NETWORK_FLOW_MONITOR_UI_FROSTED_GLASS_HOST_H
#define NETWORK_FLOW_MONITOR_UI_FROSTED_GLASS_HOST_H

#include "network_flow_monitor/ui/dpi_layout.h"

#include <windows.h>

#include <cstdint>
#include <d2d1_1.h>
#include <memory>
#include <wrl/client.h>

namespace nfm::ui {

// Owns one Windows.UI.Composition tree containing both the frosted backdrop and an opaque-content
// Direct2D surface above it. Keeping both layers in one tree prevents an asynchronously committed
// backdrop from covering the HWND redirection bitmap.
class FrostedGlassHost final {
  public:
    FrostedGlassHost() noexcept;
    ~FrostedGlassHost();
    FrostedGlassHost(const FrostedGlassHost&) = delete;
    FrostedGlassHost& operator=(const FrostedGlassHost&) = delete;
    FrostedGlassHost(FrostedGlassHost&&) = delete;
    FrostedGlassHost& operator=(FrostedGlassHost&&) = delete;

    [[nodiscard]] bool Initialize(HWND window, std::uint8_t blur_strength_percent) noexcept;
    void Reset() noexcept;
    void SetBlurStrength(std::uint8_t blur_strength_percent) noexcept;
    void SetTint(std::uint32_t rgb, std::uint8_t opacity_percent) noexcept;
    void SetClip(PixelRect local_bounds, float corner_radius_pixels) noexcept;
    void SetVisible(bool visible) noexcept;
    [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1DeviceContext> BeginDraw(PixelSize pixel_size,
                                                                       int dpi) noexcept;
    [[nodiscard]] HRESULT EndDraw() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] HRESULT last_error() const noexcept;
    [[nodiscard]] static float BlurStandardDeviation(std::uint8_t strength_percent) noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    HRESULT last_error_{S_OK};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_FROSTED_GLASS_HOST_H
