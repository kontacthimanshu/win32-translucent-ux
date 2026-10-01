#include <te/window/CustomTitleBar.h>

#include <te/render/FocusIndicator.h>
#include <te/window/DpiManager.h>

#include <te/render/SurfacePainter.h>
#include <te/render/TextHalo.h>

#include <dwmapi.h>
#include <wincodec.h>
#include <windowsx.h>

#include <wil/result.h>

#include <algorithm>
#include <utility>

namespace te
{

CustomTitleBar::CustomTitleBar(ICaptionHitTester& hitTester, IDpiManager& dpi)
    : m_hitTester(hitTester), m_dpi(dpi)
{
}

bool CustomTitleBar::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    switch (msg)
    {
    case WM_CREATE:
        ExtendFrame(hwnd);
        // Recalculate the frame now that WM_NCCALCSIZE removes the caption.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateLayout(hwnd);
        return false; // the owner continues its own WM_CREATE handling

    case WM_DWMCOMPOSITIONCHANGED:
        ExtendFrame(hwnd);
        return false; // the owner continues: it re-probes and re-applies the backdrop (T037)

    case WM_NCCALCSIZE:
        if (wParam == TRUE)
        {
            // Let Windows compute the standard frame, then give the caption back to
            // the client area. The left, right and bottom resize borders remain.
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const LONG originalTop = params->rgrc[0].top;
            *result = DefWindowProcW(hwnd, msg, wParam, lParam);
            params->rgrc[0].top = originalTop;
            // No inset when maximized: DwmDefWindowProc only hit-tests the native
            // caption buttons when the client area starts at the window's top edge.
            // A maximized window's top rows (frame + padding) are then above the
            // screen; the layout reports them as CaptionLayout::contentTopPx and
            // the app draws below them (validation-report.md, Phase 1).
            return true;
        }
        return false;

    case WM_NCHITTEST:
        return m_hitTester.HitTest(hwnd, msg, wParam, lParam, m_layout, result);

    case WM_GETMINMAXINFO:
        ApplyMinimumSize(hwnd, reinterpret_cast<MINMAXINFO*>(lParam));
        *result = 0;
        return true;

    case WM_GETDPISCALEDSIZE:
        // Before WM_DPICHANGED (T081): ask for the size that keeps the client area's DIPs
        // exactly. Maximized and minimized windows keep Windows' own answer.
        if (!IsZoomed(hwnd) && !IsIconic(hwnd))
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            const auto newDpi = static_cast<UINT>(wParam);
            *reinterpret_cast<SIZE*>(lParam) = DpiManager::ScaledWindowSize(
                {client.right, client.bottom}, m_dpi.Dpi(), newDpi, FrameSizeForDpi(hwnd, newDpi));
            *result = TRUE;
            return true;
        }
        return false;

    // The picker is HTCLIENT (T021), so its pointer input arrives as client messages.
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (HandlePickerMouse(hwnd, msg, lParam))
        {
            *result = 0;
            return true;
        }
        return false;

    default:
        return false;
    }
}

void CustomTitleBar::ExtendFrame(HWND hwnd)
{
    // -1 on every side: the frame covers the whole client area ("sheet of glass"),
    // so the system backdrop shows wherever the app draws alpha 0.
    const MARGINS margins{-1, -1, -1, -1};
    LOG_IF_FAILED(DwmExtendFrameIntoClientArea(hwnd, &margins));
}

void CustomTitleBar::UpdateLayout(HWND hwnd)
{
    m_layout = m_hitTester.Compute(hwnd, m_dpi.Dpi());
    ProbeCaptionButtons(hwnd);
}

