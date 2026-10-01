#include <te/ui/AddressBar.h>

#include <te/ui/EditHalo.h>

#include <te/a11y/HwndAnnotation.h>
#include <te/render/FocusIndicator.h>
#include <te/render/SurfacePainter.h>
#include <te/render/TextHalo.h>

#include <commctrl.h>
#include <shlwapi.h>

#include <wil/com.h>
#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace te
{

namespace
{

// Past MAX_PATH the Shell reports a folder's parsing path in 8.3 form
// ("...\SEGMEN~1\SEGMEN~1"); the address shows the long names instead (V-3h).
std::wstring LongPathForDisplay(const std::wstring& path)
{
    if (path.find(L'~') == std::wstring::npos)
    {
        return path;
    }
    constexpr std::wstring_view kPrefix = LR"(\\?\)";
    const bool unc = path.starts_with(LR"(\\)");
    const std::wstring prefixed = unc ? LR"(\\?\UNC\)" + path.substr(2) : std::wstring(kPrefix) + path;
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetLongPathNameW(prefixed.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return path;
    }
    buffer.resize(length);
    if (unc)
    {
        return LR"(\\)" + buffer.substr(8); // \\?\UNC\server -> \\server
    }
    return buffer.starts_with(kPrefix) ? buffer.substr(kPrefix.size()) : buffer;
}

D2D1_COLOR_F ToColor(Rgb rgb, float alpha = 1.0f)
{
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
}

COLORREF ToColorRef(Rgb rgb)
{
    return RGB(rgb.r, rgb.g, rgb.b);
}

bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

// Menu text: '&' would mark a mnemonic.
std::wstring MenuText(const std::wstring& name)
{
    std::wstring text;
    for (const wchar_t c : name)
    {
        text += c;
        if (c == L'&')
        {
            text += L'&';
        }
    }
    return text;
}

} // namespace

AddressBar::~AddressBar()
{
    if (m_edit && IsWindow(m_edit))
    {
        ClearEditAnnotation();
        RemoveWindowSubclass(m_edit, EditProc, 1);
        DestroyWindow(m_edit);
    }
}

void AddressBar::Attach(HWND parent, UINT dpi) noexcept
{
    m_parent = parent;
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
}

void AddressBar::SetDpi(UINT dpi)
{
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    UpdateFont();
    PositionEdit();
}

void AddressBar::SetTextFormats(const TextFormats* formats) noexcept
{
    m_formats = formats;
    LayoutCrumbs();
}

void AddressBar::SetLocationCallback(std::function<void(const ShellLocation&)> callback)
{
    m_navigateTo = std::move(callback);
}

void AddressBar::SetMenuTracker(ContextMenu::Track track)
{
    m_track = std::move(track);
}

void AddressBar::CancelLoads()
{
    m_loader.CancelAll();
    m_pendingMenu.reset();
    m_menuCrumb.reset();
}

void AddressBar::ShutdownLoader()
{
    m_loader.Shutdown();
}

void AddressBar::SetNavigateCallback(std::function<void(std::wstring_view)> callback)
{
    m_navigate = std::move(callback);
}

float AddressBar::ToPx(float dip) const noexcept
{
    return dip * static_cast<float>(m_dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

void AddressBar::Invalidate() const
{
    if (m_parent)
    {
        InvalidateRect(m_parent, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Location and editing
// ---------------------------------------------------------------------------

void AddressBar::SetLocation(const ShellLocation& location)
{
    // File-system folders show their path; virtual folders (This PC) their name.
    const bool usePath = location.IsFileSystem() && location.ParsingPath();
    m_displayText = usePath ? LongPathForDisplay(*location.ParsingPath()) : location.DisplayName();
    m_editText = m_displayText;
    m_error.clear();

    // Breadcrumbs (T091): the location and its ancestors, root first. The namespace root
    // (the Desktop) is left out, as in Explorer, unless it is the location itself.
    m_crumbs.clear();
    for (std::optional<ShellLocation> at = location; at && at->IsValid();)
    {
        std::optional<ShellLocation> parent = at->Parent();
        Crumb crumb;
        crumb.name = at->DisplayName();
        crumb.location = std::move(*at);
        m_crumbs.push_back(std::move(crumb));
        at = std::move(parent);
        if (m_crumbs.size() > 64)
        {
            break; // a guard; real paths are far shorter
        }
    }
    if (m_crumbs.size() > 1)
    {
        m_crumbs.pop_back();
    }
    std::reverse(m_crumbs.begin(), m_crumbs.end());
    m_pendingMenu.reset(); // a chevron's listing for the previous location
    m_menuCrumb.reset();
    m_hotCrumb.reset();
    m_hotChevron.reset();
    LayoutCrumbs();
    if (m_editing)
    {
        EndEdit(); // a successful navigation ends editing
    }
    Invalidate();
}

bool AddressBar::IsEditing() const
{
    return m_editing;
}

bool AddressBar::EnsureEdit()
{
    if (m_edit)
    {
        return true;
    }
    if (!m_parent)
    {
        return false;
    }
    // Layered and colour-keyed on the typical surface colour: the key colour is
    // transparent, so the Direct2D field shows through (R-02, spike (d)).
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
    AnnotateEdit();
    UpdateFont();
    // File-system and recent-URL completion, as in Explorer's address bar.
    LOG_IF_FAILED(SHAutoComplete(m_edit, SHACF_FILESYS_DIRS | SHACF_URLHISTORY));
    return true;
}

void AddressBar::SetAutomationName(std::wstring name)
{
    m_automationName = std::move(name);
    AnnotateEdit();
}

void AddressBar::AnnotateEdit()
{
    // While editing, the native EDIT is the "Address" element (T078). Dynamic Annotation
    // is the documented way to name a Win32 control: UI Automation merges these properties
    // into the EDIT's own provider, which keeps its Value and Text patterns. (An override
    // provider from the window's IRawElementProviderHwndOverride also named it, but UIA
    // then listed a nested, typeless "Address" element under it, level after level - found
    // by the accessibility audit, T083.)
    if (!m_edit)
    {
        return;
    }
    AnnotateHwnd(m_edit, m_automationName, L"AddressBar");
}

void AddressBar::ClearEditAnnotation()
{
    ClearHwndAnnotation(m_edit);
}

void AddressBar::UpdateFont()
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

void AddressBar::PositionEdit()
{
    if (!m_edit)
    {
        return;
    }
    const D2D1_RECT_F field = FieldRect();
    // Vertically centred on the text line, inset like the display text.
    const int lineHeight = static_cast<int>(std::lround(ToPx(TextFormats::kBodySize * 1.6f)));
    const int left = static_cast<int>(std::lround(ToPx(field.left + kTextInsetDip)));
    const int right = static_cast<int>(std::lround(ToPx(field.right - kTextInsetDip)));
    const int middle = static_cast<int>(std::lround(ToPx((field.top + field.bottom) / 2.0f)));
    SetWindowPos(m_edit, HWND_TOP, left, middle - lineHeight / 2, std::max(0, right - left), lineHeight,
                 SWP_NOACTIVATE);
}

void AddressBar::BeginEdit()
{
    if (!EnsureEdit())
    {
        return;
    }
    m_editing = true;
    m_error.clear();
    SetWindowTextW(m_edit, m_editText.c_str());
    PositionEdit();
    ShowWindow(m_edit, SW_SHOW);
    SetFocus(m_edit);
    SendMessageW(m_edit, EM_SETSEL, 0, -1); // select all
    Invalidate();
}

void AddressBar::EndEdit()
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

void AddressBar::CancelEdit()
{
    if (!m_editing)
    {
        return;
    }
    if (m_edit)
    {
        SetWindowTextW(m_edit, m_editText.c_str()); // restore the path
    }
    EndEdit();
}

void AddressBar::Commit()
{
    const int length = GetWindowTextLengthW(m_edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(m_edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));

    if (text == m_editText || !m_navigate)
    {
        CancelEdit(); // unchanged: nothing to do (also covers "This PC", which does not parse)
        return;
    }
    // The owner parses and navigates: on success it calls SetLocation (ends editing), on
    // failure ShowError (editing stays open so the path can be fixed).
    m_navigate(text);
}

void AddressBar::ShowError(std::wstring_view message)
{
    m_error.assign(message);
    Invalidate();
}

LRESULT CALLBACK AddressBar::EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                      DWORD_PTR data)
{
    auto* self = reinterpret_cast<AddressBar*>(data);
    switch (msg)
    {
    case WM_GETDLGCODE:
        // Enter and Escape belong to the edit, not to a dialog manager.
        return DefSubclassProc(hwnd, msg, wParam, lParam) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        {
            self->Commit();
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            self->CancelEdit();
            return 0;
        }
        if (wParam == VK_F6 && self->m_parent)
        {
            // F6 / Shift+F6 move on in the focus ring (UI §4, T080); the edit has the
            // keyboard, so the window would not see the key otherwise.
            SendMessageW(self->m_parent, WM_KEYDOWN, wParam, lParam);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE)
        {
            return 0; // no beep
        }
        break;
    case WM_NCDESTROY:
        // Destroyed with the window: annotations must be cleared before the handle goes.
        self->ClearEditAnnotation();
        self->m_edit = nullptr;
        RemoveWindowSubclass(hwnd, EditProc, 1);
        break;
    case WM_KILLFOCUS:
        // Clicking elsewhere leaves editing and restores the path, as in Explorer.
        if (!self->m_ending && self->m_editing)
        {
            const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            self->CancelEdit();
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

// ---------------------------------------------------------------------------
// Appearance and input
// ---------------------------------------------------------------------------

void AddressBar::SetFocusVisible(bool visible)
{
    if (visible != m_focusVisible)
    {
        m_focusVisible = visible;
        Invalidate();
    }
}

void AddressBar::ApplyAppearance(const EffectiveAppearance& effective)
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

bool AddressBar::HandleCtlColor(HWND control, HDC dc, LRESULT* result)
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

bool AddressBar::OnPointerDown(D2D1_POINT_2F point)
{
    const D2D1_RECT_F field = FieldRect();
    if (point.x < field.left || point.x >= field.right || point.y < field.top || point.y >= field.bottom)
    {
        return false;
    }
    if (m_editing)
    {
        return true;
    }
    // Breadcrumbs (T091): the overflow button, a segment, a chevron; else start editing.
    if (m_overflow && Contains(*m_overflow, point))
    {
        std::vector<ShellLocation> hidden; // nearest first, as Explorer lists them
        for (auto it = m_crumbs.rbegin(); it != m_crumbs.rend(); ++it)
        {
            if (!it->visible)
            {
                hidden.push_back(it->location);
            }
        }
        ShowLocationMenu(hidden, *m_overflow);
        return true;
    }
    for (std::size_t i = 0; i < m_crumbs.size(); ++i)
    {
        const Crumb& crumb = m_crumbs[i];
        if (!crumb.visible)
        {
            continue;
        }
        if (Contains(crumb.nameRect, point))
        {
            if (m_navigateTo)
            {
                m_navigateTo(crumb.location);
            }
            return true;
        }
        if (Contains(crumb.chevronRect, point))
        {
            // Its subfolders are listed off the UI thread; the menu opens when they arrive.
            m_pendingMenu = kNodeTag | ++m_nextRequest;
            m_menuCrumb = i;
            m_loader.Request(m_parent, *m_pendingMenu, crumb.location,
                             static_cast<int>(std::lround(ToPx(16.0f))));
            Invalidate();
            return true;
        }
    }
    BeginEdit(); // empty space
    return true;
}

void AddressBar::OnPointerMove(D2D1_POINT_2F point)
{
    std::optional<std::size_t> hotCrumb;
    std::optional<std::size_t> hotChevron;
    for (std::size_t i = 0; i < m_crumbs.size() && !m_editing; ++i)
    {
        if (!m_crumbs[i].visible)
        {
            continue;
        }
        if (Contains(m_crumbs[i].nameRect, point))
        {
            hotCrumb = i;
        }
        else if (Contains(m_crumbs[i].chevronRect, point))
        {
            hotChevron = i;
        }
    }
    const bool hotOverflow = !m_editing && m_overflow && Contains(*m_overflow, point);
    if (hotCrumb != m_hotCrumb || hotChevron != m_hotChevron || hotOverflow != m_hotOverflow)
    {
        m_hotCrumb = hotCrumb;
        m_hotChevron = hotChevron;
        m_hotOverflow = hotOverflow;
        Invalidate();
    }
}

void AddressBar::OnPointerLeave()
{
    if (m_hotCrumb || m_hotChevron || m_hotOverflow)
    {
        m_hotCrumb.reset();
        m_hotChevron.reset();
        m_hotOverflow = false;
        Invalidate();
    }
}

void AddressBar::OnChildren(FolderChildren& result)
{
    if (!m_pendingMenu || result.nodeId != *m_pendingMenu)
    {
        return; // superseded: another chevron, another location, or editing started
    }
    m_pendingMenu.reset();
    D2D1_RECT_F anchor = FieldRect();
    if (m_menuCrumb && *m_menuCrumb < m_crumbs.size())
    {
        anchor = m_crumbs[*m_menuCrumb].chevronRect;
    }
    std::vector<ShellLocation> folders;
    folders.reserve(result.children.size());
    for (FolderChildren::Child& child : result.children)
    {
        folders.push_back(std::move(child.location));
    }
    ShowLocationMenu(folders, anchor); // the chevron points down while its menu is open
    m_menuCrumb.reset();
    Invalidate();
}

void AddressBar::ShowLocationMenu(const std::vector<ShellLocation>& items, const D2D1_RECT_F& anchor)
{
    if (items.empty() || !m_parent)
    {
        return;
    }
    wil::unique_hmenu menu(CreatePopupMenu());
    if (!menu)
    {
        return;
    }
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        AppendMenuW(menu.get(), MF_STRING, static_cast<UINT_PTR>(i + 1),
                    MenuText(items[i].DisplayName()).c_str());
    }
    POINT at{static_cast<LONG>(std::lround(ToPx(anchor.left))),
             static_cast<LONG>(std::lround(ToPx(anchor.bottom)))};
    ClientToScreen(m_parent, &at);
    const UINT chosen =
        m_track ? m_track(menu.get(), m_parent, at)
                : static_cast<UINT>(TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                                     at.x, at.y, m_parent, nullptr));
    if (chosen >= 1 && chosen <= items.size() && m_navigateTo)
    {
        m_navigateTo(items[chosen - 1]);
    }
}

float AddressBar::MeasureText(const std::wstring& text) const
{
    IDWriteFactory3* factory = m_formats ? m_formats->Factory() : nullptr;
    IDWriteTextFormat* format = m_formats ? m_formats->Body() : nullptr;
    wil::com_ptr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics{};
    if (!factory || !format ||
        FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 100000.0f,
                                         FieldHeight(), &layout)) ||
        FAILED(layout->GetMetrics(&metrics)))
    {
        return 0.0f;
    }
    return std::ceil(metrics.widthIncludingTrailingWhitespace);
}

void AddressBar::LayoutCrumbs()
{
    m_overflow.reset();
    for (Crumb& crumb : m_crumbs)
    {
        crumb.visible = false;
        crumb.nameRect = {};
        crumb.chevronRect = {};
    }
    if (m_crumbs.empty() || !m_formats || !m_formats->Factory() || !m_formats->Body())
    {
        return; // no breadcrumbs: the plain path is shown
    }
    const D2D1_RECT_F field = FieldRect();
    // Some empty space stays at the right end: a click there starts editing.
    constexpr float kEditSpaceDip = 32.0f;
    const float left = field.left + kTextInsetDip / 2.0f;
    const float right = field.right - kEditSpaceDip;
    if (right <= left)
    {
        return;
    }
    std::vector<float> widths;
    for (const Crumb& crumb : m_crumbs)
    {
        widths.push_back(MeasureText(crumb.name) + 2.0f * kCrumbPaddingDip + kChevronWidthDip);
    }
    // Hide leading segments until the rest (and the overflow button) fit; the last one
    // always shows, cut with an ellipsis if it alone is too wide.
    std::size_t first = 0;
    const auto needed = [&](std::size_t from) {
        float total = from > 0 ? kOverflowWidthDip : 0.0f;
        for (std::size_t i = from; i < widths.size(); ++i)
        {
            total += widths[i];
        }
        return total;
    };
    while (first + 1 < m_crumbs.size() && needed(first) > right - left)
    {
        ++first;
    }
    const float top = field.top + 2.0f;
    const float bottom = field.bottom - 2.0f;
    float x = left;
    if (first > 0)
    {
        m_overflow = D2D1::RectF(x, top, x + kOverflowWidthDip, bottom);
        x += kOverflowWidthDip;
    }
    for (std::size_t i = first; i < m_crumbs.size(); ++i)
    {
        Crumb& crumb = m_crumbs[i];
        const float nameRight = std::min(x + widths[i] - kChevronWidthDip, right - kChevronWidthDip);
        crumb.nameRect = D2D1::RectF(x, top, std::max(x, nameRight), bottom);
        crumb.chevronRect =
            D2D1::RectF(crumb.nameRect.right, top, crumb.nameRect.right + kChevronWidthDip, bottom);
        crumb.visible = true;
        x = crumb.chevronRect.right;
    }
}

void AddressBar::DrawChevron(ID2D1DeviceContext* dc, ID2D1Brush* brush, const D2D1_RECT_F& rect, bool down,
                             const EffectiveAppearance& effective) const
{
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    constexpr float kHalf = 3.5f;
    const TextHalo::Line downLines[] = {
        {D2D1::Point2F(cx - kHalf, cy - kHalf / 2.0f), D2D1::Point2F(cx, cy + kHalf / 2.0f)},
        {D2D1::Point2F(cx, cy + kHalf / 2.0f), D2D1::Point2F(cx + kHalf, cy - kHalf / 2.0f)},
    };
    const TextHalo::Line rightLines[] = {
        {D2D1::Point2F(cx - kHalf / 2.0f, cy - kHalf), D2D1::Point2F(cx + kHalf / 2.0f, cy)},
        {D2D1::Point2F(cx + kHalf / 2.0f, cy), D2D1::Point2F(cx - kHalf / 2.0f, cy + kHalf)},
    };
    TextHalo::DrawLines(dc, down ? std::span<const TextHalo::Line>(downLines) : rightLines, brush, 1.2f,
                        effective);
}

void AddressBar::SetBounds(const D2D1_RECT_F& bounds)
{
    m_bounds = bounds;
    PositionEdit();
    LayoutCrumbs();
}

D2D1_RECT_F AddressBar::FieldRect() const noexcept
{
    const float middle = (m_bounds.top + m_bounds.bottom) / 2.0f;
    const float half = std::min(FieldHeight(), m_bounds.bottom - m_bounds.top) / 2.0f;
    return D2D1::RectF(m_bounds.left, middle - half, m_bounds.right, middle + half);
}

float AddressBar::FieldHeight() const noexcept
{
    return kFieldHeightDip * (m_formats ? m_formats->TextScale() : 1.0f);
}

void AddressBar::OnTextScaleChanged()
{
    UpdateFont();
    PositionEdit();
    LayoutCrumbs();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void AddressBar::Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    ApplyAppearance(effective);
    IDWriteTextFormat* format = m_formats ? m_formats->Body() : nullptr;
    const D2D1_RECT_F field = FieldRect();
    if (!dc || !format || field.right <= field.left)
    {
        return;
    }

    // The field keeps its legibility floor while editing too, so the text behind the
    // transparent edit background keeps its contrast (R-05).
    SurfacePainter::PaintTextScrim(dc, field, effective);

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
    // While editing (the keyboard is in the field) and focus cues show: a 2-DIP focus ring,
    // 3:1 against the field (T080).
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
        EditHalo::Render(dc, m_edit, m_parent, m_dpi, effective); // Transparent only
    }

    const bool crumbs =
        std::any_of(m_crumbs.begin(), m_crumbs.end(), [](const Crumb& crumb) { return crumb.visible; });
    if (!m_editing && crumbs)
    {
        // Breadcrumbs (T091): hover fill, name, chevron (down while its menu is open or
        // loading), and the overflow button in front of hidden segments.
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const auto hover = [&](const D2D1_RECT_F& rect) {
            brush->SetColor(ToColor(effective.text, 0.08f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(rect, kCornerRadiusDip, kCornerRadiusDip),
                                     brush.get());
        };
        if (m_overflow)
        {
            if (m_hotOverflow)
            {
                hover(*m_overflow);
            }
            brush->SetColor(ToColor(effective.secondaryText));
            const D2D1_RECT_F& o = *m_overflow;
            const float mid = (o.left + o.right) / 2.0f;
            // Two chevrons pointing left: "«". One halo pass for both, so neither one's
            // outline covers the other.
            const float cy = (o.top + o.bottom) / 2.0f;
            std::vector<TextHalo::Line> lines;
            for (const float dx : {-2.5f, 2.5f})
            {
                lines.push_back(
                    {D2D1::Point2F(mid + dx + 1.75f, cy - 3.5f), D2D1::Point2F(mid + dx - 1.75f, cy)});
                lines.push_back(
                    {D2D1::Point2F(mid + dx - 1.75f, cy), D2D1::Point2F(mid + dx + 1.75f, cy + 3.5f)});
            }
            TextHalo::DrawLines(dc, lines, brush.get(), 1.2f, effective);
        }
        for (std::size_t i = 0; i < m_crumbs.size(); ++i)
        {
            const Crumb& crumb = m_crumbs[i];
            if (!crumb.visible)
            {
                continue;
            }
            if (m_hotCrumb == i)
            {
                hover(crumb.nameRect);
            }
            if (m_hotChevron == i || m_menuCrumb == i)
            {
                hover(crumb.chevronRect);
            }
            brush->SetColor(ToColor(effective.text));
            TextHalo::DrawTextW(dc, crumb.name.c_str(), static_cast<UINT32>(crumb.name.size()), format,
                                crumb.nameRect, brush.get(),
                                D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT,
                                effective);
            brush->SetColor(ToColor(effective.secondaryText));
            DrawChevron(dc, brush.get(), crumb.chevronRect, m_menuCrumb == i, effective);
        }
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    else if (!m_editing && !m_displayText.empty())
    {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        brush->SetColor(ToColor(effective.text));
        TextHalo::DrawTextW(
            dc, m_displayText.c_str(), static_cast<UINT32>(m_displayText.size()), format,
            D2D1::RectF(field.left + kTextInsetDip, field.top, field.right - kTextInsetDip, field.bottom),
            brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, effective);
    }
}

void AddressBar::RenderOverlay(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    IDWriteTextFormat* format = m_formats ? m_formats->Body() : nullptr;
    if (!dc || !format || m_error.empty())
    {
        return;
    }
    const D2D1_RECT_F field = FieldRect();
    // An opaque callout under the field, in the base colour with a border, so the
    // message reads over anything below it (UI §6).
    const D2D1_RECT_F box = D2D1::RectF(field.left, field.bottom + kErrorGapDip, field.right,
                                        field.bottom + kErrorGapDip + FieldHeight());
    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(dc->CreateSolidColorBrush(ToColor(effective.base), &brush)))
    {
        return;
    }
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(box, kCornerRadiusDip, kCornerRadiusDip);
    dc->FillRoundedRectangle(rounded, brush.get());
    brush->SetColor(ToColor(effective.focus));
    dc->DrawRoundedRectangle(rounded, brush.get(), 1.0f);
    brush->SetColor(ToColor(effective.text));
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextW(m_error.c_str(), static_cast<UINT32>(m_error.size()), format,
                  D2D1::RectF(box.left + kTextInsetDip, box.top, box.right - kTextInsetDip, box.bottom),
                  brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

IRawElementProviderFragment* AddressBar::Automation()
{
    return nullptr; // T078
}

} // namespace te
