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
    // Slab face thicknesses (0 = that face is off): the caption grows by the top one, the
    // title moves in by the left one; the bottom and right ones only count toward the minimum
    // size (the caption buttons stay where the DWM puts them, over the right face).
    int slabTopPx = 0;
    int slabLeftPx = 0;
    int slabBottomPx = 0;
    int slabRightPx = 0;
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
