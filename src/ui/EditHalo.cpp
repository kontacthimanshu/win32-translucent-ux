#include <te/ui/EditHalo.h>

#include <te/render/TextHalo.h>

#include <dwrite.h>

#include <wil/com.h>
#include <wil/result_macros.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace te::EditHalo
{

namespace
{

IDWriteFactory* Factory()
{
    static wil::com_ptr<IDWriteFactory> factory = [] {
        wil::com_ptr<IDWriteFactory> created;
        LOG_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(created.put())));
        return created;
    }();
    return factory.get();
}

// A text format for the edit's GDI font: same family, weight, style, stretch and size.
wil::com_ptr<IDWriteTextFormat> FormatFor(HWND edit, float pixelsPerDip)
{
    IDWriteFactory* factory = Factory();
    const auto font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    LOGFONTW logFont{};
    if (!factory || !font || GetObjectW(font, sizeof(logFont), &logFont) == 0)
    {
        return nullptr;
    }
    wil::com_ptr<IDWriteGdiInterop> interop;
    wil::com_ptr<IDWriteFont> dwriteFont;
    wil::com_ptr<IDWriteFontFamily> family;
    wil::com_ptr<IDWriteLocalizedStrings> names;
    if (FAILED(factory->GetGdiInterop(interop.put())) ||
        FAILED(interop->CreateFontFromLOGFONT(&logFont, dwriteFont.put())) ||
        FAILED(dwriteFont->GetFontFamily(family.put())) || FAILED(family->GetFamilyNames(names.put())))
    {
        return nullptr;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists)
    {
        index = 0;
    }
    UINT32 length = 0;
    if (FAILED(names->GetStringLength(index, &length)))
    {
        return nullptr;
    }
    std::wstring name(length + 1, L'\0');
    if (FAILED(names->GetString(index, name.data(), length + 1)))
    {
        return nullptr;
    }
    name.resize(length);

    // A negative lfHeight is the em height in pixels (what CreateFontW gets from the
    // components); a positive one is the cell height, which is close enough as a fallback.
    const float sizeDip = static_cast<float>(std::abs(logFont.lfHeight)) / pixelsPerDip;
    wil::com_ptr<IDWriteTextFormat> format;
    if (FAILED(factory->CreateTextFormat(name.c_str(), nullptr, dwriteFont->GetWeight(),
                                         dwriteFont->GetStyle(), dwriteFont->GetStretch(),
                                         std::max(1.0f, sizeDip), L"", format.put())))
    {
        return nullptr;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

} // namespace

void Render(ID2D1DeviceContext* dc, HWND edit, HWND owner, UINT dpi, const EffectiveAppearance& effective)
{
    if (!effective.textHalo || !dc || !edit || !IsWindowVisible(edit) || dpi == 0)
    {
        return;
    }
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0)
    {
        return;
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(edit, text.data(), length + 1)));

    // A single-line edit answers EM_GETFIRSTVISIBLELINE with its first visible character.
    const auto first = static_cast<size_t>(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0));
    if (first >= text.size())
    {
        return;
    }
    const LRESULT position = SendMessageW(edit, EM_POSFROMCHAR, first, 0);
    if (position == -1)
    {
        return;
    }
    POINT origin{static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
    RECT client{};
    GetClientRect(edit, &client);
    MapWindowPoints(edit, owner, &origin, 1);
    MapWindowPoints(edit, owner, reinterpret_cast<POINT*>(&client), 2);

    const float pixelsPerDip = static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;
    const wil::com_ptr<IDWriteTextFormat> format = FormatFor(edit, pixelsPerDip);
    if (!format)
    {
        return;
    }
    // GDI-classic measuring: the advances GDI uses, so each character lands where the
    // edit puts it and its caret.
    const std::wstring_view shown = std::wstring_view(text).substr(first);
    wil::com_ptr<IDWriteTextLayout> layout;
    if (FAILED_LOG(Factory()->CreateGdiCompatibleTextLayout(shown.data(), static_cast<UINT32>(shown.size()),
                                                            format.get(), 1.0e6f, 1.0e6f, pixelsPerDip,
                                                            nullptr, FALSE, layout.put())))
    {
        return;
    }
    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(dc->CreateSolidColorBrush(
            D2D1::ColorF(effective.text.r / 255.0f, effective.text.g / 255.0f, effective.text.b / 255.0f),
            brush.put())))
    {
        return;
    }
    const auto toDip = [&](LONG px) { return static_cast<float>(px) / pixelsPerDip; };
    dc->PushAxisAlignedClip(
        D2D1::RectF(toDip(client.left), toDip(client.top), toDip(client.right), toDip(client.bottom)),
        D2D1_ANTIALIAS_MODE_ALIASED);
    TextHalo::DrawTextLayout(dc, D2D1::Point2F(toDip(origin.x), toDip(origin.y)), layout.get(), brush.get(),
                             D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, effective);
    dc->PopAxisAlignedClip();
}

void AfterMessage(HWND edit, UINT msg, WPARAM wParam, bool active)
{
    if (!active)
    {
        return;
    }
    switch (msg)
    {
    case WM_MOUSEMOVE:
        if ((wParam & MK_LBUTTON) == 0)
        {
            return; // not a drag selection: nothing scrolls
        }
        break;
    case WM_CHAR:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_TIMER: // auto-scroll while a drag selection is outside the edit
    case WM_SETTEXT:
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
    case WM_UNDO:
    case WM_SIZE:
    case WM_SHOWWINDOW:
    case EM_SETSEL:
    case EM_REPLACESEL:
    case EM_UNDO:
        break;
    default:
        return;
    }
    if (const HWND owner = GetParent(edit))
    {
        InvalidateRect(owner, nullptr, FALSE);
    }
}

} // namespace te::EditHalo
