#pragma once

// Per-monitor DPI state for one window (constitution Principle I, FR-017).

#include <te/window/IDpiManager.h>

#include <windows.h>

namespace te
{

class DpiManager final : public IDpiManager
{
  public:
    explicit DpiManager(UINT dpi = USER_DEFAULT_SCREEN_DPI) noexcept;

    // Reads the window's current DPI with GetDpiForWindow. Keeps the previous
    // value if hwnd is not a valid window.
    void Attach(HWND hwnd) noexcept;

    UINT Dpi() const override;
    float Scale() const override; // dpi / 96

    // DIPs to physical pixels at the current DPI, rounded to the nearest pixel
    // (halves round away from zero).
    int ToPx(float dip) const override;

    // WM_DPICHANGED: stores newDpi and moves the window to the rectangle Windows
    // suggests (lParam of the message).
    void OnDpiChanged(HWND hwnd, UINT newDpi, const RECT& suggested) override;

    // WM_GETDPISCALEDSIZE (T081): the window size at newDpi that keeps the client area's
    // size in DIPs: the client scaled by newDpi / oldDpi, rounded to the nearest pixel,
    // plus the frame the window keeps at newDpi (`frameAtNewDpi`, both borders summed).
    // Scaling the whole window rectangle instead, as Windows does by default, lets the
    // client size drift by a few pixels on every monitor change.
    [[nodiscard]] static SIZE ScaledWindowSize(SIZE clientPx, UINT oldDpi, UINT newDpi,
                                               SIZE frameAtNewDpi) noexcept;

  private:
    UINT m_dpi;
};

} // namespace te
