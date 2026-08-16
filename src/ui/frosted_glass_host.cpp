#include "network_flow_monitor/ui/frosted_glass_host.h"

#include <DispatcherQueue.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string_view>
#include <windows.graphics.directx.h>
#include <windows.graphics.effects.interop.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/base.h>
#include <wrl.h>

namespace nfm::ui {
namespace {

using GraphicsEffect = ABI::Windows::Graphics::Effects::IGraphicsEffect;
using GraphicsEffectSource = ABI::Windows::Graphics::Effects::IGraphicsEffectSource;
using GraphicsEffectInterop = ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop;
using PropertyMapping = ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING;

template <typename Interface> using WinrtAbi = winrt::impl::abi_t<Interface>;

template <typename Interface>
[[nodiscard]] HRESULT GetActivationFactory(
    LPCWSTR runtime_class, Microsoft::WRL::ComPtr<WinrtAbi<Interface>>& factory) noexcept {
    const Microsoft::WRL::Wrappers::HStringReference class_name{runtime_class};
    return ::RoGetActivationFactory(class_name.Get(), winrt::guid_of<Interface>(),
                                    reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf()));
}

template <typename Interface>
[[nodiscard]] Microsoft::WRL::ComPtr<WinrtAbi<Interface>> QueryWinrtInterface(IUnknown* object) {
    if (object == nullptr) {
        winrt::throw_hresult(E_POINTER);
    }
    Microsoft::WRL::ComPtr<WinrtAbi<Interface>> result;
    winrt::check_hresult(object->QueryInterface(winrt::guid_of<Interface>(),
                                                reinterpret_cast<void**>(result.GetAddressOf())));
    return result;
}

class ScopedRoInitialization final {
  public:
    ScopedRoInitialization() {
        const auto result = ::RoInitialize(RO_INIT_SINGLETHREADED);
        if (result == RPC_E_CHANGED_MODE) {
            return;
        }
        winrt::check_hresult(result);
        initialized_ = true;
    }

    ~ScopedRoInitialization() {
        if (initialized_) {
            ::RoUninitialize();
        }
    }

    ScopedRoInitialization(const ScopedRoInitialization&) = delete;
    ScopedRoInitialization& operator=(const ScopedRoInitialization&) = delete;
    ScopedRoInitialization(ScopedRoInitialization&&) = delete;
    ScopedRoInitialization& operator=(ScopedRoInitialization&&) = delete;

