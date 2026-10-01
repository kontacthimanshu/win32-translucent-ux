#include <te/appearance/BackdropManager.h>

#include <te/appearance/Contrast.h>

#include <dwmapi.h>
#include <wil/resource.h>

namespace te
{

// Only the documented DWMWA_SYSTEMBACKDROP_TYPE API is used. DWMWA_MICA_EFFECT (1029)
// and SetWindowCompositionAttribute are undocumented and must never be called
// (TC-010, constitution Principle IV, research R-01); builds without
// DWMWA_SYSTEMBACKDROP_TYPE fall back to Solid instead.

namespace
{

DWM_SYSTEMBACKDROP_TYPE BackdropType(BackdropMode mode)
{
    switch (mode)
    {
    case BackdropMode::Mica:
        return DWMSBT_MAINWINDOW;
    case BackdropMode::Acrylic:
        return DWMSBT_TRANSIENTWINDOW;
    case BackdropMode::Solid:
    case BackdropMode::Transparent: // the extended frame stays clear without a material
        break;
    }
    return DWMSBT_NONE;
}

template <typename T> HRESULT SetAttribute(HWND hwnd, DWMWINDOWATTRIBUTE attribute, const T& value)
{
    return DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}

} // namespace

bool BackdropManager::ProbeSystemBackdrop(HWND hwnd)
{
    return SUCCEEDED(SetAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_MAINWINDOW));
}

HRESULT BackdropManager::Apply(HWND hwnd, const EffectiveAppearance& effective)
{
    // Dark caption and frame whenever the base is dark (the theme base, or COLOR_WINDOW
    // in high contrast). In Transparent the caption follows the text instead, so the DWM's
    // caption-button glyphs are white like it. Failure is cosmetic, so it is not reported.
    const bool darkCaption = effective.applied == BackdropMode::Transparent
                                 ? Contrast::RelativeLuminance(effective.text) > 0.5
                                 : Contrast::RelativeLuminance(effective.base) < 0.5;
    const BOOL dark = darkCaption ? TRUE : FALSE;
    (void)SetAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, dark);

    const HRESULT backdrop = SetAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, BackdropType(effective.applied));

    // Without a material the DWM fills the extended frame with an opaque default color.
    // Blur-behind with an empty region makes it honor the window's per-pixel alpha
    // instead, unblurred, so the clear parts of the swap chain show what is behind.
    const bool clear = effective.applied == BackdropMode::Transparent;
    const wil::unique_hrgn emptyRegion(clear ? CreateRectRgn(0, 0, -1, -1) : nullptr);
    DWM_BLURBEHIND blur{};
    blur.dwFlags = DWM_BB_ENABLE | (clear ? DWM_BB_BLURREGION : 0);
    blur.fEnable = clear ? TRUE : FALSE;
    blur.hRgnBlur = emptyRegion.get();
    const HRESULT blurResult = DwmEnableBlurBehindWindow(hwnd, &blur);
    if (clear && SUCCEEDED(backdrop) && FAILED(blurResult))
    {
        return blurResult; // the caller falls back to Solid (rule 4)
    }

    // Caption and border colors match the opaque base in Solid mode only (R-04); in
    // Acrylic and Mica the system draws them.
    const COLORREF frame = effective.applied == BackdropMode::Solid
                               ? RGB(effective.base.r, effective.base.g, effective.base.b)
                               : DWMWA_COLOR_DEFAULT;
    (void)SetAttribute(hwnd, DWMWA_CAPTION_COLOR, frame);
    (void)SetAttribute(hwnd, DWMWA_BORDER_COLOR, frame);

    return backdrop;
}

} // namespace te
