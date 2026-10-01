# Implementation Plan: Translucent Explorer

**Branch**: `001-translucent-explorer` | **Date**: 2026-09-28 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/001-translucent-explorer/spec.md`

**Note**: This template is filled in by the `/speckit-plan` command; its definition describes the execution workflow.

## Summary

The first release of Translucent Explorer is a native Windows 11 file manager with:

- a transparent-first window
- a color picker in the title bar, immediately to the left of Minimize
- Acrylic, Mica and Solid modes
- persisted, independent tint and surface-opacity preferences
- Explorer-style navigation and safe file operations

**Technical approach** (details in [research.md](./research.md)):

- **Window**: Win32 owns the window, using the DWM "custom frame" pattern. The extended
  frame keeps the native caption buttons, and `DwmDefWindowProc` hit-tests first.
- **Backdrop**: `DWMWA_SYSTEMBACKDROP_TYPE` supplies the backdrop. It is applied only after
  a capability probe, with an honest Solid fallback.
- **Rendering**: All custom content is drawn with Direct2D and DirectWrite into a
  premultiplied-alpha DXGI composition swap chain, shown through DirectComposition. Each
  surface is composed as backdrop → surface layer (surface opacity) → tint layer (tint
  strength) → opaque content, so opacity and tint are independent (research R-04).
- **File view**: A custom Direct2D view built on `IShellItem` enumeration, as the
  constitution permits. `IExplorerBrowser` is evaluated in a spike and is expected to be
  opaque.
- **Background work**: STA worker threads do enumeration, icon loading and
  `IFileOperation`. They report back through generation-tagged messages, so the UI never
  blocks and never shows stale results.

## Technical Context

**Language/Version**: ISO C++20, MSVC (Visual Studio 2026, or Visual Studio 2022 17.8+), `/std:c++20`

**Primary Dependencies**:

- **Windows SDK 10.0.22621+ components**: Win32 (User32/GDI for the frame only), DWM,
  Windows Shell COM (`IShellItem`, `IFileOperation`, `IContextMenu`, `IExplorerBrowser`
  for the spike), Direct2D 1.1, DirectWrite, WIC, Direct3D 11, DXGI 1.2, DirectComposition,
  UI Automation provider API, Common Controls v6, and C++/WinRT, used only for
  `Windows.UI.ViewManagement.UISettings`.
- **vcpkg packages**: `wil` (RAII for handles and COM), `nlohmann-json`, and `gtest`
  (tests only).

**Storage**: A local JSON file at `%LOCALAPPDATA%\TranslucentExplorer\settings.json`,
replaced atomically. The schema is
[contracts/settings.schema.json](./contracts/settings.schema.json).

**Testing**:

- GoogleTest with CTest, as unit tests and integration tests (label `integration`, using
  real temp folders).
- A window-lifecycle resource test.
- Manual suites in `tests/manual/`: caption, DPI, file operations, accessibility and
  themes.
- Application Verifier and Accessibility Insights FastPass before a release.

**Target Platform**: Windows 11, x64. Build 22621+ gets the full Mica/Acrylic
experience; earlier builds fall back to Solid.

**Project Type**: Native Win32 desktop application (single executable, plus a static core
library that the tests link against).

**Performance Goals**:

- The UI thread is never blocked by enumeration, icon extraction or file operations.
- Folders with 10,000+ items stay interactive while they load.
- Diagnostic budget: in Debug builds, the watchdog flags any message handler over 50 ms.

The spec does not assert any latency or frame-rate target (see its Success Criteria
note).

**Constraints**:

- Documented APIs only.
- No UI frameworks: no Electron, Chromium, .NET, WinUI, Qt, MFC, WPF or WinForms.
- Per-monitor v2 DPI awareness.
- `/W4 /WX /permissive-` on application code.
- `IFileOperation` runs in an STA.
- No silent overwrite and no silent permanent delete.
- Long-path aware.

**Scale/Scope**:

- 1 main window, 1 appearance popup, 6 user stories, 22 functional requirements and 12
  technical constraints.
- About 45 source files across 9 component areas.
- Test folders of up to 10,000 items.

No `NEEDS CLARIFICATION` items remain. The four product decisions that the spec deferred
(default tint, palette, opacity range and initial folder) are settled in research R-11.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Checked against constitution v1.1.0: before research, after design, and again after
implementation (T098, 2026-09-28, after T001–T097; T095 and T096 open). The
"Post-implementation" column replaces the former "Post-design" column, which was ✅ for
every principle (the design-time evidence is kept at the start of each Evidence cell).
"Manual rows pending" means every automated check passes and the listed manual checklist
rows still need a person; T099 runs them before release.

| # | Principle | Gate | Pre-research | Post-implementation | Evidence |
|---|-----------|------|--------------|-------------|----------|
| I | Native Windows Architecture (NON-NEGOTIABLE) | Uses C++20 and MSVC; Win32 owns the window and message loop; `CreateWindowExW`/`WNDCLASSEXW`; Unicode; DWM backdrops; PMv2 DPI; x64 Debug and Release; RAII; no banned UI framework | ✅ | ✅ | R-02, R-13; `wil` RAII; manifest PMv2. DirectComposition, Direct3D 11 and C++/WinRT `UISettings` are native OS APIs, not UI frameworks (see Complexity Tracking). **Post-implementation (2026-09-28):** the product links only Windows SDK libraries (`dwmapi`, `d2d1`, `d3d11`, `dcomp`, `uiautomationcore`, `runtimeobject`, …) and `wil` / `nlohmann-json` (`src/CMakeLists.txt`); `CreateWindowExW` / `WNDCLASSEXW` and a Win32 message loop (`src/app`); the manifest declares PerMonitorV2 and `longPathAware`; `MainWindowDpiTests`, `DpiScalingTests` (T081); x64 Debug and Release build clean (T094: 200 translation units, 0 warnings); RAII through `wil` throughout (`WindowLifecycleTests`). |
| II | Transparent-First Visual Design (NON-NEGOTIABLE) | Translucent caption, navigation, tree and file area; Acrylic, Mica and Solid; readable text, icons, selection and focus; graceful fallback before 22621 | ✅ | ✅ (manual rows pending) | R-01, R-02, R-05; `EffectiveAppearance` resolution rules; custom Direct2D file view (R-06); contrast guard of at least 4.5:1, checked against the worst-case backdrop range for each mode, with extra opacity behind text only when needed (R-05, T032, V-1a). The address field stays translucent while editing if the Phase 1 spike confirms the layered edit (R-02); otherwise the tinted-edit interpretation in Complexity Tracking needs explicit sign-off by the project owner, recorded in `validation-report.md` "Sign-offs". **Post-implementation:** title bar, toolbar, address bar, navigation pane with the folder tree (T090), filter box (T092) and file list are all Direct2D over the system backdrop; the address and filter EDITs are both layered, colour-keyed and translucent while editing (`src/ui/AddressBar.cpp`, `src/ui/FilterBox.cpp`). `LegibilityTests` (56 cases, ≥ 4.5:1 over every backdrop extreme), `ContrastTests`, `FocusIndicatorTests` (focus ≥ 3:1), `SurfacePainterTests`, `BackdropManagerTests` (fallback). Screenshots in [validation/](./validation/) (us1-*, polish-folder-tree, polish-filter). Pending: [themes.md](../../tests/manual/themes.md) Part B (wallpaper extremes, transparency off, high contrast, pre-22621 VM). |
| III | Integrated Title-Bar Color Picker (NON-NEGOTIABLE) | Left of Minimize with a margin; native caption buttons kept; `DwmDefWindowProc` first; all 7 picker capabilities; tint, mode and opacity stored separately; no exact-opacity claim | ✅ | ✅ (manual rows pending) | R-03, R-04, R-10; [ui-contract §1–3](./contracts/ui-contract.md); settings schema with 4 independent fields (mode, tint color, tint strength, surface opacity) **Post-implementation:** `CaptionLayoutTests`, `UiaTitleBarButtonTests`, `ColorPickerTests`, `MainWindowSettingsTests`, `SettingsTests` (4 independent fields); the picker is repositioned on DPI and monitor changes (T081, `MainWindowDpiTests`); [validation report, US2](./validation-report.md#phase-2-us2--change-color-from-the-title-bar-phase-4). Pending: [caption-and-snap.md](../../tests/manual/caption-and-snap.md) B1–B9, B11 and [picker.md](../../tests/manual/picker.md) Part B. |
| IV | Native Shell Interoperability | Documented Shell only; `IExplorerBrowser` evaluated; `IFileOperation` for operations; Shell-hosted view evaluated for translucency | ✅ | ✅ | R-06 (spike), R-08 **Post-implementation:** `IExplorerBrowser` evaluated and rejected as opaque ([spike-explorerbrowser.md](./spike-explorerbrowser.md)); navigation through `IShellItem` / `IShellFolder` (`ShellNavigatorTests`, `ShellLocationTests`, `FolderTreeTests`); icons through `SHGetFileInfo` (`IconProviderTests`, `IconCacheTests`); operations through `IFileOperation` (`FileOperationServiceTests`, `FileOpsShellUiValidation`); `IContextMenu` (`ContextMenuTests`); the Shell data object (`ClipboardTests`). |
| V | Responsive and Familiar Explorer Experience | All 8 initial-release capabilities; keyboard access; no blocking work on the UI thread; tree, breadcrumb and search as SHOULD | ✅ | ✅ | R-07, R-15. The navigation pane provides quick locations. The full tree, breadcrumb and search follow FR-021 (SHOULD) and are tracked as optional tasks. **Post-implementation:** all 8 MUST capabilities ([US3](./validation-report.md#phase-3-us3--navigate-and-inspect-files-phase-5), [US4](./validation-report.md#phase-4-us4--manage-files-safely-phase-6)); the SHOULD items are now delivered: folder tree (T090, `FolderTreeTests`), breadcrumbs (T091, `AddressBarBreadcrumbTests`), and a filter of the current folder (T092, `FileViewFilterTests`, `MainWindowFilterTests`; see Complexity Tracking). Keyboard: `MainWindowFocusTests`, [accessibility.md](../../tests/manual/accessibility.md) A1–A5. Responsiveness: `MainWindowResponsivenessTests` (V-3f), `DirectoryEnumeratorTests`, [performance.md](../../tests/manual/performance.md) A1–A9; no latency figure is claimed. |
| VI | Separation of Responsibilities | Window, appearance, Shell, file view and settings separated by explicit interfaces; CMake with the VS generator; `/W4`, warnings as errors, `/permissive-` | ✅ | ✅ | [component-interfaces.md](./contracts/component-interfaces.md); Project Structure below; R-13 **Post-implementation:** one folder per responsibility under `src/` and `include/te/` (`app`, `window`, `render`, `appearance`, `settings`, `ui`, `shell`, `a11y`, `core`), matching [component-interfaces.md](./contracts/component-interfaces.md); `/W4 /WX /permissive-` in `CMakeLists.txt`; test hooks in production classes are recorded in Complexity Tracking. |
| VII | Rendering and Transparency Correctness | Documented DWM backdrop APIs; tint separate from opacity; no claim of unrestricted opacity; Direct2D view if the Shell view is opaque | ✅ | ✅ | R-01, R-04: a separate surface layer ("Surface opacity") and tint layer ("Tint strength"), independent because Direct2D composition supports it; both disabled in Solid. R-06; FR-022 wording in ui-contract §3 **Post-implementation:** only `DWMWA_SYSTEMBACKDROP_TYPE` is used for Mica and Acrylic (`src/appearance/BackdropManager.cpp`); surface opacity and tint strength are separate layers, both disabled in Solid (`SurfacePainterTests`, `ThemeResolveTests`); the picker's wording makes no exact-opacity claim (ui-contract §3, `ColorPickerTests`). |
| VIII | Resource, Memory and Thread Safety | RAII ownership; COM smart pointers; apartment per thread; `IFileOperation` in an STA; cancellable enumeration; stale-result guard | ✅ | ✅ (Application Verifier pending) | R-07; message ownership contract; `DirectoryRequest` and generation rules in data-model **Post-implementation:** COM apartments through `te::ComInit` (STA for the UI, `IFileOperation` and Shell workers); `GenerationGuardTests`; `DirectoryEnumeratorTests` (cancel, supersede, shutdown, every `WM_TE_*` payload freed, T084); `WindowLifecycleTests` (50 create / destroy cycles, destroy during a 10,000-item listing, CRT heap back to baseline; T085, T093); `MainWindowShutdownTests` (contract shutdown order, T088); `tools/Invoke-CrtLeakRun.ps1` report empty. Pending: T095 Application Verifier run ([performance.md](../../tests/manual/performance.md) B5), which needs an elevated prompt. |
| IX | Reliable File Operations | Report failures and conflicts; confirmation and Recycle Bin; no silent overwrite or permanent delete; long and Unicode paths | ✅ | ✅ (manual rows pending) | R-08 fixed flags (never `FOF_NOCONFIRMATION`); `FileOperationRequest` states; V-4 scenarios; V-3h (browsing), V-4h (file operations) and integration test T067(e) for long paths and Unicode names **Post-implementation:** `FileOperationServiceTests`, `MainWindowFileOpsTests`, `FileOpsShellUiValidation` (Shell confirmation, conflict and error UI, never `FOF_NOCONFIRMATION`), `FileViewRenameTests`, `FileOpTextTests`; long paths and Unicode names in V-3h / V-4h; [validation report, US4](./validation-report.md#phase-4-us4--manage-files-safely-phase-6). Pending: [file-operations.md](../../tests/manual/file-operations.md) Part B. |
| X | Accessibility and Windows Integration | Theme, text scale, high contrast, reduced motion; accessible custom controls; UIA for custom items; system menu, taskbar, Alt+Tab, snap, Snap Layouts | ✅ | ✅ (Narrator and FastPass pending) | R-05, R-09 (caption-button UIA verified in the Phase 1 spike, with fallback providers), R-15; [ui-contract §4–5](./contracts/ui-contract.md) **Post-implementation:** `MainWindowLiveSettingsTests` (dark / light, text scale, `SPI_SETCLIENTAREAANIMATION`, T082), `ThemeResolveTests` (high contrast); UIA providers for the picker, toolbar, pane tree and file grid (`UiaRootTests`, `UiaTitleBarButtonTests`, `UiaChromeTests`, `UiaFileListTests`, `UiaEventsTests`); the in-repo audit `UiaAuditTests` finds no violations (T083); visible focus (`FocusIndicatorTests`); [validation report, US5](./validation-report.md#phase-5-us5--interaction-and-accessibility-phase-7). Pending: Narrator ([accessibility.md](../../tests/manual/accessibility.md) B-b), FastPass (T096, kept for release testing by the project owner's decision), system menu / Snap Layouts by hand ([caption-and-snap.md](../../tests/manual/caption-and-snap.md)), real 100% / 200% scales ([dpi.md](../../tests/manual/dpi.md)). |

**Governance gates**:

- Release-readiness gates map to SC-001 to SC-011 and the V-1 to V-6 scenarios in
  [quickstart.md](./quickstart.md) → ✅
- Five-phase incremental delivery with reproducible stage validation (quickstart "Phase
  exit mapping") → ✅
- No undocumented Windows API is a required dependency. Research explicitly rejected
  `DWMWA_MICA_EFFECT`, `SetWindowCompositionAttribute` and the registry theme keys → ✅
  (post-implementation: still true; `BackdropManager.cpp` documents the rejection, and the
  one new COM API, `IAccPropServices`, is documented)
- Post-implementation: the automated evidence for every release-readiness gate is in the
  [validation report](./validation-report.md) (Phase 1 and US1–US6). The gates are not yet
  closed: the manual checklist rows above, T095 (Application Verifier) and T096 (FastPass)
  remain, and T099 closes them → ⏳ until T099

**Result**: PASS before research and after design. The Complexity Tracking table below
justifies the deviations from the constitution's recommendations; none of them breaks a
MUST rule. The one open question, whether the address field can stay translucent while
editing (Principle II is NON-NEGOTIABLE), was settled by the Phase 1 spike: the layered,
colour-keyed edit works ([spike-rendering.md](./spike-rendering.md)), so the tinted-edit
fallback and its sign-off are not needed. Any future sign-off is still recorded by the
project owner (the repository maintainer) in the "Sign-offs" section of
`validation-report.md`, and T099 blocks the release while any sign-off is open.

**Post-implementation result (T098, 2026-09-28)**: PASS. No MUST rule is broken by the
implementation. Four new deviations from recommended guidance appeared during
implementation and are recorded in Complexity Tracking below (opaque transient menus,
Dynamic Annotation for native EDITs, test hooks in production classes, and a folder
filter as the search facility). The "Sign-offs" section of `validation-report.md` still has
no open item. Documentation drift found during the re-check, not a constitution issue:
[ui-contract §5](./contracts/ui-contract.md) still shows the navigation pane as a List of
ListItems, while since T090 it is a Tree of TreeItems with ExpandCollapse, and it does not
list the breadcrumb buttons (T091) or the filter Edit (T092); the contract needs updating
once the project owner confirms the Tree structure.

## Project Structure

### Documentation (this feature)

```text
specs/001-translucent-explorer/
├── plan.md                        # This file
├── research.md                    # Phase 0 output (R-01 … R-15)
├── data-model.md                  # Phase 1 output
├── quickstart.md                  # Phase 1 output (build + V-1…V-6 validation)
├── contracts/
│   ├── settings.schema.json       # Persisted settings file contract
│   ├── component-interfaces.md    # Internal interfaces + cross-thread message contract
│   └── ui-contract.md             # Layout, hit testing, popup, keyboard, UIA, errors
├── checklists/requirements.md     # Spec quality checklist
├── spike-rendering.md             # Created in Phase 1 (research R-02, R-09)
├── spike-explorerbrowser.md       # Created in Phase 3 (research R-06)
├── validation-report.md           # Evidence added at each phase exit (SC-011)
└── tasks.md                       # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

