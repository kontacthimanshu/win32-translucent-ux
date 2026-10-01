# Manual Checklist: Navigation and Listing (US3)

**Covers**: quickstart V-3a to V-3h; FR-009–011, FR-018–020; SC-005, SC-009; UI contract §1,
§4, §6; research R-06, R-08, R-11; constitution Principle IX (V-3h, browsing part).
**Build under test**: record the commit and configuration in the Build column.

Prerequisite: run `.\tools\New-TestData.ps1` once. It creates `%TEMP%\te-test` with `nested\a\b\c`,
`10k`, `readonly-acl` (listing denied), `unicode`, `longpath` (the deepest folder is 344 characters)
and `unknown.zzz`. Remove it afterwards with `.\tools\New-TestData.ps1 -Remove`.

## Part A — Automated evidence

Two sources, neither injects mouse or keyboard input:

- **In-process**: `tests/integration/MainWindowNavigationTests.cpp` (6 tests) runs the real
  `MainWindow` over `%TEMP%\te-test` and drives it with window messages: `WM_COMMAND` for the
  accelerators, `WM_KEYDOWN` for the list, header clicks as `WM_LBUTTONDOWN`/`UP`, and
  `WM_SETTEXT` + Enter in the address field.
- **Live app**: the Release build, started with `%TEMP%\te-test\nested`, driven the same way
  from outside the process, with screenshots.

No file was opened with an application; opening files is in Part B.

Last run: 2026-09-28, Windows 11 build 26200, 144 DPI (150%), light mode, Mica —
**12/12 pass**.

| # | Scenario | Check | Result | Build |
|---|----------|-------|--------|-------|
| A1 | V-3a | Typing `%TEMP%\te-test\nested` + Enter lists exactly the folder's entries, folder first; every row has Type and Date modified, files have a Size, folders none; the title names the folder; icons shown ([screenshot](../../specs/001-translucent-explorer/validation/us3-v3a-nested.png)) | Pass | Debug, Release |
| A2 | V-3b | Into `a`, `b`, `c` with Down/Enter; Back ×2 → `a`; Forward → `b`; Forward → `c`; Up → `b`; Back → `c`; Backspace → `b` | Pass | Debug, Release |
| A3 | V-3c | Header clicks on Name, Date modified, Type and Size, each in both directions: the folder stays first in all 8 orders. Names in natural order: `b.log, file1, file2, file10`, reversed when descending | Pass | Debug, Release |
| A4 | V-3d | `unknown.zzz` (no association, checked with `AssocQueryStringW`: `0x80070483`): the app's own "Windows can't open this file" prompt with **Open with…** ([screenshot](../../specs/001-translucent-explorer/validation/us3-v3d-no-association.png)); the main window answers in 6 ms while it is open; Cancel closes it and the folder stays; no `OpenWith.exe` started | Pass | Release |
| A5 | V-3e | An unmapped drive (`Z:\`): inline message under the field "Can't open 'Z:\': The system cannot find the drive specified."; list, folder and history unchanged ([screenshot](../../specs/001-translucent-explorer/validation/us3-v3e-unmapped-drive.png)) | Pass | Debug, Release |
| A6 | V-3e | `readonly-acl` (listing denied): status "Can't open '…\readonly-acl': Access is denied."; the previous folder stays, no history entry, nothing pending; the window still navigates afterwards ([screenshot](../../specs/001-translucent-explorer/validation/us3-v3e-access-denied.png)) | Pass | Debug, Release |
| A7 | V-3f | `10k` opened; during the first 1.4 s, 40 `WM_NULL` round trips interleaved with wheel scrolling, two resizes and opening and closing the picker: max 10.8 ms, mean 1.3 ms (a first run: max 7.5 ms). The list scrolled and loaded ([screenshot](../../specs/001-translucent-explorer/validation/us3-v3f-loaded.png)) | Pass | Release |
| A8 | V-3g | Ten Back/Forward commands between `10k` and `nested`, posted 30 ms apart. After **every** dispatched message the list held only items of the current generation and of the folder shown: 0 violations. Final state `nested`, its entries, address matching | Pass | Debug, Release |
| A9 | V-3h | `unicode`: the listed names equal the names on disk (emoji, CJK, Hebrew, Arabic, accents); Name ascending and descending are exact reverses; the Unicode folder opens. Emoji draw in color and right-to-left text renders correctly ([screenshot](../../specs/001-translucent-explorer/validation/us3-v3h-unicode.png)) | Pass | Debug, Release |
| A10 | V-3h | `longpath`: walked with the list through all 6 levels to the 344-character folder, which lists `deep-file.txt`; the same folder typed in the address bar opens and the address shows the long names ([screenshot](../../specs/001-translucent-explorer/validation/us3-v3h-longpath.png)) | Pass | Debug, Release |
| A11 | V-3h | `ShellNavigatorTests.ParsesPathsLongerThanMaxPath`: a path past `MAX_PATH`, with and without a typed `\\?\` prefix, parses to the same location | Pass | Debug, Release |
| A12 | T049–T052 | `NavigationHistoryTests` 12, `SortModelTests` 10, `SelectionModelTests` 8, `GenerationGuardTests` 9, `DirectoryEnumeratorTests` 5: 44/44 | Pass | Debug, Release |

## Part B — Manual (needs a person)

| # | Scenario | Step | Expected | Result | Build |
|---|----------|------|----------|--------|-------|
| B1 | V-3a | Click the address bar (or Ctrl+L), type `%TEMP%\te-test\nested`, Enter | Caret visible while typing (the T027 spike could not capture it); the folder lists with icons; the list has the keyboard focus afterwards | Pending | |
| B2 | V-3b | Repeat V-3b with the mouse: double-click folders, the toolbar Back, Forward and Up buttons; then with Alt+Left, Alt+Right, Alt+Up and Backspace | Same results as A2; the buttons disable when there is nowhere to go | Pending | |
| B3 | V-3c | Click each column header twice; drag a column divider | Sort arrow on the active column; folders first; divider drag resizes and does not sort | Pending | |
| B4 | V-3d | Double-click a `.txt` file in `nested` | The associated editor opens it (not automated: it would start an application on the tester's desktop) | Pending | |
| B5 | V-3d | Double-click `unknown.zzz`, then **Open with…** | The system app chooser appears; cancelling it leaves the window responsive (not automated: the chooser records file associations) | Pending | |
| B6 | V-3f | Open `10k`; while it loads, scroll with the wheel, drag the scrollbar, resize the window by its border, and open the picker with the mouse | No stall, no flicker of the caption buttons or picker; final count "10,000 items" | Pending (automated: A7) | |
| B7 | V-3g | Press Alt+Left / Alt+Right quickly ten times between `10k` and `nested` | The list always matches the address; items of the other folder never flash | Pending (automated: A8) | |
| B8 | V-3h | Open a Unicode-named `.txt` in `unicode`, and `deep-file.txt` in the deepest `longpath` folder | The editor opens each file | Pending | |
| B9 | All | Repeat A1, A9 and A10 visually at 100% and 200% scaling | Nothing clipped; names and the address readable | Pending (only 150% available) | |
