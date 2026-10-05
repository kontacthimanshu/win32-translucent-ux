#include <te/app/MainWindow.h>

#include <te/app/CommandIds.h>
#include <te/appearance/AppearanceIds.h>
#include <te/core/Messages.h>
#include <te/core/Result.h>
#include <te/render/SurfacePainter.h>
#include <te/render/Swing.h>

#include <UIAutomationCoreApi.h>
#include <dbt.h>
#include <sherrors.h>
#include <shlobj.h>
#include <windowsx.h>

#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

namespace te
{

namespace
{

// FormatMessage with one string insert (%1).
std::wstring FormatWith(const std::wstring& pattern, const std::wstring& insert)
{
    const DWORD_PTR args[] = {reinterpret_cast<DWORD_PTR>(insert.c_str())};
    LPWSTR raw = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY |
                                            FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                        pattern.c_str(), 0, 0, reinterpret_cast<LPWSTR>(&raw), 0,
                                        reinterpret_cast<va_list*>(const_cast<DWORD_PTR*>(args)));
    const wil::unique_hlocal buffer(raw);
    return length > 0 ? std::wstring(raw, length) : pattern;
}

bool IsFolder(const ShellLocation& location)
{
    wil::com_ptr<IShellItem> item;
    SFGAOF attributes = 0;
    return SUCCEEDED(location.Item(&item)) && SUCCEEDED(item->GetAttributes(SFGAO_FOLDER, &attributes)) &&
           (attributes & SFGAO_FOLDER) != 0;
}

// FormatMessage with two string inserts (%1, %2).
std::wstring FormatWith2(const std::wstring& pattern, const std::wstring& first, const std::wstring& second)
{
    const DWORD_PTR args[] = {reinterpret_cast<DWORD_PTR>(first.c_str()),
                              reinterpret_cast<DWORD_PTR>(second.c_str())};
    LPWSTR raw = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY |
                                            FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                        pattern.c_str(), 0, 0, reinterpret_cast<LPWSTR>(&raw), 0,
                                        reinterpret_cast<va_list*>(const_cast<DWORD_PTR*>(args)));
    const wil::unique_hlocal buffer(raw);
    return length > 0 ? std::wstring(raw, length) : pattern;
}

wil::unique_hicon LoadAppIcon(HINSTANCE instance, int resourceId, int metricX, int metricY, UINT dpi)
{
    if (resourceId == 0)
    {
        return {};
    }
    return wil::unique_hicon(static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(resourceId), IMAGE_ICON, GetSystemMetricsForDpi(metricX, dpi),
                   GetSystemMetricsForDpi(metricY, dpi), LR_DEFAULTCOLOR)));
}

} // namespace

MainWindow::MainWindow(Options options) : m_options(std::move(options)), m_statusBar(m_options.statusStrings)
{
    m_fileOps = m_options.fileOperations ? m_options.fileOperations : &m_fileOpService;
    m_navigator.SetMenuTracker(m_options.contextMenuTracker);
    m_settings = ISettingsStore::Defaults(); // replaced by LoadSettings() in Create()
}

void MainWindow::LoadSettings()
{
    // Before the window exists, so the first frame already uses the saved appearance.
    if (m_options.settingsStore)
    {
        const LoadResult loaded = m_options.settingsStore->Load();
        m_settings = loaded.settings;
        m_settingsFileWasCorrupt = loaded.fileWasCorrupt;
    }
    m_savedMode = m_settings.backdropMode;
    // Debug builds: --backdrop applies to this session only and is never saved.
    if (m_options.requestedMode)
    {
        m_settings.backdropMode = *m_options.requestedMode;
        m_modeOverridden = true;
    }
}

void MainWindow::ApplyUserSettings(const AppearanceSettings& settings)
{
    if (settings.backdropMode != m_settings.backdropMode)
    {
        m_capabilities.backdropApplyFailed = false; // a new choice gets a fresh attempt
        m_modeOverridden = false;                   // the user chose a mode: save it
    }
    const bool slabChanged = settings.slabThicknessPx != m_settings.slabThicknessPx ||
                             settings.slabTop != m_settings.slabTop || settings.slabLeft != m_settings.slabLeft ||
                             settings.slabBottom != m_settings.slabBottom;
    m_settings = settings;
    if (slabChanged && m_hwnd)
    {
        UpdateLayout(); // the caption and the panes make room for the slab's faces
    }
    ApplyAppearance();

    // Debounced save: every change restarts the 100 ms timer (R-10).
    m_savePending = true;
    if (m_hwnd)
    {
        SetTimer(m_hwnd, kSaveTimerId, kSaveDelayMs, nullptr);
    }
}

void MainWindow::SaveNow()
{
    if (m_hwnd)
    {
        KillTimer(m_hwnd, kSaveTimerId);
    }
    if (!m_savePending)
    {
        return;
    }
    m_savePending = false;
    if (!m_options.settingsStore)
    {
        return;
    }
    AppearanceSettings toSave = m_settings;
    if (m_modeOverridden)
    {
        toSave.backdropMode = m_savedMode;
    }
    if (const HRESULT hr = m_options.settingsStore->Save(toSave); FAILED(hr))
    {
        // The appearance still applies for this session; only persistence failed.
        LOG_HR_MSG(hr, "Saving the appearance settings failed");
        return;
    }
    m_savedMode = toSave.backdropMode;
}

MainWindow::~MainWindow()
{
    if (m_hwnd && IsWindow(m_hwnd))
    {
        DestroyWindow(m_hwnd);
    }
}

HRESULT MainWindow::Create(int showCommand)
{
    LoadSettings();

    WNDCLASSEXW existing{sizeof(existing)};
    if (!GetClassInfoExW(m_options.instance, kClassName, &existing))
    {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS; // double-click opens in the file list
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_options.instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // the frame and Direct2D paint everything
        wc.lpszClassName = kClassName;
        if (m_options.iconResourceId != 0)
        {
            wc.hIcon = LoadIconW(m_options.instance, MAKEINTRESOURCEW(m_options.iconResourceId));
            wc.hIconSm = static_cast<HICON>(
                LoadImageW(m_options.instance, MAKEINTRESOURCEW(m_options.iconResourceId), IMAGE_ICON,
                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        }
        RETURN_LAST_ERROR_IF(RegisterClassExW(&wc) == 0);
    }

    // WS_CLIPCHILDREN: the black WM_PAINT fill must not paint over child controls
    // (the address EDIT), or they disappear (spike-rendering.md, finding b).
    const HWND hwnd =
        CreateWindowExW(WS_EX_APPWINDOW, kClassName, m_options.title.c_str(),
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, nullptr, nullptr, m_options.instance, this);
    RETURN_LAST_ERROR_IF_NULL(hwnd);

    // Size the window for the DPI of the monitor it opened on: the initial client
    // size in DIPs plus the frame borders Windows keeps, but never more than a
    // share of the monitor's work area, and centred in it so that every edge and
    // the caption buttons are on screen (small or highly scaled displays).
    RECT window{};
    RECT client{};
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    const int borderX = (window.right - window.left) - client.right;
    const int borderY = (window.bottom - window.top) - client.bottom;

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    const int width = std::min(m_dpi.ToPx(kInitialWidthDip) + borderX,
                               static_cast<int>(workWidth * kInitialMaxWorkAreaShare));
    const int height = std::min(m_dpi.ToPx(kInitialHeightDip) + borderY,
                                static_cast<int>(workHeight * kInitialMaxWorkAreaShare));
    SetWindowPos(hwnd, nullptr, work.left + (workWidth - width) / 2, work.top + (workHeight - height) / 2,
                 width, height, SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_settingsFileWasCorrupt)
    {
        // One-time notice (UI contract §6, edge case "Corrupted settings file").
        m_statusBar.SetTransientMessage(m_options.settingsResetText, kSettingsResetNoticeMs);
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);
    return S_OK;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_dpi.Attach(hwnd);
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
    {
        // Messages before WM_NCCREATE or after WM_NCDESTROY.
        FreeMessagePayload(msg, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    const LRESULT result = self->HandleMessage(msg, wParam, lParam);
    // After anything that can move the focus or selection or change the listing, tell UI
    // Automation clients what changed (T079).
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_COMMAND:
    case WM_TE_ENUM_BATCH:
    case WM_TE_ENUM_DONE:
    case WM_TE_FILEOP_ITEM:
    case WM_TE_FILEOP_DONE:
    case WM_TE_UIA_OPEN_ITEM:
    case WM_TE_UIA_NAVIGATE:
        if (self->m_hwnd)
        {
            self->UpdateAutomationState();
        }
        break;
    default:
        break;
    }
    if (msg == WM_NCDESTROY)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->m_hwnd = nullptr;
    }
    return result;
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 0. The click that dismissed the appearance popup (by deactivating it) goes no
    //    further: not to the picker, which would reopen it, nor to anything below (R-10).
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) && m_picker && m_picker->ShouldSwallowClick(msg))
    {
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) && m_slabPopup && m_slabPopup->ShouldSwallowClick(msg))
    {
        return 0;
    }

    // While a Shell context menu is open, its submenus and owner-drawn items are drawn by
    // the Shell's handler (T069, T072).
    if (msg == WM_INITMENUPOPUP || msg == WM_DRAWITEM || msg == WM_MEASUREITEM || msg == WM_MENUCHAR)
    {
        LRESULT menuResult = 0;
        if (m_navigator.HandleMenuMessage(msg, wParam, lParam, &menuResult))
        {
            return menuResult;
        }
    }

    // 1. The custom frame: WM_CREATE, WM_DWMCOMPOSITIONCHANGED, WM_NCCALCSIZE,
    //    WM_NCHITTEST (DwmDefWindowProc first, research R-03) and WM_GETMINMAXINFO.
    LRESULT frameResult = 0;
    if (m_titleBar.HandleMessage(m_hwnd, msg, wParam, lParam, &frameResult))
    {
        return frameResult;
    }

    // 2. Pointer input for the toolbar, address bar, navigation pane and file list.
    if (OnMouse(msg, wParam, lParam))
    {
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
        OnCreate();
        return 0;

    // Navigation (T064).
    case WM_TE_ENUM_BATCH:
        OnEnumBatch(lParam);
        return 0;

    case WM_TE_ENUM_DONE:
        OnEnumDone(lParam);
        return 0;

    case WM_TE_ICON_READY:
        OnIconReady(lParam);
        return 0;

    // File operations (T072).
    case WM_TE_FILEOP_ITEM:
        OnFileOpItem(lParam);
        return 0;

    case WM_TE_FILEOP_DONE:
        OnFileOpDone(lParam);
        return 0;

    case WM_TE_UIA_NAVIGATE:
        // The address's SetValue or a navigation-pane item (T078).
        if (m_uiaPendingAddress)
        {
            const std::wstring text = std::move(*m_uiaPendingAddress);
            m_uiaPendingAddress.reset();
            NavigateFromAddress(text);
        }
        else if (m_uiaPendingPlace)
        {
            const ShellLocation place = std::move(*m_uiaPendingPlace);
            m_uiaPendingPlace.reset();
            NavigateTo(place, HistoryMode::Push);
        }
        return 0;

    case WM_TE_UIA_EXPAND:
        // A tree item's Expand / Collapse through UI Automation (T090).
        if (const auto index = m_navPane.IndexOfNode(static_cast<std::uint64_t>(wParam)))
        {
            lParam != 0 ? m_navPane.Expand(*index) : m_navPane.Collapse(*index);
        }
        return 0;

    case WM_TE_TREE_CHILDREN:
        // A folder-tree node's subfolders (T090).
        if (std::unique_ptr<FolderChildren> children = TakeOwned<FolderChildren>(lParam))
        {
            // The address bar's chevron menus (T091) or the navigation pane's tree.
            if (AddressBar::OwnsNode(children->nodeId))
            {
                m_addressBar.OnChildren(*children);
            }
            else
            {
                m_navPane.OnChildren(*children);
            }
        }
        return 0;

    case WM_TE_UIA_OPEN_ITEM:
        // A row's Invoke (T077): open it if it is still in the listing on screen.
        if (static_cast<Generation>(lParam) == m_fileView.CurrentGeneration())
        {
            if (const auto index = m_fileView.IndexOfKey(static_cast<std::size_t>(wParam)))
            {
                OpenItem(m_fileView.Items()[*index]);
            }
        }
        return 0;

    case WM_CONTEXTMENU:
        if (OnContextMenu(lParam))
        {
            return 0;
        }
        break;

    case WM_KEYDOWN:
        ShowFocusCues(); // keyboard use always shows focus (T080)
        if (OnKeyDown(static_cast<UINT>(wParam)))
        {
            return 0;
        }
        break;

    case WM_SYSKEYDOWN:
        ShowFocusCues();
        break;

    case WM_UPDATEUISTATE: {
        const LRESULT result = DefWindowProcW(m_hwnd, msg, wParam, lParam);
        UpdateFocusCues();
        return result;
    }

    case WM_CTLCOLOREDIT: {
        LRESULT brush = 0;
        if (m_addressBar.HandleCtlColor(reinterpret_cast<HWND>(lParam), reinterpret_cast<HDC>(wParam),
                                        &brush) ||
            m_filterBox.HandleCtlColor(reinterpret_cast<HWND>(lParam), reinterpret_cast<HDC>(wParam),
                                       &brush) ||
            m_fileView.RenameField().HandleCtlColor(reinterpret_cast<HWND>(lParam),
                                                    reinterpret_cast<HDC>(wParam), &brush))
        {
            return brush;
        }
        break;
    }

    case WM_DEVICECHANGE:
        m_navPane.OnDeviceChange(wParam);
        break;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(m_hwnd, nullptr, FALSE); // focus rectangles
        break;

    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wParam), *reinterpret_cast<const RECT*>(lParam));
        return 0;

    case WM_ERASEBKGND:
        return 1; // everything is painted in WM_PAINT

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_CLOSE:
        // Stop background work before anything else happens (T088); DefWindowProc then
        // destroys the window.
        CancelBackgroundWork();
        break;

    case WM_DESTROY:
        OnDestroy();
        return 0;

    // UI Automation (T074; R-09).
    case WM_GETOBJECT:
        if (static_cast<long>(lParam) == static_cast<long>(UiaRootObjectId))
        {
            return OnGetObject(wParam, lParam);
        }
        break;

    // Appearance (T037). Theme and accessibility changes are coalesced through the
    // theme manager's changed callback; composition changes also re-probe the backdrop.
    case WM_SETTINGCHANGE:
        if (IsAppearanceSettingChange(wParam, lParam))
        {
            m_themes.NotifyChanged();
        }
        break;

    case WM_TIMER:
        if (wParam == kSaveTimerId)
        {
            SaveNow();
            return 0;
        }
        if (wParam == kSwingTimerId)
        {
            OnSwingTimer();
            return 0;
        }
        if (wParam == kUiaStructureTimerId)
        {
            KillTimer(m_hwnd, kUiaStructureTimerId);
            RaiseStructureChanged();
            return 0;
        }
        if (m_statusBar.OnTimer(wParam))
        {
            return 0;
        }
        break;

    case WM_THEMECHANGED:
        m_themes.NotifyChanged();
        break;

    case WM_DWMCOMPOSITIONCHANGED:
        RefreshAppearance(true);
        return 0;

    case WM_COMMAND:
        // The filter box's EN_CHANGE: filter as the user types (T092).
        if (lParam != 0 && m_filterBox.OnCommand(reinterpret_cast<HWND>(lParam), HIWORD(wParam)))
        {
            return 0;
        }
        if (HIWORD(wParam) == 1)
        {
            ShowFocusCues(); // an accelerator key (Ctrl+L, F4, Alt+Shift+C, ...)
        }
        if (OnCommand(LOWORD(wParam)))
        {
            return 0;
        }
        break;

    case WM_TE_SETTINGS_CHANGED:
        m_themes.NotifyChanged();
        return 0;

    case WM_TE_BACKDROP_FAILED:
        OnBackdropFailed(static_cast<HRESULT>(wParam));
        return 0;

