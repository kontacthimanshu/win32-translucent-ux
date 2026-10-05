#pragma once

// The application's main window (T024): owns the custom frame, the render device,
// the text formats and the layout, and dispatches window messages in the order
// research R-03 requires.

#include <te/a11y/UiaChrome.h>
#include <te/a11y/UiaFileList.h>
#include <te/a11y/UiaRoot.h>
#include <te/a11y/UiaTitleBarButton.h>
#include <te/app/FileOpText.h>
#include <te/app/MainLayout.h>
#include <te/appearance/BackdropManager.h>
#include <te/appearance/ColorPicker.h>
#include <te/appearance/SlabPopup.h>
#include <te/appearance/ThemeManager.h>
#include <te/core/GenerationGuard.h>
#include <te/render/RenderDevice.h>
#include <te/render/TextFormats.h>
#include <te/settings/ISettingsStore.h>
#include <te/shell/Clipboard.h>
#include <te/shell/DirectoryEnumerator.h>
#include <te/shell/FileOperationService.h>
#include <te/shell/IconProvider.h>
#include <te/shell/ShellNavigator.h>
#include <te/ui/AddressBar.h>
#include <te/ui/FileView.h>
#include <te/ui/FilterBox.h>
#include <te/ui/IconCache.h>
#include <te/ui/NavigationHistory.h>
#include <te/ui/NavigationPane.h>
#include <te/ui/StatusBar.h>
#include <te/ui/Toolbar.h>
#include <te/window/CaptionHitTester.h>
#include <te/window/CustomTitleBar.h>
#include <te/window/DpiManager.h>

#include <windows.h>

