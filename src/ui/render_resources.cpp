#include "network_flow_monitor/ui/render_resources.h"

#include <algorithm>
#include <iterator>

namespace nfm::ui {
namespace {

// Preferred family on Windows 11; DirectWrite's built-in font fallback substitutes a
// readable equivalent (typically Segoe UI) on systems where it is unavailable.
constexpr auto kTextFamily = L"Segoe UI Variable";

} // namespace

bool RenderResources::Initialize() noexcept {
    if (initialized()) {
        return true;
    }

    Reset();
    const D2D1_FACTORY_OPTIONS options{};
    auto result =
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options,
                          reinterpret_cast<void**>(d2d_factory_.GetAddressOf()));
    if (FAILED(result)) {
        last_error_ = result;
        return false;
    }

    Microsoft::WRL::ComPtr<IUnknown> dwrite_base;
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 dwrite_base.GetAddressOf());
    if (FAILED(result) || FAILED(dwrite_base.As(&dwrite_factory_))) {
        last_error_ = FAILED(result) ? result : E_NOINTERFACE;
        Reset();
        return false;
    }

    result = dwrite_factory_->CreateTextFormat(kTextFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               12.0F, L"", text_format_.GetAddressOf());
    if (FAILED(result)) {
        last_error_ = result;
        Reset();
        return false;
    }
    result = dwrite_factory_->CreateTextFormat(kTextFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               12.0F, L"", row_name_format_.GetAddressOf());
    if (FAILED(result) || FAILED(row_name_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)) ||
        FAILED(row_name_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER))) {
        last_error_ = FAILED(result) ? result : E_FAIL;
        Reset();
        return false;
    }
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    DWRITE_TRIMMING trimming{.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER,
                             .delimiter = 0U,
                             .delimiterCount = 0U};
    result = dwrite_factory_->CreateEllipsisTrimmingSign(row_name_format_.Get(),
                                                         ellipsis.GetAddressOf());
    if (FAILED(result) || FAILED(row_name_format_->SetTrimming(&trimming, ellipsis.Get()))) {
        last_error_ = FAILED(result) ? result : E_FAIL;
        Reset();
        return false;
    }
    result = dwrite_factory_->CreateTextFormat(kTextFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               11.0F, L"", rate_format_.GetAddressOf());
    if (FAILED(result) || FAILED(rate_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING)) ||
        FAILED(rate_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)) ||
        FAILED(rate_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER))) {
        last_error_ = FAILED(result) ? result : E_FAIL;
        Reset();
        return false;
    }
    result = dwrite_factory_->CreateTypography(rate_typography_.GetAddressOf());
    if (FAILED(result) ||
        FAILED(rate_typography_->AddFontFeature(DWRITE_FONT_FEATURE{
            .nameTag = DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, .parameter = 1U}))) {
        last_error_ = FAILED(result) ? result : E_FAIL;
        Reset();
        return false;
    }

    // Segoe Fluent Icons ships with Windows 11 and provides the Pin/Unpin/Pinned
    // glyphs used by the monitor-panel Pin button. DirectWrite substitutes a
    // readable fallback on systems where the family is absent.
    result = dwrite_factory_->CreateTextFormat(
        L"Segoe Fluent Icons", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 16.0F, L"", icon_format_.GetAddressOf());
    if (FAILED(result) || FAILED(icon_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER)) ||
        FAILED(icon_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER))) {
        last_error_ = FAILED(result) ? result : E_FAIL;
        Reset();
        return false;
    }

    last_error_ = S_OK;
    return true;
}

void RenderResources::Reset() noexcept {
    icon_format_.Reset();
    rate_typography_.Reset();
    rate_format_.Reset();
    row_name_format_.Reset();
    text_format_.Reset();
    dwrite_factory_.Reset();
    d2d_factory_.Reset();
}

Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> RenderResources::CreateHwndRenderTarget(
    HWND window, PixelSize pixel_size) const noexcept {
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target;
    if (!initialized() || window == nullptr || pixel_size.width <= 0 || pixel_size.height <= 0) {
        return target;
    }

    const auto width = static_cast<UINT>(pixel_size.width);
    const auto height = static_cast<UINT>(pixel_size.height);
    // Premultiplied alpha lets the cleared background tint stay translucent so the
    // DWM Acrylic system backdrop shows through, while opaque text/icon content keeps
    // full coverage on top of it.
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const auto hwnd_properties = D2D1::HwndRenderTargetProperties(
        window, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_NONE);
    static_cast<void>(
        d2d_factory_->CreateHwndRenderTarget(properties, hwnd_properties, target.GetAddressOf()));
    return target;
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> RenderResources::CreateRateLayout(
    std::wstring_view text, float max_width, float max_height) const noexcept {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!initialized() || rate_typography_ == nullptr) {
        return layout;
    }
    static_cast<void>(dwrite_factory_->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.size()), rate_format_.Get(), max_width, max_height,
        layout.GetAddressOf()));
    if (layout != nullptr) {
        static_cast<void>(layout->SetTypography(
            rate_typography_.Get(), DWRITE_TEXT_RANGE{.startPosition = 0U, .length = UINT32_MAX}));
    }
    return layout;
}

void RenderResources::BeginDraw(ID2D1RenderTarget& target) noexcept {
    target.BeginDraw();
}

DrawResult RenderResources::EndDraw(ID2D1RenderTarget& target) noexcept {
    return ClassifyEndDraw(target.EndDraw());
}

DrawResult RenderResources::ClassifyEndDraw(HRESULT result) noexcept {
    if (result == S_OK) {
        return DrawResult::Complete;
    }
    if (result == D2DERR_RECREATE_TARGET) {
        return DrawResult::RecreateTarget;
    }
    return DrawResult::Failed;
}

} // namespace nfm::ui