#ifdef _DEBUG
    case WM_COPYDATA:
        // A second Debug launch forwarded its --backdrop (Application::Run).
        if (const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            data && data->dwData == kCopyDataBackdrop && data->cbData == sizeof(std::int32_t) && data->lpData)
        {
            const std::int32_t mode = *static_cast<const std::int32_t*>(data->lpData);
            if (mode >= static_cast<std::int32_t>(BackdropMode::Acrylic) &&
                mode <= static_cast<std::int32_t>(BackdropMode::Transparent))
            {
                SetRequestedMode(static_cast<BackdropMode>(mode));
                m_modeOverridden = true; // like --backdrop: this session only
                return TRUE;
            }
        }
        return FALSE;
#endif

    default:
        break;
    }

    if (msg >= WM_TE_FIRST && msg <= WM_TE_LAST)
    {
        // Worker results that no handler consumes yet (handlers arrive in US1/US3/
        // US4): free the payload so nothing leaks.
        FreeMessagePayload(msg, lParam);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void MainWindow::OnCreate()
{
    LoadAppIcons();
    m_titleBar.SetTitle(m_options.title);

    LOG_IF_FAILED(m_render.Initialize(m_hwnd));
    m_text = std::make_unique<TextFormats>(m_render.DWrite());
    LOG_IF_FAILED(m_text->Rebuild(1.0f)); // the Windows text size follows in RefreshAppearance (T082)
    m_statusBar.Attach(m_hwnd);
    m_statusBar.SetTextFormats(m_text.get());
    UpdateLayout();

    // Appearance: probe -> capabilities -> Resolve -> Apply (T037). UISettings events
    // arrive as WM_TE_SETTINGS_CHANGED; without UISettings, WM_SETTINGCHANGE still works.
    // The appearance popup (T045/T046). Each change applies at once; saving it is T047.
    m_picker = std::make_unique<ColorPicker>(m_options.instance, m_options.registerModelessDialog);
    m_picker->SetChangedCallback([this](const AppearanceSettings& settings) { ApplyUserSettings(settings); });
    m_picker->SetOpenChangedCallback([this](bool open) {
        if (m_uiaPicker)
        {
            m_uiaPicker->NotifyOpenChanged(open); // ExpandCollapseState (T075)
        }
    });
    m_titleBar.SetPickerCallback([this] { TogglePicker(); });
    m_slabPopup = std::make_unique<SlabPopup>(m_options.instance, m_options.registerModelessDialog);
    m_slabPopup->SetChangedCallback([this](const AppearanceSettings& slab) {
        AppearanceSettings settings = m_settings; // only the slab fields come from the popup
        settings.slabTop = slab.slabTop;
        settings.slabLeft = slab.slabLeft;
        settings.slabBottom = slab.slabBottom;
        settings.slabThicknessPx = slab.slabThicknessPx;
        ApplyUserSettings(settings);
    });
    m_titleBar.SetSlabCallback([this] { ToggleSlabPopup(); });

    m_themes.SetChangedCallback([this] { RefreshAppearance(false); });
    if (!m_themes.Subscribe(m_hwnd))
    {
        LOG_HR_MSG(E_NOTIMPL, "UISettings unavailable; theme changes arrive through WM_SETTINGCHANGE only");
    }
    RefreshAppearance(true);

    // Navigation (T064): the toolbar, address bar, navigation pane and file list.
    const UINT navDpi = m_dpi.Dpi();
    m_toolbar.Attach(m_hwnd, navDpi);
    m_toolbar.SetTextFormats(m_text.get());
    m_toolbar.SetCallback([this](Toolbar::Button button) {
        switch (button)
        {
        case Toolbar::Button::Back:
            GoBack();
            break;
        case Toolbar::Button::Forward:
            GoForward();
            break;
        case Toolbar::Button::Up:
            GoUp();
            break;
        case Toolbar::Button::Refresh:
            RefreshFolder();
            break;
        }
    });
    m_addressBar.Attach(m_hwnd, navDpi);
    m_addressBar.SetAutomationName(m_options.addressAutomationName);
    m_addressBar.SetTextFormats(m_text.get());
    m_addressBar.SetNavigateCallback([this](std::wstring_view text) { NavigateFromAddress(text); });
    // Breadcrumbs (T091): a segment or a chevron / overflow menu item.
    m_addressBar.SetLocationCallback(
        [this](const ShellLocation& location) { NavigateTo(location, HistoryMode::Push); });
    m_addressBar.SetMenuTracker(m_options.breadcrumbMenuTracker);
    // The filter box (T092): the list follows the text as it is typed; Enter and Escape
    // give the keyboard back to the list.
    m_filterBox.Attach(m_hwnd, navDpi);
    m_filterBox.SetTextFormats(m_text.get());
    m_filterBox.SetPlaceholder(m_options.filterPlaceholder);
    m_filterBox.SetChangedCallback([this](const std::wstring& text) { m_fileView.SetFilter(text); });
    m_filterBox.SetDoneCallback([this] { SetKeyboardFocus(Focus::Files); });
    m_navPane.Attach(m_hwnd, navDpi);
    m_navPane.SetTextFormats(m_text.get());
    m_navPane.SetTreeChangedCallback([this] { RaisePaneStructureChanged(); });
    m_navPane.SetNavigateCallback([this](const ShellLocation& location) {
        SetKeyboardFocus(Focus::Places);
        NavigateTo(location, HistoryMode::Push);
    });
    m_fileView.SetTextFormats(m_text.get());
    m_fileView.AttachWindow(m_hwnd, navDpi); // hosts the inline-rename edit (T071)
    m_statusBar.SetMessageCallback([this] { RaiseStatusAnnouncement(); });
    m_fileView.RenameField().SetBadNameText(m_options.fileOpText.badName);
    m_fileView.RenameField().SetAutomationName(m_options.renameAutomationName);
    m_fileView.SetCallbacks(
        {[this](const FileItem& item) { OpenItem(item); },
         [this](std::size_t items, std::size_t selected) {
             m_statusBar.SetItemCounts(items, selected);
             UpdateAutomationState(); // also for changes made through UI Automation itself
         },
         [this] { InvalidateRect(m_hwnd, nullptr, FALSE); },
         [this](const FileItem& item, const std::wstring& newName) {
             // Inline rename (T071): the row keeps its name until the rename succeeds.
             if (auto location = LocationOf(item))
             {
                 FileOpRequest request;
                 request.kind = FileOpKind::Rename;
                 request.sources.push_back(std::move(*location));
                 request.newName = newName;
                 SubmitFileOperation(std::move(request));
             }
         }});
    UpdateLayout();
    UpdateFocusCues(); // Windows may start with focus cues hidden until a key is pressed
    SetKeyboardFocus(Focus::Files);

    if (m_options.startShell)
    {
        m_navPane.Refresh();
        std::optional<std::wstring_view> path;
        if (m_options.initialPath)
        {
            path = *m_options.initialPath;
        }
        NavigateTo(m_navigator.InitialLocation(path), HistoryMode::Push);
    }
}

void MainWindow::SetRequestedMode(BackdropMode mode)
{
    m_settings.backdropMode = mode;
    // A new choice gets a fresh attempt: rule 4 only reflects the last failed Apply.
    m_capabilities.backdropApplyFailed = false;
    ApplyAppearance();
}

void MainWindow::TogglePicker()
{
    if (!m_picker)
    {
        return;
    }
    if (m_picker->IsOpen())
    {
        m_picker->Hide();
        return;
    }
    m_picker->SetCapabilities(m_capabilities);
    m_picker->Show(m_hwnd, m_titleBar.PickerScreenRect(m_hwnd), m_settings, m_effective);
}

void MainWindow::ToggleSlabPopup()
{
    if (!m_slabPopup)
    {
        return;
    }
    if (m_slabPopup->IsOpen())
    {
        m_slabPopup->Hide();
        return;
    }
    if (m_picker)
    {
        m_picker->Hide(); // one title-bar popup at a time
    }
    m_slabPopup->Show(m_hwnd, m_titleBar.SlabScreenRect(m_hwnd), m_settings);
}

void MainWindow::RefreshAppearance(bool reprobe)
{
    if (!m_hwnd)
    {
        return;
    }
    if (reprobe)
    {
        // The probe sets DWMSBT_MAINWINDOW; ApplyAppearance sets the real type next.
        m_backdropSupported = m_backdrops.ProbeSystemBackdrop(m_hwnd);
    }
    const bool applyFailed = m_capabilities.backdropApplyFailed;
    m_capabilities = m_themes.QueryCapabilities();
    if (m_options.adjustCapabilities)
    {
        m_options.adjustCapabilities(m_capabilities);
    }
    m_capabilities.systemBackdropSupported = m_backdropSupported;
    m_capabilities.backdropApplyFailed = applyFailed;
    // Re-read on every change (T082), with nothing to restart: dark mode and the accent
    // re-resolve the colors and ApplyAppearance sets DWMWA_USE_IMMERSIVE_DARK_MODE again;
    // high contrast re-resolves to Solid with system colors (T037); the text size rebuilds
    // the formats and the layout below. animationsEnabled needs nothing yet: the app has
    // no animations (R-05), hover and pressed states change at once. Any animation added
    // later MUST check m_capabilities.animationsEnabled and show the end state at once when
    // it is false (Windows "Animation effects" off; docs/review-checklist.md).
    ApplyTextScale(static_cast<float>(m_capabilities.textScaleFactor));
    ApplyAppearance();
}

void MainWindow::ApplyTextScale(float scale)
{
    if (!m_text)
    {
        return;
    }
    const float clamped = std::clamp(scale, TextFormats::kMinTextScale, TextFormats::kMaxTextScale);
    if (clamped == m_text->TextScale())
    {
        return;
    }
    if (FAILED_LOG(m_text->Rebuild(clamped)))
    {
        return; // the previous formats stay usable
    }
    // Rows, the toolbar, the address field and the status bar grow with the text.
    m_fileView.SetTextScale(clamped);
    m_navPane.SetTextScale(clamped);
    m_titleBar.SetTextScale(clamped);
    m_addressBar.OnTextScaleChanged();
    m_filterBox.OnTextScaleChanged();
    UpdateLayout();
    if (!IsZoomed(m_hwnd) && !IsIconic(m_hwnd))
    {
        // Re-applies the minimum size, which grew with the text (WM_GETMINMAXINFO).
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ApplyAppearance()
{
    m_effective = m_themes.Resolve(m_settings, m_capabilities);
    m_statusBar.SetAppearance(m_settings, m_effective);
    if (m_picker)
    {
        m_picker->SetCapabilities(m_capabilities);
        m_picker->Update(m_settings, m_effective);
    }
    const HRESULT hr = m_backdrops.Apply(m_hwnd, m_effective);
    if (FAILED(hr) && m_effective.applied != BackdropMode::Solid)
    {
        // Rule 4: fall back to Solid on the next message (FR-022, spec US1-3). A failed
        // Solid apply is not retried, so this cannot loop.
        PostMessageW(m_hwnd, WM_TE_BACKDROP_FAILED, static_cast<WPARAM>(hr), 0);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnBackdropFailed(HRESULT hr)
{
    LOG_HR_MSG(hr, "Backdrop apply failed; falling back to Solid");
    m_capabilities.backdropApplyFailed = true;
    ApplyAppearance();
}

bool MainWindow::IsAppearanceSettingChange(WPARAM wParam, LPARAM lParam) const
{
    // High contrast on or off (T037), and Windows "Animation effects" (T082; read again
    // into RenderingCapabilities::animationsEnabled).
    if (wParam == SPI_SETHIGHCONTRAST || wParam == SPI_SETCLIENTAREAANIMATION)
    {
        return true;
    }
    const auto* area = reinterpret_cast<const wchar_t*>(lParam);
    return area != nullptr && std::wcscmp(area, L"ImmersiveColorSet") == 0;
}

void MainWindow::OnSize(UINT widthPx, UINT heightPx)
{
    LOG_IF_FAILED(m_render.Resize(widthPx, heightPx, m_dpi.Dpi()));
    UpdateLayout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::LoadAppIcons()
{
    // The title bar and taskbar icons at the window's DPI: loaded again on a DPI change,
    // so they are never a scaled bitmap (T081). The old icons stay alive until the new
    // ones are in use.
    const UINT dpi = m_dpi.Dpi();
    wil::unique_hicon bigIcon =
        LoadAppIcon(m_options.instance, m_options.iconResourceId, SM_CXICON, SM_CYICON, dpi);
    wil::unique_hicon smallIcon =
        LoadAppIcon(m_options.instance, m_options.iconResourceId, SM_CXSMICON, SM_CYSMICON, dpi);
    if (bigIcon)
    {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon.get()));
    }
    if (smallIcon)
    {
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon.get()));
    }
    m_titleBar.SetIcon(smallIcon.get());
    m_iconLarge = std::move(bigIcon);
    m_iconSmall = std::move(smallIcon);
}

void MainWindow::OnDpiChanged(UINT dpi, const RECT& suggested)
{
    // T081. Every component takes the new DPI before the window moves, so the WM_SIZE
    // sent by the move lays out and draws everything at the new scale at once.
    m_toolbar.SetDpi(dpi);
    m_addressBar.SetDpi(dpi);
    m_filterBox.SetDpi(dpi);
    m_fileView.SetDpi(dpi);
    m_navPane.SetDpi(dpi); // extracts its icons again at the new size
    if (m_text)
    {
        // The formats are in DIPs; rebuilt so layouts cached against them are dropped.
        LOG_IF_FAILED(m_text->Rebuild(m_text->TextScale()));
    }

    // The suggested rectangle (sized by WM_GETDPISCALEDSIZE); stores the DPI first, so
    // WM_GETMINMAXINFO and WM_SIZE during the move already see it.
    m_dpi.OnDpiChanged(m_hwnd, dpi, suggested);
    LoadAppIcons();

    // File icons at the new pixel size: a new generation for the icons only. The listing,
    // selection and scroll position stay; requests queued at the old size are dropped.
    const Generation iconGen = m_guard.Issue();
    m_fileView.ResetIcons(iconGen);
    m_icons.CancelOlderThan(iconGen);

    // The caption layout, render target and component layout (also when the move did not
    // change the size in pixels, so no WM_SIZE came).
    RECT client{};
    GetClientRect(m_hwnd, &client);
    OnSize(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom));

    // An open appearance popup follows the picker button onto the new monitor.
    if (m_picker && m_picker->IsOpen())
    {
        m_picker->Reposition(m_titleBar.PickerScreenRect(m_hwnd));
    }
    if (m_slabPopup && m_slabPopup->IsOpen())
    {
        m_slabPopup->Reposition(m_titleBar.SlabScreenRect(m_hwnd));
    }
    UpdateAutomationState();
}

void MainWindow::UpdateLayout()
{
    const int slab = m_settings.slabThicknessPx;
    m_hitTester.SetSlabPx(m_settings.slabTop ? slab : 0, m_settings.slabLeft ? slab : 0,
                          m_settings.slabBottom ? slab : 0);
    m_titleBar.UpdateLayout(m_hwnd);
    RECT client{};
    GetClientRect(m_hwnd, &client);
    const CaptionLayout& caption = m_titleBar.Layout();
    m_layout = MainLayout::Compute(SIZE{client.right, client.bottom}, m_dpi.Dpi(), caption.captionHeightPx,
                                   m_text ? m_text->TextScale() : 1.0f,
                                   {caption.slabTopPx, caption.slabLeftPx, caption.slabBottomPx});
    m_layout.captionButtons = MainLayout::ToDip(caption.captionButtons, m_dpi.Dpi());
    m_layout.slabOrigin = MainLayout::ToDip(RECT{0, caption.contentTopPx, 0, 0}, m_dpi.Dpi()).top;
    m_statusBar.SetBounds(m_layout.statusBar);

    // The toolbar row: navigation buttons, the address bar, then the filter box at the right
    // edge (T092), which gives up width first in a narrow window (at most a third of the row).
    constexpr float kAddressGapDip = 8.0f;
    constexpr float kAddressRightMarginDip = 12.0f;
    const float addressLeft = m_layout.toolbar.left + Toolbar::ButtonsWidth() + kAddressGapDip;
    const float rowRight = std::max(addressLeft, m_layout.toolbar.right - kAddressRightMarginDip);
    const float filterWidth = std::min(FilterBox::kWidthDip, (rowRight - addressLeft) / 3.0f);
    const float filterLeft = rowRight - std::max(0.0f, filterWidth);
    m_toolbar.SetBounds(m_layout.toolbar);
    m_addressBar.SetBounds(D2D1::RectF(addressLeft, m_layout.toolbar.top,
                                       std::max(addressLeft, filterLeft - kAddressGapDip),
                                       m_layout.toolbar.bottom));
    m_filterBox.SetBounds(D2D1::RectF(filterLeft, m_layout.toolbar.top, rowRight, m_layout.toolbar.bottom));
    m_navPane.SetBounds(m_layout.navigationPane);
    m_fileView.SetBounds(m_layout.fileList);
}

void MainWindow::OnPaint()
{
    // Black GDI surface = zero alpha in the extended frame; then the Direct2D pass.
    CustomTitleBar::PaintRedirectionSurface(m_hwnd);
    Render();
}

void MainWindow::Render()
{
    ID2D1DeviceContext* dc = m_render.BeginDraw();
    if (!dc)
    {
        return;
    }
    // Back to front (research R-04): clear, surface layer, tint layer; then each text
    // area gets its legibility floor (R-05) right before its text is drawn.
    SurfacePainter::PaintSurfaces(dc, m_layout, m_effective);
    if (m_text)
    {
        m_titleBar.Render(dc, m_text->Title(), m_effective);
        m_toolbar.Render(dc, m_effective);
        m_addressBar.Render(dc, m_effective);
        m_filterBox.Render(dc, m_effective);
        m_navPane.Render(dc, m_effective);

        // Icons: finished ones first, then requests for rows that became visible.
        ConvertPendingIcons(dc);
        if (m_current.IsValid())
        {
            const int iconPx = static_cast<int>(std::lround(
                FileView::kIconSizeDip * static_cast<float>(m_dpi.Dpi()) / USER_DEFAULT_SCREEN_DPI));
            m_fileView.ForEachVisibleWithoutIcon([&](const FileItem& item) {
                m_icons.Request(m_hwnd, m_fileView.IconGeneration(), item.key, m_current, item.info, iconPx,
                                item.icon.forceExtract);
            });
        }
        RenderFileView(dc);

        SurfacePainter::PaintTextScrim(dc, m_layout.statusBar, m_effective);
        m_statusBar.Render(dc, m_effective);
        SurfacePainter::PaintDepth(dc, m_layout, m_effective);
        SurfacePainter::PaintSlab(dc, dc->GetSize(), m_layout, m_effective);
        SurfacePainter::PaintFrameBevel(dc, dc->GetSize(), m_layout.captionButtons, IsZoomed(m_hwnd) != FALSE,
                                        m_effective);
        m_addressBar.RenderOverlay(dc, m_effective); // the inline error, over the list
    }
    LOG_IF_FAILED(m_render.EndDrawAndPresent());
}

void MainWindow::RenderFileView(ID2D1DeviceContext* dc)
{
    const D2D1_RECT_F area = m_layout.fileList;
    if (m_swingStart == 0 || area.right <= area.left || area.bottom <= area.top)
    {
        m_fileView.Render(dc, m_effective);
        return;
    }

    // The list into a layer the size of its area, at the target's DPI...
    float dpiX = 0.0f;
    float dpiY = 0.0f;
    dc->GetDpi(&dpiX, &dpiY);
    const D2D1_SIZE_U size =
        D2D1::SizeU(static_cast<UINT32>(std::ceil((area.right - area.left) * dpiX / 96.0f)),
                    static_cast<UINT32>(std::ceil((area.bottom - area.top) * dpiY / 96.0f)));
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiY);
    wil::com_ptr<ID2D1Bitmap1> layer;
    if (FAILED_LOG(dc->CreateBitmap(size, nullptr, 0, props, layer.put())))
    {
        m_fileView.Render(dc, m_effective);
        return;
    }
    wil::com_ptr<ID2D1Image> target;
    dc->GetTarget(target.put());
    D2D1_MATRIX_3X2_F transform{};
    dc->GetTransform(&transform);

    dc->SetTarget(layer.get());
    dc->Clear(D2D1::ColorF(0, 0.0f));
    dc->SetTransform(D2D1::Matrix3x2F::Translation(-area.left, -area.top) * transform);
    m_fileView.Render(dc, m_effective);
    dc->SetTransform(transform);
    dc->SetTarget(target.get());

    // ...then the layer in perspective over the surface layers already painted there.
    const ULONGLONG elapsed = GetTickCount64() - m_swingStart;
    const Swing::Frame frame = Swing::At(area, elapsed, m_swingHingeRight);
    dc->DrawBitmap(layer.get(), &area, frame.opacity, D2D1_INTERPOLATION_MODE_LINEAR, nullptr,
                   &frame.transform);
}

