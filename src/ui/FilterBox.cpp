#include <te/ui/FilterBox.h>

#include <te/ui/EditHalo.h>

#include <te/a11y/HwndAnnotation.h>
#include <te/render/FocusIndicator.h>
#include <te/render/SurfacePainter.h>
#include <te/render/TextHalo.h>

#include <commctrl.h>

#include <wil/com.h>
#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace te
{

namespace
{

D2D1_COLOR_F ToColor(Rgb rgb, float alpha = 1.0f)
{
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
}

COLORREF ToColorRef(Rgb rgb)
{
    return RGB(rgb.r, rgb.g, rgb.b);
}

} // namespace

FilterBox::~FilterBox()
{
    Detach();
}

void FilterBox::Detach()
{
    if (m_edit && IsWindow(m_edit))
    {
        ClearHwndAnnotation(m_edit);
        RemoveWindowSubclass(m_edit, EditProc, 1);
        DestroyWindow(m_edit);
    }
    m_edit = nullptr;
}

void FilterBox::Attach(HWND parent, UINT dpi) noexcept
{
    m_parent = parent;
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
}

void FilterBox::SetDpi(UINT dpi)
{
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    UpdateFont();
    PositionEdit();
}

void FilterBox::SetTextFormats(const TextFormats* formats) noexcept
{
    m_formats = formats;
}

void FilterBox::SetPlaceholder(std::wstring text)
{
    m_placeholder = std::move(text);
    if (m_edit)
    {
        AnnotateHwnd(m_edit, m_placeholder, L"FilterBox");
    }
    Invalidate();
}

void FilterBox::SetChangedCallback(std::function<void(const std::wstring&)> callback)
{
    m_changed = std::move(callback);
}

void FilterBox::SetDoneCallback(std::function<void()> callback)
{
    m_done = std::move(callback);
}

float FilterBox::ToPx(float dip) const noexcept
{
    return dip * static_cast<float>(m_dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

float FilterBox::FieldHeight() const noexcept
{
    return kFieldHeightDip * (m_formats ? m_formats->TextScale() : 1.0f);
}

void FilterBox::Invalidate() const
{
    if (m_parent)
    {
        InvalidateRect(m_parent, nullptr, FALSE);
    }
}

void FilterBox::SetBounds(const D2D1_RECT_F& bounds)
{
    m_bounds = bounds;
    PositionEdit();
}

D2D1_RECT_F FilterBox::FieldRect() const noexcept
{
    const float middle = (m_bounds.top + m_bounds.bottom) / 2.0f;
    const float half = std::min(FieldHeight(), m_bounds.bottom - m_bounds.top) / 2.0f;
    return D2D1::RectF(m_bounds.left, middle - half, m_bounds.right, middle + half);
}

void FilterBox::OnTextScaleChanged()
{
    UpdateFont();
    PositionEdit();
}

void FilterBox::SetFocusVisible(bool visible)
{
    if (visible != m_focusVisible)
    {
        m_focusVisible = visible;
        Invalidate();
    }
}

// ---------------------------------------------------------------------------
// The edit
// ---------------------------------------------------------------------------

bool FilterBox::EnsureEdit()
{
    if (m_edit)
    {
        return true;
    }
    if (!m_parent)
    {
        return false;
    }
    // Layered and colour-keyed on the typical surface colour, as the address edit (R-02).
    m_edit =
        CreateWindowExW(WS_EX_LAYERED, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, m_parent,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditControlId)),
                        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_parent, GWLP_HINSTANCE)), nullptr);
    if (!m_edit)
    {
        LOG_LAST_ERROR();
        return false;
    }
    SetLayeredWindowAttributes(m_edit, m_key, 0, LWA_COLORKEY);
    SetWindowSubclass(m_edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    AnnotateHwnd(m_edit, m_placeholder, L"FilterBox"); // "Filter", for screen readers
    UpdateFont();
    return true;
}

