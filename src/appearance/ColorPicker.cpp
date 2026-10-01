#include <te/appearance/ColorPicker.h>

#include <te/appearance/AppearanceIds.h>
#include <te/appearance/Contrast.h>
#include <te/appearance/Palette.h>
#include <te/settings/SettingsManager.h>

#include <commctrl.h>
#include <commdlg.h>

#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <variant>

namespace te
{

namespace
{

constexpr int kSwatchColumns = 4;
constexpr int kSwatchRows = 3;
constexpr int kSurfaceSteps = 18; // 0–90% in 5% steps
constexpr int kTintSteps = 16;    // 0–80% in 5% steps

// Posted to the popup after its own WM_DPICHANGED, once the dialog manager has rescaled it,
// to place it against the picker button again (T081).
constexpr UINT kRepositionMessage = WM_APP + 1;

COLORREF ToColorRef(Rgb c)
{
    return RGB(c.r, c.g, c.b);
}

Rgb FromColorRef(COLORREF c)
{
    return {GetRValue(c), GetGValue(c), GetBValue(c)};
}

int SwatchIndex(int id)
{
    const int index = id - IDC_SWATCH_0;
    return index >= 0 && index < static_cast<int>(kPresetPalette.size()) ? index : -1;
}

int ToSteps(double value)
{
    return static_cast<int>(std::lround(value / kOpacityStep));
}

void FillSolid(HDC dc, const RECT& rect, COLORREF color)
{
    const wil::unique_hbrush brush(CreateSolidBrush(color));
    FillRect(dc, &rect, brush.get());
}

void FrameSolid(HDC dc, RECT rect, COLORREF color, int thickness)
{
    for (int i = 0; i < thickness; ++i)
    {
        const wil::unique_hbrush brush(CreateSolidBrush(color));
        FrameRect(dc, &rect, brush.get());
        InflateRect(&rect, -1, -1);
    }
}

// The usual "transparent" pattern: `cell`-sized squares alternating between two colors.
void FillCheckerboard(HDC dc, const RECT& rect, COLORREF light, COLORREF dark, int cell)
{
    FillSolid(dc, rect, light);
    const wil::unique_hbrush brush(CreateSolidBrush(dark));
    for (LONG y = rect.top; y < rect.bottom; y += cell)
    {
        for (LONG x = rect.left + ((y - rect.top) / cell % 2) * cell; x < rect.right; x += 2 * cell)
        {
            const RECT square{x, y, std::min<LONG>(x + cell, rect.right),
                              std::min<LONG>(y + cell, rect.bottom)};
            FillRect(dc, &square, brush.get());
        }
    }
}

void DrawCheckMark(HDC dc, const RECT& fill, COLORREF ink, int unit)
{
    const wil::unique_hpen pen(CreatePen(PS_SOLID, 2 * unit, ink));
    const auto old = SelectObject(dc, pen.get());
    const int cx = static_cast<int>(fill.left + fill.right) / 2;
    const int cy = static_cast<int>(fill.top + fill.bottom) / 2;
    const int s = std::max(3 * unit, static_cast<int>(fill.bottom - fill.top) / 4);
    const POINT check[] = {{cx - s, cy}, {cx - s / 3, cy + s * 2 / 3}, {cx + s, cy - s * 2 / 3}};
    Polyline(dc, check, static_cast<int>(std::size(check)));
    SelectObject(dc, old);
}

constexpr Rgb kCheckerLight{0xFF, 0xFF, 0xFF};
constexpr Rgb kCheckerDark{0xCC, 0xCC, 0xCC};

} // namespace

ColorPicker::ColorPicker(HINSTANCE instance, std::function<void(HWND)> registerDialog)
    : m_instance(instance), m_registerDialog(std::move(registerDialog))
{
    // Until SetCapabilities: assume every mode works.
    m_capabilities.systemBackdropSupported = true;
}

ColorPicker::~ColorPicker()
{
    if (m_dialog && IsWindow(m_dialog))
    {
        if (IsWindowVisible(m_dialog) && m_registerDialog)
        {
            m_registerDialog(nullptr);
        }
        DestroyWindow(m_dialog);
    }
}

void ColorPicker::SetChangedCallback(std::function<void(const AppearanceSettings&)> callback)
{
    m_changed = std::move(callback);
}

void ColorPicker::SetOpenChangedCallback(std::function<void(bool open)> callback)
{
    m_openChanged = std::move(callback);
}

void ColorPicker::SetCapabilities(const RenderingCapabilities& capabilities)
{
    m_capabilities = capabilities;
    if (m_dialog)
    {
        SyncControls();
    }
}

void ColorPicker::Update(const AppearanceSettings& current, const EffectiveAppearance& effective)
{
    m_settings = current;
    m_effective = effective;
    if (m_dialog)
    {
        SyncControls();
    }
}

bool ColorPicker::EnsureDialog(HWND owner)
{
    if (m_dialog && IsWindow(m_dialog))
    {
        return true;
    }
    m_owner = owner;
    m_dialog = CreateDialogParamW(m_instance, MAKEINTRESOURCEW(IDD_APPEARANCE), owner, DialogProc,
                                  reinterpret_cast<LPARAM>(this));
    LOG_LAST_ERROR_IF_NULL(m_dialog);
    return m_dialog != nullptr;
}

void ColorPicker::Show(HWND owner, const RECT& anchorScreen, const AppearanceSettings& current,
                       const EffectiveAppearance& effective)
{
    m_settings = current;
    m_effective = effective;
    if (!EnsureDialog(owner))
    {
        return;
    }
    SyncControls();
    m_anchor = anchorScreen;
    Position(owner, anchorScreen);
    m_dismissedAt = 0;
    ShowWindow(m_dialog, SW_SHOW);
    SetForegroundWindow(m_dialog);

    // Start on the selected mode, the first stop in the tab order.
    const int modeId = m_settings.backdropMode == BackdropMode::Transparent
                           ? IDC_SWATCH_TRANSPARENT
                           : IDC_MODE_ACRYLIC + static_cast<int>(m_settings.backdropMode);
    HWND first = GetDlgItem(m_dialog, modeId);
    if (!IsWindowEnabled(first))
    {
        first = GetNextDlgTabItem(m_dialog, nullptr, FALSE);
    }
    SendMessageW(m_dialog, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(first), TRUE);

    if (m_registerDialog)
    {
        m_registerDialog(m_dialog);
    }
    if (m_openChanged)
    {
        m_openChanged(true);
    }
}

void ColorPicker::Hide()
{
    if (!m_dialog || !IsWindowVisible(m_dialog))
    {
        return;
    }
    const bool wasActive = GetActiveWindow() == m_dialog;
    ShowWindow(m_dialog, SW_HIDE); // kept alive for the next Show (R-10)
    if (m_registerDialog)
    {
        m_registerDialog(nullptr);
    }
    if (m_openChanged)
    {
        m_openChanged(false);
    }
    if (wasActive && m_owner)
    {
        SetActiveWindow(m_owner);
    }
}

bool ColorPicker::IsOpen() const
{
    return m_dialog && IsWindowVisible(m_dialog);
}

bool ColorPicker::ShouldSwallowClick(UINT message)
{
    if (message == WM_LBUTTONDOWN)
    {
        if (m_dismissedAt != 0 && GetTickCount() - m_dismissedAt <= kSwallowWindowMs)
        {
            m_dismissedAt = 0;
            m_swallowing = true;
            return true;
        }
        m_dismissedAt = 0;
        return false;
    }
    if (message == WM_LBUTTONUP && m_swallowing)
    {
        m_swallowing = false;
        return true;
    }
    return false;
}

void ColorPicker::Reposition(const RECT& anchorScreen)
{
    m_anchor = anchorScreen;
    if (IsOpen())
    {
        Position(m_owner, m_anchor);
    }
}

void ColorPicker::Position(HWND owner, const RECT& anchorScreen)
{
    RECT dialog{};
    GetWindowRect(m_dialog, &dialog);
    const int width = dialog.right - dialog.left;
    const int height = dialog.bottom - dialog.top;

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;

    // Right-aligned under the picker button (it sits next to the caption buttons); above
    // it if there is no room below; always inside the work area.
    int x = anchorScreen.right - width;
    int y = anchorScreen.bottom;
    if (y + height > work.bottom)
    {
        y = anchorScreen.top - height;
    }
    x = std::clamp(x, static_cast<int>(work.left), std::max<int>(work.left, work.right - width));
    y = std::clamp(y, static_cast<int>(work.top), std::max<int>(work.top, work.bottom - height));
    SetWindowPos(m_dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

std::wstring ColorPicker::LoadText(UINT id, const wchar_t* fallback) const
{
    const wchar_t* text = nullptr;
    const int length = LoadStringW(m_instance, id, reinterpret_cast<LPWSTR>(&text), 0);
    return length > 0 ? std::wstring(text, static_cast<size_t>(length)) : std::wstring(fallback);
}

Rgb ColorPicker::TintRgb() const
{
    return std::holds_alternative<Rgb>(m_settings.tintColor) ? std::get<Rgb>(m_settings.tintColor)
                                                             : m_capabilities.accent;
}

void ColorPicker::SyncControls()
{
    m_syncing = true;

    // Mode: the requested mode stays checked even if it cannot be applied right now.
    // Transparent has no radio; its swatch shows the check instead.
    CheckModeRadios();
    std::wstring modeReason;
    if (m_capabilities.highContrast)
    {
        modeReason = LoadText(IDS_REASON_HIGH_CONTRAST, L"High contrast is on");
    }
    else if (!m_capabilities.transparencyEffectsEnabled)
    {
        modeReason =
            LoadText(IDS_REASON_TRANSPARENCY_OFF, L"Transparency effects are turned off in Windows settings");
    }
    else if (!m_capabilities.systemBackdropSupported)
    {
        modeReason = LoadText(IDS_REASON_REQUIRES_22621, L"Requires Windows 11 build 22621 or later");
    }
    const bool translucentAvailable = modeReason.empty();
    EnableWindow(GetDlgItem(m_dialog, IDC_MODE_ACRYLIC), translucentAvailable);
    EnableWindow(GetDlgItem(m_dialog, IDC_MODE_MICA), translucentAvailable);
    EnableWindow(GetDlgItem(m_dialog, IDC_SWATCH_TRANSPARENT), translucentAvailable);
    SetDlgItemTextW(m_dialog, IDC_MODE_REASON, modeReason.c_str());

    // Opacity: two independent trackbars (Principle VII), off when the effective mode is
    // Solid or high contrast (FR-022).
    SendDlgItemMessageW(m_dialog, IDC_SURFACE, TBM_SETPOS, TRUE, ToSteps(m_settings.surfaceOpacity));
    SendDlgItemMessageW(m_dialog, IDC_TINT, TBM_SETPOS, TRUE, ToSteps(m_settings.tintOpacity));
    const bool opacity = m_effective.opacityControlEnabled;
    EnableWindow(GetDlgItem(m_dialog, IDC_SURFACE), opacity);
    EnableWindow(GetDlgItem(m_dialog, IDC_TINT), opacity);
    EnableWindow(GetDlgItem(m_dialog, IDC_SURFACE_LABEL), opacity);
    EnableWindow(GetDlgItem(m_dialog, IDC_TINT_LABEL), opacity);
    SetDlgItemTextW(m_dialog, IDC_OPACITY_REASON,
                    opacity ? L"" : LoadText(IDS_REASON_SOLID_OPAQUE, L"Solid mode is fully opaque").c_str());

    // Swatches show the selection; the preview shows the result.
    InvalidateSwatches();

    m_syncing = false;
}

void ColorPicker::CheckModeRadios()
{
    for (const int id : {IDC_MODE_ACRYLIC, IDC_MODE_MICA, IDC_MODE_SOLID})
    {
        const bool checked = id - IDC_MODE_ACRYLIC == static_cast<int>(m_settings.backdropMode);
        CheckDlgButton(m_dialog, id, checked ? BST_CHECKED : BST_UNCHECKED);
    }
}

void ColorPicker::InvalidateSwatches() const
{
    for (size_t i = 0; i < kPresetPalette.size(); ++i)
    {
        InvalidateRect(GetDlgItem(m_dialog, IDC_SWATCH_0 + static_cast<int>(i)), nullptr, TRUE);
    }
    InvalidateRect(GetDlgItem(m_dialog, IDC_SWATCH_TRANSPARENT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(m_dialog, IDC_PREVIEW), nullptr, TRUE);
}

void ColorPicker::Changed()
{
    if (m_syncing)
    {
        return;
    }
    InvalidateSwatches();
    if (m_changed)
    {
        m_changed(m_settings);
    }
}

void ColorPicker::OnCommand(int id, int code, HWND /*control*/)
{
    if (code != BN_CLICKED)
    {
        return;
    }
    if (id >= IDC_MODE_ACRYLIC && id <= IDC_MODE_SOLID)
    {
        SetMode(static_cast<BackdropMode>(id - IDC_MODE_ACRYLIC));
        return;
    }
    if (id == IDC_SWATCH_TRANSPARENT)
    {
        SetMode(BackdropMode::Transparent);
        return;
    }
    if (const int swatch = SwatchIndex(id); swatch >= 0)
    {
        m_settings.tintColor = kPresetPalette[static_cast<size_t>(swatch)].rgb;
        RaiseTintForClearGlass();
        Changed();
        return;
    }
    switch (id)
    {
    case IDC_SWATCH_ACCENT:
        m_settings.tintColor = std::monostate{};
        RaiseTintForClearGlass();
        Changed();
        break;
    case IDC_CUSTOM:
        OnCustomColor();
        break;
    case IDC_RESET:
        m_settings = SettingsManager::Reset(m_settings);
        SyncControls();
        Changed();
        break;
    case IDOK:
        // Enter activates the focused control (there is no default push button).
        if (const HWND focus = GetFocus(); focus && IsChild(m_dialog, focus))
        {
            wchar_t className[32]{};
            GetClassNameW(focus, className, static_cast<int>(std::size(className)));
            if (_wcsicmp(className, L"Button") == 0)
            {
                SendMessageW(focus, BM_CLICK, 0, 0);
            }
        }
        break;
    case IDCANCEL:
        Hide();
        break;
    default:
        break;
    }
}

void ColorPicker::RaiseTintForClearGlass()
{
    if (m_settings.backdropMode != BackdropMode::Transparent ||
        m_settings.tintOpacity >= kTransparentTintFloor)
    {
        return;
    }
    m_settings.tintOpacity = kTransparentTintFloor;
    SendDlgItemMessageW(m_dialog, IDC_TINT, TBM_SETPOS, TRUE, ToSteps(m_settings.tintOpacity));
}

void ColorPicker::SetMode(BackdropMode mode)
{
    const bool changed = mode != m_settings.backdropMode;
    m_settings.backdropMode = mode;
    if (changed)
    {
        RaiseTintForClearGlass();
    }
    CheckModeRadios();
    if (changed)
    {
        Changed();
    }
}

void ColorPicker::OnScroll(HWND trackbar)
{
    const int id = GetDlgCtrlID(trackbar);
    const auto steps = static_cast<int>(SendMessageW(trackbar, TBM_GETPOS, 0, 0));
    const double value = steps * kOpacityStep;
    // Each trackbar writes only its own field (Principle VII).
    if (id == IDC_SURFACE)
    {
        const double snapped = SnapOpacity(value, kSurfaceMin, kSurfaceMax);
        if (ToSteps(snapped) != ToSteps(m_settings.surfaceOpacity))
        {
            m_settings.surfaceOpacity = snapped;
            Changed();
        }
    }
    else if (id == IDC_TINT)
    {
        const double snapped = SnapOpacity(value, kTintMin, kTintMax);
        if (ToSteps(snapped) != ToSteps(m_settings.tintOpacity))
        {
            m_settings.tintOpacity = snapped;
            Changed();
        }
    }
}

void ColorPicker::OnCustomColor()
{
    std::array<COLORREF, 16> custom{};
    for (size_t i = 0; i < custom.size(); ++i)
    {
        custom[i] = ToColorRef(m_settings.customColors[i]);
    }

    CHOOSECOLORW choose{sizeof(choose)};
    choose.hwndOwner = m_dialog;
    choose.rgbResult = ToColorRef(TintRgb());
    choose.lpCustColors = custom.data();
    choose.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    m_inCustomDialog = true; // its activation must not dismiss the popup
    const bool chosen = ChooseColorW(&choose) != FALSE;
    m_inCustomDialog = false;

    // Custom slots are the user's palette: keep edits even when the dialog is cancelled.
    bool changed = false;
    for (size_t i = 0; i < custom.size(); ++i)
    {
        const Rgb slot = FromColorRef(custom[i]);
        changed = changed || !(slot == m_settings.customColors[i]);
        m_settings.customColors[i] = slot;
    }
    if (chosen)
    {
        m_settings.tintColor = FromColorRef(choose.rgbResult);
        RaiseTintForClearGlass();
        changed = true;
    }
    if (changed)
    {
        Changed();
    }
}

void ColorPicker::DrawSwatch(const DRAWITEMSTRUCT& item) const
{
    const int index = SwatchIndex(static_cast<int>(item.CtlID));
    if (index < 0)
    {
        return;
    }
    const Rgb color = kPresetPalette[static_cast<size_t>(index)].rgb;
    const UINT dpi = GetDpiForWindow(item.hwndItem);
    const int unit = std::max(1, MulDiv(1, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    RECT rect = item.rcItem;

    FillSolid(item.hDC, rect, GetSysColor(COLOR_BTNFACE));
    RECT fill = rect;
    InflateRect(&fill, -3 * unit, -3 * unit);
    FillSolid(item.hDC, fill, ToColorRef(color));
    // A thin outline keeps light swatches visible on the popup background.
    FrameSolid(item.hDC, fill, GetSysColor(COLOR_WINDOWTEXT), 1);

    // Check mark on the selected swatch, in whichever of black or white reads better.
    const bool selected =
        std::holds_alternative<Rgb>(m_settings.tintColor) && std::get<Rgb>(m_settings.tintColor) == color;
    if (selected)
    {
        const COLORREF ink = Contrast::ContrastRatio(color, Contrast::kLightText) >=
                                     Contrast::ContrastRatio(color, Contrast::kDarkText)
                                 ? RGB(0xFF, 0xFF, 0xFF)
                                 : RGB(0x00, 0x00, 0x00);
        DrawCheckMark(item.hDC, fill, ink, unit);
    }

    // Focus ring (2 DIP), drawn around the fill in the system focus color.
    if (item.itemState & ODS_FOCUS)
    {
        RECT ring = rect;
        InflateRect(&ring, -unit, -unit);
        FrameSolid(item.hDC, ring, GetSysColor(COLOR_HIGHLIGHT), 2 * unit);
    }
}

void ColorPicker::DrawTransparentSwatch(const DRAWITEMSTRUCT& item) const
{
    const UINT dpi = GetDpiForWindow(item.hwndItem);
    const int unit = std::max(1, MulDiv(1, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const RECT rect = item.rcItem;
    const bool enabled = (item.itemState & ODS_DISABLED) == 0;

    FillSolid(item.hDC, rect, GetSysColor(COLOR_BTNFACE));
    RECT fill = rect;
    InflateRect(&fill, -3 * unit, -3 * unit);
    FillCheckerboard(item.hDC, fill, ToColorRef(kCheckerLight), ToColorRef(kCheckerDark), 4 * unit);
    FrameSolid(item.hDC, fill, GetSysColor(COLOR_WINDOWTEXT), 1);

    // Unlike the color swatches this one shows its caption: black on the light
    // checkerboard, gray while the mode is unavailable.
    wchar_t caption[64]{};
    GetWindowTextW(item.hwndItem, caption, static_cast<int>(std::size(caption)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, enabled ? RGB(0x00, 0x00, 0x00) : RGB(0x6E, 0x6E, 0x6E));
    RECT text = fill;
    DrawTextW(item.hDC, caption, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Check mark at the left end while Transparent is the requested mode.
    if (m_settings.backdropMode == BackdropMode::Transparent)
    {
        const LONG side = fill.bottom - fill.top;
        const RECT box{fill.left, fill.top, fill.left + side + 4 * unit, fill.bottom};
        DrawCheckMark(item.hDC, box, RGB(0x00, 0x00, 0x00), unit);
    }

    if (item.itemState & ODS_FOCUS)
    {
        RECT ring = rect;
        InflateRect(&ring, -unit, -unit);
        FrameSolid(item.hDC, ring, GetSysColor(COLOR_HIGHLIGHT), 2 * unit);
    }
}

void ColorPicker::DrawPreview(const DRAWITEMSTRUCT& item) const
{
    if (m_effective.applied == BackdropMode::Transparent)
    {
        // Clear glass: the surface and tint layers over a checkerboard standing in for
        // whatever is behind the window.
        const auto shown = [&](Rgb behind) {
            const Rgb surface =
                Contrast::Composite(behind, m_effective.base, static_cast<float>(m_settings.surfaceOpacity));
            return ToColorRef(
                Contrast::Composite(surface, TintRgb(), static_cast<float>(m_settings.tintOpacity)));
        };
        const UINT dpi = GetDpiForWindow(item.hwndItem);
        const int unit = std::max(1, MulDiv(1, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
        FillCheckerboard(item.hDC, item.rcItem, shown(kCheckerLight), shown(kCheckerDark), 6 * unit);
        FrameSolid(item.hDC, item.rcItem, GetSysColor(COLOR_WINDOWTEXT), 1);
        return;
    }

    // The tint over the current theme base, as the window's surfaces show it.
    const Rgb base = m_effective.base;
    const bool solid = m_effective.applied == BackdropMode::Solid;
    const Rgb shown =
        solid ? base : Contrast::Composite(base, TintRgb(), static_cast<float>(m_settings.tintOpacity));
    FillSolid(item.hDC, item.rcItem, ToColorRef(shown));
    FrameSolid(item.hDC, item.rcItem, GetSysColor(COLOR_WINDOWTEXT), 1);
}

INT_PTR CALLBACK ColorPicker::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ColorPicker* self = nullptr;
    if (msg == WM_INITDIALOG)
    {
        self = reinterpret_cast<ColorPicker*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        self->m_dialog = hwnd;
    }
    else
    {
        self = reinterpret_cast<ColorPicker*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    return self ? self->HandleMessage(msg, wParam, lParam) : FALSE;
}

INT_PTR ColorPicker::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        for (size_t i = 0; i < kPresetPalette.size(); ++i)
        {
            SetWindowSubclass(GetDlgItem(m_dialog, IDC_SWATCH_0 + static_cast<int>(i)), SwatchProc, 1,
                              reinterpret_cast<DWORD_PTR>(this));
        }
        SendDlgItemMessageW(m_dialog, IDC_SURFACE, TBM_SETRANGE, TRUE, MAKELPARAM(0, kSurfaceSteps));
        SendDlgItemMessageW(m_dialog, IDC_TINT, TBM_SETRANGE, TRUE, MAKELPARAM(0, kTintSteps));
        for (const int id : {IDC_SURFACE, IDC_TINT})
        {
            SendDlgItemMessageW(m_dialog, id, TBM_SETPAGESIZE, 0, 2);
            SendDlgItemMessageW(m_dialog, id, TBM_SETTICFREQ, 2, 0);
        }
        return FALSE; // Show() sets the focus

    case WM_COMMAND:
        OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
        return TRUE;

    case WM_HSCROLL:
        if (lParam)
        {
            OnScroll(reinterpret_cast<HWND>(lParam));
        }
        return TRUE;

    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID == IDC_PREVIEW)
        {
            DrawPreview(*item);
        }
        else if (item->CtlID == IDC_SWATCH_TRANSPARENT)
        {
            DrawTransparentSwatch(*item);
        }
        else
        {
            DrawSwatch(*item);
        }
        return TRUE;
    }

    case WM_ACTIVATE:
        // Clicking anywhere else dismisses the popup, except the Custom... color dialog,
        // which the popup owns (R-10).
        if (LOWORD(wParam) == WA_INACTIVE && !m_inCustomDialog && IsWindowVisible(m_dialog))
        {
            m_dismissedAt = GetTickCount();
            if (m_dismissedAt == 0)
            {
                m_dismissedAt = 1; // 0 means "no dismissal"
            }
            Hide();
        }
        return FALSE;

    case WM_DPICHANGED:
        // A per-monitor-v2 dialog is rescaled by the dialog manager after this returns
        // FALSE; its new size is known only then, so the placement follows as a post.
        PostMessageW(m_dialog, kRepositionMessage, 0, 0);
        return FALSE;

    case kRepositionMessage:
        if (IsOpen())
        {
            Position(m_owner, m_anchor);
        }
        return TRUE;

    case WM_DESTROY:
        // Destroyed with its owner while open: the message loop must forget it too.
        if (IsWindowVisible(m_dialog) && m_registerDialog)
        {
            m_registerDialog(nullptr);
        }
        for (size_t i = 0; i < kPresetPalette.size(); ++i)
        {
            RemoveWindowSubclass(GetDlgItem(m_dialog, IDC_SWATCH_0 + static_cast<int>(i)), SwatchProc, 1);
        }
        return FALSE;

    case WM_NCDESTROY:
        m_dialog = nullptr;
        return FALSE;

    default:
        return FALSE;
    }
}

// Arrow keys move within the 4 x 3 swatch grid; Tab leaves it (UI contract §3).
LRESULT CALLBACK ColorPicker::SwatchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                         DWORD_PTR data)
{
    auto* self = reinterpret_cast<ColorPicker*>(data);
    if (msg == WM_GETDLGCODE)
    {
        return DefSubclassProc(hwnd, msg, wParam, lParam) | DLGC_WANTARROWS;
    }
    if (msg == WM_KEYDOWN)
    {
        const int index = SwatchIndex(GetDlgCtrlID(hwnd));
        const int column = index % kSwatchColumns;
        const int row = index / kSwatchColumns;
        int target = -1;
        switch (wParam)
        {
        case VK_LEFT:
            target = column > 0 ? index - 1 : index;
            break;
        case VK_RIGHT:
            target = column < kSwatchColumns - 1 ? index + 1 : index;
            break;
        case VK_UP:
            target = row > 0 ? index - kSwatchColumns : index;
            break;
        case VK_DOWN:
            target = row < kSwatchRows - 1 ? index + kSwatchColumns : index;
            break;
        default:
            break;
        }
        if (target >= 0)
        {
            if (target != index)
            {
                // Roving tab stop: Tab returns to the swatch that last had focus.
                const HWND next = GetDlgItem(self->m_dialog, IDC_SWATCH_0 + target);
                SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) & ~WS_TABSTOP);
                SetWindowLongPtrW(next, GWL_STYLE, GetWindowLongPtrW(next, GWL_STYLE) | WS_TABSTOP);
                SendMessageW(self->m_dialog, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(next), TRUE);
            }
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace te
