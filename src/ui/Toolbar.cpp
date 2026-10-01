#include <te/ui/Toolbar.h>

#include <te/render/TextHalo.h>

#include <commctrl.h>

#include <wil/result.h>

#include <cmath>
#include <utility>

namespace te
{

namespace
{

// Segoe Fluent Icons / Segoe MDL2 Assets code points (same in both fonts).
constexpr wchar_t kGlyphs[Toolbar::kButtonCount] = {
    L'', // Back
    L'', // Forward
    L'', // Up
    L'', // Refresh
};

D2D1_COLOR_F ToColor(Rgb rgb, float alpha)
{
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
}

bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

} // namespace

Toolbar::Toolbar(Strings strings) : m_strings(std::move(strings)) {}

Toolbar::~Toolbar()
{
    if (m_tooltip && IsWindow(m_tooltip))
    {
        DestroyWindow(m_tooltip);
    }
}

void Toolbar::Attach(HWND parent, UINT dpi)
{
    m_parent = parent;
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    if (m_tooltip || !parent)
    {
        return;
    }
    m_tooltip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr,
                        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    if (!m_tooltip)
    {
        LOG_LAST_ERROR();
        return;
    }
    const std::wstring* texts[] = {&m_strings.back, &m_strings.forward, &m_strings.up, &m_strings.refresh};
    for (std::size_t i = 0; i < kButtonCount; ++i)
    {
        // TTF_SUBCLASS: the tooltip watches the parent's mouse messages over each rect.
        TTTOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = parent;
        tool.uId = i;
        tool.lpszText = const_cast<LPWSTR>(texts[i]->c_str());
        SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    UpdateTooltipRects();
}

void Toolbar::SetDpi(UINT dpi)
{
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    UpdateTooltipRects();
}

void Toolbar::SetTextFormats(const TextFormats* formats)
{
    m_formats = formats;
    m_glyphs.reset(); // recreated at the current text scale
}

void Toolbar::SetCallback(std::function<void(Button)> onClick)
{
    m_onClick = std::move(onClick);
}

void Toolbar::SetState(bool canBack, bool canForward, bool canUp)
{
    const std::array<bool, kButtonCount> enabled{canBack, canForward, canUp, true};
    if (enabled != m_enabled)
    {
        m_enabled = enabled;
        Invalidate();
    }
}

void Toolbar::SetBounds(const D2D1_RECT_F& bounds)
{
    m_bounds = bounds;
    UpdateTooltipRects();
}

D2D1_RECT_F Toolbar::ButtonRect(Button button) const noexcept
{
    const auto index = static_cast<float>(button);
    const float left = m_bounds.left + kLeftPaddingDip + index * (kButtonSizeDip + kButtonGapDip);
    const float middle = (m_bounds.top + m_bounds.bottom) / 2.0f;
    return D2D1::RectF(left, middle - kButtonSizeDip / 2.0f, left + kButtonSizeDip,
                       middle + kButtonSizeDip / 2.0f);
}

std::optional<Toolbar::Button> Toolbar::ButtonAt(D2D1_POINT_2F point) const noexcept
{
    for (std::size_t i = 0; i < kButtonCount; ++i)
    {
        if (Contains(ButtonRect(static_cast<Button>(i)), point))
        {
            return static_cast<Button>(i);
        }
    }
    return std::nullopt;
}

void Toolbar::UpdateTooltipRects()
{
    if (!m_tooltip || !m_parent)
    {
        return;
    }
    const float scale = static_cast<float>(m_dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    for (std::size_t i = 0; i < kButtonCount; ++i)
    {
        const D2D1_RECT_F r = ButtonRect(static_cast<Button>(i));
        TTTOOLINFOW tool{sizeof(tool)};
        tool.hwnd = m_parent;
        tool.uId = i;
        tool.rect = {static_cast<LONG>(std::lround(r.left * scale)),
                     static_cast<LONG>(std::lround(r.top * scale)),
                     static_cast<LONG>(std::lround(r.right * scale)),
                     static_cast<LONG>(std::lround(r.bottom * scale))};
        SendMessageW(m_tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void Toolbar::Invalidate() const
{
    if (m_parent)
    {
        InvalidateRect(m_parent, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool Toolbar::OnPointerDown(D2D1_POINT_2F point)
{
    const auto button = ButtonAt(point);
    if (!button)
    {
        return false;
    }
    if (IsEnabled(*button))
    {
        m_pressed = button;
        Invalidate();
    }
    return true; // a disabled button still owns the click (nothing below it reacts)
}

bool Toolbar::OnPointerMove(D2D1_POINT_2F point)
{
    const auto button = ButtonAt(point);
    if (button != m_hot)
    {
        m_hot = button;
        Invalidate();
    }
    return button.has_value() || m_pressed.has_value();
}

bool Toolbar::OnPointerUp(D2D1_POINT_2F point)
{
    if (!m_pressed)
    {
        return false;
    }
    const Button pressed = *m_pressed;
    m_pressed.reset();
    Invalidate();
    if (ButtonAt(point) == pressed && IsEnabled(pressed) && m_onClick)
    {
        m_onClick(pressed);
    }
    return true;
}

void Toolbar::OnPointerLeave()
{
    if (m_hot)
    {
        m_hot.reset();
        Invalidate();
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

HRESULT Toolbar::EnsureGlyphFormat()
{
    if (m_glyphs)
    {
        return S_OK;
    }
    RETURN_HR_IF(E_UNEXPECTED, !m_formats || !m_formats->Factory());
    IDWriteFactory3* factory = m_formats->Factory();

    // Segoe Fluent Icons ships with Windows 11; MDL2 Assets is the Windows 10 fallback.
    m_glyphFamily = L"Segoe MDL2 Assets";
    wil::com_ptr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(factory->GetSystemFontCollection(fonts.put())))
    {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &exists)) && exists)
        {
            m_glyphFamily = L"Segoe Fluent Icons";
        }
    }
    RETURN_IF_FAILED(factory->CreateTextFormat(m_glyphFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               kGlyphSizeDip, L"", &m_glyphs));
    m_glyphs->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_glyphs->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return S_OK;
}

void Toolbar::Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    if (!dc || FAILED_LOG(EnsureGlyphFormat()))
    {
        return;
    }
    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(dc->CreateSolidColorBrush(ToColor(effective.text, 1.0f), &brush)))
    {
        return;
    }
    for (std::size_t i = 0; i < kButtonCount; ++i)
    {
        const auto button = static_cast<Button>(i);
        const D2D1_RECT_F rect = ButtonRect(button);
        const bool enabled = IsEnabled(button);
        // Hover and pressed fills match the caption buttons and the picker (T046).
        if (enabled && (m_pressed == button || m_hot == button))
        {
            brush->SetColor(ToColor(effective.text, m_pressed == button ? 0.06f : 0.10f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(rect, kCornerRadiusDip, kCornerRadiusDip),
                                     brush.get());
        }
        brush->SetColor(ToColor(effective.text, enabled ? 1.0f : 0.4f));
        const wchar_t glyph[] = {kGlyphs[i], L'\0'};
        TextHalo::DrawTextW(dc, glyph, 1, m_glyphs.get(), rect, brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP,
                            effective);
    }
}

} // namespace te
