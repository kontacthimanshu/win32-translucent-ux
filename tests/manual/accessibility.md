# Manual Checklist: Accessibility (US5)

**Covers**: quickstart V-5a to V-5c; FR-015, FR-016; SC-004, SC-007; UI contract §4, §5;
research R-09; constitution Principle X.
**Build under test**: record the commit and configuration in the Build column.

Prerequisite: `.\tools\New-TestData.ps1` (creates `%TEMP%\te-test`). Remove it afterwards
with `.\tools\New-TestData.ps1 -Remove`.

Tools for Part B: Narrator (built in, Ctrl+Win+Enter), Accessibility Insights for Windows
(FastPass), and optionally Inspect (Windows SDK) to read UIA properties.

## Part A — Automated evidence

Three sources, none of which injects mouse or keyboard input or starts Narrator:

- **Keyboard** (`MainWindowFocusTests` 6, `FocusIndicatorTests` 5, plus the keyboard tests
  of US2–US4): keys are sent to the window as `WM_KEYDOWN` / accelerator `WM_COMMAND`, the
  way the message loop delivers them.
- **UI Automation through the real UIA client** (`UiaRootTests` 6, `UiaTitleBarButtonTests`
  4, `UiaFileListTests` 12, `UiaChromeTests` 9, `UiaEventsTests` 4): the properties,
  patterns and events Narrator reads.
- **Accessibility audit** (`UiaAuditTests` 4, T083): walks the UIA tree of the main window
  (idle, while editing the address, during an inline rename) and of the appearance popup,
  and applies checks modelled on FastPass's automated rules (Axe.Windows): names
  (`NameNotEmpty`, `NameNotWhiteSpace`, `NameReasonableLength`,
  `NameExcludesLocalizedControlType`), `ControlTypeValid`, `LocalizedControlTypeNotEmpty`,
  `BoundingRectangleNotNull`, `BoundingRectangleContainedInParent`, the patterns each
  control type needs (Button: Invoke / Toggle / ExpandCollapse; Edit: Value or Text;
  List: Selection; DataGrid: Grid and Table; ListItem / DataItem: SelectionItem; Slider:
  RangeValue; a TreeItem with children: ExpandCollapse), parent types (items in a List /
  DataGrid, HeaderItems in a Header, TreeItems in a Tree or TreeItem; the navigation pane
  is a Tree since T090),
  `IsKeyboardFocusableShouldBeTrue`, `SiblingUniqueAndFocusable`, and that the focused
  element has and accepts keyboard focus. The title bar is exempt: it is Windows' own
  non-client element and reports an empty name for every Win32 window (checked against a
  plain `WS_OVERLAPPEDWINDOW` window). This audit is not FastPass; C1–C2 remain the
  release check.

Last run: 2026-09-28, Windows 11 build 26200, 144 DPI (150%), light mode — **13/13 pass**.

| # | Scenario | Check | Result | Build |
|---|----------|-------|--------|-------|
| A1 | V-5a | F6 / Shift+F6 cycle picker → address → navigation pane → file list and wrap both ways, also from inside the address EDIT (`MainWindowFocusTest.F6Cycles…`, `ShiftF6GoesBackwards`) | Pass | Debug, Release |
| A2 | V-5a | Enter and Space on the focused picker open the popup (`EnterAndSpaceOnThePickerOpenThePopup`); the popup is keyboard-operable: mode radios, the 4 × 3 swatch grid with arrow keys, trackbars, Reset, Escape (`ColorPickerTests`) | Pass | Debug, Release |
| A3 | V-5a | Focus always visible: a 2-DIP indicator on the focused row, place, address field and picker, at least 3:1 against what it sits on, also on a selected row (`FocusIndicatorTests`; [row](../../specs/001-translucent-explorer/validation/us5-focus-row.png), [picker](../../specs/001-translucent-explorer/validation/us5-focus-picker.png), [address](../../specs/001-translucent-explorer/validation/us5-focus-address.png), [pane](../../specs/001-translucent-explorer/validation/us5-focus-pane.png)) | Pass | Debug, Release |
| A4 | V-5a | Hidden focus cues (`UISF_HIDEFOCUS`) are shown by any key or accelerator and not by a click, as in Windows (`KeyboardInputShowsHiddenFocusCues`, `AMouseClickDoesNotShowHiddenFocusCues`) | Pass | Debug, Release |
| A5 | V-5a | V-3b / V-4a flows by keyboard: Enter opens, Backspace / Alt+Up go up, Alt+Left / Alt+Right, F5, Ctrl+L / Alt+D / F4, F2 rename, Delete / Shift+Delete, Ctrl+C / Ctrl+X / Ctrl+V with the app's data objects, Shift+F10 / Menu key (`MainWindowNavigationTests`, `MainWindowFileOpsTests`) | Pass | Debug, Release |
| A6 | V-5b | The picker is a Button named "Appearance and color", AcceleratorKey "Alt+Shift+C", ExpandCollapseState Collapsed, and Expanded while the popup is open, with a property-changed event on each change (`UiaTitleBarButtonTests`) | Pass | Debug, Release |
| A7 | V-5b | The picker is keyboard-focusable, reports HasKeyboardFocus in the F6 ring and is the root's focused element (`ThePickerIsTheFocusedElementForUiAutomation`) | Pass | Debug, Release |
| A8 | V-5b | The file list is a DataGrid "Items"; each row a DataItem named after the file, with SelectionItem.IsSelected; arrow keys raise AutomationFocusChanged and ElementSelected on the new row (`UiaFileListTests`, `UiaEventsTest.ArrowKeysRaiseFocusAndElementSelected`) | Pass | Debug, Release |
| A9 | V-5b | Status messages are a polite live region (`UiaEventsTest.StatusMessagesAreAnnouncedAsALiveRegion`) | Pass | Debug, Release |
| A10 | V-5c | Audit, main window idle: 50 elements, no violations (`UiaAuditTest.TheMainWindowPassesTheAudit`) | Pass (after two fixes, see findings) | Debug, Release |
| A11 | V-5c | Audit while editing the address: one Edit "Address" (the native EDIT) with nothing below it (`TheAddressWhileEditingPassesTheAudit`) | Pass (after a fix) | Debug, Release |
| A12 | V-5c | Audit during an inline rename: the rename Edit is named "Name" (`AnInlineRenamePassesTheAudit`) | Pass (after a fix) | Debug, Release |
| A13 | V-5c | Audit of the appearance popup: 3 radios, 15 buttons (12 named swatches, Accent color, Custom…, Reset), 2 sliders, no violations (`UiaAuditPopup.TheAppearancePopupPassesTheAudit`) | Pass | Debug, Release |