  private:
    bool initialized_{};
};

class GaussianBlurEffect final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRtClassicComMix>, GraphicsEffect,
          GraphicsEffectSource, GraphicsEffectInterop> {
    InspectableClass(L"NetworkFlowMonitor.GaussianBlurEffect", BaseTrust);

  public:
    HRESULT Initialize(float standard_deviation, GraphicsEffectSource* source) noexcept {
        blur_amount_ = standard_deviation;
        source_ = source;
        return name_.Set(L"Blur");
    }

    IFACEMETHODIMP get_Name(HSTRING* name) noexcept override {
        return name == nullptr ? E_POINTER : name_.CopyTo(name);
    }
    IFACEMETHODIMP put_Name(HSTRING name) noexcept override {
        return name_.Set(name);
    }
    IFACEMETHODIMP GetEffectId(GUID* id) noexcept override {
        if (id == nullptr) {
            return E_POINTER;
        }
        *id = CLSID_D2D1GaussianBlur;
        return S_OK;
    }
    IFACEMETHODIMP GetNamedPropertyMapping(LPCWSTR name, UINT* index,
                                           PropertyMapping* mapping) noexcept override {
        if (name == nullptr || index == nullptr || mapping == nullptr) {
            return E_POINTER;
        }
        const std::wstring_view property{name};
        if (property == L"BlurAmount") {
            *index = D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
        } else if (property == L"Optimization") {
            *index = D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION;
        } else if (property == L"BorderMode") {
            *index = D2D1_GAUSSIANBLUR_PROP_BORDER_MODE;
        } else {
            return E_INVALIDARG;
        }
        *mapping = PropertyMapping::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyCount(UINT* count) noexcept override {
        if (count == nullptr) {
            return E_POINTER;
        }
        *count = 3U;
        return S_OK;
    }
    IFACEMETHODIMP GetProperty(UINT index,
                               ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
        if (value == nullptr) {
            return E_POINTER;
        }
        *value = nullptr;
        using PropertyValueStatics = winrt::Windows::Foundation::IPropertyValueStatics;
        Microsoft::WRL::ComPtr<WinrtAbi<PropertyValueStatics>> factory;
        auto result = GetActivationFactory<PropertyValueStatics>(
            L"Windows.Foundation.PropertyValue", factory);
        if (FAILED(result)) {
            return result;
        }
        Microsoft::WRL::ComPtr<IInspectable> property;
        if (index == D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION) {
            result = factory->CreateSingle(blur_amount_,
                                           reinterpret_cast<void**>(property.GetAddressOf()));
        } else if (index == D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION) {
            result = factory->CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED,
                                           reinterpret_cast<void**>(property.GetAddressOf()));
        } else if (index == D2D1_GAUSSIANBLUR_PROP_BORDER_MODE) {
            result = factory->CreateUInt32(D2D1_BORDER_MODE_HARD,
                                           reinterpret_cast<void**>(property.GetAddressOf()));
        } else {
            return E_INVALIDARG;
        }
        return SUCCEEDED(result) ? property.CopyTo(value) : result;
    }
    IFACEMETHODIMP GetSource(UINT index, GraphicsEffectSource** source) noexcept override {
        if (source == nullptr) {
            return E_POINTER;
        }
        *source = nullptr;
        return index == 0U ? source_.CopyTo(source) : E_INVALIDARG;
    }
    IFACEMETHODIMP GetSourceCount(UINT* count) noexcept override {
        if (count == nullptr) {
            return E_POINTER;
        }
        *count = 1U;
        return S_OK;
    }

  private:
    Microsoft::WRL::Wrappers::HString name_;
    Microsoft::WRL::ComPtr<GraphicsEffectSource> source_;
    float blur_amount_{};
};

[[nodiscard]] Microsoft::WRL::ComPtr<ABI::Windows::System::IDispatcherQueueController>
EnsureDispatcherQueue() {
    using DispatcherQueueStatics = winrt::Windows::System::IDispatcherQueueStatics;
    Microsoft::WRL::ComPtr<WinrtAbi<DispatcherQueueStatics>> factory;
    winrt::check_hresult(
        GetActivationFactory<DispatcherQueueStatics>(L"Windows.System.DispatcherQueue", factory));
    Microsoft::WRL::ComPtr<IInspectable> existing_queue;
    winrt::check_hresult(
        factory->GetForCurrentThread(reinterpret_cast<void**>(existing_queue.GetAddressOf())));
    if (existing_queue != nullptr) {
        return {};
    }
    const DispatcherQueueOptions options{.dwSize = sizeof(DispatcherQueueOptions),
                                         .threadType = DQTYPE_THREAD_CURRENT,
                                         .apartmentType = DQTAT_COM_STA};
    Microsoft::WRL::ComPtr<ABI::Windows::System::IDispatcherQueueController> controller;
    winrt::check_hresult(::CreateDispatcherQueueController(options, controller.GetAddressOf()));
    return controller;
}

} // namespace

class FrostedGlassHost::Impl final {
  public:
    void Initialize(HWND window, std::uint8_t blur_strength_percent) {
        dispatcher_controller_ = EnsureDispatcherQueue();
        const Microsoft::WRL::Wrappers::HStringReference compositor_name{
            L"Windows.UI.Composition.Compositor"};
        winrt::check_hresult(
            ::RoActivateInstance(compositor_name.Get(), compositor_.GetAddressOf()));
        InitializeGraphicsDevice();

        Microsoft::WRL::ComPtr<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>
            desktop_interop;
        winrt::check_hresult(compositor_.As(&desktop_interop));
        winrt::check_hresult(desktop_interop->CreateDesktopWindowTarget(
            window, TRUE,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
                target_.GetAddressOf())));

