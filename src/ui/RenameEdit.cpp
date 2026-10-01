#include <te/ui/RenameEdit.h>

#include <te/ui/EditHalo.h>

#include <te/a11y/HwndAnnotation.h>
#include <te/shell/FileOperationService.h>

#include <commctrl.h>

#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace te
{

namespace
{

COLORREF ToColorRef(Rgb rgb)
{
    return RGB(rgb.r, rgb.g, rgb.b);
}

} // namespace

RenameEdit::~RenameEdit()
{
    if (m_edit && IsWindow(m_edit))
    {
        ClearHwndAnnotation(m_edit);
        RemoveWindowSubclass(m_edit, EditProc, 1);
        DestroyWindow(m_edit);
    }
}

void RenameEdit::Attach(HWND parent, UINT dpi) noexcept
{
    m_parent = parent;
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
}

void RenameEdit::SetDpi(UINT dpi)
{
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    UpdateFont();
}

void RenameEdit::SetAutomationName(std::wstring name)
{
    m_automationName = std::move(name);
    AnnotateHwnd(m_edit, m_automationName, L"RenameEdit");
}

void RenameEdit::SetTextFormats(const TextFormats* formats) noexcept
{
    m_formats = formats;
}

void RenameEdit::SetBadNameText(std::wstring text)
{
    m_badNameText = std::move(text);
}

float RenameEdit::ToPx(float dip) const noexcept
{
    return dip * static_cast<float>(m_dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

bool RenameEdit::EnsureEdit()
{
    if (m_edit)
    {
        return true;
    }
    if (!m_parent)
    {
        return false;
    }
    // Layered and colour-keyed, like the address edit (R-02, spike (d)).
    m_edit =
        CreateWindowExW(WS_EX_LAYERED, WC_EDITW, L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0,
                        m_parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditControlId)),
                        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_parent, GWLP_HINSTANCE)), nullptr);
    if (!m_edit)
    {
        LOG_LAST_ERROR();
        return false;
    }
    SetLayeredWindowAttributes(m_edit, m_key, 0, LWA_COLORKEY);
    SetWindowSubclass(m_edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    // Without a name, screen readers announce only "edit" (found by the audit, T083).
    AnnotateHwnd(m_edit, m_automationName, L"RenameEdit");
    UpdateFont();
    return true;
}

void RenameEdit::UpdateFont()
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

bool RenameEdit::Begin(const std::wstring& name, bool isFolder, const D2D1_RECT_F& cellDip,
                       Callbacks callbacks)
{
    if (m_active)
    {
        Cancel();
    }
    if (!EnsureEdit())
    {
        return false;
    }
    m_original = name;
    m_callbacks = std::move(callbacks);
    m_rejected = false;
    m_active = true;
    SetWindowTextW(m_edit, name.c_str());
    Move(cellDip);
    ShowWindow(m_edit, SW_SHOW);
    SetFocus(m_edit);
    // Files: the name without its extension, as Explorer selects it. A leading dot
    // (".gitignore") is part of the name, not an extension.
    const std::size_t dot = name.find_last_of(L'.');
    const int end = (!isFolder && dot != std::wstring::npos && dot > 0) ? static_cast<int>(dot) : -1;
    SendMessageW(m_edit, EM_SETSEL, 0, end);
    return true;
}

void RenameEdit::Move(const D2D1_RECT_F& cellDip)
{
    if (!m_edit || !m_active)
    {
        return;
    }
    const int left = static_cast<int>(std::lround(ToPx(cellDip.left)));
    const int top = static_cast<int>(std::lround(ToPx(cellDip.top)));
    const int right = static_cast<int>(std::lround(ToPx(cellDip.right)));
    const int bottom = static_cast<int>(std::lround(ToPx(cellDip.bottom)));
    SetWindowPos(m_edit, HWND_TOP, left, top, std::max(0, right - left), std::max(0, bottom - top),
                 SWP_NOACTIVATE);
}

std::wstring RenameEdit::Text() const
{
    if (!m_edit)
    {
        return {};
    }
    const int length = GetWindowTextLengthW(m_edit);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(m_edit, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void RenameEdit::Commit(bool fromEnter)
{
    const std::wstring text = Text();
    if (!FileOperationService::ValidateNewName(text))
    {
        if (!fromEnter)
        {
            Cancel(); // focus left with an invalid name: keep the old one
            return;
        }
        m_rejected = true;
        EDITBALLOONTIP tip{sizeof(tip)};
        tip.pszTitle = L"";
        tip.pszText = m_badNameText.c_str();
        tip.ttiIcon = TTI_NONE;
        SendMessageW(m_edit, EM_SHOWBALLOONTIP, 0, reinterpret_cast<LPARAM>(&tip));
        return; // stay in edit mode (V-4c)
    }
    if (text == m_original)
    {
        Cancel();
        return;
    }
    // The list keeps the old name until the rename succeeds (US4-3): the owner submits
    // the request and calls FileView::ApplyRename on success.
    auto commit = m_callbacks.commit;
    End();
    if (commit)
    {
        commit(text);
    }
}

void RenameEdit::Cancel()
{
    if (m_active)
    {
        End();
    }
}

void RenameEdit::End()
{
    m_ending = true;
    m_active = false;
    if (m_edit)
    {
        SendMessageW(m_edit, EM_HIDEBALLOONTIP, 0, 0);
        const bool hadFocus = GetFocus() == m_edit;
        ShowWindow(m_edit, SW_HIDE);
        if (hadFocus && m_parent)
        {
            SetFocus(m_parent);
        }
    }
    m_ending = false;
    auto ended = std::move(m_callbacks.ended);
    m_callbacks = {};
    if (ended)
    {
        ended();
    }
}

LRESULT CALLBACK RenameEdit::EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                      DWORD_PTR data)
{
    auto* self = reinterpret_cast<RenameEdit*>(data);
    switch (msg)
    {
    case WM_NCDESTROY:
        // Destroyed with the window: annotations must be cleared before the handle goes.
        ClearHwndAnnotation(hwnd);
        self->m_edit = nullptr;
        RemoveWindowSubclass(hwnd, EditProc, 1);
        break;
    case WM_GETDLGCODE:
        return DefSubclassProc(hwnd, msg, wParam, lParam) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        {
            self->Commit(true);
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            self->Cancel();
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
        // Clicking elsewhere commits a valid name, as in Explorer.
        if (!self->m_ending && self->m_active)
        {
            const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            self->Commit(false);
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

void RenameEdit::ApplyAppearance(const EffectiveAppearance& effective)
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

bool RenameEdit::HandleCtlColor(HWND control, HDC dc, LRESULT* result)
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

void RenameEdit::Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective) const
{
    if (m_active)
    {
        EditHalo::Render(dc, m_edit, m_parent, m_dpi, effective);
    }
}

} // namespace te
