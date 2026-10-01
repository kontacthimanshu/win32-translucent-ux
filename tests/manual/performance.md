# Manual Checklist: Responsiveness and Resources (US6)

**Covers**: quickstart V-3f, V-3g, V-6a to V-6c; FR-018, FR-019, FR-020; SC-009, SC-010;
research R-07, R-14; constitution Principles III and VIII.
**Build under test**: record the commit and configuration in the Build column.

**No latency target is asserted.** The spec sets none: the checks below require that the
window keeps responding and that results are correct, and they record timings as
observations only. The Debug watchdog (R-14) reports any message handler slower than
50 ms as `[te-watchdog] msg=0x.... took N ms` through `OutputDebugStringW`; that is a
diagnostic aid, not a pass/fail limit.

Prerequisite: `.\tools\New-TestData.ps1` (creates `%TEMP%\te-test`, including `10k` and
`nested`). Remove it afterwards with `.\tools\New-TestData.ps1 -Remove`.

## Part A — Automated evidence

None of these injects mouse or keyboard input; the window is driven through window
messages.

Last run: 2026-09-28, Windows 11 build 26200, 144 DPI (150%), local SSD — **9/9 pass** (A9 since T093).

| # | Scenario | Check | Result | Build |
|---|----------|-------|--------|-------|
| A1 | V-3f | While `10k` loads: wheel scroll, two resizes, End / Home, the appearance popup; each took effect, the listing completed with 10,000 items (`MainWindowResponsivenessTest.TheWindowRespondsWhileTenThousandItemsLoad`). Observed, 3 runs each: Debug — all 4 actions while loading (256 → about 5,700 items listed), listing done in about 880 ms, longest handler 25 ms, none over 50 ms, no watchdog line; Release — 2 of 4 while loading (the listing finished in about 550 ms), longest handler 17 ms, none over 50 ms | Pass | Debug, Release |
| A2 | V-3g | Rapid Back / Forward between `10k` and `nested`, 10 times: after every message the list holds only the current folder's items and the final list matches the address (`MainWindowNavigationTest.RapidBackForwardNeverMixesFolders`, T066) | Pass | Debug, Release |
| A3 | V-6a | 10,000 items in batches of at most 256, all accepted by the generation guard (`DirectoryEnumeratorTest.TenThousandFiles…`); a newer navigation supersedes the running one (`NewerStartSupersedes…`); `CancelAll` after the first batch → `EnumDone { cancelled }` at once and no batch after it (`CancelAllAfterTheFirstBatchStopsTheBatches`); `Shutdown` mid-listing returns and frees every payload (`ShutdownWhileEnumeratingFreesEveryPayload`) (T084) | Pass | Debug, Release |
| A4 | V-6b | 50 create / show / destroy cycles: GDI 11 → 11, USER 5 → 5, handles 276 → 276 (`WindowLifecycle.RepeatedCreateAndDestroyDoesNotLeak`) | Pass | Debug, Release |
| A5 | V-6b | 50 cycles destroying the window while `10k` is listed: GDI 47 → 47, USER 6 → 6, handles 473 → 473, every `WM_TE_*` payload freed; the Windows component-catalog cache sections (`…\Caches\cversions.*.ro`), which COM remaps at random, are counted apart (`WindowLifecycle.DestroyingWhileEnumeratingTenThousandItemsDoesNotLeak`, T085, T086) | Pass | Debug, Release |
| A6 | V-6b | Shutdown order on close while listing, with a UI Automation client and a file operation finishing: cancel → file operations → workers → drain → UI Automation → graphics; every payload freed; device and providers released (`MainWindowShutdownTests`, T088) | Pass | Debug, Release |
| A7 | FR-018 | Only visible rows are laid out and drawn; text layouts cached per visible row; batches merged, not re-sorted (`FileViewRender.TextLayoutsAreCachedForTheVisibleRows`, `SortModel.MergingBatchesMatchesAFullSort`, T086) | Pass | Debug, Release |
| A8 | FR-018 | Icons: LIFO, visible rows plus one screen ahead, one bitmap per Shell icon, at most 32 conversions per frame; over `10k` exactly one conversion for all 10,000 `.txt` rows (`IconCacheTests`, `IconProviderTests`, `MainWindowIconTest.TenThousandTextFilesShareOneConvertedIcon`, T087) | Pass | Debug, Release |
| A9 | V-6b | Debug CRT leak report at exit (T093): `.\tools\Invoke-CrtLeakRun.ps1` starts the Debug build with `TE_CRT_REPORT` set, runs V-3 (address, Enter, Back / Forward / Up / Refresh, `10k` with End / Home, rapid Back / Forward, Unicode, a bad path, the filter, the appearance popup, the tree) and V-4 (F2 rename, Delete to the Recycle Bin) through window messages, closes it: report empty (a deliberately planted 42-byte leak was reported with its size and allocation number, so the report works). In every `ctest` run, `WindowLifecycle.TheCrtHeapReturnsToBaselineAfterBrowsing` (Debug) compares the CRT heap before and after 5 browsing windows: 0 blocks more | Pass | Debug |

Watchdog output (the DebugView row, B6): the Debug run of A1 captures the watchdog's
lines through its test sink; none was written.

## Part B — Manual (needs a person)

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B1 | V-3f by hand: open `10k`; while it loads, scroll with the wheel and the scrollbar, resize the window, open the picker | The UI responds throughout; no "Not Responding"; the popup opens at once | Pending | |
| B2 | V-3g by hand: in `10k`, press Alt+Left / Alt+Right quickly, ten times, between `10k` and `nested` | The list always matches the address; items from the other folder never appear, not even briefly | Pending | |
| B3 | 20 round trips between a local folder and a network share (`\\localhost\c$` from an elevated account, or a real share): Ctrl+L, type the path, Enter; then Alt+Left | Each listing matches its location; an unreachable share shows the error in the address bar and the previous folder stays (FR-020); the window never freezes. (Not automated: this PC has only the admin shares, which the test account cannot open, and creating a share would change the system.) | Pending | |
| B4 | Disconnect a USB drive while it is being listed (a folder with a few thousand files) | The listing stops with an error in the status bar; the window stays responsive; the drive disappears from the navigation pane (`WM_DEVICECHANGE`) | Pending | |
| B5 | V-6c: `appverif /verify TranslucentExplorer.exe` (Basics: Handles, Heaps, Locks; elevated prompt). Run V-3 and V-4 under the debugger (WinDbg or Visual Studio), then `appverif /n TranslucentExplorer.exe` to switch it off again | No verifier stop. (Application Verifier is installed here, but enabling it changes machine settings for the executable, so it was not run automatically.) | Pending | |
| B6 | DebugView (Sysinternals), Capture Win32 on; run the Debug build through B1–B4 | Record every `[te-watchdog]` line (message and duration) here; they are observations, not failures | Pending | |

Recorded watchdog lines:

| Scenario | Line | Notes |
|----------|------|-------|
| | | |
