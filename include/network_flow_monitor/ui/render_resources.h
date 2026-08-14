#ifndef NETWORK_FLOW_MONITOR_UI_RENDER_RESOURCES_H
#define NETWORK_FLOW_MONITOR_UI_RENDER_RESOURCES_H

#include "network_flow_monitor/ui/dpi_layout.h"

#include <windows.h>

#include <cstdint>
#include <d2d1.h>
#include <dwrite.h>
#include <strsafe.h>
#include <wrl/client.h>

namespace nfm::ui {

enum class DrawResult : std::uint8_t {
    Complete,
    RecreateTarget,
    Failed
};

class RenderResources final {
  public:
    RenderResources() = default;
    ~RenderResources() = default;
    RenderResources(const RenderResources&) = delete;
    RenderResources& operator=(const RenderResources&) = delete;
    RenderResources(RenderResources&&) = delete;
    RenderResources& operator=(RenderResources&&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return d2d_factory_ != nullptr && dwrite_factory_ != nullptr && text_format_ != nullptr &&
               row_name_format_ != nullptr && rate_format_ != nullptr;
    }
    [[nodiscard]] HRESULT last_error() const noexcept {
        return last_error_;
    }
    [[nodiscard]] ID2D1Factory* d2d_factory() const noexcept {
        return d2d_factory_.Get();
    }
    [[nodiscard]] IDWriteFactory* dwrite_factory() const noexcept {
        return dwrite_factory_.Get();
    }
    [[nodiscard]] IDWriteTextFormat* text_format() const noexcept {
        return text_format_.Get();
    }
    [[nodiscard]] IDWriteTextFormat* row_name_format() const noexcept {
        return row_name_format_.Get();
    }
    [[nodiscard]] IDWriteTextFormat* rate_format() const noexcept {
        return rate_format_.Get();
    }

    [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> CreateHwndRenderTarget(
        HWND window, PixelSize pixel_size) const noexcept;
    static void BeginDraw(ID2D1RenderTarget& target) noexcept;
    [[nodiscard]] static DrawResult EndDraw(ID2D1RenderTarget& target) noexcept;
    [[nodiscard]] static DrawResult ClassifyEndDraw(HRESULT result) noexcept;

  private:
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> row_name_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> rate_format_;
    HRESULT last_error_{S_OK};
};

} // namespace nfm::ui

#endif // NETWORK_FLOW_MONITOR_UI_RENDER_RESOURCES_H
