# win32-translucent-ux
This project is to develop window based user experience for InferenceOS which is transparent and has different color themes.

## Translucent Explorer

A native Win32 file explorer for Windows 11, written in C++20, whose whole window is
translucent over the system backdrop:

- **Appearance**: Mica, Acrylic or Solid, a tint colour (the accent colour, 12 presets
  or a custom colour), surface opacity and tint strength, chosen from a picker button in
  the custom title bar, next to Minimize. Text keeps at least 4.5:1 contrast; high
  contrast, transparency effects off and older Windows builds fall back to Solid with the
  reason shown.
- **Browsing**: an address bar with breadcrumbs, Back / Forward / Up / Refresh, a
  navigation pane with a folder tree, a file list with sortable columns and Shell icons,
  and a filter box (Ctrl+F). Large folders (10,000+ items) stay responsive.
- **Files**: copy, cut, paste, rename in place, delete to the Recycle Bin, and the Shell
  context menu, through `IFileOperation` with the Shell's own progress and conflict
  dialogs.
- **Windows integration**: per-monitor DPI, text size, dark / light and high contrast
  followed live, Snap Layouts, keyboard access throughout (F6 focus ring) and UI
  Automation for screen readers.

It uses Win32, Direct2D / DirectWrite, DirectComposition and DWM system backdrops; no UI
framework.

## Prerequisites

| Requirement | Version |
|-------------|---------|
| Windows 11 | Build 22621 or later for the full backdrop checks; an earlier build or Windows 10 is used only for the fallback check (V-1c) |
| Visual Studio | 2026, or 2022 version 17.8 or later, with the **Desktop development with C++** workload |
| Windows SDK | 10.0.22621.0 or later |
| CMake | 3.28 or later (the copy bundled with Visual Studio is fine) |
| vcpkg | The copy bundled with Visual Studio (`VCPKG_ROOT` set), or a standalone clone |

The dependencies (`wil`, `nlohmann-json`, `gtest`) come from vcpkg through
[`vcpkg.json`](vcpkg.json).

## Build and test

From a Developer PowerShell for Visual Studio at the repository root:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug --output-on-failure            # unit tests
ctest --preset x64-debug -L integration --output-on-failure

cmake --preset x64-release
cmake --build --preset x64-release
```

The `x64-*` presets use Visual Studio 2026; with Visual Studio 2022 use
`vs2022-x64-debug` and `vs2022-x64-release` instead. Both configurations build with zero
warnings in application code (`/W4 /WX`).

Many integration tests use generated test data; create it first (and remove it with
`-Remove` when done):

```powershell
.\tools\New-TestData.ps1 -Root "$env:TEMP\te-test"
```

## Run

```powershell
.\out\build\x64-debug\src\Debug\TranslucentExplorer.exe             # opens This PC
.\out\build\x64-debug\src\Debug\TranslucentExplorer.exe "C:\Windows" # opens a given folder
.\out\build\x64-debug\src\Debug\TranslucentExplorer.exe --backdrop=acrylic  # Debug only: acrylic, mica, solid or transparent
```

The appearance picker's **Transparent** swatch (below the color grid) makes the window clear
glass: no Mica or Acrylic blur, so whatever is behind it shows through. Colors and the two
opacity sliders still apply on top, and the selection highlight stays translucent.
The tint is the glass's only color here, so choosing Transparent or a color while in it
raises Tint strength to at least 45% (lower it with the slider if you want it fainter),
and the tint covers the whole window, caption buttons included. Text gets a one-pixel contrasting halo instead of an
opaque backing, so the whole window stays equally clear and still reads over anything.

Depth: the panes are drawn as stacked glass, with a soft shadow under the toolbar and
beside the navigation pane, a light edge along the top of each pane, and a bevelled
window rim lit from the top left and shaded at the bottom right (none in high contrast). Navigating swings the new listing into place in perspective, hinged on the
left (on the right for Back); it is skipped when Windows "Animation effects" is off.

Useful keys: `Alt+Shift+C` appearance picker, `Ctrl+L` / `Alt+D` / `F4` address,
`Ctrl+F` filter, `F6` / `Shift+F6` move between the picker, address bar, navigation pane
and file list, `Alt+Left` / `Alt+Right` / `Alt+Up` / `Backspace` navigation, `F5` refresh,
`F2` rename, `Delete` / `Shift+Delete`, `Ctrl+C` / `Ctrl+X` / `Ctrl+V`, `Shift+F10` context
menu. The full list is in the [UI contract §4](specs/001-translucent-explorer/contracts/ui-contract.md).

The Debug build reports leaked heap blocks at exit (to the debugger, or to the file named
by `TE_CRT_REPORT`); [`tools/Invoke-CrtLeakRun.ps1`](tools/Invoke-CrtLeakRun.ps1) runs the
navigation and file-operation scenarios under it.

## Project documents

The project is built with [Spec Kit](https://github.com/github/spec-kit); its documents
are the reference for behaviour and design:

- [Constitution](.specify/memory/constitution.md): the principles every change follows.
- [Feature 001: Translucent Explorer](specs/001-translucent-explorer/):
  [spec](specs/001-translucent-explorer/spec.md),
  [plan](specs/001-translucent-explorer/plan.md),
  [research](specs/001-translucent-explorer/research.md),
  [data model](specs/001-translucent-explorer/data-model.md),
  [contracts](specs/001-translucent-explorer/contracts/),
  [tasks](specs/001-translucent-explorer/tasks.md),
  [quickstart and validation scenarios](specs/001-translucent-explorer/quickstart.md) and the
  [validation report](specs/001-translucent-explorer/validation-report.md) with screenshots.
- Manual checklists for what needs a person (Narrator, FastPass, real display scales,
  Application Verifier, network shares): [`tests/manual/`](tests/manual/).
- [Code review checklist](docs/review-checklist.md).

## Layout

| Folder | Contents |
|--------|----------|
| `src/`, `include/te/` | The application: `app` (window and message loop), `window` (title bar, DPI), `render` (Direct2D), `appearance` (themes, backdrops, picker), `settings` (saved appearance settings, JSON), `ui` (toolbar, address bar, filter box, navigation pane, file list), `shell` (enumeration, icons, file operations, clipboard, context menu), `a11y` (UI Automation), `core` |
| `resources/` | Icon, strings, accelerators, the appearance dialog and the manifest (PerMonitorV2, long paths) |
| `tests/unit/`, `tests/integration/` | GoogleTest suites, run by `ctest` |
| `tests/manual/` | Checklists for manual validation |
| `tools/` | Test-data and leak-check scripts |