void CustomTitleBar::ProbeCaptionButtons(HWND hwnd)
{
    const RECT& bounds = m_layout.captionButtons;
    const LONG third = (bounds.right - bounds.left) / 3;
    for (int i = 0; i < 3; ++i)
    {
        m_buttonRects[static_cast<size_t>(i)] = {bounds.left + third * i, bounds.top,
                                                 i == 2 ? bounds.right : bounds.left + third * (i + 1),
                                                 bounds.bottom};
    }
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top || !IsWindowVisible(hwnd))
    {
        return;
    }

    // One row across the middle of the bounds, a pixel at a time.
    std::array<RECT, 3> found{};
    std::array<bool, 3> seen{};
    const LONG y = (bounds.top + bounds.bottom) / 2;
    for (LONG x = bounds.left; x < bounds.right; ++x)
    {
        POINT screen{x, y};
        ClientToScreen(hwnd, &screen);
        LRESULT hit = HTNOWHERE;
        if (!DwmDefWindowProc(hwnd, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y), &hit))
        {
            continue;
        }
        const int index = hit == HTMINBUTTON ? 0 : hit == HTMAXBUTTON ? 1 : hit == HTCLOSE ? 2 : -1;
        if (index < 0)
        {
            continue;
        }
        RECT& rect = found[static_cast<size_t>(index)];
        if (!seen[static_cast<size_t>(index)])
        {
            rect = {x, bounds.top, x + 1, bounds.bottom};
            seen[static_cast<size_t>(index)] = true;
        }
        rect.right = x + 1;
    }
    if (seen[0] && seen[1] && seen[2])
    {
        m_buttonRects = found;
    }
}

void CustomTitleBar::ApplyMinimumSize(HWND hwnd, MINMAXINFO* info) const
{
    // Client minimum: caption buttons + picker + margin + title space wide, and
    // caption + toolbar + five rows + status bar high. The window's own borders
    // (window size minus client size) are added on top.
    const LONG buttonsWidth = m_layout.captionButtons.right - m_layout.captionButtons.left;
    const LONG pickerWidth = m_layout.picker.right - m_layout.picker.left;
    const LONG gap = m_layout.captionButtons.left - m_layout.picker.right;
    const LONG minClientWidth = std::max<LONG>(buttonsWidth, 0) + std::max<LONG>(pickerWidth, 0) +
                                std::max<LONG>(gap, 0) + m_dpi.ToPx(kMinTitleWidthDip);
    const LONG minClientHeight =
        m_layout.captionHeightPx +
        m_dpi.ToPx((kToolbarHeightDip + kRowHeightDip * kMinVisibleRows + kStatusBarHeightDip) * m_textScale);

    RECT window{};
    RECT client{};
    LONG borderX = 0;
    LONG borderY = 0;
    if (GetWindowRect(hwnd, &window) && GetClientRect(hwnd, &client))
    {
        borderX = std::max<LONG>((window.right - window.left) - client.right, 0);
        borderY = std::max<LONG>((window.bottom - window.top) - client.bottom, 0);
    }
    info->ptMinTrackSize.x = minClientWidth + borderX;
    info->ptMinTrackSize.y = minClientHeight + borderY;
}

void CustomTitleBar::PaintRedirectionSurface(HWND hwnd)
{
    PAINTSTRUCT ps{};
    if (HDC hdc = BeginPaint(hwnd, &ps))
    {
        FillRect(hdc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(hwnd, &ps);
    }
}

void CustomTitleBar::SetTitle(std::wstring title)
{
    m_title = std::move(title);
}

SIZE CustomTitleBar::FrameSizeForDpi(HWND hwnd, UINT dpi)
{
    RECT frame{};
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if (!AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, dpi))
    {
        return SIZE{};
    }
    // frame.top (the caption and the top border) is client area here (WM_NCCALCSIZE).
    return SIZE{frame.right - frame.left, frame.bottom};
}

void CustomTitleBar::SetTextScale(float scale) noexcept
{
    m_textScale = std::max(1.0f, scale);
}

void CustomTitleBar::SetIcon(HICON icon)
{
    m_icon = icon;
    m_iconBitmap.reset();
}

float CustomTitleBar::ToDip(LONG px) const
{
    return static_cast<float>(px) / m_dpi.Scale();
}

