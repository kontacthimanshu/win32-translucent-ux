#include <te/ui/StatusBar.h>

#include <te/render/TextHalo.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace te
{

namespace
{

// FormatMessageW over a template with inserts passed as an argument array
// (%1 strings as pointers, %n!u! numbers as values).
std::wstring Format(const std::wstring& pattern, std::initializer_list<DWORD_PTR> args)
{
    LPWSTR raw = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY |
                                            FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                        pattern.c_str(), 0, 0, reinterpret_cast<LPWSTR>(&raw), 0,
                                        reinterpret_cast<va_list*>(const_cast<DWORD_PTR*>(args.begin())));
    const wil::unique_hlocal buffer(raw);
    return length > 0 ? std::wstring(raw, length) : std::wstring();
}

DWORD_PTR Text(const std::wstring& text)
{
    return reinterpret_cast<DWORD_PTR>(text.c_str());
}

DWORD_PTR Percent(double fraction)
{
    return static_cast<DWORD_PTR>(std::lround(std::clamp(fraction, 0.0, 1.0) * 100.0));
}

const std::wstring& ModeName(const StatusBar::Strings& strings, BackdropMode mode)
{
    switch (mode)
    {
    case BackdropMode::Acrylic:
        return strings.modeAcrylic;
    case BackdropMode::Mica:
        return strings.modeMica;
    case BackdropMode::Transparent:
        return strings.modeTransparent;
    case BackdropMode::Solid:
        break;
    }
    return strings.modeSolid;
}

const std::wstring* ReasonText(const StatusBar::Strings& strings, FallbackReason reason)
{
    switch (reason)
    {
    case FallbackReason::HighContrast:
        return &strings.fallbackHighContrast;
    case FallbackReason::TransparencyOff:
        return &strings.fallbackTransparencyOff;
    case FallbackReason::BackdropUnsupported:
        return &strings.fallbackUnsupported;
    case FallbackReason::BackdropApplyFailed:
        return &strings.fallbackApplyFailed;
    case FallbackReason::None:
        break;
    }
    return nullptr;
}

void LoadInto(HINSTANCE instance, UINT id, std::wstring& target)
{
    if (id == 0)
    {
        return;
    }
    const wchar_t* text = nullptr;
    // With a zero buffer size LoadStringW returns a read-only pointer into the
    // resource; it is not null-terminated, so use the returned length.
    const int length = LoadStringW(instance, id, reinterpret_cast<LPWSTR>(&text), 0);
    if (length > 0)
    {
        target.assign(text, static_cast<std::size_t>(length));
    }
}

D2D1_COLOR_F ToColor(Rgb rgb)
{
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f);
}

void DrawAligned(ID2D1DeviceContext* dc, IDWriteTextFormat* format, const std::wstring& text,
                 const D2D1_RECT_F& rect, DWRITE_TEXT_ALIGNMENT alignment, ID2D1Brush* brush,
                 const EffectiveAppearance& effective)
{
    if (text.empty() || rect.right <= rect.left)
    {
        return;
    }
    // The status format belongs to the status bar alone, so its alignment is set per draw.
    format->SetTextAlignment(alignment);
    TextHalo::DrawTextW(dc, text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP, effective);
}

} // namespace

StatusBar::Strings StatusBar::Strings::Load(HINSTANCE instance, const StringIds& ids)
{
    Strings strings;
    LoadInto(instance, ids.modeAcrylic, strings.modeAcrylic);
    LoadInto(instance, ids.modeMica, strings.modeMica);
    LoadInto(instance, ids.modeSolid, strings.modeSolid);
    LoadInto(instance, ids.modeTransparent, strings.modeTransparent);
    LoadInto(instance, ids.appearanceFmt, strings.appearanceFmt);
    LoadInto(instance, ids.appearanceSolidFmt, strings.appearanceSolidFmt);
    LoadInto(instance, ids.fallbackFmt, strings.fallbackFmt);
    LoadInto(instance, ids.fallbackHighContrast, strings.fallbackHighContrast);
    LoadInto(instance, ids.fallbackTransparencyOff, strings.fallbackTransparencyOff);
    LoadInto(instance, ids.fallbackUnsupported, strings.fallbackUnsupported);
    LoadInto(instance, ids.fallbackApplyFailed, strings.fallbackApplyFailed);
    LoadInto(instance, ids.itemsFmt, strings.itemsFmt);
    LoadInto(instance, ids.selectedFmt, strings.selectedFmt);
    return strings;
}

StatusBar::StatusBar(Strings strings) : m_strings(std::move(strings)) {}

