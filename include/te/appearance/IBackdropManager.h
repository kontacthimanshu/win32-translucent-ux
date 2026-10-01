#pragma once

// Applies DWM window attributes for the effective appearance (research R-01, R-04).

#include <te/appearance/AppearanceSettings.h>

#include <windows.h>

namespace te
{

class IBackdropManager
{
  public:
    virtual ~IBackdropManager() = default;

    // Applies DWMWA_SYSTEMBACKDROP_TYPE, DWMWA_USE_IMMERSIVE_DARK_MODE and,
    // in Solid mode only, DWMWA_CAPTION_COLOR / DWMWA_BORDER_COLOR.
    // Returns the HRESULT of the backdrop call so the caller can fall back
    // (FallbackReason::BackdropApplyFailed).
    virtual HRESULT Apply(HWND hwnd, const EffectiveAppearance&) = 0;

    virtual bool ProbeSystemBackdrop(HWND hwnd) = 0;
};

} // namespace te
