#pragma once

// Direct3D 11 + DXGI composition swap chain + Direct2D + DirectComposition
// (research R-02). Everything is drawn with premultiplied alpha, so pixels left at
// alpha 0 show the DWM system backdrop.
//
// UI thread only: the Direct2D factory is single-threaded.

#include <te/render/IRenderDevice.h>

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>

#include <wil/com.h>

namespace te
{

class RenderDevice final : public IRenderDevice
{
  public:
    RenderDevice() = default;
    ~RenderDevice() override = default;

    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    // Creates the factories (once) and the device-dependent resources for hwnd,
    // sized to its client area at its current DPI.
    HRESULT Initialize(HWND hwnd) override;

    // Resizes the swap chain (width/height in physical pixels) and sets the
    // Direct2D DPI, so drawing code works in DIPs.
    HRESULT Resize(UINT widthPx, UINT heightPx, UINT dpi) override;

    // Starts a frame. Returns nullptr if the device is not available.
    ID2D1DeviceContext* BeginDraw() override;

    // Ends the frame and presents it. If the target or device was lost
    // (D2DERR_RECREATE_TARGET, DXGI_ERROR_DEVICE_REMOVED/RESET), all
    // device-dependent resources are recreated and the window is invalidated so
    // the next WM_PAINT redraws; the return value is then the recreation result.
    HRESULT EndDrawAndPresent() override;

    IDWriteFactory3* DWrite() override;

    // WM_DESTROY (T088): releases the DirectComposition visual and target, the Direct2D
    // device and context, the swap chain and the Direct3D device, in that order. Drawing
    // does nothing afterwards (BeginDraw returns null).
    void ReleaseDevice() noexcept
    {
        ReleaseDeviceResources();
        m_hwnd = nullptr;
    }
    [[nodiscard]] bool HasDevice() const noexcept
    {
        return m_d3dDevice != nullptr;
    }

    // What the target was last sized for (tests, T081).
    [[nodiscard]] UINT Dpi() const noexcept
    {
        return m_dpi;
    }
    [[nodiscard]] SIZE SizePx() const noexcept
    {
        return SIZE{static_cast<LONG>(m_widthPx), static_cast<LONG>(m_heightPx)};
    }

  private:
    HRESULT CreateDeviceResources();
    HRESULT CreateTargetBitmap();
    void ReleaseDeviceResources() noexcept;
    HRESULT HandleDeviceLoss(HRESULT failure);

    HWND m_hwnd = nullptr;
    UINT m_widthPx = 1;
    UINT m_heightPx = 1;
    UINT m_dpi = USER_DEFAULT_SCREEN_DPI;

    // Device-independent: created once.
    wil::com_ptr<ID2D1Factory1> m_d2dFactory;
    wil::com_ptr<IDWriteFactory3> m_dwriteFactory;

    // Device-dependent: recreated after device loss.
    wil::com_ptr<ID3D11Device> m_d3dDevice;
    wil::com_ptr<IDXGISwapChain1> m_swapChain;
    wil::com_ptr<ID2D1Device> m_d2dDevice;
    wil::com_ptr<ID2D1DeviceContext> m_context;
    wil::com_ptr<ID2D1Bitmap1> m_target;
    wil::com_ptr<IDCompositionDevice> m_dcompDevice;
    wil::com_ptr<IDCompositionTarget> m_dcompTarget;
    wil::com_ptr<IDCompositionVisual> m_dcompVisual;
};

} // namespace te