void FilterBox::UpdateFont()
{
    if (!m_edit)
    {
        return;
    }
    const std::wstring family = m_formats ? m_formats->FamilyName() : L"Segoe UI";
    const float size = TextFormats::kBodySize * (m_formats ? m_formats->TextScale() : 1.0f);
    m_font.reset(CreateFontW(-static_cast<int>(std::lround(ToPx(size))), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, family.c_str()));
    SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_font.get()), TRUE);
}

void FilterBox::PositionEdit()
{
    if (!m_edit)
    {
        return;
    }
    const D2D1_RECT_F field = FieldRect();
    const int lineHeight = static_cast<int>(std::lround(ToPx(TextFormats::kBodySize * 1.6f)));
    const int left = static_cast<int>(std::lround(ToPx(field.left + kTextInsetDip)));
    const int right = static_cast<int>(std::lround(ToPx(field.right - kTextInsetDip)));
    const int middle = static_cast<int>(std::lround(ToPx((field.top + field.bottom) / 2.0f)));
    SetWindowPos(m_edit, HWND_TOP, left, middle - lineHeight / 2, std::max(0, right - left), lineHeight,
                 SWP_NOACTIVATE);
}

void FilterBox::Focus()
{
    if (!EnsureEdit())
    {
        return;
    }
    m_editing = true;
    m_settingText = true;
    SetWindowTextW(m_edit, m_text.c_str());
    m_settingText = false;
    PositionEdit();
    ShowWindow(m_edit, SW_SHOW);
    SetFocus(m_edit);
    SendMessageW(m_edit, EM_SETSEL, 0, -1);
    Invalidate();
}

void FilterBox::EndEdit()
{
    m_ending = true;
    m_editing = false;
    if (m_edit)
    {
        const bool hadFocus = GetFocus() == m_edit;
        ShowWindow(m_edit, SW_HIDE);
        if (hadFocus && m_parent)
        {
            SetFocus(m_parent);
        }
    }
    m_ending = false;
    Invalidate();
}

void FilterBox::Clear()
{
    const bool changed = !m_text.empty();
    m_text.clear();
    if (m_edit)
    {
        m_settingText = true;
        SetWindowTextW(m_edit, L"");
        m_settingText = false;
    }
    if (changed && m_changed)
    {
        m_changed(m_text);
    }
    Invalidate();
}

bool FilterBox::OnCommand(HWND control, UINT code)
{
    if (!m_edit || control != m_edit)
    {
        return false;
    }
    if (code == EN_CHANGE && !m_settingText)
    {
        const int length = GetWindowTextLengthW(m_edit);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(m_edit, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(length));
        if (text != m_text)
        {
            m_text = std::move(text);
            if (m_changed)
            {
                m_changed(m_text); // filter as the user types
            }
        }
    }
    return true;
}

LRESULT CALLBACK FilterBox::EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                     DWORD_PTR data)
{
    auto* self = reinterpret_cast<FilterBox*>(data);
    switch (msg)
    {
    case WM_NCDESTROY:
        ClearHwndAnnotation(hwnd);
        self->m_edit = nullptr;
        RemoveWindowSubclass(hwnd, EditProc, 1);
        break;
    case WM_GETDLGCODE:
        return DefSubclassProc(hwnd, msg, wParam, lParam) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            self->Clear(); // Escape clears the filter and leaves
            self->EndEdit();
            if (self->m_done)
            {
                self->m_done();
            }
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            self->EndEdit(); // Enter keeps the filter and goes back to the list
            if (self->m_done)
            {
                self->m_done();
            }
            return 0;
        }
        if (wParam == VK_F6 && self->m_parent)
        {
            SendMessageW(self->m_parent, WM_KEYDOWN, wParam, lParam); // the focus ring (T080)
            return 0;
        }
        break;
    case WM_CHAR:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE)
        {
            return 0; // no beep
        }
        break;
    case WM_KILLFOCUS:
        if (!self->m_ending && self->m_editing)
        {
            const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            self->EndEdit(); // the filter stays; the box shows it
            return result;
        }
        break;
    default:
        break;
    }
    const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
    EditHalo::AfterMessage(hwnd, msg, wParam, self->m_ownText);
    return result;
}

