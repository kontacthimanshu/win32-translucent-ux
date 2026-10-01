# Quickstart & Validation Guide: Translucent Explorer

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

This guide shows how to build the application, run it and prove that each user story
works from start to finish. The expected behavior is defined in:

- [contracts/ui-contract.md](./contracts/ui-contract.md)
- [data-model.md](./data-model.md)

This guide refers to those files rather than repeating them.

## Prerequisites

| Requirement | Version |
|-------------|---------|
| Windows 11 | Build 22621 or later for the full backdrop checks; an earlier build or Windows 10 is used only for the fallback check (V-1c) |
| Visual Studio | 2026, or 2022 version 17.8 or later, with the **Desktop development with C++** workload. The `x64-*` presets use Visual Studio 2026; on a Visual Studio 2022 machine use the `vs2022-x64-*` presets instead. |
| Windows SDK | 10.0.22621.0 or later |
| CMake | 3.28 or later (the copy bundled with Visual Studio is fine) |
| vcpkg | The copy bundled with Visual Studio (`VCPKG_ROOT` set), or a standalone clone |
| Test tools | Accessibility Insights for Windows, Narrator, Application Verifier (Windows SDK), the Colour Contrast Analyser (TPGi) or an equivalent contrast checker, and a second monitor or a virtual display set to a different scale |

## Build and test

Run these from a "Developer PowerShell for VS 2022" prompt at the repository root:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug --output-on-failure            # unit tests
ctest --preset x64-debug -L integration --output-on-failure

cmake --preset x64-release
cmake --build --preset x64-release
```

**Expected result**:

- Both configurations build with **zero** warnings in application code, because `/W4 /WX`
  is on.
- All tests pass (SC-010, first part).

**Run the application**:

```powershell
.\out\build\x64-debug\src\Debug\TranslucentExplorer.exe             # opens This PC
.\out\build\x64-debug\src\Debug\TranslucentExplorer.exe "C:\Windows" # opens a given folder
.\out\build\x64-debug\src\Debug\TranslucentExplorer.exe --backdrop=acrylic  # Debug only
```

`--backdrop` works only in Debug builds. Running it again while the app is open switches
the mode of the running window ([ui-contract §4](./contracts/ui-contract.md)).

## Test data

```powershell
# Creates %TEMP%\te-test\ with: nested\a\b\c, 10k\ (10,000 files),
# conflicts\ (same names in src\ and dst\), readonly-acl\ (deny write for current user),
# unicode\ (names with emoji, CJK, RTL), and longpath\ (> 260 chars).
.\tools\New-TestData.ps1 -Root "$env:TEMP\te-test"
```

## Validation scenarios

Each scenario lists its steps, the expected outcome, and the spec items it covers.

### V-1 Translucent window (User Story 1 · FR-001–003, FR-008 · SC-001, SC-004)

**V-1a**:

1. Launch the application on build 22621 or later.
2. Expected: a resizable window opens with a Mica backdrop. The caption, toolbar,
   navigation pane and list show the desktop wallpaper influence. Text is readable.
3. Worst-case legibility (research R-05): in dark mode, set a plain white wallpaper,
   choose Acrylic with Surface opacity 0% and Tint strength 0%, and place the window over
   a white area. Repeat in light mode with a black wallpaper.
4. Expected: every text element (file names, address, navigation entries, status text,
   title) stays readable, because the app adds opacity behind the text only. The rest
   of the surface stays translucent. Spot-check contrast with the Colour Contrast
   Analyser or a similar tool: at least 4.5:1.
5. Selected rows: set a mid-brightness Windows accent color (for example gray, or a
   medium green), select several files, and repeat step 3.
6. Expected: the file names on selected rows also reach at least 4.5:1, and the
   selection highlight is still clearly visible against unselected rows.

**V-1b**:

1. Use the picker to switch between Acrylic, Mica and Solid.
2. Expected: each mode applies without restarting. The folder and selection are
   unchanged.

**V-1c** (fallback):

1. Turn off Windows Settings → Personalization → Colors → *Transparency effects*.
2. Expected: the window goes Solid and the status bar shows
   "(fallback: transparency effects off)".
3. Turn the setting back on.
4. Expected: the requested mode is restored with no restart.
5. On a build earlier than 22621, expected: Solid, with the Acrylic and Mica options
   disabled and the reason shown.

**V-1d**:

1. Drag, resize from every edge and corner, and double-click the caption.
2. Try Minimize, Maximize and Restore, Close, `Alt+Space`, `Win+Arrow` snapping, hovering
   Maximize (Snap Layouts), `Alt+Tab`, and the taskbar thumbnail.
3. Expected: everything behaves as it does for a native Windows 11 window.

**V-1e**:

1. Turn on high contrast (`Left Alt+Left Shift+Print Screen`).
2. Expected: the window becomes opaque with system colors. All text, the selection and
   focus are visible.

### V-2 Title-bar color picker (User Story 2 · FR-004–007, FR-022 · SC-002, SC-003)

**V-2a**:

1. Inspect the caption.
2. Expected: the picker is directly left of Minimize, with a visible gap of about 8 DIP.
3. Repeat in maximized and narrow windows. The result must be the same.

**V-2b**:

1. Activate the picker by clicking it, and separately with `Alt+Shift+C`.
2. Expected: the popup opens under the button. The caption buttons are neither moved nor
   disabled.

**V-2c**:

1. Choose "Teal".
2. Choose Custom… and pick `#8764B8`.
3. Move Tint strength to 50%.
4. Expected: the preview and the window update live.
5. Move Surface opacity from 0% to 60%.
6. Expected: the surfaces become more opaque, the tint color and strength are unchanged,
   and text stays readable.
