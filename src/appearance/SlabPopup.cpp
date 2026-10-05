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
    m_settings.slabThicknessPx = std::clamp(settings.slabThicknessPx, kSlabMinPx, kSlabMaxPx);
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
    CheckDlgButton(m_dialog, IDC_SLAB_TOP, m_settings.slabTop ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_dialog, IDC_SLAB_LEFT, m_settings.slabLeft ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_dialog, IDC_SLAB_BOTTOM, m_settings.slabBottom ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_dialog, IDC_SLAB_RIGHT, m_settings.slabRight ? BST_CHECKED : BST_UNCHECKED);
    const int thickness = m_settings.slabThicknessPx;
    SendDlgItemMessageW(m_dialog, IDC_SLAB_TRACK, TBM_SETPOS, TRUE, thickness);
    const std::wstring value = thickness == 0 ? L"0 px (flat)" : std::to_wstring(thickness) + L" px";
    SetDlgItemTextW(m_dialog, IDC_SLAB_VALUE, value.c_str());
    const bool anyEdge = m_settings.slabTop || m_settings.slabLeft || m_settings.slabBottom || m_settings.slabRight;
    for (const int id : {IDC_SLAB_LABEL, IDC_SLAB_VALUE, IDC_SLAB_TRACK, IDC_SLAB_DEFAULT})
    {
        EnableWindow(GetDlgItem(m_dialog, id), anyEdge);
    }
    m_syncing = false;
}

void SlabPopup::OnEdgeClicked(int id)
{
    const bool checked = IsDlgButtonChecked(m_dialog, id) == BST_CHECKED;
    bool& edge = id == IDC_SLAB_TOP      ? m_settings.slabTop
                 : id == IDC_SLAB_LEFT   ? m_settings.slabLeft
                 : id == IDC_SLAB_BOTTOM ? m_settings.slabBottom
                                         : m_settings.slabRight;
    if (checked == edge)
    {
        return;
    }
    edge = checked;
    SyncControls();
    Changed();
}

void SlabPopup::Changed()
{
    if (m_changed)
    {
        m_changed(m_settings);
    }
}

void SlabPopup::SetThickness(int px)
{
    px = std::clamp(px, kSlabMinPx, kSlabMaxPx);
    if (px == m_settings.slabThicknessPx)
    {
        return;
    }
    m_settings.slabThicknessPx = px;
    SyncControls();
    Changed();
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
        SendDlgItemMessageW(m_dialog, IDC_SLAB_TRACK, TBM_SETRANGE, TRUE, MAKELPARAM(kSlabMinPx, kSlabMaxPx));
        SendDlgItemMessageW(m_dialog, IDC_SLAB_TRACK, TBM_SETPAGESIZE, 0, 4);
        SendDlgItemMessageW(m_dialog, IDC_SLAB_TRACK, TBM_SETTICFREQ, 4, 0);
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
                SetThickness(kSlabDefaultPx);
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
            SetThickness(static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), TBM_GETPOS, 0, 0)));
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
