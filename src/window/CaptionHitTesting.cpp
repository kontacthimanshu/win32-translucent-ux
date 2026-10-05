#include <te/window/CaptionHitTester.h>

#include <te/window/DpiManager.h>

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace te
{

namespace
{

// Width of the three native caption buttons (Minimize, Maximize/Restore, Close) in
// DIPs, used only if the DWM does not report their bounds.
constexpr float kFallbackCaptionButtonsWidthDip = 3 * 46.0f;

// Real metrics for a window: system metrics at the window's DPI and the DWM
// caption-button bounds converted from window to client coordinates.
class SystemCaptionMetricsProvider final : public ICaptionMetricsProvider
{
  public:
    CaptionMetrics Query(HWND hwnd, UINT dpi) override
    {
        CaptionMetrics m;
        m.dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
        m.captionPx = GetSystemMetricsForDpi(SM_CYCAPTION, m.dpi);
        m.framePx = GetSystemMetricsForDpi(SM_CYFRAME, m.dpi);
        m.paddedBorderPx = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m.dpi);
        m.maximized = IsZoomed(hwnd) != FALSE;

        RECT client{};
        GetClientRect(hwnd, &client);
        m.clientSize = {client.right - client.left, client.bottom - client.top};

        // DWMWA_CAPTION_BUTTON_BOUNDS is window-relative; shift it by the offset of
        // the client origin within the window rectangle.
        RECT buttons{};
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof(buttons))))
        {
            RECT window{};
            POINT origin{0, 0};
            GetWindowRect(hwnd, &window);
            ClientToScreen(hwnd, &origin);
            OffsetRect(&buttons, window.left - origin.x, window.top - origin.y);
            m.captionButtons = buttons;
        }
        return m;
    }
};

} // namespace

CaptionHitTester::CaptionHitTester(std::unique_ptr<ICaptionMetricsProvider> metrics)
    : m_metrics(metrics ? std::move(metrics) : std::make_unique<SystemCaptionMetricsProvider>())
{
}

CaptionLayout CaptionHitTester::ComputeLayout(const CaptionMetrics& m)
{
    const DpiManager scale(m.dpi);
    CaptionLayout layout;
    // The slab's top face is part of the caption strip: it drags the window too.
    layout.slabTopPx = std::max(0, m.slabTopPx);
    layout.slabLeftPx = std::max(0, m.slabLeftPx);
    layout.slabBottomPx = std::max(0, m.slabBottomPx);
    layout.slabRightPx = std::max(0, m.slabRightPx);
    layout.captionHeightPx = m.captionPx + m.framePx + m.paddedBorderPx + layout.slabTopPx;
    layout.resizeBandPx = m.maximized ? 0 : m.framePx + m.paddedBorderPx;
    // Maximized: the client area starts at the window's top edge, which is above
    // the screen by the frame thickness; visible content starts below it.
    layout.contentTopPx = m.maximized ? m.framePx + m.paddedBorderPx : 0;
    const LONG visibleTop = std::max(layout.resizeBandPx, layout.contentTopPx);

    layout.captionButtons = m.captionButtons;
    if (IsRectEmpty(&layout.captionButtons))
    {
        // No DWM bounds (e.g. before the frame exists): assume the standard
        // buttons at the right edge of the caption strip.
        layout.captionButtons = {m.clientSize.cx - scale.ToPx(kFallbackCaptionButtonsWidthDip), 0,
                                 m.clientSize.cx, layout.captionHeightPx};
    }

    // Picker: immediately left of Minimize with the margin, aligned with the
    // caption buttons, and below the top resize band or the off-screen rows
    // (UI contract §1–§2).
    const LONG right = layout.captionButtons.left - scale.ToPx(kPickerMarginDip);
    const LONG left = std::max<LONG>(0, right - scale.ToPx(kPickerWidthDip));
    const LONG top = std::max<LONG>(layout.captionButtons.top, visibleTop);
    const LONG bottom =
        layout.captionButtons.bottom > top ? layout.captionButtons.bottom : LONG{layout.captionHeightPx};
    layout.picker = {left, top, std::max(left, right), bottom};

    // The slab button: immediately left of the picker, the same size.
    const LONG slabRight = layout.picker.left;
    const LONG slabLeft = std::max<LONG>(0, slabRight - (layout.picker.right - layout.picker.left));
    layout.slabButton = {slabLeft, top, slabRight, bottom};

    // Drag region: the caption strip below and right of the slab's faces, up to the
    // slab button (where the icon and title go).
    layout.dragRegion = {std::min<LONG>(layout.slabLeftPx, layout.slabButton.left),
                         visibleTop + layout.slabTopPx, layout.slabButton.left, layout.captionHeightPx};
    return layout;
}

CaptionLayout CaptionHitTester::Compute(HWND hwnd, UINT dpi)
{
    CaptionMetrics metrics = m_metrics->Query(hwnd, dpi);
    const DpiManager scale(metrics.dpi);
    metrics.slabTopPx = scale.ToPx(static_cast<float>(m_slabTopPx));
    metrics.slabLeftPx = scale.ToPx(static_cast<float>(m_slabLeftPx));
    metrics.slabBottomPx = scale.ToPx(static_cast<float>(m_slabBottomPx));
    metrics.slabRightPx = scale.ToPx(static_cast<float>(m_slabRightPx));
    return ComputeLayout(metrics);
}

LRESULT CaptionHitTester::ClassifyPoint(POINT pt, SIZE clientSize, const CaptionLayout& layout)
{
    // Top resize band, with corners as wide as the band (not when maximized).
    if (layout.resizeBandPx > 0 && pt.y < layout.resizeBandPx)
    {
        if (pt.x < layout.resizeBandPx)
        {
            return HTTOPLEFT;
        }
        if (pt.x >= clientSize.cx - layout.resizeBandPx)
        {
            return HTTOPRIGHT;
        }
        return HTTOP;
    }
    if (PtInRect(&layout.picker, pt) || PtInRect(&layout.slabButton, pt))
    {
        return HTCLIENT;
    }
    // Anything else in the caption strip, including the gap between the picker
    // and the caption buttons, drags the window. (The buttons themselves were
    // already claimed by DwmDefWindowProc.)
    if (pt.y < layout.captionHeightPx)
    {
        return HTCAPTION;
    }
    return HTCLIENT;
}

bool CaptionHitTester::HitTest(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, const CaptionLayout& layout,
                               LRESULT* result)
{
    if (msg != WM_NCHITTEST)
    {
        return false;
    }

    // 1. Native caption buttons first: Minimize, Maximize (Snap Layouts), Close.
    LRESULT dwmResult = 0;
    if (DwmDefWindowProc(hwnd, msg, wParam, lParam, &dwmResult))
    {
        *result = dwmResult;
        return true;
    }

    // The side and bottom borders stay non-client (WM_NCCALCSIZE keeps them), so
    // points outside the client area are left to DefWindowProc.
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &pt);
    RECT client{};
    GetClientRect(hwnd, &client);
    if (!PtInRect(&client, pt))
    {
        return false;
    }

    // 2–5.
    *result = ClassifyPoint(pt, {client.right, client.bottom}, layout);
    return true;
}

} // namespace te
