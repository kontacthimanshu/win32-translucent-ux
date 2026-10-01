# Manual Checklist: Themes, Backdrops and Fallbacks (US1)

**Covers**: quickstart V-1a, V-1b, V-1c and V-1e; FR-001–003, FR-008, FR-022; SC-001, SC-004;
research R-01, R-04, R-05. V-1d (caption behaviour) is in
[caption-and-snap.md](./caption-and-snap.md).
**Build under test**: record the commit and configuration in the Build column.

Until US2 adds the picker, switch modes with the Debug-only `--backdrop` handoff (T037):
start `TranslucentExplorer.exe --backdrop=mica` from a Debug build, then run a second Debug
launch with `--backdrop=acrylic|mica|solid`. It hands the mode to the running window and
exits. Rows that need the file list, address bar, navigation pane or selection are marked
"US3" and are run again when those exist.

## How to run

1. Build: `cmake --build --preset x64-debug` (and `x64-release` for the rows marked Release).
2. Launch `out\build\x64-debug\src\Debug\TranslucentExplorer.exe --backdrop=mica`.
3. Work through the rows and fill in Result and Build. For contrast rows, use the Colour
   Contrast Analyser (TPGi) or an equivalent tool on the text and the pixels next to it.

## Part A — Automated evidence

These run without changing any system setting. They support the manual rows; they do not
replace them.

Last run: 2026-09-28, Windows 11 build 26200, 144 DPI, light mode, transparency effects
on — Debug and Release.

| # | Check | Expected | Result | Build |
|---|-------|----------|--------|-------|
| A1 | T031 `ThemeResolveTests` (fallback truth table, alphas, Principle VII independence, accent, base colors, scrim) | All pass | Pass, 69/69 | Debug, Release |
| A2 | T032 `ContrastTests` (WCAG math, backdrop extremes, text color, scrim floor, selected rows) | All pass | Pass, 20/20 | Debug, Release |
| A3 | `LegibilityTests`: dark, light and high contrast × Acrylic/Mica/Solid × surface 0/30/90% × tint 0/20/80%, rendered with `SurfacePainter` and blended over every backdrop extreme | Text and secondary text ≥ 4.5:1 | Pass (56 cases) | Debug, Release |
| A4 | `BackdropManagerTests`: probe on build ≥ 22621; `DWMWA_SYSTEMBACKDROP_TYPE` read back per mode | Supported; Mica=2, Acrylic=3, Solid=1 | Pass | Debug, Release |
| A5 | `StatusBarTests`: summary text per mode and each fallback reason | "Mica · Surface 0% · Tint 20%", "Solid", "Solid (fallback: …)" | Pass | Debug, Release |
| A6 | Live app, `--backdrop=mica`, then handoffs to acrylic, solid, mica (window messages and DWM read-back only) | Backdrop type 2 → 3 → 1 → 2 on the same window; each second launch exits 0; one process | Pass | Debug |
| A7 | Live app, Release build, second launch with `--backdrop` | No handoff: each launch opens its own window; the Release binary contains no handoff code | Pass | Release |

## Part B — Manual (needs a person)

### V-1a Launch and legibility

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B1 | Launch on build 22621 or later with default settings | Resizable window with Mica; caption, toolbar, navigation pane and list show the wallpaper influence; title and status text readable; status bar "Mica · Surface 0% · Tint 20%" | Pass — screenshot [us1-light-mica.png](../../specs/001-translucent-explorer/validation/us1-light-mica.png) (light mode) | Debug |
| B2 | Dark mode, plain **white** wallpaper, `--backdrop=acrylic`, Surface 0%, Tint 0% (not settable until US2: default tint 20% is acceptable for now), window over a white area | Every text element readable; a lighter or darker band sits behind text only; the rest stays translucent; spot-check ≥ 4.5:1 | Pending | |
| B3 | Same as B2 in **light** mode with a plain **black** wallpaper | Same as B2 | Pending | |
| B4 | Light mode, `--backdrop=acrylic` over the normal wallpaper | Title and status readable, bands behind text only | Pass — screenshot [us1-light-acrylic.png](../../specs/001-translucent-explorer/validation/us1-light-acrylic.png) | Debug |
| B5 | Selected rows with a mid-brightness accent (V-1a step 5) | Selected-row text ≥ 4.5:1; selection visible | US3 | |
| B6 | File names, address and navigation entries (V-1a step 4) | Readable, ≥ 4.5:1 | US3 | |

### V-1b Runtime mode switching

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B7 | Switch Mica → Acrylic → Solid → Mica with the `--backdrop` handoff | Each mode applies without restarting; the window keeps its position and size; status bar names the mode | Pass (A6, and screenshots [mica](../../specs/001-translucent-explorer/validation/us1-light-mica.png) / [acrylic](../../specs/001-translucent-explorer/validation/us1-light-acrylic.png) / [solid](../../specs/001-translucent-explorer/validation/us1-light-solid.png)) | Debug |
| B8 | Same with the picker (US2) | Same, and the folder and selection are unchanged | US2/US3 | |

### V-1c Fallback

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B9 | While running in Mica, turn off Settings → Personalization → Colors → *Transparency effects* | Window goes Solid; status bar "Solid (fallback: transparency effects off)"; no restart | Pending | |
| B10 | Turn *Transparency effects* back on | Requested mode (Mica) restored, status bar "Mica · Surface 0% · Tint 20%"; no restart | Pending | |
| B11 | Repeat B9–B10 in Acrylic | Same | Pending | |
| B12 | On a build earlier than 22621 (VM), launch with `--backdrop=mica` | Solid; status bar "Solid (fallback: requires Windows 11 build 22621)" | Pending (needs a pre-22621 VM; not available) | |
| B13 | Same, with the picker (US2) | Acrylic and Mica options disabled with the reason | US2 | |

### V-1e High contrast

| # | Step | Expected | Result | Build |
|---|------|----------|--------|-------|
| B14 | While running in Mica, turn high contrast on (`Left Alt+Left Shift+Print Screen`) | Window becomes opaque in the high-contrast window color; title and status text in the high-contrast text color; status bar "Solid (fallback: high contrast)"; no restart | Pending | |
| B15 | Turn high contrast off | Mica restored with the normal colors; no restart | Pending | |
| B16 | Selection and focus in high contrast (V-1e step 2) | Visible in `COLOR_HIGHLIGHT` / `COLOR_HOTLIGHT` | US3 | |