HRESULT CustomTitleBar::EnsureIconBitmap(ID2D1DeviceContext* dc)
{
    if (m_iconBitmap && m_iconDpi == m_dpi.Dpi())
    {
        return S_OK;
    }
    m_iconBitmap.reset();
    RETURN_HR_IF(S_FALSE, !m_icon);

    // HICON -> WIC (32bpp premultiplied BGRA) -> Direct2D bitmap.
    // Non-throwing: Render runs inside WM_PAINT, where an exception must not escape.
    const auto wic = wil::CoCreateInstanceNoThrow<IWICImagingFactory>(CLSID_WICImagingFactory);
    RETURN_HR_IF_NULL(REGDB_E_CLASSNOTREG, wic);
    wil::com_ptr<IWICBitmap> wicIcon;
    RETURN_IF_FAILED(wic->CreateBitmapFromHICON(m_icon, wicIcon.put()));
    wil::com_ptr<IWICFormatConverter> converter;
    RETURN_IF_FAILED(wic->CreateFormatConverter(converter.put()));
    RETURN_IF_FAILED(converter->Initialize(wicIcon.get(), GUID_WICPixelFormat32bppPBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom));
    RETURN_IF_FAILED(dc->CreateBitmapFromWicBitmap(converter.get(), nullptr, m_iconBitmap.put()));
    m_iconDpi = m_dpi.Dpi();
    return S_OK;
}

void CustomTitleBar::SetPickerCallback(std::function<void()> onActivate)
{
    m_onPicker = std::move(onActivate);
}

