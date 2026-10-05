#pragma once

// Custom-frame caption layout and hit testing (research R-03, UI contract §2).

#include <windows.h>

namespace te
{

struct CaptionLayout
{
    int captionHeightPx = 0;
    RECT captionButtons{}; // client coords, from DWMWA_CAPTION_BUTTON_BOUNDS
    RECT picker{};         // right edge = captionButtons.left - 8 DIP
    RECT slabButton{};     // immediately left of the picker, same size
    RECT dragRegion{};     // caption strip minus the title-bar buttons and caption buttons
    int resizeBandPx = 0;  // top resize band height; 0 when maximized
    int contentTopPx = 0;  // rows above the screen when maximized (frame + padding); 0 otherwise
    int slabPx = 0;        // slab face thickness: the caption grows by it, the title moves in by it
};

class ICaptionHitTester
{
  public:
    virtual ~ICaptionHitTester() = default;

    // Computes the layout for the current DPI, window size and show state.
    virtual CaptionLayout Compute(HWND hwnd, UINT dpi) = 0;

    // Order: DwmDefWindowProc -> top resize band -> picker, slab button (HTCLIENT)
    //        -> drag region (HTCAPTION) -> HTCLIENT.
    // Returns true and sets *result if handled.
    virtual bool HitTest(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, const CaptionLayout& layout,
                         LRESULT* result) = 0;
};

} // namespace te