This follows the constitution's recommended structure. It adds `core/`, `render/` and
`a11y/`; the reasons are in Complexity Tracking.

```text
CMakeLists.txt                     # top level: options, warnings, subdirs
CMakePresets.json                  # x64-debug, x64-release (VS 18 2026); vs2022-x64-debug/-release (VS 17 2022)
vcpkg.json                         # wil, nlohmann-json, gtest (baseline pinned)
README.md

include/te/
├── core/        Types.h, Result.h, Messages.h (WM_TE_*), ComInit.h, StaWorker.h, Watchdog.h
├── app/         Application.h, MainWindow.h, MainLayout.h
├── window/      ICaptionHitTester.h, CaptionHitTester.h, IDpiManager.h, DpiManager.h, CustomTitleBar.h
├── appearance/  IThemeManager.h, IBackdropManager.h, IColorPicker.h, ColorPicker.h,
│                AppearanceSettings.h, Palette.h, Contrast.h
├── settings/    ISettingsStore.h, SettingsManager.h
├── shell/       ShellTypes.h (ShellLocation, ShellItemInfo), IShellNavigator.h,
│                IDirectoryEnumerator.h, IIconProvider.h, IFileOperationService.h,
│                FileOperationService.h, ContextMenu.h, Clipboard.h
├── render/      IRenderDevice.h, RenderDevice.h, TextFormats.h, SurfacePainter.h
├── ui/          FileItem.h, FileItemFormat.h, SortTypes.h, IFileView.h, FileView.h, IAddressBar.h, AddressBar.h,
│                INavigationPane.h, NavigationPane.h, IStatusBar.h, StatusBar.h, Toolbar.h,
│                NavigationHistory.h, SortModel.h, SelectionModel.h
└── a11y/        UiaRoot.h

src/
├── CMakeLists.txt                 # te_core (static lib) + TranslucentExplorer (WIN32 exe)
├── app/         wWinMain.cpp, Application.cpp, MainWindow.cpp, MainLayout.cpp
├── core/        ComInit.cpp, Result.cpp, StaWorker.cpp (jthread + CoInitializeEx STA + pump),
│                Watchdog.cpp (Debug)
├── window/      CustomTitleBar.cpp, CaptionHitTesting.cpp, DpiManager.cpp
├── appearance/  BackdropManager.cpp, ThemeManager.cpp, ColorPicker.cpp, Contrast.cpp
├── settings/    SettingsManager.cpp
├── shell/       ShellLocation.cpp, ShellNavigator.cpp, DirectoryEnumerator.cpp,
│                IconProvider.cpp, FileOperationService.cpp, ContextMenu.cpp, Clipboard.cpp,
│                ExplorerBrowserSpike.cpp (only with TE_SPIKE_EXPLORERBROWSER; removed after the spike)
├── render/      RenderDevice.cpp (D3D11 + DXGI composition swap chain + DComp), TextFormats.cpp,
│                SurfacePainter.cpp
├── ui/          NavigationPane.cpp, AddressBar.cpp, Toolbar.cpp, FileView.cpp, StatusBar.cpp,
│                FileItemFormat.cpp, NavigationHistory.cpp, SortModel.cpp, SelectionModel.cpp
└── a11y/        UiaRoot.cpp, UiaTitleBarButton.cpp, UiaFileList.cpp, UiaFileItem.cpp,
                 UiaChrome.cpp (toolbar, address bar, navigation pane, status bar, and caption
                 buttons if the spike shows they need providers)

resources/
├── resource.h
├── TranslucentExplorer.rc         # icons, DIALOGEX for appearance popup, accelerators, strings
├── TranslucentExplorer.manifest   # PMv2, longPathAware, comctl32 v6, supportedOS, UTF-8
└── app.ico

tests/
├── CMakeLists.txt
├── unit/         SettingsTests.cpp, ThemeResolveTests.cpp, ContrastTests.cpp,
│                 CaptionLayoutTests.cpp, NavigationHistoryTests.cpp, SortModelTests.cpp,
│                 GenerationGuardTests.cpp
├── integration/  DirectoryEnumeratorTests.cpp, FileOperationServiceTests.cpp,
│                 WindowLifecycleTests.cpp
└── manual/       caption-and-snap.md, themes.md, picker.md, navigation.md,
                  file-operations.md, accessibility.md, dpi.md, performance.md

tools/
└── New-TestData.ps1               # builds %TEMP%\te-test fixtures (quickstart)
```

