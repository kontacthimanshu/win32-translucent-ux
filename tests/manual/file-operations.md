# Manual Checklist: File Operations (US4)

**Covers**: quickstart V-4a to V-4h; FR-012–FR-014, FR-020; SC-006; UI contract §4, §6;
research R-08; constitution Principle IX.
**Build under test**: record the commit and configuration in the Build column.

Prerequisite: `.\tools\New-TestData.ps1` (creates `%TEMP%\te-test`, including
`conflicts\src` / `conflicts\dst`, `readonly-acl`, `10k`, `unicode` and `longpath`).
Remove it afterwards with `.\tools\New-TestData.ps1 -Remove`.

## Part A — Automated evidence

Three sources, none of which injects mouse or keyboard input or touches the tester's
clipboard:

- **Integration tests** (in every `ctest` run): `FileOperationServiceTests` (8),
  `ClipboardTests` (7), `ContextMenuTests` (6), `FileViewRenameTests` (8),
  `MainWindowFileOpsTests` (11), and the unit tests `FileOpTextTests` (5). They run on
  fresh `%TEMP%` trees; the window tests use a fake operation service except for a real
  rename and a real copy.
- **Shell-UI validation tests** (`tests/integration/FileOpsShellUiValidation.cpp`,
  `DISABLED_` so a normal run shows no dialogs). They run the production service with the
  production flags and answer the Shell's own dialogs through UI Automation (Invoke):

  ```powershell
  out\build\x64-release\tests\Release\te_integration_tests.exe --gtest_also_run_disabled_tests --gtest_filter=FileOpsShellUi.*
  ```

  The Recycle Bin test adds one uniquely named temp file to the Recycle Bin and removes
  exactly that item afterwards. The elevation "Continue" button is never chosen.
- **Live app** (T072): the real context menu on a file, dismissed with Escape.

Last run: 2026-09-28, Windows 11 build 26200, 144 DPI (150%), light mode — **16/16 pass**
(Shell-UI tests 6/6 in Release and Debug).