void FilterBox::ApplyAppearance(const EffectiveAppearance& effective)
{
    const COLORREF key = ToColorRef(effective.typicalSurfaceColor);
    const COLORREF text = ToColorRef(effective.text);
    if (key == m_key && text == m_textColor && effective.textHalo == m_ownText && m_keyBrush)
    {
        return;
    }
    m_key = key;
    m_textColor = text;
    m_ownText = effective.textHalo;
    m_keyBrush.reset(CreateSolidBrush(m_key));
    if (m_edit)
    {
        SetLayeredWindowAttributes(m_edit, m_key, 0, LWA_COLORKEY);
        InvalidateRect(m_edit, nullptr, TRUE);
    }
}

bool FilterBox::HandleCtlColor(HWND control, HDC dc, LRESULT* result)
{
    if (!m_edit || control != m_edit)
    {
        return false;
    }
    if (!m_keyBrush)
    {
        m_keyBrush.reset(CreateSolidBrush(m_key));
    }
    SetBkColor(dc, m_key);
    // Text in the key colour vanishes with the background; Render draws it instead.
    SetTextColor(dc, m_ownText ? m_key : m_textColor);
    *result = reinterpret_cast<LRESULT>(m_keyBrush.get());
    return true;
}

bool FilterBox::OnPointerDown(D2D1_POINT_2F point)
{
    const D2D1_RECT_F field = FieldRect();
    if (point.x < field.left || point.x >= field.right || point.y < field.top || point.y >= field.bottom)
    {
        return false;
    }
    if (!m_editing)
    {
        Focus();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void FilterBox::Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    ApplyAppearance(effective);
    IDWriteTextFormat* format = m_formats ? m_formats->Body() : nullptr;
    const D2D1_RECT_F field = FieldRect();
    if (!dc || !format || field.right <= field.left)
    {
        return;
    }
    SurfacePainter::PaintTextScrim(dc, field, effective); // legibility floor (R-05)

    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(
            dc->CreateSolidColorBrush(ToColor(effective.secondaryText, m_editing ? 0.8f : 0.35f), &brush)))
    {
        return;
    }
    const float inset = 0.5f;
    dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(field.left + inset, field.top + inset,
                                                           field.right - inset, field.bottom - inset),
                                               kCornerRadiusDip, kCornerRadiusDip),
                             brush.get(), m_editing ? 1.5f : 1.0f);
    if (m_editing && m_focusVisible)
    {
        brush->SetColor(ToColor(FocusIndicator::ColorOver(effective, false)));
        const float ring = FocusIndicator::kWidthDip / 2.0f;
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(field.left + ring, field.top + ring,
                                                               field.right - ring, field.bottom - ring),
                                                   kCornerRadiusDip, kCornerRadiusDip),
                                 brush.get(), FocusIndicator::kWidthDip);
    }
    if (m_editing)
    {
        // The edit draws the text, except in Transparent, where it is drawn here.
        EditHalo::Render(dc, m_edit, m_parent, m_dpi, effective);
        return;
    }
    // The filter text, or the placeholder in the secondary colour.
    const bool placeholder = m_text.empty();
    const std::wstring& shown = placeholder ? m_placeholder : m_text;
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    brush->SetColor(ToColor(placeholder ? effective.secondaryText : effective.text));
    TextHalo::DrawTextW(
        dc, shown.c_str(), static_cast<UINT32>(shown.size()), format,
        D2D1::RectF(field.left + kTextInsetDip, field.top, field.right - kTextInsetDip, field.bottom),
        brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, effective);
}

} // namespace te
