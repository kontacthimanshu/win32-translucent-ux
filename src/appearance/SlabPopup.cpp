#include <te/appearance/SlabPopup.h>

#include <te/appearance/AppearanceIds.h>

#include <commctrl.h>

#include <wil/result.h>

#include <algorithm>
#include <string>
#include <utility>

namespace te
{

namespace
{

// Posted to the popup after its own WM_DPICHANGED, once the dialog manager has rescaled it.
constexpr UINT kRepositionMessage = WM_APP + 1;

// One row of the popup: an edge's controls and its fields.
struct EdgeRow
{
    int check;
    int track;
    int value;
    bool AppearanceSettings::*on;
    int AppearanceSettings::*px;
};

constexpr EdgeRow kRows[] = {
    {IDC_SLAB_TOP, IDC_SLAB_TOP_TRACK, IDC_SLAB_TOP_VALUE, &AppearanceSettings::slabTop, &AppearanceSettings::slabTopPx},
    {IDC_SLAB_LEFT, IDC_SLAB_LEFT_TRACK, IDC_SLAB_LEFT_VALUE, &AppearanceSettings::slabLeft,
     &AppearanceSettings::slabLeftPx},
    {IDC_SLAB_BOTTOM, IDC_SLAB_BOTTOM_TRACK, IDC_SLAB_BOTTOM_VALUE, &AppearanceSettings::slabBottom,
     &AppearanceSettings::slabBottomPx},
    {IDC_SLAB_RIGHT, IDC_SLAB_RIGHT_TRACK, IDC_SLAB_RIGHT_VALUE, &AppearanceSettings::slabRight,
     &AppearanceSettings::slabRightPx},
};

} // namespace

SlabPopup::SlabPopup(HINSTANCE instance, std::function<void(HWND)> registerDialog)
    : m_instance(instance), m_registerDialog(std::move(registerDialog))
{
}

SlabPopup::~SlabPopup()
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

void SlabPopup::SetChangedCallback(std::function<void(const AppearanceSettings&)> callback)
{
    m_changed = std::move(callback);
}

bool SlabPopup::EnsureDialog(HWND owner)
{
    if (m_dialog && IsWindow(m_dialog))
    {
        return true;
    }
    m_owner = owner;
    m_dialog = CreateDialogParamW(m_instance, MAKEINTRESOURCEW(IDD_SLAB), owner, DialogProc,
                                  reinterpret_cast<LPARAM>(this));
    LOG_LAST_ERROR_IF_NULL(m_dialog);
    return m_dialog != nullptr;
}

void SlabPopup::Show(HWND owner, const RECT& anchorScreen, const AppearanceSettings& settings)
{
    m_settings = settings;
    for (const EdgeRow& row : kRows)
    {
        m_settings.*row.px = std::clamp(settings.*row.px, kSlabMinPx, kSlabMaxPx);
    }
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
    SendMessageW(m_dialog, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(m_dialog, IDC_SLAB_TOP)), TRUE);
    if (m_registerDialog)
    {
        m_registerDialog(m_dialog);
    }
}

void SlabPopup::Hide()
{
    if (!m_dialog || !IsWindowVisible(m_dialog))
    {
        return;
    }
    const bool wasActive = GetActiveWindow() == m_dialog;
    ShowWindow(m_dialog, SW_HIDE); // kept alive for the next Show
    if (m_registerDialog)
    {
        m_registerDialog(nullptr);
    }
    if (wasActive && m_owner)
    {
        SetActiveWindow(m_owner);
    }
}

bool SlabPopup::IsOpen() const
{
    return m_dialog && IsWindowVisible(m_dialog);
}

bool SlabPopup::ShouldSwallowClick(UINT message)
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

void SlabPopup::Reposition(const RECT& anchorScreen)
{
    m_anchor = anchorScreen;
    if (IsOpen())
    {
        Position(m_owner, m_anchor);
    }
}

