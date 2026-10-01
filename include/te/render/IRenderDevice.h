#pragma once

// Direct3D 11 + DXGI composition swap chain + DirectComposition target (research R-02).

#include <windows.h>

#include <d2d1_1.h>
#include <dwrite_3.h>

namespace te
{

class IRenderDevice
{
  public:
    virtual ~IRenderDevice() = default;

    // Creates a D3D11 device + DXGI composition swap chain
    // (DXGI_ALPHA_MODE_PREMULTIPLIED) + DComp target (topmost = FALSE).
    virtual HRESULT Initialize(HWND hwnd) = 0;
    virtual HRESULT Resize(UINT widthPx, UINT heightPx, UINT dpi) = 0;
    virtual ID2D1DeviceContext* BeginDraw() = 0;
    // Handles D2DERR_RECREATE_TARGET / device-lost by re-initializing.
    virtual HRESULT EndDrawAndPresent() = 0;
    virtual IDWriteFactory3* DWrite() = 0;
};

} // namespace te
