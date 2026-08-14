#include "network_flow_monitor/ui/render_resources.h"

#include <algorithm>
#include <iterator>

namespace nfm::ui {

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

    result = dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               12.0F, L"", text_format_.GetAddressOf());
    if (FAILED(result)) {
        last_error_ = result;
        Reset();
        return false;
    }
    result = dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
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
    result = dwrite_factory_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               11.0F, L"", rate_format_.GetAddressOf());
    if (FAILED(result) || FAILED(rate_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING)) ||
        FAILED(rate_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)) ||
        FAILED(rate_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER))) {
        last_error_ = FAILED(result) ? result : E_FAIL;
        Reset();
        return false;
    }

    last_error_ = S_OK;
    return true;
}

void RenderResources::Reset() noexcept {
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
    const auto properties = D2D1::RenderTargetProperties();
    const auto hwnd_properties = D2D1::HwndRenderTargetProperties(
        window, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_NONE);
    static_cast<void>(
        d2d_factory_->CreateHwndRenderTarget(properties, hwnd_properties, target.GetAddressOf()));
    return target;
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