        const auto compositor =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ICompositor>(compositor_.Get());
        winrt::check_hresult(
            compositor->CreateContainerVisual(reinterpret_cast<void**>(root_.GetAddressOf())));
        const auto root_child =
            QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(root_.Get());
        const auto root_container =
            QueryWinrtInterface<winrt::Windows::UI::Composition::IContainerVisual>(root_.Get());
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::IVisualCollection>>
            root_children;
        winrt::check_hresult(
            root_container->get_Children(reinterpret_cast<void**>(root_children.GetAddressOf())));

        winrt::check_hresult(compositor->CreateContainerVisual(
            reinterpret_cast<void**>(material_container_.GetAddressOf())));
        const auto material_child = QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(
            material_container_.Get());
        winrt::check_hresult(root_children->InsertAtTop(material_child.Get()));
        const auto material =
            QueryWinrtInterface<winrt::Windows::UI::Composition::IContainerVisual>(
                material_container_.Get());
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::IVisualCollection>>
            material_children;
        winrt::check_hresult(
            material->get_Children(reinterpret_cast<void**>(material_children.GetAddressOf())));

        winrt::check_hresult(compositor->CreateSpriteVisual(
            reinterpret_cast<void**>(backdrop_visual_.GetAddressOf())));
        const auto backdrop_child =
            QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(backdrop_visual_.Get());
        winrt::check_hresult(material_children->InsertAtTop(backdrop_child.Get()));

        winrt::check_hresult(
            compositor->CreateSpriteVisual(reinterpret_cast<void**>(tint_visual_.GetAddressOf())));
        winrt::check_hresult(compositor->CreateColorBrushWithColor(
            {.A = 255U, .R = 32U, .G = 32U, .B = 32U},
            reinterpret_cast<void**>(tint_brush_.GetAddressOf())));
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::ICompositionBrush>>
            tint_brush;
        winrt::check_hresult(tint_brush_->QueryInterface(
            winrt::guid_of<winrt::Windows::UI::Composition::ICompositionBrush>(),
            reinterpret_cast<void**>(tint_brush.GetAddressOf())));
        const auto tint_sprite =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ISpriteVisual>(tint_visual_.Get());
        winrt::check_hresult(tint_sprite->put_Brush(tint_brush.Get()));
        const auto tint_child =
            QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(tint_visual_.Get());
        winrt::check_hresult(material_children->InsertAtTop(tint_child.Get()));

        RECT client{};
        winrt::check_bool(::GetClientRect(window, &client));
        CreateDrawingSurface({.cx = std::max(1L, client.right - client.left),
                              .cy = std::max(1L, client.bottom - client.top)});
        winrt::Windows::UI::Composition::Compositor projected_compositor{nullptr};
        winrt::copy_from_abi(projected_compositor, compositor_.Get());
        if (!projected_compositor) {
            winrt::throw_hresult(E_UNEXPECTED);
        }
        foreground_visual_ = projected_compositor.CreateSpriteVisual();
        SetForegroundSize(drawing_surface_size_);
        foreground_visual_.Brush(foreground_brush_);
        const auto foreground_child =
            foreground_visual_.as<winrt::Windows::UI::Composition::IVisual>();
        winrt::check_hresult(
            material_children->InsertAbove(winrt::get_abi(foreground_child), tint_child.Get()));

