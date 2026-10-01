# Manual Checklist: Title-Bar Color Picker (US2)

**Covers**: quickstart V-2a to V-2h; FR-004–007, FR-022; SC-002, SC-003; UI contract §2–§3;
research R-10, R-11, R-12.
**Build under test**: record the commit and configuration in the Build column.

Settings live in `%LOCALAPPDATA%\TranslucentExplorer\settings.json`. Back the folder up
before V-2e–V-2g and restore it afterwards.

## Part A — Automated evidence

Run against the Release build with window messages only (hit tests, `WM_COMMAND` to the
popup's controls, trackbar messages, `WM_ACTIVATE`); no mouse or keyboard input was
injected. The settings folder did not exist before the run and was removed afterwards.

Last run: 2026-09-28, Windows 11 build 26200, 144 DPI (150%), light mode — **20/20 pass**.

| # | Scenario | Check | Result | Build |
|---|----------|-------|--------|-------|
| A1 | V-2a | Normal window: picker 40 DIP wide, 8 DIP gap to the caption-button bounds, caption drag region to its left, Minimize still `HTMINBUTTON` | Pass | Release |
| A2 | V-2a | Maximized: same | Pass | Release |
| A3 | V-2a | Narrow: resized to 200 px, clamped to the 554 px minimum; same placement, no overlap ([screenshot](../../specs/001-translucent-explorer/validation/us2-v2a-narrow.png)) | Pass | Release |
| A4 | V-2b | A click on the picker opens the popup directly under it ([screenshot](../../specs/001-translucent-explorer/validation/us2-v2b-open.png)) | Pass | Release |
| A5 | V-2b | While open, the caption buttons keep their bounds and hit tests (Minimize 8, Close 20) | Pass | Release |
| A6 | V-2b | `IDM_OPEN_APPEARANCE` (the Alt+Shift+C command) opens it | Pass | Release |
| A7 | V-2c | Teal, tint strength 50%, surface opacity 60% apply live ([screenshot](../../specs/001-translucent-explorer/validation/us2-v2c-custom.png)); Custom… opens the color dialog and the popup stays open | Pass | Release |
| A8 | V-2c | After another color change (Purple) surface stays 60% and tint 50% | Pass | Release |
| A9 | V-2c | Saved: Acrylic, `#8764B8`, tint 0.5, surface 0.6 | Pass | Release |
| A10 | V-2d | Solid: both trackbars disabled, "Solid mode is fully opaque" ([screenshot](../../specs/001-translucent-explorer/validation/us2-v2d-solid.png)) | Pass | Release |
| A11 | V-2d | Back to Acrylic: both enabled, values kept | Pass | Release |
| A12 | V-2e | Relaunch restores mode, tint strength and surface opacity ([screenshot](../../specs/001-translucent-explorer/validation/us2-v2e-restored.png)) | Pass | Release |
| A13 | V-2e | Reset: Mica, surface 0%, tint 20% | Pass | Release |
| A14 | V-2e | Reset saved: accent tint; custom color slot `#123456` kept | Pass | Release |
| A15 | V-2f | `{not json`: defaults used ([screenshot](../../specs/001-translucent-explorer/validation/us2-v2f-corrupt.png) shows the reset notice) | Pass | Release |
| A16 | V-2f | One `settings.corrupt-<timestamp>.json` backup | Pass | Release |
| A17 | V-2g | `backdropMode: Acrylic, tintOpacity: 5`: Acrylic kept, tint clamped to 80% | Pass | Release |
| A18 | V-2h | Escape closes the popup; settings unchanged | Pass | Release |
| A19 | V-2h | Deactivation closes it and the following click on the picker is swallowed (it does not reopen) | Pass | Release |
| A20 | T041 | `SettingsTests` 17/17; `MainWindowSettingsTests` 7/7; `ColorPickerTests` 13/13 | Pass | Debug, Release |

## Part B — Manual (needs a person)

| # | Scenario | Step | Expected | Result | Build |
|---|----------|------|----------|--------|-------|
| B1 | V-2a | Look at the caption at 100%, 150% and 200% scaling, normal, maximized and narrow | Picker directly left of Minimize with a visible gap of about 8 DIP; nothing clipped or overlapping | Pending (only 150% available) | |
| B2 | V-2b | Click the picker with the mouse; press `Alt+Shift+C` | Popup opens under the button; caption buttons neither moved nor disabled; hover and pressed states visible on the picker | Pending | |
| B3 | V-2b | Keyboard in the popup: Tab through Mode → swatches → Accent → Custom… → Surface → Tint → Reset; arrow keys in the 4 × 3 grid; Enter/Space on a swatch | Focus visible everywhere; arrows move by row and column; Enter/Space apply | Pending | |
| B4 | V-2c | Custom…, pick `#8764B8`, OK | Preview and window turn purple live; the swatch check mark clears. (Automation could open the dialog but not set its fields.) | Pending | |
| B5 | V-2c | Drag Surface opacity 0% → 60% | Surfaces become more opaque; tint color and strength unchanged; text readable | Pending (automated values pass: A7–A8) | |
| B6 | V-2h | With the popup open, click the window body (the file list after US3) | Popup closes; the click does nothing else (file-list selection unchanged after US3) | Pending | |
| B7 | V-2h | With the popup open, click the picker again | Popup closes and does not reopen | Pending (automated: A19) | |
| B8 | FR-022 | High contrast on, open the popup | Acrylic and Mica disabled with "High contrast is on"; both trackbars disabled | Pending | |
| B9 | a11y | Narrator on the popup | Swatches read their color names; trackbars read "Surface opacity" / "Tint strength" with their values | Pending (full accessibility pass is T078–T080) | |
