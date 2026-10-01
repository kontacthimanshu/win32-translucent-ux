# Manual Checklist: Caption, Window Management and Snap

**Covers**: quickstart V-1d and V-5d (caption only), FR-008, SC-004, UI contract §2.
**Build under test**: record the commit and configuration in the Build column.

The picker is not visible until US2 (T046); its reserved area is checked by the unit tests
(T020) and by the automated rows below.

## How to run

1. Build: `cmake --build --preset x64-release` (and `x64-debug`).
2. Launch `out\build\x64-release\src\Release\TranslucentExplorer.exe`.
3. Work through Part B with the mouse and keyboard, and fill in Result and Build.

## Part A — Automated preconditions

These checks run against the real application with window messages only: `WM_NCHITTEST`,
`WM_SYSCOMMAND` and window queries. No mouse or keyboard input is injected. They prove the
behaviour Windows needs for each manual row; they do not replace the manual rows.

Last run: 2026-09-28, 144 DPI (150%), Windows 11 build 26200, Release and Debug — **26/26
pass** in both.

| # | Check | Expected | Result | Build |
|---|-------|----------|--------|-------|
| A1 | Client area starts at the window top (custom frame) | client.top = window.top | Pass | Release, Debug |
| A2 | Caption drag region | `HTCAPTION` (2) | Pass | Release, Debug |
| A3 | Gap between picker area and Minimize | `HTCAPTION` (2) | Pass | Release, Debug |
| A4 | Picker area | `HTCLIENT` (1) | Pass | Release, Debug |
| A5 | Minimize button (via `DwmDefWindowProc`) | `HTMINBUTTON` (8) | Pass | Release, Debug |
| A6 | Maximize button (Snap Layouts trigger) | `HTMAXBUTTON` (9) | Pass | Release, Debug |
| A7 | Close button | `HTCLOSE` (20) | Pass | Release, Debug |
| A8 | Top edge / top-left / top-right corner | 12 / 13 / 14 | Pass | Release, Debug |
| A9 | Left / right / bottom edge | 10 / 11 / 15 | Pass | Release, Debug |
| A10 | Bottom-left / bottom-right corner | 16 / 17 | Pass | Release, Debug |
| A11 | Window body | `HTCLIENT` (1) | Pass | Release, Debug |
| A12 | System menu items | Restore, Move, Size, Minimize, Maximize, Close | Pass | Release, Debug |
| A13 | Taskbar / Alt+Tab eligibility | no owner, `WS_EX_APPWINDOW`, not a tool window, visible | Pass | Release, Debug |
| A14 | `SC_MINIMIZE`, then `SC_RESTORE` | minimized, then restored | Pass | Release, Debug |
| A15 | `SC_MAXIMIZE` | maximized; client starts at the window top edge (above the work area) | Pass | Release, Debug |
| A16 | Maximized: Minimize / Restore / Close | 8 / 9 / 20; buttons on screen | Pass | Release, Debug |
| A17 | `SC_RESTORE` from maximized | previous rectangle restored exactly | Pass | Release, Debug |
| A18 | `SC_CLOSE` | process exits with code 0 | Pass | Release, Debug |

## Part B — Manual (needs a person)

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B1 | Drag the window by the caption (left of the picker area) | The window follows the pointer smoothly | Pending | |
| B2 | Resize from each edge and each corner | Resize cursor at each edge; the window resizes; it cannot be made smaller than the minimum size | Pending | |
| B3 | Double-click the caption | Maximizes; double-click again restores | Pending | |
| B4 | Hover, press and click Minimize, Maximize/Restore and Close | Native hover and press highlights; each button works | Pending | |
| B5 | Right-click the caption; press `Alt+Space` | The system menu opens at the pointer or at the caption; each command works | Pending | |
| B6 | `Win+Left`, `Win+Right`, `Win+Up`, `Win+Down` | Snaps left and right, maximizes, restores and minimizes like any Windows 11 window | Pending | |
| B7 | Hover the Maximize button (normal and maximized) | The Snap Layouts flyout appears; choosing a layout snaps the window | Pending | |
| B8 | `Alt+Tab` | The window appears with its icon, title and a live thumbnail | Pending | |
| B9 | Hover the taskbar button | A thumbnail preview appears; clicking restores and activates the window | Pending | |
| B10 | Maximized: caption layout | Icon and title fully visible and aligned with the caption buttons (reference: `specs/001-translucent-explorer/validation/phase1-maximized-caption.png`) | Pass (screenshot) | Release |
| B11 | V-5d (caption only): repeat B1–B7 at 100%, 150% and 200% scaling, and after moving the window between monitors with different scaling | Same results at every scale; caption buttons never overlap the picker area | Pending (only 150% available on the test machine) | |