        SetBlurStrength(blur_strength_percent);
        const auto composition_target =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ICompositionTarget>(target_.Get());
        winrt::check_hresult(composition_target->put_Root(root_child.Get()));
    }

    void SetBlurStrength(std::uint8_t blur_strength_percent) {
        const auto amount = FrostedGlassHost::BlurStandardDeviation(blur_strength_percent);
        // HostBackdrop samples behind the HWND before this window is drawn. A regular
        // BackdropBrush would include the HWND's legacy Direct2D redirection content and
        // feed the previous frame back into the blur on repeated paints.
        const auto compositor3 =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ICompositor3>(compositor_.Get());
        Microsoft::WRL::ComPtr<IInspectable> backdrop;
        winrt::check_hresult(compositor3->CreateHostBackdropBrush(
            reinterpret_cast<void**>(backdrop.GetAddressOf())));
        const auto sprite = QueryWinrtInterface<winrt::Windows::UI::Composition::ISpriteVisual>(
            backdrop_visual_.Get());
        const auto backdrop_brush =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ICompositionBrush>(backdrop.Get());
        if (amount <= 0.0F) {
            winrt::check_hresult(sprite->put_Brush(backdrop_brush.Get()));
            return;
        }
        using SourceParameterFactory =
            winrt::Windows::UI::Composition::ICompositionEffectSourceParameterFactory;
        Microsoft::WRL::ComPtr<WinrtAbi<SourceParameterFactory>> source_factory;
        winrt::check_hresult(GetActivationFactory<SourceParameterFactory>(
            L"Windows.UI.Composition.CompositionEffectSourceParameter", source_factory));
        const Microsoft::WRL::Wrappers::HStringReference source_name{L"backdrop"};
        Microsoft::WRL::ComPtr<IInspectable> source_parameter;
        winrt::check_hresult(source_factory->Create(
            source_name.Get(), reinterpret_cast<void**>(source_parameter.GetAddressOf())));
        Microsoft::WRL::ComPtr<GraphicsEffectSource> source_interface;
        winrt::check_hresult(source_parameter.As(&source_interface));

        const auto effect = Microsoft::WRL::Make<GaussianBlurEffect>();
        if (effect == nullptr) {
            winrt::throw_hresult(E_OUTOFMEMORY);
        }
        winrt::check_hresult(effect->Initialize(amount, source_interface.Get()));
        const auto compositor =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ICompositor>(compositor_.Get());
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::ICompositionEffectFactory>>
            effect_factory;
        winrt::check_hresult(compositor->CreateEffectFactory(
            effect.Get(), reinterpret_cast<void**>(effect_factory.GetAddressOf())));
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::ICompositionEffectBrush>>
            brush;
        winrt::check_hresult(
            effect_factory->CreateBrush(reinterpret_cast<void**>(brush.GetAddressOf())));
        winrt::check_hresult(brush->SetSourceParameter(source_name.Get(), backdrop_brush.Get()));
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::ICompositionBrush>>
            effect_brush;
        winrt::check_hresult(brush->QueryInterface(
            winrt::guid_of<winrt::Windows::UI::Composition::ICompositionBrush>(),
            reinterpret_cast<void**>(effect_brush.GetAddressOf())));
        winrt::check_hresult(sprite->put_Brush(effect_brush.Get()));
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void SetTint(std::uint32_t rgb, std::uint8_t opacity_percent) {
        const ABI::Windows::UI::Color color{.A = 255U,
                                            .R = static_cast<std::uint8_t>((rgb >> 16U) & 0xFFU),
                                            .G = static_cast<std::uint8_t>((rgb >> 8U) & 0xFFU),
                                            .B = static_cast<std::uint8_t>(rgb & 0xFFU)};
        winrt::check_hresult(tint_brush_->put_Color(color));
        const auto visual =
            QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(tint_visual_.Get());
        winrt::check_hresult(visual->put_Opacity(
            static_cast<float>(std::min<std::uint8_t>(opacity_percent, 100U)) / 100.0F));
    }

    void SetClip(PixelRect bounds, float radius) {
        const auto compositor5 =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ICompositor5>(compositor_.Get());
        Microsoft::WRL::ComPtr<
            WinrtAbi<winrt::Windows::UI::Composition::ICompositionRoundedRectangleGeometry>>
            geometry;
        winrt::check_hresult(compositor5->CreateRoundedRectangleGeometry(
            reinterpret_cast<void**>(geometry.GetAddressOf())));
        winrt::check_hresult(geometry->put_Offset(
            {static_cast<float>(bounds.left), static_cast<float>(bounds.top)}));
        winrt::check_hresult(
            geometry->put_Size({static_cast<float>(std::max(0, bounds.Width())),
                                static_cast<float>(std::max(0, bounds.Height()))}));
        const auto clamped_radius = std::max(0.0F, radius);
        winrt::check_hresult(geometry->put_CornerRadius({clamped_radius, clamped_radius}));
        const auto compositor6 =
            QueryWinrtInterface<winrt::Windows::UI::Composition::ICompositor6>(compositor_.Get());
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::ICompositionGeometricClip>>
            clip;
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::ICompositionGeometry>>
            composition_geometry;
        winrt::check_hresult(geometry->QueryInterface(
            winrt::guid_of<winrt::Windows::UI::Composition::ICompositionGeometry>(),
            reinterpret_cast<void**>(composition_geometry.GetAddressOf())));
        winrt::check_hresult(compositor6->CreateGeometricClipWithGeometry(
            composition_geometry.Get(), reinterpret_cast<void**>(clip.GetAddressOf())));
        const auto visual = QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(
            material_container_.Get());
        Microsoft::WRL::ComPtr<WinrtAbi<winrt::Windows::UI::Composition::ICompositionClip>>
            composition_clip;
        winrt::check_hresult(clip->QueryInterface(
            winrt::guid_of<winrt::Windows::UI::Composition::ICompositionClip>(),
            reinterpret_cast<void**>(composition_clip.GetAddressOf())));
        winrt::check_hresult(visual->put_Clip(composition_clip.Get()));
    }

    void SetVisible(bool visible) {
        if (backdrop_visual_ != nullptr) {
            const auto backdrop = QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(
                backdrop_visual_.Get());
            winrt::check_hresult(backdrop->put_IsVisible(visible));
        }
        if (tint_visual_ != nullptr) {
            const auto tint =
                QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(tint_visual_.Get());
            winrt::check_hresult(tint->put_IsVisible(visible));
        }
    }

    [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1DeviceContext> BeginDraw(PixelSize pixel_size,
                                                                       int dpi) {
        if (drawing_) {
            winrt::throw_hresult(E_UNEXPECTED);
        }
        const SIZE requested{.cx = std::max(1, pixel_size.width),
                             .cy = std::max(1, pixel_size.height)};
        if (requested.cx != drawing_surface_size_.cx || requested.cy != drawing_surface_size_.cy) {
            CreateDrawingSurface(requested);
            foreground_visual_.Brush(foreground_brush_);
            SetForegroundSize(requested);
        }
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
        winrt::check_hresult(drawing_surface_interop_->BeginDraw(
            nullptr, __uuidof(ID2D1DeviceContext), reinterpret_cast<void**>(context.GetAddressOf()),
            &drawing_offset_));
        drawing_ = true;
        drawing_dpi_ = static_cast<float>(std::max(1, dpi));
        // Set the DPI the caller will also request so the two SetDpi calls agree and the
        // transform below stays correct.  Clear() ignores transforms, so the identity is
        // fine for the initial erase of the atlas clip region.
        context->SetDpi(drawing_dpi_, drawing_dpi_);
        context->SetTransform(D2D1::Matrix3x2F::Identity());
        context->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
        // The composition atlas returns a pixel-coordinate updateOffset.  Convert it to
        // DIPs at the caller's DPI so that (0,0) DIP drawing lands at exactly the offset
        // pixel: dip * (dpi / 96) = pixel => dip = pixel * 96 / dpi.
        const auto dip_scale = 96.0F / drawing_dpi_;
        context->SetTransform(
            D2D1::Matrix3x2F::Translation(static_cast<float>(drawing_offset_.x) * dip_scale,
                                          static_cast<float>(drawing_offset_.y) * dip_scale));
        drawing_context_ = context;
        return context;
    }

    [[nodiscard]] HRESULT EndDraw() noexcept {
        if (!drawing_) {
            return E_UNEXPECTED;
        }
        drawing_ = false;
        HRESULT result = S_OK;
        if (drawing_context_ != nullptr) {
            drawing_context_->SetTransform(D2D1::Matrix3x2F::Identity());
            result = drawing_context_->Flush();
        }
        drawing_context_.Reset();
        const auto end_result = drawing_surface_interop_->EndDraw();
        return FAILED(result) ? result : end_result;
    }

  private:
    void InitializeGraphicsDevice() {
        constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL feature_level{D3D_FEATURE_LEVEL_11_0};
        auto result = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                          nullptr, 0U, D3D11_SDK_VERSION,
                                          d3d_device_.GetAddressOf(), &feature_level, nullptr);
        if (FAILED(result)) {
            result = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0U,
                                         D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
                                         &feature_level, nullptr);
        }
        winrt::check_hresult(result);
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
        winrt::check_hresult(d3d_device_.As(&dxgi_device));
        Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
        const D2D1_FACTORY_OPTIONS options{};
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                               __uuidof(ID2D1Factory1), &options,
                                               reinterpret_cast<void**>(factory.GetAddressOf())));
        winrt::check_hresult(factory->CreateDevice(dxgi_device.Get(), d2d_device_.GetAddressOf()));
        Microsoft::WRL::ComPtr<ABI::Windows::UI::Composition::ICompositorInterop>
            compositor_interop;
        winrt::check_hresult(compositor_.As(&compositor_interop));
        winrt::check_hresult(compositor_interop->CreateGraphicsDevice(
            d2d_device_.Get(), graphics_device_.GetAddressOf()));
    }

    void CreateDrawingSurface(SIZE size) {
        winrt::Windows::UI::Composition::CompositionGraphicsDevice projected_device{nullptr};
        winrt::copy_from_abi(projected_device, graphics_device_.Get());
        if (!projected_device) {
            winrt::throw_hresult(E_UNEXPECTED);
        }
        drawing_surface_ = projected_device.CreateDrawingSurface(
            {static_cast<float>(size.cx), static_cast<float>(size.cy)},
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
        drawing_surface_interop_ =
            drawing_surface_.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        winrt::Windows::UI::Composition::Compositor projected_compositor{nullptr};
        winrt::copy_from_abi(projected_compositor, compositor_.Get());
        if (!projected_compositor) {
            winrt::throw_hresult(E_UNEXPECTED);
        }
        foreground_brush_ = projected_compositor.CreateSurfaceBrush(drawing_surface_);
        foreground_brush_.Stretch(winrt::Windows::UI::Composition::CompositionStretch::Fill);
        drawing_surface_size_ = size;
    }

    void SetForegroundSize(SIZE size) {
        const winrt::Windows::Foundation::Numerics::float2 visual_size{static_cast<float>(size.cx),
                                                                       static_cast<float>(size.cy)};
        const std::array visuals{root_.Get(), material_container_.Get(), backdrop_visual_.Get(),
                                 tint_visual_.Get()};
        for (auto* const object : visuals) {
            const auto visual =
                QueryWinrtInterface<winrt::Windows::UI::Composition::IVisual>(object);
            winrt::check_hresult(visual->put_Size(visual_size));
        }
        foreground_visual_.Size(visual_size);
    }

    // Declared first so the apartment remains initialized until every COM member has released.
    ScopedRoInitialization apartment_;
    Microsoft::WRL::ComPtr<ABI::Windows::System::IDispatcherQueueController> dispatcher_controller_;
    Microsoft::WRL::ComPtr<IInspectable> compositor_;
    Microsoft::WRL::ComPtr<IInspectable> target_;
    Microsoft::WRL::ComPtr<IInspectable> root_;
    Microsoft::WRL::ComPtr<IInspectable> material_container_;
    Microsoft::WRL::ComPtr<IInspectable> backdrop_visual_;
    Microsoft::WRL::ComPtr<IInspectable> tint_visual_;
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Composition::ICompositionColorBrush> tint_brush_;
    winrt::Windows::UI::Composition::SpriteVisual foreground_visual_{nullptr};
    winrt::Windows::UI::Composition::CompositionSurfaceBrush foreground_brush_{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2d_device_;
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Composition::ICompositionGraphicsDevice>
        graphics_device_;
    winrt::Windows::UI::Composition::CompositionDrawingSurface drawing_surface_{nullptr};
    winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>
        drawing_surface_interop_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> drawing_context_;
    SIZE drawing_surface_size_{};
    POINT drawing_offset_{};
    float drawing_dpi_{96.0F};
    bool drawing_{};
};

FrostedGlassHost::FrostedGlassHost() noexcept = default;

FrostedGlassHost::~FrostedGlassHost() = default;

bool FrostedGlassHost::Initialize(HWND window, std::uint8_t blur_strength_percent) noexcept {
    if (active()) {
        return true;
    }
    if (window == nullptr) {
        last_error_ = E_INVALIDARG;
        return false;
    }
    try {
        auto created = std::make_unique<Impl>();
        created->Initialize(window, blur_strength_percent);
        impl_ = std::move(created);
        last_error_ = S_OK;
        return true;
    } catch (...) {
        last_error_ = winrt::to_hresult();
        impl_.reset();
        return false;
    }
}

void FrostedGlassHost::Reset() noexcept {
    impl_.reset();
}

void FrostedGlassHost::SetBlurStrength(std::uint8_t blur_strength_percent) noexcept {
    if (!active()) {
        return;
    }
    try {
        impl_->SetBlurStrength(blur_strength_percent);
        last_error_ = S_OK;
    } catch (...) {
        last_error_ = winrt::to_hresult();
        impl_.reset();
    }
}

void FrostedGlassHost::SetTint(std::uint32_t rgb, std::uint8_t opacity_percent) noexcept {
    if (!active()) {
        return;
    }
    try {
        impl_->SetTint(rgb, opacity_percent);
        last_error_ = S_OK;
    } catch (...) {
        last_error_ = winrt::to_hresult();
        impl_.reset();
    }
}

void FrostedGlassHost::SetClip(PixelRect local_bounds, float corner_radius_pixels) noexcept {
    if (!active()) {
        return;
    }
    try {
        impl_->SetClip(local_bounds, corner_radius_pixels);
        last_error_ = S_OK;
    } catch (...) {
        last_error_ = winrt::to_hresult();
        impl_.reset();
    }
}

void FrostedGlassHost::SetVisible(bool visible) noexcept {
    if (!active()) {
        return;
    }
    try {
        impl_->SetVisible(visible);
        last_error_ = S_OK;
    } catch (...) {
        last_error_ = winrt::to_hresult();
        impl_.reset();
    }
}

Microsoft::WRL::ComPtr<ID2D1DeviceContext> FrostedGlassHost::BeginDraw(PixelSize pixel_size,
                                                                       int dpi) noexcept {
    if (!active()) {
        return {};
    }
    try {
        auto context = impl_->BeginDraw(pixel_size, dpi);
        last_error_ = S_OK;
        return context;
    } catch (...) {
        last_error_ = winrt::to_hresult();
        impl_.reset();
        return {};
    }
}

HRESULT FrostedGlassHost::EndDraw() noexcept {
    if (!active()) {
        return E_HANDLE;
    }
    const auto result = impl_->EndDraw();
    last_error_ = result;
    if (FAILED(result)) {
        impl_.reset();
    }
    return result;
}

bool FrostedGlassHost::active() const noexcept {
    return impl_ != nullptr;
}

HRESULT FrostedGlassHost::last_error() const noexcept {
    return last_error_;
}

float FrostedGlassHost::BlurStandardDeviation(std::uint8_t strength_percent) noexcept {
    constexpr auto kMaximumBlurDip = 30.0F;
    return kMaximumBlurDip * static_cast<float>(std::min<std::uint8_t>(strength_percent, 100U)) /
           100.0F;
}

} // namespace nfm::ui