RECT CustomTitleBar::PickerScreenRect(HWND hwnd) const
{
    RECT rect = m_layout.picker;
    MapWindowPoints(hwnd, HWND_DESKTOP, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

bool CustomTitleBar::InPicker(POINT client) const noexcept
{
    return PtInRect(&m_layout.picker, client) != FALSE;
}

// Returns true when the message belonged to the picker (the caller stops there).
bool CustomTitleBar::HandlePickerMouse(HWND hwnd, UINT msg, LPARAM lParam)
{
    const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const auto setHot = [&](bool hot) {
        if (hot != m_pickerHot)
        {
            m_pickerHot = hot;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    };

    switch (msg)
    {
    case WM_MOUSEMOVE: {
        const bool inside = InPicker(point);
        setHot(inside);
        if (inside && !m_trackingLeave)
        {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            m_trackingLeave = TrackMouseEvent(&track) != FALSE;
        }
        return inside || m_pickerPressed;
    }
    case WM_MOUSELEAVE:
        m_trackingLeave = false;
        setHot(false);
        return false;

    case WM_LBUTTONDOWN:
        if (!InPicker(point))
        {
            return false;
        }
        m_pickerPressed = true;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;

    case WM_LBUTTONUP: {
        if (!m_pickerPressed)
        {
            return false;
        }
        const bool activate = InPicker(point);
        m_pickerPressed = false;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        if (activate && m_onPicker)
        {
            m_onPicker();
        }
        return true;
    }
    case WM_CAPTURECHANGED:
        if (m_pickerPressed)
        {
            m_pickerPressed = false; // capture taken away: cancel the click
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return false;

    default:
        return false;
    }
}

void CustomTitleBar::RenderPicker(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    const float left = ToDip(m_layout.picker.left);
    const float top = ToDip(m_layout.picker.top);
    const float right = ToDip(m_layout.picker.right);
    const float bottom = ToDip(m_layout.picker.bottom);
    if (right <= left || bottom <= top || !m_textBrush)
    {
        return;
    }
    const auto color = [](Rgb rgb, float alpha) {
        return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
    };

    // Hover and pressed fills, like the Windows 11 caption buttons: the text color at low
    // opacity over the whole button.
    if (m_pickerPressed || m_pickerHot)
    {
        m_textBrush->SetColor(color(effective.text, m_pickerPressed ? 0.06f : 0.10f));
        dc->FillRectangle(D2D1::RectF(left, top, right, bottom), m_textBrush.get());
    }

    const float radius = kPickerDiameterDip / 2.0f;
    const D2D1_ELLIPSE circle =
        D2D1::Ellipse(D2D1::Point2F((left + right) / 2.0f, (top + bottom) / 2.0f), radius, radius);
    m_textBrush->SetColor(color(effective.tint, 1.0f));
    dc->FillEllipse(circle, m_textBrush.get());
    // The ring keeps a tint that matches the caption visible (40% of the text color).
    m_textBrush->SetColor(color(effective.text, 0.40f));
    const D2D1_ELLIPSE ring = D2D1::Ellipse(circle.point, radius - 0.5f, radius - 0.5f);
    dc->DrawEllipse(ring, m_textBrush.get(), 1.0f);

    // Keyboard focus (T080): a 2-DIP ring around the button, 3:1 against the caption.
    if (m_pickerFocused && m_focusVisible)
    {
        m_textBrush->SetColor(color(FocusIndicator::ColorOver(effective, false), 1.0f));
        const float inset = FocusIndicator::kWidthDip / 2.0f;
        dc->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(left + inset, top + inset, right - inset, bottom - inset), 4.0f,
                              4.0f),
            m_textBrush.get(), FocusIndicator::kWidthDip);
    }
}

void CustomTitleBar::RenderCaptionButtonHalos(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    const RECT& buttons = m_layout.captionButtons;
    if (!effective.textHalo || buttons.right <= buttons.left || buttons.bottom <= buttons.top)
    {
        return;
    }
    // The glyphs the DWM draws: Windows 11 caption buttons use Segoe Fluent Icons at
    // 10 DIP, centred in each button (ProbeCaptionButtons). The DWM draws them in the
    // light or dark caption color, which in Transparent follows our text
    // (BackdropManager), so the halo for our text color suits them.
    if (!m_captionGlyphs)
    {
        if (!m_dwrite && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                    reinterpret_cast<IUnknown**>(m_dwrite.put()))))
        {
            return;
        }
        if (FAILED(m_dwrite->CreateTextFormat(L"Segoe Fluent Icons", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                              kCaptionGlyphSizeDip, L"", m_captionGlyphs.put())))
        {
            return;
        }
        m_captionGlyphs->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_captionGlyphs->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Minimize, Maximize (Restore while maximized: no resize band then), Close.
    const bool maximized = m_layout.resizeBandPx == 0;
    constexpr wchar_t kMinimize = 0xE921;
    constexpr wchar_t kMaximize = 0xE922;
    constexpr wchar_t kRestore = 0xE923;
    constexpr wchar_t kClose = 0xE8BB;
    const wchar_t glyphs[] = {kMinimize, maximized ? kRestore : kMaximize, kClose};
    // Over the buttons is the glass (SurfacePainter::PaintSurfaces tints them in
    // Transparent), so that is what goes back inside each ring.
    const D2D1_COLOR_F glass = SurfacePainter::GlassColor(effective);
    for (size_t i = 0; i < m_buttonRects.size(); ++i)
    {
        const RECT& r = m_buttonRects[i];
        const D2D1_RECT_F button = D2D1::RectF(ToDip(r.left), ToDip(r.top), ToDip(r.right), ToDip(r.bottom));
        TextHalo::DrawHaloAround(dc, &glyphs[i], 1, m_captionGlyphs.get(), button, effective.text, glass,
                                 effective);
    }
}

void CustomTitleBar::SetPickerFocused(bool focused, bool visible)
{
    m_pickerFocused = focused;
    m_focusVisible = visible;
}

D2D1_RECT_F CustomTitleBar::TitleArea() const
{
    // From the top of the client area: the resize band above the title is part of the
    // same caption strip, and stopping the floor below it leaves a visible seam. When
    // maximized, the rows above contentTopPx are off screen anyway.
    const float bottom = ToDip(m_layout.captionHeightPx);
    const float right = std::max(0.0f, ToDip(m_layout.dragRegion.right));
    return D2D1::RectF(0.0f, 0.0f, right, std::max(0.0f, bottom));
}

void CustomTitleBar::Render(ID2D1DeviceContext* dc, IDWriteTextFormat* titleFormat,
                            const EffectiveAppearance& effective)
{
    const D2D1_COLOR_F textColor =
        D2D1::ColorF(effective.text.r / 255.0f, effective.text.g / 255.0f, effective.text.b / 255.0f);
    // Bitmaps and brushes belong to one Direct2D device; after a device loss the
    // render device creates a new one, so drop what was made on the old one.
    wil::com_ptr<ID2D1Device> device;
    dc->GetDevice(device.put());
    if (device != m_resourceDevice)
    {
        m_iconBitmap.reset();
        m_textBrush.reset();
        m_resourceDevice = std::move(device);
    }

    // Surface-opacity floor behind the icon and title (does nothing when not needed).
    SurfacePainter::PaintTextScrim(dc, TitleArea(), effective);

    // Everything in DIPs: the device context applies the DPI.
    const float top = ToDip(std::max(m_layout.resizeBandPx, m_layout.contentTopPx));
    const float bottom = ToDip(m_layout.captionHeightPx);
    const float regionRight = ToDip(m_layout.dragRegion.right);
    const float middle = (top + bottom) / 2.0f;

    float textLeft = kIconMarginDip;
    if (SUCCEEDED(EnsureIconBitmap(dc)) && m_iconBitmap)
    {
        const D2D1_RECT_F iconRect = D2D1::RectF(kIconMarginDip, middle - kIconSizeDip / 2.0f,
                                                 kIconMarginDip + kIconSizeDip, middle + kIconSizeDip / 2.0f);
        dc->DrawBitmap(m_iconBitmap.get(), iconRect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        textLeft = iconRect.right + kIconTitleGapDip;
    }

    if (!m_textBrush)
    {
        LOG_IF_FAILED(dc->CreateSolidColorBrush(textColor, m_textBrush.put()));
    }
    RenderPicker(dc, effective);
    RenderCaptionButtonHalos(dc, effective);

    const float textRight = regionRight - kIconTitleGapDip;
    if (m_title.empty() || !titleFormat || textRight <= textLeft)
    {
        return;
    }
    if (!m_textBrush)
    {
        return;
    }
    m_textBrush->SetColor(textColor);
    // Clipping keeps the ellipsis-trimmed title out of the picker area.
    constexpr D2D1_DRAW_TEXT_OPTIONS options =
        D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;
    const auto length = static_cast<UINT32>(m_title.size());
    const float available = std::max(0.0f, bottom - top);
    wil::com_ptr<IDWriteTextLayout> layout;
    if (m_dwrite || SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                  reinterpret_cast<IUnknown**>(m_dwrite.put()))))
    {
        (void)m_dwrite->CreateTextLayout(m_title.c_str(), length, titleFormat, textRight - textLeft,
                                         available, layout.put());
    }
    if (!layout)
    {
        TextHalo::DrawTextW(dc, m_title.c_str(), length, titleFormat,
                            D2D1::RectF(textLeft, top, textRight, bottom), m_textBrush.get(), options,
                            effective);
        return;
    }
    // The caption strip keeps the height Windows gives it, while the title grows with the
    // Windows text size (T082): a title taller than the strip is drawn smaller, so it fits.
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics)) && metrics.height > available && available > 0.0f)
    {
        (void)layout->SetFontSize(titleFormat->GetFontSize() * available / metrics.height,
                                  DWRITE_TEXT_RANGE{0, length});
    }
    TextHalo::DrawTextLayout(dc, D2D1::Point2F(textLeft, top), layout.get(), m_textBrush.get(), options,
                             effective);
}

} // namespace te