7. Change the tint color again.
8. Expected: Surface opacity stays at 60% (the settings are independent, FR-006).

**V-2d**:

1. Select Solid.
2. Expected: Surface opacity and Tint strength are both disabled with the text
   "Solid mode is fully opaque" (FR-022).

**V-2e**:

1. Close and relaunch the application.
2. Expected: the mode, tint, tint strength and surface opacity are all restored.
3. Click Reset.
4. Expected: Mica, accent color, tint strength 20% and surface opacity 0%, with the
   custom colors kept.

**V-2f** (corrupt settings):

1. Replace `%LOCALAPPDATA%\TranslucentExplorer\settings.json` with `{not json`.
2. Relaunch.
3. Expected: defaults are used, a `settings.corrupt-*.json` backup exists, and the status
   bar shows the reset notice.

**V-2g** (invalid field):

1. Edit `settings.json` so that one field is invalid and another is valid. The settings
   are nested inside `appearance`:

   ```json
   { "schemaVersion": 1, "appearance": { "backdropMode": "Acrylic", "tintOpacity": 5 } }
   ```

2. Relaunch.
3. Expected: Acrylic is kept and the tint strength is clamped to 80%.

**V-2h**:

1. With the popup open, press Escape, or click the file list.
2. Expected: the popup closes. The file-list selection does not change.

### V-3 Navigation and listing (User Story 3 · FR-009–011, FR-018–020 · SC-005, SC-009)

**V-3a**:

1. Type `%TEMP%\te-test\nested` in the address bar and press Enter.
2. Expected: the Name, Date modified, Type and Size columns are shown, with icons.

**V-3b**:

1. Go into `a`, then `b`, then `c`.
2. Press Back twice.
3. Expected: `a`.
4. Press Forward.
5. Expected: `b`.
6. Press Up from `c`.
7. Expected: `b`. Pressing Back then returns to `c`.

**V-3c**:

1. Sort by each column and toggle the direction.
2. Expected: folders come first. Names use natural order (`file2` before `file10`).

**V-3d**:

1. Open a `.txt` file.
2. Expected: the associated editor launches.
3. Open a file with an unknown extension.
4. Expected: the "can't open" dialog with Open with…. The window stays responsive.

**V-3e**:

1. Navigate to `Z:\` (unmapped) and to `readonly-acl` with list permission denied.
2. Expected: a recoverable error. The previous folder stays shown.

**V-3f**:

1. Open `10k`.
2. While it loads, scroll, resize, and open the picker.
3. Expected: the UI responds throughout.

**V-3g**:

1. Quickly alternate between `10k` and `nested` ten times using Back and Forward.
2. Expected: the final list always matches the address bar. Items from the other folder
   never appear, not even briefly.

**V-3h** (long paths and Unicode names: browsing, constitution Principle IX):

1. Open `%TEMP%\te-test\unicode`.
2. Expected: every name (emoji, CJK, right-to-left) displays correctly, sorts, and opens.
3. Navigate into `%TEMP%\te-test\longpath` all the way to the deepest folder, using the
   address bar and the list.
4. Expected: each level lists correctly, and opening a file there works.

File operations on these folders are checked in V-4h, once they exist.

### V-4 File operations (User Story 4 · FR-012–014 · SC-006)

**V-4a**:

1. Copy three files with Ctrl+C and Ctrl+V into another folder.
2. Expected: the Shell progress UI appears as needed and the status shows
   "3 items copied".

**V-4b**:

1. Move `conflicts\src\*` to `conflicts\dst\`.
2. Expected: the Shell conflict dialog appears. Choose *Skip*.
3. Expected: the target files are unchanged (compare hashes) and the status reports the
   skipped items.

**V-4c**:

1. Press F2 on a file and type a name with `:`.
2. Expected: the name is rejected and the old name is kept.
3. Rename it to a valid name.
4. Expected: the list shows the new name only after success.

**V-4d**:

1. Press Delete.
2. Expected: the item goes to the Recycle Bin, as confirmed by the Recycle Bin contents.
3. Press Shift+Delete.
4. Expected: the Shell permanent-delete confirmation appears. Cancel.
5. Expected: the file is still present.

**V-4e**:

1. Copy into `readonly-acl`.
2. Expected: an access-denied error listing the failed items. No partial file is left
   behind.

**V-4f**:

1. Start a copy of `10k`, then cancel it in the Shell progress dialog.
2. Expected: the status shows "cancelled — N completed" and the window was responsive
   throughout.

**V-4g**:

1. Right-click a file, and separately press Shift+F10.
2. Expected: the native Shell context menu appears. Its "Properties" item works.

**V-4h** (long paths and Unicode names: file operations, constitution Principle IX):

1. In `%TEMP%\te-test\unicode`, rename a file to another Unicode name, then copy it to
   `nested`.
2. Expected: both operations succeed and the names are unchanged.
3. In the deepest folder of `%TEMP%\te-test\longpath`, rename a file, then copy a file
   out of that folder.
4. Expected: each operation either succeeds, or reports a clear path-length error from
   Windows. Nothing is silently truncated or lost.

### V-5 Interaction and accessibility (User Story 5 · FR-015–017 · SC-004, SC-007, SC-008)

**V-5a** (keyboard only):

1. Unplug the mouse or don't use it.
2. Complete V-3b, V-2c and V-4a with the keyboard alone, using the shortcuts in
   [ui-contract §4](./contracts/ui-contract.md).
3. Expected: focus is always visible.

**V-5b** (Narrator):

1. Focus the picker.
2. Expected: Narrator announces "Appearance and color, button, collapsed".
3. Open the picker.
4. Expected: "expanded".
5. Arrow through the file list.
6. Expected: each item's name and its selected state are announced.

**V-5c** (Accessibility Insights FastPass):

1. Run FastPass on the main window and on the popup.
2. Expected: zero failures.

**V-5d** (scaling):

1. At 100%, 150% and 200% scaling, repeat V-2a and V-1d.
2. Move the window between monitors with different scaling.
3. Expected: nothing is clipped or overlapping, and the picker stays aligned with
   Minimize.

**V-5e**:

1. While the application is running, switch Windows between light and dark mode, set
   Text size to 150%, and turn on *Animation effects: off*.
2. Expected: the application adapts without a restart.

### V-6 Responsiveness and resources (User Story 6 · FR-018–019 · SC-009, SC-010)

**V-6a**:

1. Run `ctest -L integration -R Enumerator`.
2. Expected: the 10,000-item enumeration, the mid-enumeration cancellation and the stale
   generation tests pass.

**V-6b**:

1. Run `ctest -R WindowLifecycle`.
2. Expected: after 50 create-and-destroy cycles, the GDI, USER and handle counts are back
   at baseline.
3. Expected: the Debug build reports no CRT leaks at exit.

**V-6c**:

1. Run the V-3 and V-4 scenarios under Application Verifier (Basics).
2. Expected: no stops.

## Phase exit mapping (spec "Delivery Slices")

| Phase | Must pass before moving on |
|-------|----------------------------|
| 1 | Build (both configurations), V-1d, V-5d (caption only), caption-layout unit test (picker area reserved), window lifecycle test |
| 2 | V-1a–c/e, V-2a–h (V-2a is the first check of the visible picker) |
| 3 | V-3a–h, V-6a |
| 4 | V-4a–h |
| 5 | V-5a–e, V-6b–c, then a full rerun of V-1 to V-6 |

**Recording evidence (SC-011)**: At each phase exit, add a section to
`specs/001-translucent-explorer/validation-report.md` with the date, commit, Windows build,
the scenarios run, pass or fail for each, and links to screenshots or test logs. Work does
not move to the next phase until that section shows every required check passing. The
last numbered task of each phase does this, so `/speckit-implement` runs it.

The report also has a **Sign-offs** section for decisions the project owner (the
repository maintainer) must approve. Each entry records the decision, the date, the
approver and a link to the evidence. Today there is one possible entry: the tinted
address-edit fallback, if the Phase 1 spike shows a translucent edit is not possible
(plan.md Complexity Tracking). A release cannot ship with an open sign-off.