StatusBar::~StatusBar()
{
    if (m_owner && IsWindow(m_owner))
    {
        KillTimer(m_owner, kTransientTimerId);
    }
}

void StatusBar::Attach(HWND owner) noexcept
{
    m_owner = owner;
}

void StatusBar::SetTextFormats(const TextFormats* formats) noexcept
{
    m_formats = formats;
}

bool StatusBar::OnTimer(UINT_PTR timerId)
{
    if (timerId != kTransientTimerId)
    {
        return false;
    }
    if (m_owner)
    {
        KillTimer(m_owner, kTransientTimerId);
    }
    m_transient.clear();
    Invalidate();
    return true;
}

void StatusBar::SetItemCounts(std::size_t items, std::size_t selected)
{
    m_items = items;
    m_selected = selected;
    Invalidate();
}

void StatusBar::SetAppearance(const AppearanceSettings& requested, const EffectiveAppearance& effective)
{
    m_rightText = FormatAppearance(m_strings, requested, effective);
    Invalidate();
}

void StatusBar::SetTransientMessage(std::wstring text, UINT durationMs)
{
    m_transient = std::move(text);
    if (m_owner)
    {
        // Replaces any running timer, so the newest message gets its full duration.
        SetTimer(m_owner, kTransientTimerId, std::max<UINT>(durationMs, USER_TIMER_MINIMUM), nullptr);
    }
    Invalidate();
    if (m_messageSet)
    {
        m_messageSet();
    }
}

void StatusBar::SetOperationMessage(std::wstring text)
{
    m_operation = std::move(text);
    Invalidate();
    if (m_messageSet)
    {
        m_messageSet();
    }
}

void StatusBar::ClearOperationMessage()
{
    m_operation.clear();
    Invalidate();
}

void StatusBar::SetBounds(const D2D1_RECT_F& bounds)
{
    m_bounds = bounds;
}

std::wstring StatusBar::LeftText() const
{
    if (!m_operation.empty())
    {
        return m_operation;
    }
    if (!m_transient.empty())
    {
        return m_transient;
    }
    if (!m_items)
    {
        return {};
    }
    std::wstring text = Format(m_strings.itemsFmt, {static_cast<DWORD_PTR>(*m_items)});
    if (m_selected > 0)
    {
        text += L"   "; // as in the UI §1 mock-up: "1,204 items   3 selected"
        text += Format(m_strings.selectedFmt, {static_cast<DWORD_PTR>(m_selected)});
    }
    return text;
}

std::wstring StatusBar::FormatAppearance(const Strings& strings, const AppearanceSettings& requested,
                                         const EffectiveAppearance& effective)
{
    const std::wstring& mode = ModeName(strings, effective.applied);
    std::wstring text = effective.applied == BackdropMode::Solid
                            ? Format(strings.appearanceSolidFmt, {Text(mode)})
                            : Format(strings.appearanceFmt, {Text(mode), Percent(requested.surfaceOpacity),
                                                             Percent(requested.tintOpacity)});

    // The status bar always names the applied mode; a different requested mode is
    // explained (UI §3, spec US1-3).
    if (effective.applied != effective.requested)
    {
        if (const std::wstring* reason = ReasonText(strings, effective.reason))
        {
            text += L' ';
            text += Format(strings.fallbackFmt, {Text(*reason)});
        }
    }
    return text;
}

void StatusBar::Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    IDWriteTextFormat* format = m_formats ? m_formats->Status() : nullptr;
    if (!dc || !format || m_bounds.right <= m_bounds.left || m_bounds.bottom <= m_bounds.top)
    {
        return;
    }

    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(dc->CreateSolidColorBrush(ToColor(effective.text), &brush)))
    {
        return;
    }

    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const D2D1_RECT_F inner =
        D2D1::RectF(m_bounds.left + kPaddingDip, m_bounds.top, m_bounds.right - kPaddingDip, m_bounds.bottom);

    // Left: primary text. Right: the appearance summary in the secondary color.
    DrawAligned(dc, format, LeftText(), inner, DWRITE_TEXT_ALIGNMENT_LEADING, brush.get(), effective);
    brush->SetColor(ToColor(effective.secondaryText));
    DrawAligned(dc, format, m_rightText, inner, DWRITE_TEXT_ALIGNMENT_TRAILING, brush.get(), effective);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

IRawElementProviderFragment* StatusBar::Automation()
{
    return nullptr;
}

void StatusBar::Invalidate() const
{
    if (m_owner)
    {
        InvalidateRect(m_owner, nullptr, FALSE);
    }
}

} // namespace te
