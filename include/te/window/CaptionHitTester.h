#pragma once

// Caption layout and custom-frame hit testing (research R-03, UI contract §1–§2).
//
// The layout maths is a pure function of CaptionMetrics, so it can be unit-tested
// with injected values (T020); the real metrics come from the system (T021).

#include <te/window/ICaptionHitTester.h>

#include <windows.h>

#include <memory>

namespace te
{

// Everything the layout depends on, in physical pixels at `dpi`.
struct CaptionMetrics
{
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    int captionPx = 0;      // GetSystemMetricsForDpi(SM_CYCAPTION, dpi)
    int framePx = 0;        // GetSystemMetricsForDpi(SM_CYFRAME, dpi)
    int paddedBorderPx = 0; // GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
    RECT captionButtons{};  // client coords, from DWMWA_CAPTION_BUTTON_BOUNDS
    SIZE clientSize{};      // client area size
    bool maximized = false;
    // The slab faces' thicknesses at `dpi` (AppearanceSettings::slab*Px scaled; 0 for
    // a face that is off).
    int slabTopPx = 0;
    int slabLeftPx = 0;
    int slabBottomPx = 0;
    int slabRightPx = 0;
};

// Supplies CaptionMetrics for a window. Replaced by a fake in tests.
class ICaptionMetricsProvider
{
  public:
    virtual ~ICaptionMetricsProvider() = default;
    virtual CaptionMetrics Query(HWND hwnd, UINT dpi) = 0;
};

class CaptionHitTester final : public ICaptionHitTester
{
  public:
    // Picker geometry in DIPs (UI contract §1).
    static constexpr float kPickerWidthDip = 40.0f;
    static constexpr float kPickerMarginDip = 8.0f; // gap between picker and Minimize

    // With no provider, the system metrics provider is used.
    explicit CaptionHitTester(std::unique_ptr<ICaptionMetricsProvider> metrics = nullptr);

    // The layout for the given metrics (pure).
    static CaptionLayout ComputeLayout(const CaptionMetrics& metrics);

    // Steps 2–5 of the WM_NCHITTEST order for a point in client coordinates (pure):
    // top resize band (HTTOP/HTTOPLEFT/HTTOPRIGHT) -> picker (HTCLIENT) ->
    // rest of the caption strip (HTCAPTION) -> HTCLIENT. Step 1, DwmDefWindowProc for
    // the native caption buttons, is done by HitTest() before calling this.
    static LRESULT ClassifyPoint(POINT clientPt, SIZE clientSize, const CaptionLayout& layout);

    CaptionLayout Compute(HWND hwnd, UINT dpi) override;
    // The slab faces' thicknesses in pixels at 100% scale (0 = off); Compute scales them to
    // the window's DPI for the metrics.
    void SetSlabPx(int topPx, int leftPx, int bottomPx, int rightPx) noexcept
    {
        m_slabTopPx = topPx > 0 ? topPx : 0;
        m_slabLeftPx = leftPx > 0 ? leftPx : 0;
        m_slabBottomPx = bottomPx > 0 ? bottomPx : 0;
        m_slabRightPx = rightPx > 0 ? rightPx : 0;
    }
    bool HitTest(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, const CaptionLayout& layout,
                 LRESULT* result) override;

  private:
    std::unique_ptr<ICaptionMetricsProvider> m_metrics;
    int m_slabTopPx = 0;
    int m_slabLeftPx = 0;
    int m_slabBottomPx = 0;
    int m_slabRightPx = 0;
};

} // namespace te