Findings from the audit (fixed in T083):

1. **Address while editing listed a typeless "Address" element under the EDIT, level after
   level.** The property-override provider returned through the root's
   `IRawElementProviderHwndOverride` (T078) was also used by UIA as a nested element of its
   own. The EDIT is now named with Dynamic Annotation (`IAccPropServices`, new
   `te::AnnotateHwnd`), and the override provider is gone.
2. **The inline-rename EDIT had no name** (the open item from T078): a screen reader said
   only "edit". It is now named "Name" (`IDS_A11Y_RENAME`), with AutomationId `RenameEdit`.

## Part B — Manual (needs a person)

### B-a Keyboard only (V-5a)

Don't touch the mouse. Start `TranslucentExplorer.exe %TEMP%\te-test`.

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B1 | F6 repeatedly, then Shift+F6 | Focus moves picker → address → navigation pane → file list and back; the focused part always shows a visible 2-DIP indicator | Pending | |
| B2 | V-3b by keyboard: in the list, arrows to `nested`, Enter; Backspace; Alt+Left; Alt+Right; Ctrl+L, type `%TEMP%\te-test\unicode`, Enter | Each navigation happens; focus stays visible | Pending | |
| B3 | V-2c by keyboard: F6 to the picker, Enter; Tab / arrows through the popup; change mode, tint, opacity; Escape | Each change applies at once; focus visible inside the popup; Escape closes it and focus returns to the picker | Pending | |
| B4 | V-4a by keyboard: select a file, Ctrl+C, go to another folder, Ctrl+V | Copied; the status bar reports it | Pending | |

### B-b Narrator (V-5b)

Start Narrator (Ctrl+Win+Enter). Stop it again afterwards (same keys).

Expected announcements (Narrator's exact wording can differ slightly between Windows
versions; the parts in the second column must all be there):

| # | Action | Narrator says (in this order) | Result | Build |
|---|--------|-------------------------------|--------|-------|
| B5 | F6 until the picker is focused | "Appearance and color, button, collapsed" (and may add "Alt+Shift+C") | Pending | |
| B6 | Enter | "expanded", then the popup's first control: "Mica, radio button, selected" (or the current mode) | Pending | |
| B7 | Escape | Back on the picker: "Appearance and color, button, collapsed" | Pending | |
| B8 | F6 to the file list, Down arrow | "Items, data grid" (first time only), then the row's name and "selected", e.g. "a, selected" | Pending | |
| B9 | Down arrow twice | Each row's name and "selected" | Pending | |
| B10 | Ctrl+Space on the focused row | "not selected" / "selected" as it toggles | Pending | |
| B11 | F2 | "Name, edit, <file name>" | Pending | |
| B12 | Ctrl+L | "Address, edit, <path>" | Pending | |
| B13 | Copy and paste a file | The status message is read out ("1 item copied") | Pending | |

### B-c Accessibility Insights FastPass (V-5c)

Open Accessibility Insights for Windows → FastPass. Target the window, run the automated
checks, then the tab-stops step (Tab / F6 through the window).

This is task T096, kept for release testing by the project owner's decision (2026-09-28):
the tool is not installed on the development machine. Also run it with the folder tree
expanded (T090), the breadcrumbs showing and a chevron menu open (T091), and the filter box
in use (Ctrl+F, T092), which were added after the in-repo audit's first run.

| # | Target | Automated checks: failures | Tab stops: issues | Result | Build |
|---|--------|---------------------------|-------------------|--------|-------|
| C1 | Main window over `%TEMP%\te-test` (idle; then while editing the address; then during F2 rename) | expected 0 | expected none | Pending | |
| C2 | Appearance popup (open with Alt+Shift+C) | expected 0 | expected none | Pending | |

Record each failure's rule name and element here if any are found, and file the fix
against US5.
