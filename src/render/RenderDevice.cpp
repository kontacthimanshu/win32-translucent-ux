#include <te/render/RenderDevice.h>

#include <wil/result.h>

#include <algorithm>
#include <iterator>

namespace te
{

namespace
{

bool IsDeviceLoss(HRESULT hr) noexcept
{
    return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
}

HRESULT CreateD3DDevice(D3D_DRIVER_TYPE driverType, ID3D11Device** device)
{
    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_1,
    };
    return D3D11CreateDevice(nullptr, driverType, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                             static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, device, nullptr,
                             nullptr);
}

} // namespace

HRESULT RenderDevice::Initialize(HWND hwnd)
{
    RETURN_HR_IF(E_INVALIDARG, !IsWindow(hwnd));
    m_hwnd = hwnd;

    RECT client{};
    GetClientRect(hwnd, &client);
    m_widthPx = static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
    m_heightPx = static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));
    if (const UINT dpi = GetDpiForWindow(hwnd); dpi != 0)
    {
        m_dpi = dpi;
    }

    if (!m_d2dFactory)
    {
        D2D1_FACTORY_OPTIONS options{};
#ifdef _DEBUG
        options.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
#endif
        RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                           &options, m_d2dFactory.put_void()));
    }
    if (!m_dwriteFactory)
    {
        RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                             reinterpret_cast<IUnknown**>(m_dwriteFactory.put())));
    }

    ReleaseDeviceResources();
    return CreateDeviceResources();
}

HRESULT RenderDevice::CreateDeviceResources()
{
    // Hardware first; WARP (software) keeps the app usable where no hardware
    // device is available, e.g. some virtual machines and remote sessions.
    HRESULT hr = CreateD3DDevice(D3D_DRIVER_TYPE_HARDWARE, m_d3dDevice.put());
    if (FAILED(hr))
    {
        hr = CreateD3DDevice(D3D_DRIVER_TYPE_WARP, m_d3dDevice.put());
    }
    RETURN_IF_FAILED(hr);

    const auto dxgiDevice = m_d3dDevice.query<IDXGIDevice>();
    wil::com_ptr<IDXGIAdapter> adapter;
    RETURN_IF_FAILED(dxgiDevice->GetAdapter(adapter.put()));
    wil::com_ptr<IDXGIFactory2> dxgiFactory;
    RETURN_IF_FAILED(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.put())));

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = m_widthPx;
    desc.Height = m_heightPx;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    RETURN_IF_FAILED(
        dxgiFactory->CreateSwapChainForComposition(dxgiDevice.get(), &desc, nullptr, m_swapChain.put()));

    RETURN_IF_FAILED(m_d2dFactory->CreateDevice(dxgiDevice.get(), m_d2dDevice.put()));
    RETURN_IF_FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_context.put()));
    RETURN_IF_FAILED(CreateTargetBitmap());

    // topmost = FALSE: the visual sits below the window's GDI redirection
    // surface, so native child controls (the address EDIT) still paint above it.
    RETURN_IF_FAILED(DCompositionCreateDevice(dxgiDevice.get(), IID_PPV_ARGS(m_dcompDevice.put())));
    RETURN_IF_FAILED(m_dcompDevice->CreateTargetForHwnd(m_hwnd, FALSE, m_dcompTarget.put()));
    RETURN_IF_FAILED(m_dcompDevice->CreateVisual(m_dcompVisual.put()));
    RETURN_IF_FAILED(m_dcompVisual->SetContent(m_swapChain.get()));
    RETURN_IF_FAILED(m_dcompTarget->SetRoot(m_dcompVisual.get()));
    RETURN_IF_FAILED(m_dcompDevice->Commit());
    return S_OK;
}

HRESULT RenderDevice::CreateTargetBitmap()
{
    wil::com_ptr<IDXGISurface> backBuffer;
    RETURN_IF_FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put())));

    const auto dpi = static_cast<float>(m_dpi);
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    RETURN_IF_FAILED(m_context->CreateBitmapFromDxgiSurface(backBuffer.get(), &props, m_target.put()));
    m_context->SetTarget(m_target.get());
    m_context->SetDpi(dpi, dpi);
    return S_OK;
}

void RenderDevice::ReleaseDeviceResources() noexcept
{
    // The DirectComposition target must be released before a new one can be
    // created for the same window.
    m_dcompVisual.reset();
    m_dcompTarget.reset();
    m_dcompDevice.reset();
    if (m_context)
    {
        m_context->SetTarget(nullptr);
    }
    m_target.reset();
    m_context.reset();
    m_d2dDevice.reset();
    m_swapChain.reset();
    m_d3dDevice.reset();
}

HRESULT RenderDevice::Resize(UINT widthPx, UINT heightPx, UINT dpi)
{
    m_widthPx = std::max(widthPx, 1u);
    m_heightPx = std::max(heightPx, 1u);
    if (dpi != 0)
    {
        m_dpi = dpi;
    }
    if (!m_swapChain)
    {
        return S_OK; // applied when the device is (re)created
    }

    // All references to the back buffer must be released before ResizeBuffers.
    m_context->SetTarget(nullptr);
    m_target.reset();

    const HRESULT hr = m_swapChain->ResizeBuffers(0, m_widthPx, m_heightPx, DXGI_FORMAT_UNKNOWN, 0);
    if (IsDeviceLoss(hr))
    {
        return HandleDeviceLoss(hr);
    }
    RETURN_IF_FAILED(hr);
    return CreateTargetBitmap();
}

ID2D1DeviceContext* RenderDevice::BeginDraw()
{
    if (!m_context || !m_target)
    {
        return nullptr;
    }
    m_context->BeginDraw();
    return m_context.get();
}

HRESULT RenderDevice::EndDrawAndPresent()
{
    RETURN_HR_IF(E_UNEXPECTED, !m_context);

    HRESULT hr = m_context->EndDraw();
    if (SUCCEEDED(hr))
    {
        hr = m_swapChain->Present(1, 0);
    }
    if (IsDeviceLoss(hr))
    {
        return HandleDeviceLoss(hr);
    }
    return hr;
}

HRESULT RenderDevice::HandleDeviceLoss(HRESULT failure)
{
    LOG_HR_MSG(failure, "Render device lost; recreating device resources");
    ReleaseDeviceResources();
    const HRESULT hr = CreateDeviceResources();
    if (SUCCEEDED(hr))
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    return hr;
}

IDWriteFactory3* RenderDevice::DWrite()
{
    return m_dwriteFactory.get();
}

} // namespace te
