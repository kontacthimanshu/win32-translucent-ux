# Manual Checklist: Scaling and Live OS Settings (US5)

**Covers**: quickstart V-5d, V-5e; FR-017; SC-008; spec US5 scenarios 2 and 4; UI contract
§1–§2; constitution Principles I and X.
**Build under test**: record the commit and configuration in the Build column.

Prerequisite: `.\tools\New-TestData.ps1` (creates `%TEMP%\te-test`). For B5–B6, a second
monitor, or a virtual display, set to a different scale. Every setting changed in Part B
must be put back afterwards.

## Part A — Automated evidence

No test changes a display or accessibility setting. The window is sent the messages Windows
sends when the setting changes, with what the window reads adjusted by the test:

- **DPI** (`MainWindowDpiTests` 5, `DpiScalingTests` 4, T081): `WM_GETDPISCALEDSIZE`, then
  `WM_DPICHANGED` with the rectangle built from its answer. The window's real DPI stays the
  monitor's, so the caption buttons, which the DWM draws, stay at the real scale in the
  captures.
- **Live settings** (`MainWindowLiveSettingsTests` 5, `MainLayoutTests` 3, T082):
  `MainWindow::Options::adjustCapabilities` changes what the window reads; the
  notification is sent as Windows would (`WM_TE_SETTINGS_CHANGED` for the UISettings
  events, `WM_SETTINGCHANGE` otherwise).
- **Captures** (disabled tests, run on demand with `--gtest_also_run_disabled_tests`;
  output to `%TE_CAPTURE_DIR%`): `MainWindowDpiTest.DISABLED_CaptureAtEachScale`,
  `MainWindowLiveSettingsTest.DISABLED_CaptureAtEachTextSize`.

Last run: 2026-09-28, Windows 11 build 26200, one monitor 1920 × 1200 at 144 DPI (150%),
light mode — **12/12 pass**.

| # | Scenario | Check | Result | Build |
|---|----------|-------|--------|-------|
| A1 | V-5d | `WM_GETDPISCALEDSIZE` asks for a size that keeps the client area's DIPs exactly at ×1.5, ×2 and ×⅔; the frame model matches the real window (`GetDpiScaledSizeKeepsTheClientAreaInDips`, `TheFrameModelMatchesTheWindow`, `DpiScalingTests`) | Pass | Debug, Release |
| A2 | V-5d | After `WM_DPICHANGED`: the suggested rectangle applied; render target, caption layout (40-DIP picker left of Minimize), layout, text formats, title-bar icon and file icons (new icon generation, re-requested at the new size) all at the new DPI; listing, selection and scroll kept (`DpiChangeRescalesEverythingAndKeepsTheListing`) | Pass | Debug, Release |
| A3 | V-5d | 100 % → 200 % → 100 %: the same window size, no drift (`BackToTheOriginalDpiRestoresTheWindowSize`) | Pass | Debug, Release |
| A4 | V-5d | An open appearance popup follows the picker button, inside the monitor's work area (`AnOpenPopupFollowsThePickerButton`, `ColorPickerTest.RepositionMovesAnOpenPopupWithTheButton`) | Pass | Debug, Release |
| A5 | V-5d | Visual: 150 %, 100 %, 200 %, back to 150 % — nothing clipped or overlapping, text and icons sharp ([150](../../specs/001-translucent-explorer/validation/us5-dpi-1-144.png), [100](../../specs/001-translucent-explorer/validation/us5-dpi-2-96.png), [200](../../specs/001-translucent-explorer/validation/us5-dpi-3-192.png), [150 again](../../specs/001-translucent-explorer/validation/us5-dpi-4-144.png)) | Pass | Release |
| A6 | V-5e | Text size 150 %: formats rebuilt; rows, header, navigation-pane rows, toolbar, address field and status bar grow by 1.5; the minimum window height grows; back to 100 % restores all (`TextSizeRebuildsTheFormatsAndGrowsTheRows`); limited to 225 % (`TextSizeIsLimitedToTheWindowsRange`) | Pass | Debug, Release |
| A7 | V-5e | Visual: text size 100 %, 150 %, 225 % — nothing clipped; the title fits the caption strip ([100](../../specs/001-translucent-explorer/validation/us5-text-100.png), [150](../../specs/001-translucent-explorer/validation/us5-text-150.png), [225](../../specs/001-translucent-explorer/validation/us5-text-225.png)) | Pass (after a fix, T082) | Release |
| A8 | V-5e | Animation effects off / on are read again (`AnimationEffectsAreReadAgainWhenTheyChange`); the app has no animations, hover and pressed states change at once (R-05; `docs/review-checklist.md`) | Pass | Debug, Release |
| A9 | V-5e | Light ↔ dark: `DWMWA_USE_IMMERSIVE_DARK_MODE`, base and text colors follow each switch (`LightAndDarkModeUpdateTheFrameAndTheColors`) | Pass | Debug, Release |
| A10 | V-5e | High contrast on → Solid with the HighContrast reason; off → the requested mode again (`HighContrastOnAndOffReResolves`) | Pass | Debug, Release |
| A11 | V-5e | Unrelated setting changes re-read nothing (same test as A8) | Pass | Debug, Release |
| A12 | V-5e | Layout heights follow the text size and never go negative in a small window (`MainLayoutTests`) | Pass | Debug, Release |

## Part B — Manual (needs a person)

### B-a Display scale (V-5d)

Settings → System → Display → Scale. For each scale, open
`TranslucentExplorer.exe %TEMP%\te-test`, then repeat V-2a (open the picker, change the
tint) and V-1d (drag, double-click, Snap Layouts on Maximize).

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B1 | 100 % | Nothing clipped or overlapping; the picker 8 DIP left of Minimize and as tall as the caption buttons; the popup opens under it inside the screen | Pending | |
| B2 | 150 % | As B1 | Pending | |
| B3 | 200 % | As B1 | Pending | |
| B4 | Change the scale while the app runs (100 % → 200 %), with the popup open | The window rescales without a restart; caption buttons, picker and content stay aligned; the popup moves with the picker or closes | Pending | |
| B5 | Two monitors with different scales: drag the window from one to the other and back | It rescales once it crosses; the content keeps its size in DIPs (no growth after a round trip); icons are sharp on both | Pending | |
| B6 | As B5, maximized on each monitor (Win+Shift+Left / Right) | Maximized correctly on each; the caption buttons work | Pending | |

### B-b Live OS settings (V-5e)

Keep the app open for the whole section; do not restart it.

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B7 | Settings → Personalization → Colors → mode Dark, then Light | Frame, caption buttons, surfaces and text follow each switch within a second | Pending | |
| B8 | Settings → Accessibility → Text size 150 %, Apply; then 100 % | Text, rows, toolbar and status bar grow and shrink; nothing clipped | Pending | |
| B9 | Settings → Accessibility → Visual effects → Animation effects Off | Nothing animates (nothing did before); the app keeps working | Pending | |
| B10 | Settings → Accessibility → Contrast themes → Aquatic, Apply; then None | Solid with system colors while on, the popup explains why; back to the chosen mode when off | Pending | |
| B11 | Settings → Accessibility → Visual effects → Transparency effects Off; then On | Solid with the reason while off; the chosen mode when on | Pending | |

Put every setting back as it was.