| # | Scenario | Check | Result | Build |
|---|----------|-------|--------|-------|
| A1 | V-4a | Three files copied through the window with the real service: "Copying 3 items…" while it runs, then "3 items copied"; sources kept (`MainWindowFileOpsTest.CopyingThreeFilesReportsThreeItemsCopied`) | Pass | Debug, Release |
| A2 | V-4a | Cut, then paste into another folder: the files move through the service, with the data object marshalled to its worker (`ClipboardTest.PastingACutMovesTheFiles`); the copy data object carries the Shell ID list and `CF_HDROP` | Pass | Debug, Release |
| A3 | V-4b | Move of 5 conflicting files: the Shell's **"Replace or Skip Files"** dialog appears; Skip → every target unchanged (content compared), sources kept, every item reported as skipped (`0x00270005`), none counted as done → status "0 of 5 items completed" | Pass | Debug, Release |
| A4 | V-4c | F2: the edit shows the real name with the extension, the name without it selected; `bad:name` → balloon tip, still editing, nothing submitted; a valid name is shown only after the rename succeeds (`FileViewRenameTests`, `MainWindowFileOpsTest.RenameFromTheList…`, `RealRenameEndToEnd`) ([edit](../../specs/001-translucent-explorer/validation/us4-rename-edit.png), [invalid](../../specs/001-translucent-explorer/validation/us4-rename-invalid.png)) | Pass | Debug, Release |
| A5 | V-4d | Delete → Recycle: the file is found in the Recycle Bin by its original path (Recycle Bin confirmation setting off on this machine, so no dialog), then removed from it by the test | Pass | Debug, Release |
| A6 | V-4d | Shift+Delete → the Shell's permanent-delete confirmation appears; "No" → the file is kept and the operation ends Cancelled. The window maps `Delete` / `Shift+Delete` to Recycle / DeletePermanent (`MainWindowFileOpsTest.DeleteRecyclesAndShiftDeleteDeletesPermanently`) | Pass | Debug, Release |
| A7 | V-4e | Copy into `readonly-acl`: the Shell's **"Destination Folder Access Denied"** dialog appears (no "Skip" offered for one item); Cancel → operation Cancelled, no file left in the folder. Without the Shell UI (test-only `FOF_NOERRORUI`), the item fails with `COPYENGINE_E_ACCESS_DENIED_DEST` and the window lists it (`FileOperationServiceTest.CopyIntoAFolderThatDeniesWriting…`, `MainWindowFileOpsTest.PartialFailureLists…`) | Pass | Debug, Release |
| A8 | V-4f | Copy of `10k`: the Shell's progress dialog appears and its cancel button ("Cancel tile") cancels after 729 of 10,000 files → Cancelled; the UI thread's longest pause during the copy 31 ms. The window's status for a cancel: "Operation cancelled — N items completed before cancellation" (`MainWindowFileOpsTest.StatusShowsProgressThenTheResult`) | Pass | Debug, Release |
| A9 | V-4f | "Copying N items…" is shown for the whole operation and replaced by the result, also with two operations queued (`MainWindowFileOpsTest.StatusShowsProgressThenTheResult`) | Pass | Debug, Release |
| A10 | V-4g | The Shell's context menu for a file offers Open, Cut, Copy, Delete, Rename and Properties; the folder background has its own menu; Shift adds extended verbs (`ContextMenuTests`). The real menu in the app ([screenshot](../../specs/001-translucent-explorer/validation/us4-context-menu.png)) | Pass | Release |
| A11 | V-4g | Properties from the context menu opens the Shell's **"props Properties"** sheet, closed again by the test | Pass | Debug, Release |
| A12 | V-4g | `Shift+F10` / the Menu key (`WM_CONTEXTMENU` at −1, −1) opens the menu at the focused row; right-click selects the row under the pointer; Rename from the menu starts inline rename (`MainWindowFileOpsTests`) | Pass | Debug, Release |
| A13 | V-4h | Unicode (emoji, CJK, Hebrew, Arabic) rename and copy round-trip exactly (`FileOperationServiceTest.UnicodeNamesRoundTrip…`, `MainWindowFileOpsTest.RealRenameEndToEnd` with a CJK name) | Pass | Debug, Release |
| A14 | V-4h | 344 characters deep: rename succeeds with the exact new name; copying a 65,543-byte file out gives identical content (`…RenameBeyondMaxPath…`, `…CopyFromBeyondMaxPath…`) | Pass | Debug, Release |
| A15 | Flags | Every kind: never `FOF_NOCONFIRMATION`, `FOF_NOERRORUI` or `FOF_RENAMEONCOLLISION`; always `FOFX_ADDUNDORECORD`; Recycle adds `FOF_ALLOWUNDO` and `FOF_WANTNUKEWARNING`; the service sets exactly these, once, with an owner window | Pass | Debug, Release |
| A16 | T067 | `FileOperationServiceTests` 8/8 (T067's 6 plus Move and the long-path rename) | Pass | Debug, Release |

## Part B — Manual (needs a person)

Hash commands for V-4b (run before and after; the two lists must be identical):

```powershell
Get-ChildItem "$env:TEMP\te-test\conflicts\dst" | Get-FileHash -Algorithm SHA256 | Format-Table Hash, Path
```

Recycle Bin check for V-4d: open the Recycle Bin in Explorer (or
`(New-Object -ComObject Shell.Application).NameSpace(10).Items() | Select Name`) and
confirm the deleted file is listed with its original location.

| # | Scenario | Step | Expected | Result | Build |
|---|----------|------|----------|--------|-------|
| B1 | V-4a | Select three files in `nested`, **Ctrl+C**, go to `nested\a`, **Ctrl+V** | "Copying 3 items…" then "3 items copied"; the files appear in the list | Pending (automated without the clipboard: A1, A2) | |
| B2 | V-4a | **Ctrl+X** on a file, **Ctrl+V** elsewhere, then **Ctrl+V** again | Moved; the second paste does nothing (the clipboard was emptied) | Pending | |
| B3 | V-4b | Hash `conflicts\dst`; select all in `conflicts\src`, Ctrl+X, paste in `dst`; choose **Skip** | Hashes identical; status "0 of 5 items completed" | Pending (automated: A3) | |
| B4 | V-4c | F2 on a file, type `a:b`, Enter; then a valid name | Balloon tip, old name kept; the new name appears only after the rename | Pending (automated: A4) | |
| B5 | V-4d | **Delete** a file | It is in the Recycle Bin (check above); status "1 items deleted" | Pending (automated: A5) | |
| B6 | V-4d | **Shift+Delete** a file, then **No** | The Shell's permanent-delete confirmation; the file stays; status "Operation cancelled — 0 items completed before cancellation" | Pending (automated: A6) | |
| B7 | V-4e | Copy a file into `readonly-acl` | The Shell's access-denied dialog; after Cancel, no partial file (the folder cannot be listed; the status says cancelled) | Pending (automated: A7) | |
| B8 | V-4f | Copy `10k` into a temp folder, then cancel in the Shell progress dialog; meanwhile scroll and resize the window | The status shows "Copying 1 items…" throughout, then "Operation cancelled — …"; the window stays responsive | Pending (automated: A8, A9) | |
| B9 | V-4g | Right-click a file; separately select one and press **Shift+F10**; choose **Properties** | The Shell's context menu, with icons and "Send to" submenu; Properties opens | Pending (automated: A10–A12) | |
| B10 | V-4h | In `unicode`, rename a file to another Unicode name, then copy it to `nested`; in the deepest `longpath` folder, rename a file and copy one out | All succeed with exact names, or a clear Windows error; nothing truncated or lost | Pending (automated: A13, A14) | |
| B11 | All | Repeat B1, B6 and B9 at 100% and 200% scaling | Dialogs owned by the window and placed over it | Pending (only 150% available) | |