void MainWindow::StartSwing(bool hingeRight)
{
    // Reduced motion (T082, docs/review-checklist.md): no swing, the end state at once.
    if (!m_capabilities.animationsEnabled)
    {
        return;
    }
    m_swingStart = std::max<ULONGLONG>(GetTickCount64(), 1);
    m_swingHingeRight = hingeRight;
    SetTimer(m_hwnd, kSwingTimerId, Swing::kFrameIntervalMs, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnSwingTimer()
{
    if (m_swingStart != 0 && GetTickCount64() - m_swingStart >= Swing::kDurationMs)
    {
        m_swingStart = 0;
        KillTimer(m_hwnd, kSwingTimerId);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::TraceShutdown(const wchar_t* step) const
{
    if (m_options.shutdownTrace)
    {
        m_options.shutdownTrace(step);
    }
}

void MainWindow::CancelBackgroundWork()
{
    if (m_backgroundCancelled)
    {
        return;
    }
    m_backgroundCancelled = true;
    TraceShutdown(L"cancel");
    // The running enumeration posts no further batch (T084), and queued icon requests are
    // dropped when they reach the front; neither blocks.
    m_enumerator.CancelAll();
    m_icons.CancelOlderThan(std::numeric_limits<Generation>::max());
    m_navPane.CancelLoads();    // the folder tree's subfolder listings (T090)
    m_addressBar.CancelLoads(); // the breadcrumb chevrons' listings (T091)
}

// Shutdown order (T088; contracts/component-interfaces.md, "Cross-thread message contract"):
// stop the producers, wait for them, free what they left in the queue, then release the
// consumers - UI Automation, then the graphics devices.
void MainWindow::OnDestroy()
{
    KillTimer(m_hwnd, kSaveTimerId);
    KillTimer(m_hwnd, kUiaStructureTimerId);
    KillTimer(m_hwnd, kSwingTimerId);
    m_swingStart = 0;
    m_statusBar.SetMessageCallback(nullptr);
    SaveNow();        // flush a change made less than kSaveDelayMs ago, before any wait
    m_picker.reset(); // the popup is owned by this window: destroy it first
    m_slabPopup.reset();

    // 1. No new work: the enumeration is cancelled, queued icon requests are dropped.
    CancelBackgroundWork();

    // 2. A running file operation finishes (its Shell UI is modal to this window and keeps
    //    pumping messages meanwhile); queued ones are dropped before they touch anything.
    TraceShutdown(L"fileops");
    m_fileOps->Shutdown();
    m_ops.clear();
    m_clipboard.FlushOnExit(); // a copy or cut stays pasteable after the app closes

    // 3. The enumeration and icon workers stop and their threads are joined.
    TraceShutdown(L"workers");
    m_enumerator.Shutdown();
    m_icons.Shutdown();
    m_navPane.ShutdownLoader();
    m_addressBar.ShutdownLoader();

    // 4. Nothing posts to this window any more: free what is still queued.
    TraceShutdown(L"drain");
    m_iconQueue.clear();
    DrainPendingMessages(m_hwnd);

    // 5. UI Automation: clients let go of the providers before the components go.
    TraceShutdown(L"uia");
    if (m_uiaRoot)
    {
        UiaReturnRawElementProvider(m_hwnd, 0, 0, nullptr);
        if (m_uiaPicker)
        {
            m_uiaPicker->Disconnect();
            m_uiaPicker.Reset();
        }
        if (m_uiaFileList)
        {
            m_uiaFileList->Disconnect();
            m_uiaFileList.Reset();
        }
        // ComPtr overloads operator&, hence std::addressof.
        for (auto* chrome :
             {std::addressof(m_uiaToolbar), std::addressof(m_uiaPane), std::addressof(m_uiaStatus)})
        {
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            if (*chrome && SUCCEEDED(chrome->As(&simple)))
            {
                LOG_IF_FAILED(UiaDisconnectProvider(simple.Get()));
            }
            chrome->Reset();
        }
        m_uiaChrome.reset();
        m_uiaRoot->Disconnect();
        m_uiaRoot.Reset();
    }

    // 6. Direct2D, DirectComposition and Direct3D: every bitmap made on the device first,
    //    then the device itself.
    TraceShutdown(L"graphics");
    m_iconCache.SetDevice(nullptr);
    m_fileView.ResetIcons(m_fileView.IconGeneration(), false);
    m_navPane.ReleaseDeviceResources();
    m_titleBar.ReleaseDeviceResources();
    m_render.ReleaseDevice();

    if (m_options.quitOnDestroy)
    {
        PostQuitMessage(0);
    }
}

// ---------------------------------------------------------------------------
// Navigation controller (T064)
// ---------------------------------------------------------------------------

void MainWindow::NavigateTo(const ShellLocation& location, HistoryMode mode)
{
    if (!location.IsValid() || !m_hwnd)
    {
        return;
    }
    // A new generation: anything still arriving for an earlier request is dropped.
    const Generation gen = m_guard.Advance();
    m_pending = PendingNavigation{location, mode, gen, m_navigatingFromAddress};
    m_enumerator.Start(m_hwnd, gen, location);
    // The icons of the folder on screen keep loading until the new one is committed (or
    // stays on screen after a failure, FR-020); CommitPending cancels them.
}

void MainWindow::GoBack()
{
    if (m_history.CanBack())
    {
        NavigateTo(m_history.Entries()[m_history.Index() - 1], HistoryMode::Back);
    }
}

void MainWindow::GoForward()
{
    if (m_history.CanForward())
    {
        NavigateTo(m_history.Entries()[m_history.Index() + 1], HistoryMode::Forward);
    }
}

void MainWindow::GoUp()
{
    if (auto parent = m_current.Parent())
    {
        NavigateTo(*parent, HistoryMode::Push); // an ordinary entry, so Back returns (DM)
    }
}

void MainWindow::RefreshFolder()
{
    if (m_current.IsValid())
    {
        NavigateTo(m_current, HistoryMode::Refresh);
    }
}

// The first batch (or an empty, successful result) makes the pending folder the one on
// screen: the view restarts and the history is committed only now (DM NavigationHistory).
void MainWindow::CommitPending()
{
    PendingNavigation pending = std::move(*m_pending);
    m_pending.reset();
    m_fileView.BeginLocation(pending.gen); // unfiltered; the filter box is emptied too (T092)
    m_filterBox.Clear();
    // Icons get a generation issued now: newer than any the icon provider has seen, even
    // one issued for a DPI change while this navigation was pending (T081).
    const Generation iconGen = m_guard.Issue();
    m_fileView.ResetIcons(iconGen);
    m_icons.CancelOlderThan(iconGen);
    switch (pending.mode)
    {
    case HistoryMode::Push:
        m_history.Navigate(pending.location);
        break;
    case HistoryMode::Back:
        m_history.Back();
        break;
    case HistoryMode::Forward:
        m_history.Forward();
        break;
    case HistoryMode::Refresh:
        break;
    }
    // The new listing swings in, from the side it was reached from; not for the first
    // folder or a refresh.
    if (m_current.IsValid() && pending.mode != HistoryMode::Refresh)
    {
        StartSwing(pending.mode == HistoryMode::Back);
    }
    m_current = std::move(pending.location);
    UpdateNavigationChrome();
}

void MainWindow::OnEnumBatch(LPARAM lParam)
{
    std::unique_ptr<EnumBatch> batch = m_guard.Take<EnumBatch>(lParam);
    if (!batch)
    {
        return; // stale: the user has navigated elsewhere
    }
    if (m_pending && m_pending->gen == batch->gen)
    {
        CommitPending();
    }
    if (batch->gen != m_fileView.CurrentGeneration())
    {
        return;
    }
    std::vector<FileItem> items;
    items.reserve(batch->items.size());
    for (ShellItemInfo& info : batch->items)
    {
        FileItem item;
        item.info = std::move(info);
        items.push_back(std::move(item));
    }
    m_fileView.AppendItems(batch->gen, std::move(items));
}

void MainWindow::OnEnumDone(LPARAM lParam)
{
    std::unique_ptr<EnumDone> done = m_guard.Take<EnumDone>(lParam);
    if (!done || done->cancelled)
    {
        return;
    }
    if (m_pending && m_pending->gen == done->gen)
    {
        if (SUCCEEDED(done->hr))
        {
            CommitPending(); // an empty folder: no batch arrived
        }
        else
        {
            // Nothing was committed: the previous folder stays, with its generation, so
            // its icons are still accepted (FR-020).
            const std::wstring target =
                m_pending->location.ParsingPath().value_or(m_pending->location.DisplayName());
            const bool fromAddress = m_pending->fromAddressBar;
            m_pending.reset();
            m_guard.Rewind(m_fileView.CurrentGeneration());
            ShowNavigationError(done->hr, target);
            if (fromAddress && m_addressBar.IsEditing())
            {
                m_addressBar.ShowError(
                    FormatWith2(m_options.locationErrorFmt, target, HresultMessage(done->hr)));
            }
        }
    }
    else if (FAILED(done->hr) && done->gen == m_fileView.CurrentGeneration())
    {
        // Failed part-way through a listing already on screen (e.g. a network drop).
        ShowNavigationError(done->hr, m_current.ParsingPath().value_or(m_current.DisplayName()));
    }
    // A refresh after a file operation: select the same items again (T072).
    if (m_reselect && m_reselect->gen == done->gen)
    {
        if (SUCCEEDED(done->hr) && done->gen == m_fileView.CurrentGeneration())
        {
            const auto realName = [](const FileItem& item) -> const std::wstring& {
                return item.info.editName.empty() ? item.info.name : item.info.editName;
            };
            const Reselect reselect = std::move(*m_reselect);
            m_fileView.RestoreSelection(
                [&](const FileItem& item) { return reselect.selected.contains(realName(item)); },
                [&](const FileItem& item) {
                    return reselect.focused && *reselect.focused == realName(item);
                });
        }
        m_reselect.reset();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnIconReady(LPARAM lParam)
{
    // Icons have their own generation (T081): accepted only for the one the list is on.
    std::unique_ptr<IconReady> icon = TakeOwned<IconReady>(lParam);
    if (icon && icon->gen == m_fileView.IconGeneration())
    {
        m_iconQueue.push_back(std::move(icon)); // converted with the device context in Render
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::ConvertPendingIcons(ID2D1DeviceContext* dc)
{
    // Bitmaps belong to one Direct2D device: after a device loss the rows' icons (made on
    // the old one) are dropped and requested again, extracted (T087).
    wil::com_ptr<ID2D1Device> device;
    dc->GetDevice(device.put());
    if (m_iconCache.SetDevice(device.get()))
    {
        const Generation iconGen = m_guard.Issue();
        m_fileView.ResetIcons(iconGen, false);
        m_icons.CancelOlderThan(iconGen);
    }
    // At most IconCache::kMaxConversionsPerFrame conversions; the rest in the next frame.
    const bool more = m_iconCache.Drain(
        dc, m_fileView.IconGeneration(), m_iconQueue,
        [this](const IconReady& result, wil::com_ptr<ID2D1Bitmap1> bitmap, bool extractAgain) {
            if (extractAgain)
            {
                m_fileView.RequestIconAgain(result.gen, result.itemKey);
            }
            else
            {
                m_fileView.SetIcon(result.gen, result.itemKey, std::move(bitmap)); // null = fallback
            }
        });
    if (more)
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::ShowNavigationError(HRESULT hr, const std::wstring& target)
{
    m_statusBar.SetTransientMessage(FormatWith2(m_options.locationErrorFmt, target, HresultMessage(hr)),
                                    kErrorNoticeMs);
}

void MainWindow::OpenItem(const FileItem& item)
{
    const HRESULT hr = m_navigator.Open(m_hwnd, m_current, item.info);
    if (hr == S_FALSE)
    {
        // A folder: browse into it.
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> child(
            reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(ILCombine(m_current.IdList(), item.info.childPidl.get())));
        ShellLocation location;
        if (child && SUCCEEDED_LOG(ShellLocation::FromIdList(child.get(), &location)))
        {
            NavigateTo(location, HistoryMode::Push);
        }
        return;
    }
    // A cancelled "Open with..." is the user's choice, not an error.
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION) &&
        hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        ShowNavigationError(hr, item.info.name);
    }
}

void MainWindow::NavigateFromAddress(std::wstring_view text)
{
    ShellLocation location;
    const HRESULT hr = m_navigator.Parse(text, &location);
    if (FAILED(hr))
    {
        // Invalid path: inline message under the field, the folder stays (UI §6, US3-6).
        // "Can't find" only for not-found errors; anything else (access denied, a device
        // not ready) says what actually went wrong.
        const bool notFound = hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
                              hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
                              hr == HRESULT_FROM_WIN32(ERROR_BAD_NETPATH) ||
                              hr == HRESULT_FROM_WIN32(ERROR_BAD_NET_NAME) ||
                              hr == HRESULT_FROM_WIN32(ERROR_INVALID_NAME) || hr == E_INVALIDARG;
        const std::wstring message =
            notFound ? FormatWith(m_options.pathNotFoundFmt, std::wstring(text))
                     : FormatWith2(m_options.locationErrorFmt, std::wstring(text), HresultMessage(hr));
        m_addressBar.ShowError(message);
        m_statusBar.SetTransientMessage(message, kErrorNoticeMs);
        return;
    }
    if (IsFolder(location))
    {
        m_navigatingFromAddress = true;
        NavigateTo(location, HistoryMode::Push);
        m_navigatingFromAddress = false;
        SetKeyboardFocus(Focus::Files); // Enter moves on to the list, as in Explorer
        return;
    }
    // A file path: open it, as Explorer does, and stay in this folder.
    if (auto parent = location.Parent())
    {
        ShellItemInfo item;
        item.name = location.DisplayName();
        item.childPidl.reset(reinterpret_cast<ITEMID_CHILD*>(ILClone(ILFindLastID(location.IdList()))));
        m_addressBar.CancelEdit();
        const HRESULT openHr = m_navigator.Open(m_hwnd, *parent, item);
        if (FAILED(openHr) && openHr != HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION) &&
            openHr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            ShowNavigationError(openHr, item.name);
        }
    }
}

void MainWindow::UpdateNavigationChrome()
{
    m_addressBar.SetLocation(m_current);
    m_navPane.SetCurrent(m_current);
    m_toolbar.SetState(m_history.CanBack(), m_history.CanForward(), m_current.Parent().has_value());
    const std::wstring title = FormatWith(m_options.windowTitleFmt, m_current.DisplayName());
    SetWindowTextW(m_hwnd, title.c_str());
    m_titleBar.SetTitle(title);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SetKeyboardFocus(Focus focus)
{
    m_focus = focus;
    m_fileView.SetFocused(focus == Focus::Files);
    m_navPane.SetFocused(focus == Focus::Places);
    m_titleBar.SetPickerFocused(focus == Focus::Picker, m_focusCuesVisible);
    InvalidateRect(m_hwnd, nullptr, FALSE); // the picker's ring
    if (focus == Focus::Address)
    {
        m_addressBar.BeginEdit(); // selects all (Ctrl+L / Alt+D / F4)
        return;
    }
    if (m_hwnd && GetFocus() != m_hwnd)
    {
        SetFocus(m_hwnd); // takes it from the address edit, which then leaves edit mode
    }
}

void MainWindow::CycleFocus(bool backwards)
{
    // F6 / Shift+F6 (UI §4, T080): picker -> address bar -> navigation pane -> file list.
    constexpr Focus order[] = {Focus::Picker, Focus::Address, Focus::Places, Focus::Files};
    const Focus current = m_addressBar.IsEditing() ? Focus::Address : m_focus;
    std::size_t index = 0;
    while (order[index] != current)
    {
        ++index;
    }
    const std::size_t next =
        backwards ? (index + std::size(order) - 1) % std::size(order) : (index + 1) % std::size(order);
    SetKeyboardFocus(order[next]);
}

D2D1_POINT_2F MainWindow::ToDip(LPARAM lParam) const noexcept
{
    const float scale = static_cast<float>(m_dpi.Dpi()) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    return D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)) / scale,
                         static_cast<float>(GET_Y_LPARAM(lParam)) / scale);
}

bool MainWindow::OnMouse(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN: {
        const D2D1_POINT_2F point = ToDip(lParam);
        if (m_toolbar.OnPointerDown(point))
        {
            SetCapture(m_hwnd);
            return true;
        }
        if (m_addressBar.OnPointerDown(point))
        {
            m_focus = Focus::Address;
            m_fileView.SetFocused(false);
            m_navPane.SetFocused(false);
            return true;
        }
        if (m_filterBox.OnPointerDown(point)) // T092
        {
            m_fileView.SetFocused(false);
            m_navPane.SetFocused(false);
            return true;
        }
        if (m_navPane.OnPointerDown(point))
        {
            SetKeyboardFocus(Focus::Places);
            return true;
        }
        if (m_fileView.OnPointerDown(point, (wParam & MK_CONTROL) != 0, (wParam & MK_SHIFT) != 0))
        {
            SetKeyboardFocus(Focus::Files);
            if (m_fileView.Dragging())
            {
                SetCapture(m_hwnd);
            }
            return true;
        }
        return false;
    }
    case WM_RBUTTONDOWN:
        // Select what the context menu will be for (T072); WM_RBUTTONUP then reaches
        // DefWindowProc, which sends WM_CONTEXTMENU.
        if (m_fileView.SelectForContextMenu(ToDip(lParam)))
        {
            SetKeyboardFocus(Focus::Files);
            return true;
        }
        return false;
    case WM_LBUTTONDBLCLK:
        if (m_fileView.OnDoubleClick(ToDip(lParam)))
        {
            return true;
        }
        return OnMouse(WM_LBUTTONDOWN, wParam, lParam); // elsewhere, a second click
    case WM_MOUSEMOVE: {
        if (!m_trackingLeave)
        {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, m_hwnd, 0};
            m_trackingLeave = TrackMouseEvent(&track) != FALSE;
        }
        const D2D1_POINT_2F point = ToDip(lParam);
        m_toolbar.OnPointerMove(point);
        m_addressBar.OnPointerMove(point);
        m_navPane.OnPointerMove(point);
        return m_fileView.OnPointerMove(point); // true while dragging a divider or the thumb
    }
    case WM_LBUTTONUP: {
        const D2D1_POINT_2F point = ToDip(lParam);
        const bool toolbar = m_toolbar.OnPointerUp(point);
        const bool files = m_fileView.OnPointerUp(point);
        if (GetCapture() == m_hwnd)
        {
            ReleaseCapture();
        }
        return toolbar || files;
    }
    case WM_MOUSELEAVE:
        m_trackingLeave = false;
        m_toolbar.OnPointerLeave();
        m_addressBar.OnPointerLeave();
        m_navPane.OnPointerLeave();
        return false; // the title bar's picker also listens
    case WM_MOUSEWHEEL: {
        POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(m_hwnd, &screen);
        const D2D1_POINT_2F point = ToDip(MAKELPARAM(screen.x, screen.y));
        const D2D1_RECT_F& pane = m_layout.navigationPane;
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (point.x >= pane.left && point.x < pane.right && point.y >= pane.top && point.y < pane.bottom)
        {
            return m_navPane.OnWheel(delta);
        }
        return m_fileView.OnWheel(delta);
    }
    default:
        return false;
    }
}

bool MainWindow::OnKeyDown(UINT vk)
{
    // Only arrives while the main window has focus, so never while typing in the
    // address bar: Backspace there edits text instead of going back (UI §4).
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    switch (vk)
    {
    case VK_BACK:
        GoBack();
        return true;
    case VK_F6:
        CycleFocus(shift);
        return true;
    default:
        break;
    }
    if (m_focus == Focus::Address)
    {
        // The address edit has closed (Escape or focus loss) and handed the keyboard back
        // to the window: the file list takes it, as in Explorer.
        SetKeyboardFocus(Focus::Files);
    }
    switch (m_focus)
    {
    case Focus::Files:
        return OnFileListKey(vk, ctrl, shift) || m_fileView.OnKeyDown(vk, ctrl, shift);
    case Focus::Places:
        return m_navPane.OnKeyDown(vk);
    case Focus::Picker:
        // Enter and Space open the appearance popup, as a click does (T080).
        if (vk == VK_RETURN || vk == VK_SPACE)
        {
            TogglePicker();
            return true;
        }
        return false;
    case Focus::Address:
        break;
    }
    return false;
}

void MainWindow::UpdateFocusCues()
{
    m_focusCuesVisible = (SendMessageW(m_hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0;
    m_fileView.SetFocusVisible(m_focusCuesVisible);
    m_navPane.SetFocusVisible(m_focusCuesVisible);
    m_addressBar.SetFocusVisible(m_focusCuesVisible);
    m_titleBar.SetPickerFocused(m_focus == Focus::Picker, m_focusCuesVisible);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ShowFocusCues()
{
    if (!m_focusCuesVisible)
    {
        // DefWindowProc turns this into WM_UPDATEUISTATE for the window and its children.
        SendMessageW(m_hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
    }
}

bool MainWindow::OnCommand(UINT id)
{
    switch (id)
    {
    case IDM_OPEN_APPEARANCE:
        TogglePicker();
        return true;
    case IDM_BACK:
        GoBack();
        return true;
    case IDM_FORWARD:
        GoForward();
        return true;
    case IDM_UP:
        GoUp();
        return true;
    case IDM_REFRESH:
        RefreshFolder();
        return true;
    case IDM_FOCUS_ADDRESS:
        SetKeyboardFocus(Focus::Address);
        return true;
    case IDM_FOCUS_FILTER:
        // Ctrl+F (T092): the list loses the keyboard focus to the filter's edit.
        m_fileView.SetFocused(false);
        m_navPane.SetFocused(false);
        m_filterBox.Focus();
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// File operations (T072; research R-08; UI contract §4, §6)
// ---------------------------------------------------------------------------

std::vector<const ShellItemInfo*> MainWindow::SelectedInfos() const
{
    std::vector<const ShellItemInfo*> infos;
    for (const FileItem* item : m_fileView.Selection())
    {
        infos.push_back(&item->info);
    }
    return infos;
}

std::optional<ShellLocation> MainWindow::LocationOf(const FileItem& item) const
{
    if (!m_current.IsValid() || !item.info.childPidl)
    {
        return std::nullopt;
    }
    // ILCombine returns an __unaligned pointer on x64; the allocation itself is aligned.
    const wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> full(
        reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(ILCombine(m_current.IdList(), item.info.childPidl.get())));
    ShellLocation location;
    if (!full || FAILED_LOG(ShellLocation::FromIdList(full.get(), &location)))
    {
        return std::nullopt;
    }
    return location;
}

std::uint64_t MainWindow::SubmitFileOperation(FileOpRequest request)
{
    const std::uint64_t id = m_nextOpId++;
    request.id = id;

    RunningOperation op;
    op.kind = request.kind;
    op.destination = request.destination;
    const auto addFolder = [&op](std::optional<ShellLocation> folder) {
        if (folder && std::none_of(op.sourceFolders.begin(), op.sourceFolders.end(),
                                   [&](const ShellLocation& known) { return known == *folder; }))
        {
            op.sourceFolders.push_back(std::move(*folder));
        }
    };
    if (request.dataObject)
    {
        // A paste: the items (and where they come from) are in the data object.
        wil::com_ptr<IShellItemArray> items;
        DWORD count = 0;
        if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(request.dataObject.get(), IID_PPV_ARGS(&items))) &&
            SUCCEEDED(items->GetCount(&count)) && count > 0)
        {
            op.total = count;
            wil::com_ptr<IShellItem> first;
            wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
            if (SUCCEEDED(items->GetItemAt(0, &first)) &&
                SUCCEEDED(SHGetIDListFromObject(first.get(), wil::out_param(pidl))) && pidl)
            {
                ShellLocation location;
                if (SUCCEEDED(ShellLocation::FromIdList(pidl.get(), &location)))
                {
                    addFolder(location.Parent());
                }
            }
        }
        op.pastedCut = request.kind == FileOpKind::Move;
    }
    else
    {
        op.total = request.sources.size();
        for (const ShellLocation& source : request.sources)
        {
            addFolder(source.Parent());
        }
    }

    // Persistent until the operation ends; the window stays responsive meanwhile (US4-6).
    m_statusBar.SetOperationMessage(m_options.fileOpText.Progress(op.kind, op.total));
    m_ops.emplace(id, std::move(op));
    m_fileOps->Submit(m_hwnd, std::move(request));
    return id;
}

bool MainWindow::OnFileListKey(UINT vk, bool ctrl, bool shift)
{
    if (m_fileView.IsRenaming())
    {
        return false;
    }
    switch (vk)
    {
    case VK_DELETE:
        DeleteSelection(shift); // Shift+Delete: permanent, with the Shell's confirmation
        return true;
    case 'C':
    case 'X':
        if (ctrl && !shift)
        {
            CopySelection(vk == 'X');
            return true;
        }
        return false;
    case 'V':
        if (ctrl && !shift)
        {
            Paste();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void MainWindow::DeleteSelection(bool permanent)
{
    FileOpRequest request;
    request.kind = permanent ? FileOpKind::DeletePermanent : FileOpKind::Recycle;
    for (const FileItem* item : m_fileView.Selection())
    {
        if (auto location = LocationOf(*item))
        {
            request.sources.push_back(std::move(*location));
        }
    }
    if (!request.sources.empty())
    {
        SubmitFileOperation(std::move(request));
    }
}

void MainWindow::CopySelection(bool cut)
{
    const auto items = SelectedInfos();
    if (items.empty())
    {
        return;
    }
    if (const HRESULT hr = m_clipboard.CopyToClipboard(m_current, items, cut); FAILED(hr))
    {
        m_statusBar.SetTransientMessage(MakeStatus(hr, {}).message, kErrorNoticeMs);
    }
}

void MainWindow::Paste()
{
    FileOpRequest request;
    const HRESULT hr = Clipboard::GetPasteRequest(m_current, &request);
    if (hr == DV_E_FORMATETC || hr == CLIPBRD_E_CANT_OPEN)
    {
        return; // nothing to paste (text or an image), as in Explorer
    }
    if (FAILED(hr))
    {
        m_statusBar.SetTransientMessage(MakeStatus(hr, {}).message, kErrorNoticeMs);
        return;
    }
    SubmitFileOperation(std::move(request));
}

bool MainWindow::OnContextMenu(LPARAM lParam)
{
    POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const float scale = static_cast<float>(m_dpi.Dpi()) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    if (screen.x == -1 && screen.y == -1)
    {
        // Shift+F10 or the Menu key: at the focused row, or the list's top-left corner.
        if (m_focus != Focus::Files)
        {
            return false;
        }
        D2D1_POINT_2F anchor{m_layout.fileList.left + FileView::kCellPaddingDip,
                             m_layout.fileList.top + m_fileView.HeaderHeight()};
        if (const auto focus = m_fileView.SelectionState().FocusIndex())
        {
            const D2D1_RECT_F cell = m_fileView.NameCellRect(*focus);
            anchor = D2D1::Point2F(cell.left, cell.bottom);
        }
        screen = POINT{static_cast<LONG>(anchor.x * scale), static_cast<LONG>(anchor.y * scale)};
        ClientToScreen(m_hwnd, &screen);
    }
    else
    {
        POINT client = screen;
        ScreenToClient(m_hwnd, &client);
        const D2D1_POINT_2F point = ToDip(MAKELPARAM(client.x, client.y));
        const D2D1_RECT_F& list = m_layout.fileList;
        if (point.x < list.left || point.x >= list.right || point.y < list.top || point.y >= list.bottom)
        {
            return false; // not over the file list
        }
    }
    if (!m_current.IsValid())
    {
        return false;
    }

    const auto items = SelectedInfos(); // empty: the folder background
    const bool extended = GetKeyState(VK_SHIFT) < 0;
    const HRESULT hr = m_navigator.ShowContextMenu(m_hwnd, screen, m_current, items, extended);
    if (hr == kContextMenuRename)
    {
        m_fileView.BeginRename(); // "Rename" goes to inline rename (R-08)
    }
    else if (hr == S_OK)
    {
        // A Shell verb ran (delete, paste, new folder, ...). It runs synchronously, as no
        // CMIC_MASK_ASYNCOK is set, so the folder can be read again now.
        RefreshKeepingSelection();
    }
    else if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        m_statusBar.SetTransientMessage(MakeStatus(hr, {}).message, kErrorNoticeMs);
    }
    return true;
}

void MainWindow::OnFileOpItem(LPARAM lParam)
{
    const std::unique_ptr<FileOpItem> item = TakeOwned<FileOpItem>(lParam);
    const auto op = m_ops.find(item->opId);
    if (op == m_ops.end())
    {
        return;
    }
    // Skipped in the Shell's conflict dialog: not done (and not failed).
    if (SUCCEEDED(item->hr) && item->hr != COPYENGINE_S_USER_IGNORED)
    {
        ++op->second.completed;
    }
    if (item->kind != FileOpKind::Rename || !item->newName || !item->item.IsValid())
    {
        return;
    }
    // A successful rename: only now does the list show the new name (US4-3).
    for (const FileItem& row : m_fileView.Items())
    {
        const auto location = LocationOf(row);
        if (!location || !(*location == item->item))
        {
            continue;
        }
        const std::wstring oldName = row.info.editName.empty() ? row.info.name : row.info.editName;
        std::wstring shown = *item->newName;
        // Explorer hides this type's extension (the display name is the real name without
        // it): show the new name the same way until the refresh reads it again.
        const std::size_t dot = oldName.find_last_of(L'.');
        if (row.info.name != oldName && dot != std::wstring::npos && row.info.name == oldName.substr(0, dot))
        {
            if (const std::size_t newDot = shown.find_last_of(L'.');
                newDot != std::wstring::npos && newDot > 0)
            {
                shown.resize(newDot);
            }
        }
        op->second.renamed = std::pair{oldName, *item->newName};
        m_fileView.ApplyRename(row.key, std::move(shown), *item->newName);
        break;
    }
}

void MainWindow::OnFileOpDone(LPARAM lParam)
{
    const std::unique_ptr<FileOpDone> done = TakeOwned<FileOpDone>(lParam);
    const auto found = m_ops.find(done->opId);
    if (found == m_ops.end())
    {
        return;
    }
    const RunningOperation op = std::move(found->second);
    m_ops.erase(found);

    // The next operation still running keeps its message; otherwise the result replaces it.
    if (!m_ops.empty())
    {
        const RunningOperation& next = m_ops.begin()->second;
        m_statusBar.SetOperationMessage(m_options.fileOpText.Progress(next.kind, next.total));
    }
    else
    {
        m_statusBar.ClearOperationMessage();
    }
    m_statusBar.SetTransientMessage(m_options.fileOpText.Result(op.kind, done->state, op.completed, op.total),
                                    kErrorNoticeMs);

    if (op.pastedCut && done->state == FileOpFinalState::Succeeded)
    {
        m_clipboard.ClearIfCurrent(); // the moved files cannot be pasted again
    }

    // Refresh when the folder on screen was a source or the destination.
    const bool affectsCurrent =
        m_current.IsValid() &&
        ((op.destination && *op.destination == m_current) ||
         std::any_of(op.sourceFolders.begin(), op.sourceFolders.end(),
                     [&](const ShellLocation& folder) { return folder == m_current; }));
    if (affectsCurrent)
    {
        RefreshKeepingSelection(op.renamed);
    }

    if (done->state == FileOpFinalState::Failed || done->state == FileOpFinalState::PartiallySucceeded)
    {
        ShowOperationFailures(done->errors); // after the status, so both are visible
    }
}

void MainWindow::ShowOperationFailures(const std::vector<te::Status>& errors)
{
    constexpr std::size_t kListedInline = 10;
    const std::wstring& title = m_options.fileOpText.failedTitle;
    const std::wstring items = FileOpText::FailureList(errors, kListedInline);
    if (m_options.showOperationFailures)
    {
        m_options.showOperationFailures(m_hwnd, title, items);
        return;
    }
    // UI §6: a task dialog listing each failed item with its system error text; the full
    // list is under "See details" when it is long.
    const std::wstring all = FileOpText::FailureList(errors, errors.size());
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = m_hwnd;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = title.c_str();
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = title.c_str();
    config.pszContent = items.c_str();
    if (errors.size() > kListedInline)
    {
        config.pszExpandedInformation = all.c_str();
    }
    LOG_IF_FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr));
}

void MainWindow::RefreshKeepingSelection(const std::optional<std::pair<std::wstring, std::wstring>>& renamed)
{
    Reselect reselect;
    const auto realName = [&](const FileItem& item) {
        std::wstring name = item.info.editName.empty() ? item.info.name : item.info.editName;
        if (renamed && name == renamed->first)
        {
            name = renamed->second;
        }
        return name;
    };
    for (const FileItem* item : m_fileView.Selection())
    {
        reselect.selected.insert(realName(*item));
    }
    if (const auto focus = m_fileView.SelectionState().FocusIndex();
        focus && *focus < m_fileView.Items().size())
    {
        reselect.focused = realName(m_fileView.Items()[*focus]);
    }
    RefreshFolder();
    if (m_pending)
    {
        reselect.gen = m_pending->gen;
        m_reselect = std::move(reselect);
    }
}

// ---------------------------------------------------------------------------
// UI Automation (T074; research R-09; UI contract §5)
// ---------------------------------------------------------------------------

LRESULT MainWindow::OnGetObject(WPARAM wParam, LPARAM lParam)
{
    // While editing, the address field's EDIT is the "Address" element: AddressBar names it
    // with Dynamic Annotation, and the toolbar's own Address steps aside (T078, T083).
    if (!m_uiaRoot && FAILED_LOG(Microsoft::WRL::MakeAndInitialize<UiaRoot>(
                          &m_uiaRoot, m_hwnd, [this] { return AutomationChildren(); },
                          [this] { return AutomationFocus(); })))
    {
        return DefWindowProcW(m_hwnd, WM_GETOBJECT, wParam, lParam);
    }
    if (!m_uiaPicker)
    {
        UiaTitleBarButton::Host host;
        host.name = m_options.pickerAutomationName;
        host.screenRect = [this] { return m_titleBar.PickerScreenRect(m_hwnd); };
        host.isOpen = [this] { return m_picker && m_picker->IsOpen(); };
        // Posted, so Invoke returns at once and the popup opens from the message loop.
        host.requestToggle = [this] { PostMessageW(m_hwnd, WM_COMMAND, IDM_OPEN_APPEARANCE, 0); };
        host.hasFocus = [this] { return m_focus == Focus::Picker && GetFocus() == m_hwnd; };
        host.setFocus = [this] { SetKeyboardFocus(Focus::Picker); };
        LOG_IF_FAILED(Microsoft::WRL::MakeAndInitialize<UiaTitleBarButton>(&m_uiaPicker, m_uiaRoot.Get(),
                                                                           std::move(host)));
    }
    if (!m_uiaFileList)
    {
        UiaFileList::Host host;
        host.view = &m_fileView;
        host.name = m_options.fileListAutomationName;
        host.toScreen = [this](const D2D1_RECT_F& dip) { return DipToScreen(dip); };
        host.fromScreen = [this](double x, double y) { return ScreenToDip(x, y); };
        host.hasFocus = [this] { return m_focus == Focus::Files && GetFocus() == m_hwnd; };
        host.setFocus = [this] { SetKeyboardFocus(Focus::Files); };
        host.open = [this](std::size_t key, Generation gen) {
            PostMessageW(m_hwnd, WM_TE_UIA_OPEN_ITEM, static_cast<WPARAM>(key), static_cast<LPARAM>(gen));
        };
        LOG_IF_FAILED(
            Microsoft::WRL::MakeAndInitialize<UiaFileList>(&m_uiaFileList, m_uiaRoot.Get(), std::move(host)));
    }
    if (!m_uiaChrome)
    {
        auto chrome = std::make_shared<UiaChromeHost>();
        chrome->toolbar = &m_toolbar;
        chrome->address = &m_addressBar;
        chrome->pane = &m_navPane;
        chrome->status = &m_statusBar;
        chrome->toolbarName = m_options.toolbarAutomationName;
        chrome->addressName = m_options.addressAutomationName;
        chrome->paneName = m_options.paneAutomationName;
        chrome->toScreen = [this](const D2D1_RECT_F& dip) { return DipToScreen(dip); };
        chrome->fromScreen = [this](double x, double y) { return ScreenToDip(x, y); };
        chrome->toolbarBounds = [this] { return m_layout.toolbar; };
        chrome->invokeButton = [this](Toolbar::Button button) {
            static constexpr UINT kCommands[] = {IDM_BACK, IDM_FORWARD, IDM_UP, IDM_REFRESH};
            PostMessageW(m_hwnd, WM_COMMAND, kCommands[static_cast<std::size_t>(button)], 0);
        };
        chrome->navigateToText = [this](std::wstring text) {
            m_uiaPendingAddress = std::move(text);
            m_uiaPendingPlace.reset();
            PostMessageW(m_hwnd, WM_TE_UIA_NAVIGATE, 0, 0);
        };
        chrome->navigateToPlace = [this](const ShellLocation& place) {
            m_uiaPendingPlace = place;
            m_uiaPendingAddress.reset();
            PostMessageW(m_hwnd, WM_TE_UIA_NAVIGATE, 0, 0);
        };
        chrome->expandPlace = [this](std::uint64_t nodeId, bool expand) {
            PostMessageW(m_hwnd, WM_TE_UIA_EXPAND, static_cast<WPARAM>(nodeId), expand ? 1 : 0);
        };
        chrome->paneHasFocus = [this] { return m_focus == Focus::Places && GetFocus() == m_hwnd; };
        chrome->focusPane = [this] { SetKeyboardFocus(Focus::Places); };
        m_uiaChrome = chrome;
        LOG_IF_FAILED(MakeUiaToolbar(m_uiaRoot.Get(), chrome, &m_uiaToolbar));
        LOG_IF_FAILED(MakeUiaNavigationPane(m_uiaRoot.Get(), chrome, &m_uiaPane));
        LOG_IF_FAILED(MakeUiaStatusBar(m_uiaRoot.Get(), chrome, &m_uiaStatus));
    }
    // The first snapshot for the events (T079): without it, the first change after a client
    // connects would only be recorded, not announced.
    if (!m_uiaSnapshot.valid)
    {
        UpdateAutomationState();
    }
    return UiaReturnRawElementProvider(m_hwnd, wParam, lParam, m_uiaRoot.Get());
}

std::vector<IRawElementProviderFragment*> MainWindow::AutomationChildren()
{
    // UI §5 order. Components without a provider yet return null and are skipped: the
    // toolbar with the address (T078), the navigation pane (T078), the file list (T076)
    // and the status bar (T078).
    return {m_uiaPicker.Get(), m_uiaToolbar.Get(), m_uiaPane.Get(), m_uiaFileList.Get(), m_uiaStatus.Get()};
}

IRawElementProviderFragment* MainWindow::AutomationFocus()
{
    switch (m_focus)
    {
    case Focus::Files:
        return m_uiaFileList.Get();
    case Focus::Places:
        return m_uiaPane.Get();
    case Focus::Address:
        return nullptr; // the native EDIT while editing (its own UIA), else the window
    case Focus::Picker:
        return m_uiaPicker.Get();
    }
    return nullptr;
}

void MainWindow::UpdateAutomationState()
{
    if (!m_uiaRoot || !m_uiaRoot->IsConnected() || !m_uiaFileList)
    {
        return;
    }
    AutomationSnapshot now;
    now.valid = true;
    now.focus = m_focus;
    now.windowFocused = GetFocus() == m_hwnd;
    now.addressEditing = m_addressBar.IsEditing();
    const auto& items = m_fileView.Items();
    if (const auto focus = m_fileView.SelectionState().FocusIndex(); focus && *focus < items.size())
    {
        now.fileFocusKey = items[*focus].key;
    }
    now.paneFocus = m_navPane.FocusIndex();
    now.gen = m_fileView.CurrentGeneration();
    now.itemCount = items.size();
    for (const FileItem* item : m_fileView.Selection())
    {
        now.selectedKeys.push_back(item->key);
    }
    std::sort(now.selectedKeys.begin(), now.selectedKeys.end());

    const AutomationSnapshot before = std::exchange(m_uiaSnapshot, now);
    if (!before.valid || !UiaClientsAreListening())
    {
        return;
    }

    // The listing changed: its rows are re-read (throttled).
    if (now.gen != before.gen || now.itemCount != before.itemCount)
    {
        RaiseStructureChanged();
    }

    // Selection, within one listing (a new listing starts empty; the structure event covers it).
    if (now.gen == before.gen && now.selectedKeys != before.selectedKeys)
    {
        std::vector<std::size_t> added;
        std::vector<std::size_t> removed;
        std::set_difference(now.selectedKeys.begin(), now.selectedKeys.end(), before.selectedKeys.begin(),
                            before.selectedKeys.end(), std::back_inserter(added));
        std::set_difference(before.selectedKeys.begin(), before.selectedKeys.end(), now.selectedKeys.begin(),
                            now.selectedKeys.end(), std::back_inserter(removed));
        const auto raiseFor = [this](std::size_t key, EVENTID event) {
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> row;
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            const auto index = m_fileView.IndexOfKey(key);
            if (index && SUCCEEDED(m_uiaFileList->MakeRow(*index, &row)) && SUCCEEDED(row.As(&simple)))
            {
                LOG_IF_FAILED(UiaRaiseAutomationEvent(simple.Get(), event));
            }
        };
        constexpr std::size_t kMaxSelectionEvents = 20; // beyond that, one "invalidated"
        if (added.size() + removed.size() > kMaxSelectionEvents)
        {
            LOG_IF_FAILED(UiaRaiseAutomationEvent(m_uiaFileList.Get(), UIA_Selection_InvalidatedEventId));
        }
        else if (now.selectedKeys.size() == 1 && added.size() == 1)
        {
            raiseFor(added.front(), UIA_SelectionItem_ElementSelectedEventId);
        }
        else
        {
            for (const std::size_t key : added)
            {
                raiseFor(key, UIA_SelectionItem_ElementAddedToSelectionEventId);
            }
            for (const std::size_t key : removed)
            {
                raiseFor(key, UIA_SelectionItem_ElementRemovedFromSelectionEventId);
            }
        }
    }

    // Focus: the element that now has it, if it is one of ours (the address EDIT and the
    // appearance popup raise their own).
    const bool focusMoved = now.focus != before.focus || now.windowFocused != before.windowFocused ||
                            now.addressEditing != before.addressEditing ||
                            (now.focus == Focus::Files && now.fileFocusKey != before.fileFocusKey) ||
                            (now.focus == Focus::Places && now.paneFocus != before.paneFocus);
    if (focusMoved && now.windowFocused && !now.addressEditing)
    {
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> focused;
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
        if (SUCCEEDED(m_uiaRoot->GetFocus(&focused)) && focused && SUCCEEDED(focused.As(&simple)))
        {
            LOG_IF_FAILED(UiaRaiseAutomationEvent(simple.Get(), UIA_AutomationFocusChangedEventId));
        }
    }
}

void MainWindow::RaiseStructureChanged()
{
    if (!m_uiaFileList || !m_uiaRoot || !m_uiaRoot->IsConnected())
    {
        return;
    }
    // At most one per kUiaStructureIntervalMs: a burst of batches ends with one trailing event.
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG since = now - m_uiaLastStructure;
    if (since < kUiaStructureIntervalMs)
    {
        if (!m_uiaStructurePending)
        {
            m_uiaStructurePending = true;
            SetTimer(m_hwnd, kUiaStructureTimerId, static_cast<UINT>(kUiaStructureIntervalMs - since),
                     nullptr);
        }
        return;
    }
    m_uiaStructurePending = false;
    m_uiaLastStructure = now;
    if (!UiaClientsAreListening())
    {
        return;
    }
    // Clients registered on the list itself match its full ID; the short
    // {UiaAppendRuntimeId, n} form is not expanded here, and such handlers never saw the
    // event (found in T079).
    std::vector<int> runtimeId = FullRuntimeId(m_uiaFileList.Get());
    LOG_IF_FAILED(UiaRaiseStructureChangedEvent(m_uiaFileList.Get(), StructureChangeType_ChildrenInvalidated,
                                                runtimeId.data(), static_cast<int>(runtimeId.size())));
}

std::vector<int> MainWindow::FullRuntimeId(IRawElementProviderSimple* provider)
{
    // The element's runtime ID with the window's prefix, as clients know it.
    std::vector<int> runtimeId;
    HUIANODE node = nullptr;
    if (SUCCEEDED(UiaNodeFromProvider(provider, &node)) && node)
    {
        SAFEARRAY* ids = nullptr;
        if (SUCCEEDED(UiaGetRuntimeId(node, &ids)) && ids)
        {
            LONG lower = 0;
            LONG upper = -1;
            SafeArrayGetLBound(ids, 1, &lower);
            SafeArrayGetUBound(ids, 1, &upper);
            for (LONG i = lower; i <= upper; ++i)
            {
                int part = 0;
                SafeArrayGetElement(ids, &i, &part);
                runtimeId.push_back(part);
            }
            SafeArrayDestroy(ids);
        }
        UiaNodeRelease(node);
    }
    return runtimeId;
}

void MainWindow::RaisePaneStructureChanged()
{
    // The folder tree gained or lost rows (T090); expansions are user actions, so no
    // throttling is needed.
    if (!m_uiaPane || !m_uiaRoot || !m_uiaRoot->IsConnected() || !UiaClientsAreListening())
    {
        return;
    }
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> pane;
    if (FAILED(m_uiaPane.As(&pane)))
    {
        return;
    }
    std::vector<int> runtimeId = FullRuntimeId(pane.Get());
    LOG_IF_FAILED(UiaRaiseStructureChangedEvent(pane.Get(), StructureChangeType_ChildrenInvalidated,
                                                runtimeId.data(), static_cast<int>(runtimeId.size())));
}

void MainWindow::RaiseStatusAnnouncement()
{
    if (!m_uiaStatus || !m_uiaRoot || !m_uiaRoot->IsConnected() || !UiaClientsAreListening())
    {
        return;
    }
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> status;
    if (SUCCEEDED(m_uiaStatus.As(&status)))
    {
        LOG_IF_FAILED(UiaRaiseAutomationEvent(status.Get(), UIA_LiveRegionChangedEventId));
    }
}

UiaRect MainWindow::DipToScreen(const D2D1_RECT_F& dip) const
{
    const float scale = static_cast<float>(m_dpi.Dpi()) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    POINT origin{0, 0};
    ClientToScreen(m_hwnd, &origin);
    return UiaRect{origin.x + std::floor(dip.left * scale), origin.y + std::floor(dip.top * scale),
                   std::ceil((dip.right - dip.left) * scale), std::ceil((dip.bottom - dip.top) * scale)};
}

D2D1_POINT_2F MainWindow::ScreenToDip(double x, double y) const
{
    const float scale = static_cast<float>(m_dpi.Dpi()) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    POINT origin{0, 0};
    ClientToScreen(m_hwnd, &origin);
    return D2D1::Point2F(static_cast<float>(x - origin.x) / scale, static_cast<float>(y - origin.y) / scale);
}

void MainWindow::FreeMessagePayload(UINT msg, LPARAM lParam) noexcept
{
    switch (msg)
    {
    case WM_TE_ENUM_BATCH:
        TakeOwned<EnumBatch>(lParam).reset();
        break;
    case WM_TE_ENUM_DONE:
        TakeOwned<EnumDone>(lParam).reset();
        break;
    case WM_TE_ICON_READY:
        TakeOwned<IconReady>(lParam).reset();
        break;
    case WM_TE_FILEOP_ITEM:
        TakeOwned<FileOpItem>(lParam).reset();
        break;
    case WM_TE_FILEOP_DONE:
        TakeOwned<FileOpDone>(lParam).reset();
        break;
    case WM_TE_TREE_CHILDREN:
        TakeOwned<FolderChildren>(lParam).reset();
        break;
    default:
        break; // WM_TE_SETTINGS_CHANGED, WM_TE_BACKDROP_FAILED and WM_TE_UIA_* carry no payload
    }
}

std::size_t MainWindow::DrainPendingMessages(HWND hwnd) noexcept
{
    std::size_t drained = 0;
    MSG msg{};
    while (PeekMessageW(&msg, hwnd, WM_TE_FIRST, WM_TE_LAST, PM_REMOVE))
    {
        FreeMessagePayload(msg.message, msg.lParam);
        ++drained;
    }
    return drained;
}

} // namespace te