#include <wil/resource.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace te
{

class MainWindow
{
  public:
    struct Options
    {
        HINSTANCE instance = nullptr;
        int iconResourceId = 0;    // IDI_APP from resources/resource.h
        std::wstring title;        // IDS_APP_TITLE
        bool quitOnDestroy = true; // post WM_QUIT when the window is destroyed
        // Overrides the requested backdrop mode (--backdrop, Debug builds only).
        std::optional<BackdropMode> requestedMode;
        // Status bar text (StatusBar::Strings::Load); English defaults otherwise.
        StatusBar::Strings statusStrings;
        // Registers the open appearance popup with the message loop for
        // IsDialogMessageW (Application::SetModelessDialog); nullptr when it closes.
        std::function<void(HWND)> registerModelessDialog;
        // Tests only: adjusts what the OS reported each time the capabilities are read, so
        // a settings change (text size, animation effects, dark mode, high contrast) can be
        // simulated without changing the user's settings (T082). Null in the application.
        std::function<void(RenderingCapabilities&)> adjustCapabilities;
        // Tests only: called with the name of each shutdown step as it runs (T088), so the
        // order can be checked. Null in the application.
        std::function<void(const wchar_t* step)> shutdownTrace;
        // Where the appearance settings are loaded from and saved to (T047). nullptr keeps
        // them in memory only (tests), so nothing touches the user's settings file. Must
        // outlive the window.
        ISettingsStore* settingsStore = nullptr;
        // Shown once in the status bar when the settings file was corrupt (IDS_SETTINGS_RESET).
        std::wstring settingsResetText = L"Appearance settings were reset";

        // Navigation (T064). When false (tests), the window neither fills the navigation
        // pane nor opens a folder, so it touches no Shell state.
        bool startShell = false;
        // The command-line folder (R-11); This PC when empty or not a folder.
        std::optional<std::wstring> initialPath;
        // IDS_WINDOW_TITLE_FMT and IDS_ERR_PATH_NOT_FOUND_FMT (FormatMessage, %1).
        // A fixed title: no %1, so the open folder is not shown in it.
        std::wstring windowTitleFmt = L"Inference Explorer - The PC";
        std::wstring pathNotFoundFmt = L"Windows can't find '%1'. Check the spelling and try again.";
        std::wstring locationErrorFmt = L"Can't open '%1': %2"; // IDS_ERR_LOCATION_FMT

        // File operations (T072). Status and dialog text (IDS_OP_*).
        FileOpText fileOpText;
        // Runs the operations; nullptr = the window's own FileOperationService. Tests
        // inject one; it must outlive the window.
        IFileOperationService* fileOperations = nullptr;
        // Shows the failed items of a PartiallySucceeded or Failed operation. nullptr =
        // TaskDialogIndirect; tests inject their own so no modal UI appears.
        std::function<void(HWND owner, const std::wstring& title, const std::wstring& items)>
            showOperationFailures;
        // Replaces TrackPopupMenuEx for context menus (tests); see ShellNavigator.
        ContextMenu::Track contextMenuTracker;
        // Replaces TrackPopupMenuEx for the breadcrumb chevron and overflow menus (tests, T091).
        ContextMenu::Track breadcrumbMenuTracker;

        // UI Automation name of the picker button (IDS_A11Y_PICKER_NAME, T075).
        std::wstring pickerAutomationName = L"Appearance and color";
        // UI Automation name of the file list (IDS_A11Y_FILE_LIST, T076).
        std::wstring fileListAutomationName = L"Items";
        // UI Automation names of the toolbar, address and navigation pane (T078).
        std::wstring toolbarAutomationName = L"Navigation";
        std::wstring addressAutomationName = L"Address";
        std::wstring paneAutomationName = L"Navigation pane";
        // UI Automation name of the inline-rename edit (IDS_A11Y_RENAME, T083).
        std::wstring renameAutomationName = L"Name";
        // The filter box's placeholder and UIA name (IDS_FILTER_PLACEHOLDER, T092).
        std::wstring filterPlaceholder = L"Filter";
    };

    // Initial client size in DIPs, scaled to the monitor's DPI, limited to this
    // share of the monitor's work area and centred in it.
    static constexpr float kInitialWidthDip = 1200.0f;
    static constexpr float kInitialHeightDip = 800.0f;
    static constexpr float kInitialMaxWorkAreaShare = 0.9f;

    explicit MainWindow(Options options);
    // Destroys the window if it still exists.
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Registers the window class (once per process), creates the window and
    // shows it with showCommand.
    HRESULT Create(int showCommand);

    [[nodiscard]] HWND Hwnd() const noexcept
    {
        return m_hwnd;
    }
    [[nodiscard]] UINT Dpi() const
    {
        return m_dpi.Dpi();
    }
    [[nodiscard]] const MainLayout& Layout() const noexcept
    {
        return m_layout;
    }
    [[nodiscard]] const EffectiveAppearance& Effective() const noexcept
    {
        return m_effective;
    }
    [[nodiscard]] const RenderingCapabilities& Capabilities() const noexcept
    {
        return m_capabilities;
    }
    [[nodiscard]] StatusBar& Status() noexcept
    {
        return m_statusBar;
    }
    // DPI-dependent state, for tests (T081).
    [[nodiscard]] const CustomTitleBar& TitleBar() const noexcept
    {
        return m_titleBar;
    }
    [[nodiscard]] const RenderDevice& Renderer() const noexcept
    {
        return m_render;
    }
    [[nodiscard]] const TextFormats* Text() const noexcept
    {
        return m_text.get();
    }
    [[nodiscard]] HICON SmallIcon() const noexcept
    {
        return m_iconSmall.get();
    }
    [[nodiscard]] const IconCache& FileIcons() const noexcept
    {
        return m_iconCache;
    }

    // Switches the requested backdrop mode at runtime (US1-2): re-resolves and
    // re-applies without recreating the window, so layout and navigation state stay.
    void SetRequestedMode(BackdropMode mode);

    // A change from the appearance popup (or Reset): applies at once and saves after
    // kSaveDelayMs without further changes (T047, R-10).
    void ApplyUserSettings(const AppearanceSettings& settings);
    [[nodiscard]] const AppearanceSettings& Settings() const noexcept
    {
        return m_settings;
    }
    [[nodiscard]] bool SavePending() const noexcept
    {
        return m_savePending;
    }

    static constexpr UINT_PTR kSaveTimerId = 1; // ID_TIMER_SAVE
    // The trailing StructureChanged event of a burst of listing changes (T079).
    static constexpr UINT_PTR kUiaStructureTimerId = 2;
    static constexpr UINT kUiaStructureIntervalMs = 250;
    // Frames of the navigation swing (Swing::kFrameIntervalMs apart) while it runs.
    static constexpr UINT_PTR kSwingTimerId = 3;
    static constexpr UINT kSaveDelayMs = 100;
    static constexpr UINT kSettingsResetNoticeMs = 8000;
    static constexpr UINT kErrorNoticeMs = 8000;

    // Opens the appearance popup under the picker button, or closes it if it is open
    // (picker click, Alt+Shift+C / IDM_OPEN_APPEARANCE).
    void TogglePicker();
    [[nodiscard]] ColorPicker* Picker() noexcept
    {
        return m_picker.get();
    }
    // Opens the slab-thickness popup under the slab button, or closes it if it is open.
    void ToggleSlabPopup();
    [[nodiscard]] SlabPopup* Slab() noexcept
    {
        return m_slabPopup.get();
    }

    // Navigation controller (T064). How a navigation affects the history once it
    // succeeds: a new entry, a step back or forward, or none (Refresh).
    enum class HistoryMode
    {
        Push,
        Back,
        Forward,
        Refresh,
    };
    // Starts enumerating `location` under a new generation. The view and the history
    // change only when the first batch (or an empty, successful result) arrives, so a
    // failure keeps the previous folder on screen (FR-020).
    void NavigateTo(const ShellLocation& location, HistoryMode mode);
    void GoBack();
    void GoForward();
    void GoUp();
    void RefreshFolder();

    [[nodiscard]] const ShellLocation& CurrentLocation() const noexcept
    {
        return m_current;
    }
    [[nodiscard]] bool NavigationPending() const noexcept
    {
        return m_pending.has_value();
    }
    [[nodiscard]] const NavigationHistory<ShellLocation>& History() const noexcept
    {
        return m_history;
    }
    [[nodiscard]] FileView& Files() noexcept
    {
        return m_fileView;
    }
    [[nodiscard]] AddressBar& Address() noexcept
    {
        return m_addressBar;
    }
    [[nodiscard]] FilterBox& Filter() noexcept
    {
        return m_filterBox;
    }
    [[nodiscard]] NavigationPane& Places() noexcept
    {
        return m_navPane;
    }
    [[nodiscard]] Toolbar& Buttons() noexcept
    {
        return m_toolbar;
    }

    // File operations (T072). Assigns the request an ID, shows "Copying N items…" in the
    // status bar until it ends, and hands it to the service. Returns the ID.
    std::uint64_t SubmitFileOperation(FileOpRequest request);
    [[nodiscard]] bool OperationsRunning() const noexcept
    {
        return !m_ops.empty();
    }

    // Keyboard focus ring (T080; UI §4): F6 / Shift+F6 cycle picker -> address bar ->
    // navigation pane -> file list.
    enum class Focus
    {
        Picker,
        Address,
        Places,
        Files,
    };
    [[nodiscard]] Focus KeyboardFocus() const noexcept
    {
        return m_addressBar.IsEditing() ? Focus::Address : m_focus;
    }
    // False while Windows hides keyboard focus cues (UISF_HIDEFOCUS, e.g. before any key
    // was pressed); any keyboard input shows them.
    [[nodiscard]] bool FocusCuesVisible() const noexcept
    {
        return m_focusCuesVisible;
    }

    // The UI Automation fragment root (T074), created on the first WM_GETOBJECT; null
    // before that and after WM_DESTROY.
    [[nodiscard]] UiaRoot* Automation() const noexcept
    {
        return m_uiaRoot.Get();
    }

#ifdef _DEBUG
    // WM_COPYDATA dwData for a --backdrop forwarded by a second Debug launch; the data is
    // one std::int32_t holding a BackdropMode (T037).
    static constexpr ULONG_PTR kCopyDataBackdrop = 0x54454244; // 'TEBD'
#endif

    // Frees the heap payload of a WM_TE_* message (contracts/component-interfaces.md,
    // "Cross-thread message contract"). Does nothing for other messages.
    static void FreeMessagePayload(UINT msg, LPARAM lParam) noexcept;
    // Removes every queued WM_TE_* message for hwnd and frees its payload.
    // Returns how many were removed.
    static std::size_t DrainPendingMessages(HWND hwnd) noexcept;

    static constexpr const wchar_t* kClassName = L"TranslucentExplorer.MainWindow";

  private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnSize(UINT widthPx, UINT heightPx);
    void OnDpiChanged(UINT dpi, const RECT& suggested);
    // WM_CLOSE and step 1 of WM_DESTROY (T088): no worker starts anything new for this
    // window; runs once.
    void CancelBackgroundWork();
    void TraceShutdown(const wchar_t* step) const;
    // Title bar and taskbar icons at the current DPI (WM_CREATE, WM_DPICHANGED).
    void LoadAppIcons();
    // The Windows text size (UISettings.TextScaleFactor, T082): rebuilds the text formats
    // and lays out again when it changed.
    void ApplyTextScale(float scale);
    void OnPaint();
    void OnDestroy();
    void UpdateLayout();
    void Render();
    // The file list, through an offscreen layer drawn in perspective while the
    // navigation swing runs, directly otherwise.
    void RenderFileView(ID2D1DeviceContext* dc);
    void StartSwing(bool hingeRight);
    void OnSwingTimer();

    // Appearance pipeline (T037): capabilities -> Resolve -> Apply.
    void RefreshAppearance(bool reprobe);
    void ApplyAppearance();
    void OnBackdropFailed(HRESULT hr);
    bool IsAppearanceSettingChange(WPARAM wParam, LPARAM lParam) const;
    void LoadSettings();
    void SaveNow();

    // Navigation (T064).
    void OnEnumBatch(LPARAM lParam);
    void OnEnumDone(LPARAM lParam);
    void OnIconReady(LPARAM lParam);
    void CommitPending();
    void ShowNavigationError(HRESULT hr, const std::wstring& target);
    void OpenItem(const FileItem& item);
    void NavigateFromAddress(std::wstring_view text);
    void UpdateNavigationChrome();
    void SetKeyboardFocus(Focus focus);
    void CycleFocus(bool backwards);
    // WM_UPDATEUISTATE: re-reads UISF_HIDEFOCUS and tells the components.
    void UpdateFocusCues();
    // After keyboard input: ask Windows to show focus cues (WM_CHANGEUISTATE).
    void ShowFocusCues();
    [[nodiscard]] D2D1_POINT_2F ToDip(LPARAM lParam) const noexcept;
    bool OnMouse(UINT msg, WPARAM wParam, LPARAM lParam);
    bool OnKeyDown(UINT vk);
    bool OnCommand(UINT id);
    void ConvertPendingIcons(ID2D1DeviceContext* dc);

    // UI Automation (T074).
    LRESULT OnGetObject(WPARAM wParam, LPARAM lParam);
    [[nodiscard]] std::vector<IRawElementProviderFragment*> AutomationChildren();
    [[nodiscard]] IRawElementProviderFragment* AutomationFocus();
    // Client DIPs <-> screen pixels, for the providers' bounds and hit tests.
    // UI Automation events (T079): compares what clients see with the last snapshot and
    // raises focus, selection, structure and live-region events for what changed.
    void UpdateAutomationState();
    void RaiseStructureChanged();
    // The folder tree's rows changed (T090).
    void RaisePaneStructureChanged();
    [[nodiscard]] static std::vector<int> FullRuntimeId(IRawElementProviderSimple* provider);
    void RaiseStatusAnnouncement();
    [[nodiscard]] UiaRect DipToScreen(const D2D1_RECT_F& dip) const;
    [[nodiscard]] D2D1_POINT_2F ScreenToDip(double x, double y) const;

    // File operations (T072).
    [[nodiscard]] std::vector<const ShellItemInfo*> SelectedInfos() const;
    [[nodiscard]] std::optional<ShellLocation> LocationOf(const FileItem& item) const;
    bool OnFileListKey(UINT vk, bool ctrl, bool shift);
    void DeleteSelection(bool permanent);
    void CopySelection(bool cut);
    void Paste();
    bool OnContextMenu(LPARAM lParam);
    void OnFileOpItem(LPARAM lParam);
    void OnFileOpDone(LPARAM lParam);
    void ShowOperationFailures(const std::vector<te::Status>& errors);
    // Refreshes the folder and, when the new listing is complete, selects and focuses the
    // same items again by their real names (a rename maps the old name to the new one).
    void RefreshKeepingSelection(const std::optional<std::pair<std::wstring, std::wstring>>& renamed = {});

    Options m_options;
    HWND m_hwnd = nullptr;

    DpiManager m_dpi;
    CaptionHitTester m_hitTester;
    CustomTitleBar m_titleBar{m_hitTester, m_dpi};
    RenderDevice m_render;
    std::unique_ptr<TextFormats> m_text;
    MainLayout m_layout;
    StatusBar m_statusBar;

    ThemeManager m_themes;
    BackdropManager m_backdrops;
    AppearanceSettings m_settings; // what is applied (includes a Debug --backdrop override)
    // A Debug --backdrop mode is for this session only (T047): while it is in effect,
    // saves write the mode that was loaded instead.
    bool m_modeOverridden = false;
    BackdropMode m_savedMode = BackdropMode::Mica;
    bool m_savePending = false;
    bool m_settingsFileWasCorrupt = false;
    RenderingCapabilities m_capabilities;
    EffectiveAppearance m_effective;
    bool m_backdropSupported = false; // last ProbeSystemBackdrop result
    std::unique_ptr<ColorPicker> m_picker;
    std::unique_ptr<SlabPopup> m_slabPopup;

    // Navigation (T064).
    ShellNavigator m_navigator;
    DirectoryEnumerator m_enumerator;
    IconProvider m_icons;
    GenerationGuard m_guard;
    NavigationHistory<ShellLocation> m_history;
    ShellLocation m_current; // the folder on screen (committed)
    struct PendingNavigation
    {
        ShellLocation location;
        HistoryMode mode = HistoryMode::Push;
        Generation gen = 0;
        bool fromAddressBar = false;
    };
    std::optional<PendingNavigation> m_pending;
    bool m_navigatingFromAddress = false;               // set while NavigateFromAddress runs
    bool m_backgroundCancelled = false;                 // CancelBackgroundWork ran (T088)
    ULONGLONG m_swingStart = 0;                         // GetTickCount64 at its start; 0 = none
    bool m_swingHingeRight = false;                     // Back swings from the right edge
    std::deque<std::unique_ptr<IconReady>> m_iconQueue; // converted in the next frames (T087)
    IconCache m_iconCache;                              // one bitmap per Shell icon and size
    Toolbar m_toolbar;
    AddressBar m_addressBar;
    FilterBox m_filterBox; // T092
    NavigationPane m_navPane;
    FileView m_fileView;
    Focus m_focus = Focus::Files;
    bool m_trackingLeave = false;
    bool m_focusCuesVisible = true; // !UISF_HIDEFOCUS (T080)

    // File operations (T072).
    FileOperationService m_fileOpService;
    IFileOperationService* m_fileOps = nullptr; // m_fileOpService or the injected one
    Clipboard m_clipboard;
    struct RunningOperation
    {
        FileOpKind kind = FileOpKind::Copy;
        std::size_t total = 0;
        std::size_t completed = 0;
        std::vector<ShellLocation> sourceFolders;
        std::optional<ShellLocation> destination;
        bool pastedCut = false; // a move from a cut: the clipboard is emptied afterwards
        std::optional<std::pair<std::wstring, std::wstring>> renamed; // old and new name
    };
    std::map<std::uint64_t, RunningOperation> m_ops; // in submission order
    std::uint64_t m_nextOpId = 1;
    struct Reselect
    {
        Generation gen = 0;
        std::set<std::wstring> selected; // real names (FileItem editName, else name)
        std::optional<std::wstring> focused;
    };
    std::optional<Reselect> m_reselect;

    Microsoft::WRL::ComPtr<UiaRoot> m_uiaRoot;             // UI Automation (T074)
    Microsoft::WRL::ComPtr<UiaTitleBarButton> m_uiaPicker; // T075
    Microsoft::WRL::ComPtr<UiaFileList> m_uiaFileList;     // T076
    std::shared_ptr<UiaChromeHost> m_uiaChrome;            // T078
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> m_uiaToolbar;
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> m_uiaPane;
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> m_uiaStatus;
    std::optional<std::wstring> m_uiaPendingAddress; // WM_TE_UIA_NAVIGATE targets
    std::optional<ShellLocation> m_uiaPendingPlace;
    struct AutomationSnapshot
    {
        bool valid = false;
        Focus focus = Focus::Files;
        bool windowFocused = false;
        bool addressEditing = false;
        std::optional<std::size_t> fileFocusKey;
        std::optional<std::size_t> paneFocus;
        Generation gen = 0;
        std::size_t itemCount = 0;
        std::vector<std::size_t> selectedKeys; // sorted
    };
    AutomationSnapshot m_uiaSnapshot;
    ULONGLONG m_uiaLastStructure = 0;
    bool m_uiaStructurePending = false;

    wil::unique_hicon m_iconLarge;
    wil::unique_hicon m_iconSmall;
};

} // namespace te