void SlabPopup::Position(HWND owner, const RECT& anchorScreen)
{
    RECT dialog{};
    GetWindowRect(m_dialog, &dialog);
    const int width = dialog.right - dialog.left;
    const int height = dialog.bottom - dialog.top;

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;

    // Right-aligned under the slab button; above it if there is no room below; always
    // inside the work area.
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

void SlabPopup::SyncControls()
{
    m_syncing = true;
    for (const EdgeRow& row : kRows)
    {
        const bool on = m_settings.*row.on;
        const int px = m_settings.*row.px;
        CheckDlgButton(m_dialog, row.check, on ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(m_dialog, row.track, TBM_SETPOS, TRUE, px);
        SetDlgItemTextW(m_dialog, row.value, (std::to_wstring(px) + L" px").c_str());
        EnableWindow(GetDlgItem(m_dialog, row.track), on);
        EnableWindow(GetDlgItem(m_dialog, row.value), on);
    }
    m_syncing = false;
}

void SlabPopup::OnEdgeClicked(int id)
{
    for (const EdgeRow& row : kRows)
    {
        if (row.check != id)
        {
            continue;
        }
        const bool checked = IsDlgButtonChecked(m_dialog, id) == BST_CHECKED;
        if (checked != m_settings.*row.on)
        {
            m_settings.*row.on = checked;
            SyncControls();
            Changed();
        }
        return;
    }
}

void SlabPopup::OnScroll(HWND trackbar)
{
    const int id = GetDlgCtrlID(trackbar);
    for (const EdgeRow& row : kRows)
    {
        if (row.track != id)
        {
            continue;
        }
        const int px = std::clamp(static_cast<int>(SendMessageW(trackbar, TBM_GETPOS, 0, 0)), kSlabMinPx, kSlabMaxPx);
        if (px != m_settings.*row.px)
        {
            m_settings.*row.px = px;
            SyncControls();
            Changed();
        }
        return;
    }
}

void SlabPopup::SetDefaultThickness()
{
    bool changed = false;
    for (const EdgeRow& row : kRows)
    {
        changed = changed || m_settings.*row.px != kSlabDefaultPx;
        m_settings.*row.px = kSlabDefaultPx;
    }
    if (changed)
    {
        SyncControls();
        Changed();
    }
}

void SlabPopup::Changed()
{
    if (m_changed)
    {
        m_changed(m_settings);
    }
}

INT_PTR CALLBACK SlabPopup::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SlabPopup* self = nullptr;
    if (msg == WM_INITDIALOG)
    {
        self = reinterpret_cast<SlabPopup*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        self->m_dialog = hwnd;
    }
    else
    {
        self = reinterpret_cast<SlabPopup*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    return self ? self->HandleMessage(msg, wParam, lParam) : FALSE;
}

INT_PTR SlabPopup::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        for (const EdgeRow& row : kRows)
        {
            SendDlgItemMessageW(m_dialog, row.track, TBM_SETRANGE, TRUE, MAKELPARAM(kSlabMinPx, kSlabMaxPx));
            SendDlgItemMessageW(m_dialog, row.track, TBM_SETPAGESIZE, 0, 4);
            SendDlgItemMessageW(m_dialog, row.track, TBM_SETTICFREQ, 4, 0);
        }
        return FALSE; // Show() sets the focus

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDC_SLAB_TOP:
            case IDC_SLAB_LEFT:
            case IDC_SLAB_BOTTOM:
            case IDC_SLAB_RIGHT:
                if (!m_syncing)
                {
                    OnEdgeClicked(LOWORD(wParam));
                }
                break;
            case IDC_SLAB_DEFAULT:
                SetDefaultThickness();
                break;
            case IDCANCEL:
                Hide();
                break;
            default:
                break;
            }
        }
        return TRUE;

    case WM_HSCROLL:
        if (lParam && !m_syncing)
        {
            OnScroll(reinterpret_cast<HWND>(lParam));
        }
        return TRUE;

    case WM_ACTIVATE:
        // Clicking anywhere else dismisses the popup.
        if (LOWORD(wParam) == WA_INACTIVE && IsWindowVisible(m_dialog))
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
        PostMessageW(m_dialog, kRepositionMessage, 0, 0);
        return FALSE;

    case kRepositionMessage:
        if (IsOpen())
        {
            Position(m_owner, m_anchor);
        }
        return TRUE;

    case WM_DESTROY:
        if (IsWindowVisible(m_dialog) && m_registerDialog)
        {
            m_registerDialog(nullptr);
        }
        return FALSE;

    case WM_NCDESTROY:
        m_dialog = nullptr;
        return FALSE;

    default:
        return FALSE;
    }
}

} // namespace te