**Structure Decision**: One CMake project with two targets:

- **`te_core`**: a static library holding everything except `wWinMain`.
- **`TranslucentExplorer.exe`**: a WIN32 subsystem executable.

The tests link `te_core`, so pure logic (settings, theme resolution, layout math, history,
sorting) is testable without a window. Integration tests use real Shell and COM objects
on temp folders. Each `src/<area>` directory implements only the interfaces in
`include/te/<area>`, which enforces Principle VI.

## Complexity Tracking

> Deviations from the constitution's *recommended* or *preferred* guidance. No MUST rule
> is broken. As the constitution's Governance section requires for architectural
> choices, each row records the reason, compatibility with the native Win32 requirement,
> the effect on transparency and accessibility, and the tests that validate it.

| Deviation | Why Needed | Simpler Alternative Rejected Because | Win32 compatibility | Transparency / accessibility impact | Validating tests |
|-----------|------------|-------------------------------------|---------------------|-------------------------------------|------------------|
| DirectComposition + Direct3D 11 + DXGI composition swap chain, which are not in the constitution's preferred-technology table | Direct2D can only give per-pixel premultiplied alpha over a DWM system backdrop through a composition swap chain (FR-003, Principle II) | `ID2D1HwndRenderTarget` and GDI cannot composite correct alpha; `WS_EX_LAYERED` disables system backdrops (research R-02) | Native, documented Windows APIs bound to the Win32 HWND; Win32 still owns the window and message loop | Enables translucency. Custom-drawn content has no built-in accessibility, so UIA providers are required (R-09, US5) | T027 rendering spike; V-1a–e; T029/T085 lifecycle leak tests (device resources released) |
| C++/WinRT `Windows.UI.ViewManagement.UISettings` | This is the documented Win32 way to read dark/light mode, the transparency-effects setting, text scale and the accent color (Principle X) | The registry keys are undocumented (constitution Governance forbids undocumented dependencies); there is no pure Win32 equivalent for `AdvancedEffectsEnabled` | A WinRT API called from Win32, not a UI framework; no WinUI or XAML is loaded | Improves both: honors the transparency-effects switch and text scaling | T031 theme resolution tests; V-1c; V-5e |
| Extra source folders `core/`, `render/`, `a11y/` beyond the recommended layout | Shared threading and COM helpers, the render device and the UIA providers have no natural home among the recommended folders, and mixing them in would blur the Principle VI boundaries | Putting render and UIA code inside `ui/` would couple the file view to device and automation details | No effect | None | Build (T005); code review against Principle VI |
| Third-party packages `wil`, `nlohmann-json`, `gtest` | RAII wrappers (Principle VIII), a JSON parser (preferred settings format) and a test framework | Hand-written RAII and a JSON parser would be more code to verify. All three are header-only or test-only, and none is a UI framework. | Header-only or test-only; nothing is added to the UI stack | None | T041 settings tests; T029/T085 leak tests; T093 CRT leak check |
| Appearance popup drawn opaque | Standard controls give accessibility, IME and high-contrast support for free (Principle X). The popup is transient, and it is not one of the surfaces that FR-003 and Principle II require to be translucent. | A fully custom Direct2D popup would duplicate the UIA work for little benefit | Standard Win32 dialog and common controls | No effect on the required surfaces. Improves accessibility | V-2b–h; V-5a–c; T096 FastPass |
| Address field while editing: translucent layered, colour-keyed edit (confirmed by the T027 spike; the tinted opaque fallback is not used) | A standard `EDIT` gives IME, caret, selection and UIA text support. Principle II requires the navigation area, which includes the address bar, to be translucent. | A custom-drawn text editor would need its own IME, caret and UIA text provider. The risk is disproportionate for the first release. | Standard Win32 `EDIT` control (optionally layered, supported for child windows since Windows 8) | With the layered edit: translucent, no deviation. With the fallback: opaque only while the user is typing, colored to match the surface, translucent otherwise. **The fallback requires explicit sign-off by the project owner**, recorded in `validation-report.md` "Sign-offs", because Principle II is NON-NEGOTIABLE. | T027 spike records which approach works; V-3a; V-5b (Narrator reads the address field) |
| Transient menus drawn opaque: the Shell context menu (T069) and the breadcrumb chevron menu (T091, `TrackPopupMenuEx`) | Standard Win32 and Shell menus give keyboard access, UIA, high contrast and Shell verbs for free (Principles IV, X) | Custom Direct2D menus would duplicate menu accessibility and could not host Shell verbs from `IContextMenu` | Standard Win32 popup menus; the Shell's own `IContextMenu` | Menus are transient and not among the surfaces that FR-003 and Principle II require to be translucent. Improves accessibility | `ContextMenuTests`; `AddressBarBreadcrumbTests`; V-4 scenarios; `UiaAuditTests` |
| Dynamic Annotation (`IAccPropServices`, oleacc) to name the native address, rename and filter EDITs (T083) | A native EDIT inside a custom-drawn parent has no label, so a screen reader said only "edit" (Principle X) | The `IRawElementProviderHwndOverride` provider used before T083 also appeared as a stray nested element in the UIA tree | Documented Windows accessibility COM API, called on the UI thread; no UI framework | Adds accessible names ("Address", "Name", filter placeholder); no visual effect | `UiaAuditTests`; `UiaChromeTests`; `FileViewRenameTests` |
| Test hooks in production classes: `MainWindow::Options` callbacks (`adjustCapabilities`, `shutdownTrace`, `contextMenuTracker`, `breadcrumbMenuTracker`, `registerModelessDialog`), `NavigationPane::SetPlacesForTesting`, `Watchdog::SetSinkForTesting` | Lets integration tests drive modal menus, OS capability changes and shutdown order through window messages alone, without injecting input or changing the tester's system settings | Mocking the OS behind more interfaces would add layers to every component; injecting mouse or keyboard input is unreliable and disturbs the desktop | No effect; the hooks are empty in the shipped application | None | `MainWindowShutdownTests`; `MainWindowLiveSettingsTests`; `AddressBarBreadcrumbTests`; `ContextMenuTests`; code review ([review-checklist.md](../../docs/review-checklist.md)) |
| The search facility (Principle V, SHOULD) is a filter of the current folder by name (T092), not a recursive search | It covers the common "find a file in this folder" need at no cost to responsiveness, and uses the linguistic, case-insensitive matching of `FindNLSStringEx` | A recursive search (or Windows Search) needs its own cancellable worker, result view and UI Automation, which is out of proportion for the first release | Pure Win32 / NLS; the filter box is a layered native EDIT like the address bar | Stays translucent while typing; named for UIA; Ctrl+F from anywhere | `FileViewFilterTests`; `MainWindowFilterTests`; [polish-filter.png](./validation/polish-filter.png) |
