---

description: "Task list for Translucent Explorer (001-translucent-explorer)"
---

# Tasks: Translucent Explorer

**Input**: Design documents from `/specs/001-translucent-explorer/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Included. The spec requires pass/fail test suites (SC-005 to SC-011). The
constitution requires reproducible validation for every stage, and plan.md / research R-14
define unit, integration and manual suites. Where test tasks appear, write them before the
implementation they cover, and confirm they fail first.

**Organization**: Tasks are grouped by user story so each story can be built and tested on
its own.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on unfinished tasks)
- **[Story]**: The user story the task belongs to (US1 to US6)
- Every task names the exact files it touches

## Path Conventions

This is a single native project at the repository root:

- `include/te/<area>/` holds the interfaces
- `src/<area>/` holds the implementations
- `resources/` holds `.rc`, manifest and icon files
- `tests/unit|integration|manual/` holds the tests
- `tools/` holds scripts

Design references used below:

- **R-xx**: [research.md](./research.md)
- **DM**: [data-model.md](./data-model.md)
- **CI**: [contracts/component-interfaces.md](./contracts/component-interfaces.md)
- **UI**: [contracts/ui-contract.md](./contracts/ui-contract.md)
- **V-x**: [quickstart.md](./quickstart.md)

## Constitution delivery-phase mapping

| Constitution phase | Tasks in this file |
|--------------------|--------------------|
| 1 — Win32 shell, MSVC build, title bar | Phase 1 Setup + Phase 2 Foundational |
| 2 — Backdrops, picker, tint, persistence | Phase 3 (US1) + Phase 4 (US2) |
| 3 — Shell navigation, file view | Phase 5 (US3) |
| 4 — File operations, context menus, errors | Phase 6 (US4) |
| 5 — Accessibility, DPI, performance, release | Phase 7 (US5) + Phase 8 (US6) + Phase 9 Polish |

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the CMake/MSVC project, dependencies, manifest and resources.

- [X] T001 Create the directory skeleton from plan.md "Source Code": `include/te/{core,window,appearance,settings,shell,render,ui}/`, `src/{app,core,window,appearance,settings,shell,render,ui,a11y}/`, `resources/`, `tests/{unit,integration,manual}/` and `tools/`. Put a `.gitkeep` in each empty folder.
- [X] T002 Create `vcpkg.json` at the repo root with the dependencies `wil`, `nlohmann-json` and `gtest`. Pin `builtin-baseline` to a current vcpkg commit SHA, and set the project name to `translucent-explorer`, version `0.1.0` (R-13).
- [X] T003 Create the root `CMakeLists.txt`:
  - `cmake_minimum_required(VERSION 3.28)` and `project(TranslucentExplorer LANGUAGES CXX)`.
  - `CMAKE_CXX_STANDARD 20` with the standard required.
  - Fail configuration unless `CMAKE_GENERATOR_PLATFORM` is `x64`.
  - An INTERFACE target `te_warnings` with `/W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus /guard:cf /sdl /external:anglebrackets /external:W0`.
  - An INTERFACE target `te_defs` with `UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 NTDDI_VERSION=NTDDI_WIN10_NI`.
  - Release adds `/O2 /GL` and links with `/LTCG /GUARD:CF /DYNAMICBASE`.
  - `enable_testing()`, then `add_subdirectory(src)` and `add_subdirectory(tests)`.
- [X] T004 Create `CMakePresets.json`:
  - Configure presets `x64-debug` and `x64-release`: generator "Visual Studio 18 2026" (the development machine has Visual Studio 2026), plus `vs2022-x64-debug` and `vs2022-x64-release` variants with generator "Visual Studio 17 2022"; architecture `x64`, `binaryDir` `${sourceDir}/out/build/${presetName}`, toolchain `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`.
  - Matching build presets (configuration Debug or Release).
  - Test presets with `output.outputOnFailure = true`.
- [X] T005 Create `src/CMakeLists.txt` with two targets:
  - Collect sources with `file(GLOB_RECURSE TE_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")` and remove `app/wWinMain.cpp` from the list.
  - Static library `te_core` from that list, with public include dir `include/`. So that CMake can configure before any source exists, add a one-line placeholder `src/core/Placeholder.cpp` (`namespace te {}`); delete it once T013 adds real sources.
  - `add_executable(TranslucentExplorer WIN32 app/wWinMain.cpp ../resources/TranslucentExplorer.rc)`, linked to `te_core`. Create `src/app/wWinMain.cpp` now as a stub (`int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return 0; }`); T025 replaces the body.
  - Both link `te_warnings`, `te_defs`, `WIL::WIL`, `nlohmann_json::nlohmann_json`, `dwmapi d2d1 dwrite d3d11 dxgi dcomp windowscodecs uiautomationcore shlwapi comctl32 propsys runtimeobject`.
  - Embed the manifest with `/MANIFESTINPUT:resources/TranslucentExplorer.manifest` and `/MANIFEST:EMBED`.
- [X] T006 Create `tests/CMakeLists.txt`:
  - `find_package(GTest CONFIG REQUIRED)`.
  - Executables `te_unit_tests` (all `tests/unit/*.cpp`) and `te_integration_tests` (all `tests/integration/*.cpp`, both globbed with `CONFIGURE_DEPENDS`), both linked to `te_core`, `GTest::gtest_main`, `te_warnings` and `te_defs`. Create each executable only `if()` its source list is not empty, so CMake configures before the first test file exists.
  - `gtest_discover_tests` with `PROPERTIES LABELS unit` or `LABELS integration`, and `DISCOVERY_TIMEOUT 60`.
- [X] T007 [P] Create `resources/TranslucentExplorer.manifest`:
  - `<dpiAwareness>PerMonitorV2</dpiAwareness>` and `<longPathAware>true</longPathAware>`.
  - A Common Controls v6 dependency (`Microsoft.Windows.Common-Controls` 6.0.0.0).
  - The `supportedOS` Id `{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}`.
  - `<activeCodePage>UTF-8</activeCodePage>`.
- [X] T008 [P] Create `resources/resource.h` and `resources/TranslucentExplorer.rc`:
  - `IDI_APP` icon pointing to `resources/app.ico` (add a placeholder 16/32/48/256 icon).
  - A `STRINGTABLE` with the app title "Translucent Explorer" and all user-visible status and error strings from UI §3 and §6.
  - An empty `IDR_ACCEL` accelerator table, to be filled by later tasks.
- [X] T009 [P] Add build output to the ignore list: `out/`, `vcpkg_installed/`, `*.user`, `.vs/`. Check the existing `.gitignore` at the repo root and add only the entries that are missing.
- [X] T010 [P] Add `.clang-format` (based on Microsoft, `ColumnLimit: 110`) and `.editorconfig` (UTF-8, CRLF for `*.rc`, LF otherwise, 4-space indent) at the repo root.

**Checkpoint**: `cmake --preset x64-debug` configures, and `cmake --build --preset
x64-debug` builds the stub executable with zero warnings.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: A working native window with a custom DWM frame, correct hit testing,
Direct2D/DirectComposition rendering, STA worker infrastructure and the shared contract
headers. This is constitution delivery phase 1.

**⚠️ CRITICAL**: No user-story work can start until this phase is complete.

- [X] T011 [P] Create `include/te/core/Types.h` with `Generation`, `BackdropMode`, `FallbackReason`, `Rgb` and `Status`, exactly as in CI "Common types".
- [X] T012 Create `include/te/core/Messages.h`:
  - Constants `WM_TE_ENUM_BATCH` (`WM_APP+1`) to `WM_TE_BACKDROP_FAILED` (`WM_APP+7`).
  - The payload structs from the CI message table. They use `ShellLocation` and `ShellItemInfo` from `include/te/shell/ShellTypes.h` (T016), so this task depends on T016.
  - `template<class T> bool PostOwned(HWND, UINT, std::unique_ptr<T>&)`, which releases ownership only when `PostMessageW` succeeds.
  - `template<class T> std::unique_ptr<T> TakeOwned(LPARAM)`.
- [X] T013 [P] Create `include/te/core/ComInit.h` and `src/core/ComInit.cpp` with RAII scopes `OleScope` (`OleInitialize` / `OleUninitialize`) and `StaScope` (`CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)` / `CoUninitialize`). Both throw `wil::ResultException` on failure. Delete the temporary `src/core/Placeholder.cpp` from T005, now that `te_core` has real sources.
- [X] T014 Create `include/te/core/StaWorker.h` and `src/core/StaWorker.cpp`, a class `StaWorker`:
  - Owns a `std::jthread` that enters a `StaScope` and runs a `PeekMessage` / `MsgWaitForMultipleObjectsEx` pump.
  - Serves a thread-safe `std::deque<std::function<void(std::stop_token)>>`.
  - Provides `Post(task)`, `PostFront(task)` (for LIFO use), `RequestStop()` and `Join()`.
  - The destructor requests stop and joins (R-07).
- [X] T015 [P] Create `include/te/core/Result.h` and `src/core/Result.cpp`:
  - `std::wstring HresultMessage(HRESULT)` using `FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS)`, trimmed.
  - `Status MakeStatus(HRESULT, std::wstring_view context)`.
- [X] T016 [P] Create the shared data-type headers, so that the messages and interfaces compile before US3. These hold declarations and plain data only; the behavior is implemented in US3 (T053, T055, T056):
  - `include/te/shell/ShellTypes.h`: the `ShellLocation` class declaration (PIDL owned by `wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>`, with the methods listed in T053) and the `ShellItemInfo` struct (the DM "FileItem" fields without UI state: child PIDL, name, type text, optional size, optional modified time, attribute flags).
  - `include/te/ui/FileItem.h`: `FileItem` (`ShellItemInfo` plus `IconSlot`, selection state and generation, as DM "FileItem").
  - `include/te/ui/SortTypes.h`: the `SortField` and `SortDirection` enums and the `SortState` struct (DM "SortState").
  Then create all interface headers exactly as declared in CI:
  - `include/te/window/ICaptionHitTester.h`, `include/te/window/IDpiManager.h`
  - `include/te/appearance/{IThemeManager.h,IBackdropManager.h,IColorPicker.h,AppearanceSettings.h}`
  - `include/te/settings/ISettingsStore.h`
  - `include/te/shell/{IShellNavigator.h,IDirectoryEnumerator.h,IIconProvider.h,IFileOperationService.h}`
  - `include/te/render/IRenderDevice.h`
  - `include/te/ui/{IFileView.h,IAddressBar.h,INavigationPane.h,IStatusBar.h}`
  - Use forward declarations only where CI marks a struct as defined elsewhere.
- [X] T017 [P] Implement `IDpiManager` in `src/window/DpiManager.cpp` (class declared in `include/te/window/DpiManager.h`):
  - `GetDpiForWindow` and `Scale()`.
  - `ToPx(dip) = MulDiv(dip, dpi, 96)` with rounding.
  - `OnDpiChanged` calls `SetWindowPos` with the suggested `RECT` (`SWP_NOZORDER | SWP_NOACTIVATE`).
- [X] T018 Implement `IRenderDevice` in `src/render/RenderDevice.cpp` (class declared in `include/te/render/RenderDevice.h`) (R-02):
  - `D3D11CreateDevice` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`), `IDXGIFactory2::CreateSwapChainForComposition` (`DXGI_FORMAT_B8G8R8A8_UNORM`, `DXGI_ALPHA_MODE_PREMULTIPLIED`, `FLIP_SEQUENTIAL`, 2 buffers).
  - `ID2D1Factory1` → `ID2D1Device` → `ID2D1DeviceContext` whose target is an `ID2D1Bitmap1` from the back buffer.
  - `DCompositionCreateDevice`, then `CreateTargetForHwnd(hwnd, FALSE, …)`, a visual `SetContent(swapChain)` and `Commit`.
  - `Resize` calls `ResizeBuffers` and sets the Direct2D DPI.
  - `EndDrawAndPresent` handles `D2DERR_RECREATE_TARGET` and `DXGI_ERROR_DEVICE_REMOVED` by recreating everything.
- [X] T019 [P] Create `src/render/TextFormats.cpp` and `include/te/render/TextFormats.h`: a DirectWrite factory, plus `IDWriteTextFormat`s for title, body, header and status in "Segoe UI Variable" (falling back to "Segoe UI"). Sizes are in DIP × `textScaleFactor`, and the formats are rebuilt on a DPI or text-scale change. (Implemented: the system collection lists the variable font as "Segoe UI Variable Text", which is tried first; DIP sizes need no DPI change because the Direct2D context applies the DPI, but `Rebuild` is safe to call on `WM_DPICHANGED`.)
- [X] T020 [P] Write the failing test `tests/unit/CaptionLayoutTests.cpp` for `CaptionHitTester::Compute`, using injected metric values. Cover 96, 144 and 192 DPI, normal and maximized. Assert that:
  - the picker's right edge equals `captionButtons.left - ToPx(8)`
  - the picker width equals `ToPx(40)`
  - `pickerRect ∩ captionButtons` is empty
  - `dragRegion` excludes the picker and the caption buttons
  - the top resize band is used only when not maximized
- [X] T021 Implement `ICaptionHitTester` in `src/window/CaptionHitTesting.cpp` (R-03, UI §2):
  - `Compute` uses `GetSystemMetricsForDpi(SM_CYCAPTION, SM_CYFRAME, SM_CXPADDEDBORDER)` and `DwmGetWindowAttribute(DWMWA_CAPTION_BUTTON_BOUNDS)`. Metrics come through an injectable provider, so T020 can run without a window.
  - `HitTest` checks, in order: `DwmDefWindowProc`, the top band, the picker (`HTCLIENT`), the drag region (`HTCAPTION`), then `HTCLIENT`.
  - Make T020 pass.
- [X] T022 Create `src/window/CustomTitleBar.cpp` and `include/te/window/CustomTitleBar.h` for the custom frame (R-03):
  - `WM_NCCALCSIZE` (`wParam == TRUE`): keep the left, right and bottom borders from `DefWindowProc`, and remove the top caption. (Phase 1 validation: do **not** add a top inset when maximized — `DwmDefWindowProc` only hit-tests the caption buttons when the client area starts at the window's top edge; the layout reports the off-screen rows as `CaptionLayout::contentTopPx` instead. See validation-report.md.)
  - After `WM_CREATE` and `WM_DWMCOMPOSITIONCHANGED`, call `DwmExtendFrameIntoClientArea({-1,-1,-1,-1})`.
  - `WM_PAINT` fills the update region with `BLACK_BRUSH`.
  - `WM_GETMINMAXINFO` enforces the minimum size from UI §1.
  - A `Render(ID2D1DeviceContext*)` method draws the app icon and title. It leaves the picker rectangle and the caption-button area at zero alpha until US2 adds the picker. The title uses `DWRITE_TRIMMING_GRANULARITY_CHARACTER` with an ellipsis sign, so it is truncated before it reaches the picker in narrow windows (spec edge case "Very narrow windows").
- [X] T023 Create `src/app/MainLayout.cpp` and `include/te/app/MainLayout.h`: compute the rectangles for the caption strip, toolbar (40 DIP), navigation pane (220 DIP, clamped), file list and status bar (24 DIP) from the client size and DPI (UI §1).
- [X] T024 Create `src/app/MainWindow.cpp` and `include/te/app/MainWindow.h`:
  - Register `WNDCLASSEXW` (`CS_HREDRAW | CS_VREDRAW`, `hbrBackground = nullptr`, icon `IDI_APP`).
  - `CreateWindowExW(WS_EX_APPWINDOW, …, WS_OVERLAPPEDWINDOW, …)`.
  - The window procedure dispatches in this order: `CustomTitleBar` → hit tester for `WM_NCHITTEST` → `WM_SIZE` (render `Resize` + layout) → `WM_DPICHANGED` (`IDpiManager`, text formats, layout) → `WM_PAINT` → a render pass that draws the empty layout regions.
  - `WM_DESTROY` runs the shutdown sequence from CI "Cross-thread message contract": shut down workers, then drain `WM_TE_*` with `PeekMessageW(PM_REMOVE)`, freeing payloads with `TakeOwned`, then `PostQuitMessage`.
- [X] T025 Create `src/app/Application.cpp`, `include/te/app/Application.h` and `src/app/wWinMain.cpp`:
  - `wWinMain` creates an `OleScope`, calls `InitCommonControlsEx(ICC_STANDARD_CLASSES | ICC_BAR_CLASSES)` and parses the command line with `CommandLineToArgvW`: an optional folder path, plus, in Debug builds only (`#ifdef _DEBUG`), `--backdrop=acrylic|mica|solid` for testing. Release builds ignore `--backdrop` (UI §4 "Command line").
  - It builds `MainWindow` and runs the message loop: `GetMessageW` → `IsDialogMessageW` for the registered modeless popup → `TranslateAcceleratorW(IDR_ACCEL)` → `TranslateMessage` / `DispatchMessageW`. `LoadAcceleratorsW(IDR_ACCEL)` returns nullptr until the table has its first entry (rc.exe omits empty tables; see the note in the `.rc`), so skip `TranslateAcceleratorW` when the handle is null.
  - The exit code comes from `WM_QUIT`.
- [X] T026 [P] Create `src/core/Watchdog.cpp` and `include/te/core/Watchdog.h`: a Debug-only (`#ifdef _DEBUG`) RAII timer around `DispatchMessageW` in `Application.cpp`. It logs `OutputDebugStringW(L"[te-watchdog] msg=0x%04X took %lld ms")` when a handler takes over 50 ms (R-14). (Implemented: messages that run a nested modal loop inside `DispatchMessageW` — `WM_NCLBUTTONDOWN`, `WM_SYSCOMMAND`, `WM_NCRBUTTONUP`, `WM_CONTEXTMENU` — are not reported, since their duration is the user's move/size or menu time.)
- [X] T027 Rendering spike (R-02, R-09) — results in [spike-rendering.md](./spike-rendering.md): (a) pass, (b) pass only with `WS_CLIPCHILDREN` (now on `MainWindow`) and plain `EDIT` text is transparent, (c) pass, (d) pass, (e) pass: in the running app, temporarily create a child `EDIT` in the toolbar rectangle and verify:
  - (a) the DWM caption buttons are visible and clickable
  - (b) the `EDIT` paints above the DirectComposition visual
  - (c) text is correct in light and dark mode
  - (d) the address edit can be truly translucent: make it a layered child (`WS_EX_LAYERED` + `SetLayeredWindowAttributes(LWA_COLORKEY)`, background brush = key color) and check that the backdrop shows through, the text has no key-color fringes (try grayscale anti-aliasing), and the caret and selection render correctly
  - (e) with Accessibility Insights for Windows, the native Minimize, Maximize/Restore and Close buttons still appear in the UIA tree as buttons that can be invoked
  Record the results and screenshots in `specs/001-translucent-explorer/spike-rendering.md`. Record these decisions there and in the approach notes of the affected tasks in this file:
  - If (b) fails, use the owned-popup fallback for the address edit (R-02) in T061.
  - If (d) passes, T061 uses the layered edit. If (d) fails, T061 uses the tinted `WM_CTLCOLOREDIT` fallback, and the plan.md Complexity Tracking row for the address field must be signed off before release.
  - If (e) fails, T078 also adds caption-button providers.
  Remove the temporary `EDIT` afterwards.
- [X] T028 [P] Create `tools/New-TestData.ps1`. With parameter `-Root`, it creates:
  - `nested\a\b\c` containing a few `.txt` files
  - `10k\` with 10,000 zero-byte files named `file00001.txt`…
  - `conflicts\src` and `conflicts\dst` with 5 identically named files of different content
  - `readonly-acl\`, with list and write access denied to the current user via `icacls`
  - `unicode\` with emoji, CJK and RTL filenames
  - `longpath\`, a path deeper than 260 characters using the `\\?\` prefix
  - `unknown.zzz`, a file with no association
  The script is idempotent and has a `-Remove` switch that restores the ACLs and deletes the tree (quickstart "Test data").
- [X] T029 Write `tests/integration/WindowLifecycleTests.cpp` (a baseline is taken after 2 warm-up cycles, because the first window loads per-process resources such as graphics driver DLLs):
  - Record a baseline of three counts: `GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)`, `GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)` (two separate calls; the flags are values, not combinable bits) and `GetProcessHandleCount`.
  - Create a `MainWindow`, pump messages until it is shown, then `DestroyWindow` and pump until destroyed, 50 times.
  - Assert that each of the three counts returns to its baseline (SC-010, V-6b).
- [X] T030 Create the manual checklist `tests/manual/caption-and-snap.md`, covering V-1d and V-5d for the caption only. The picker is not visible yet; its area is checked by T020. Include: drag, resize from all edges, double-click, Min/Max/Restore/Close, `Alt+Space`, `Win+Arrow`, Snap Layouts hover, `Alt+Tab` and the taskbar thumbnail. Each row has Expected, Result and Build columns. Then, as the last step of Phase 2, run the phase exit checks (see the checkpoint below), create `specs/001-translucent-explorer/validation-report.md` with an empty "Sign-offs" section, and add the "Phase 1" section with the results (SC-011, quickstart "Recording evidence").

**Checkpoint (constitution phase 1 exit)**:

- Both presets build with zero warnings.
- T020 and T029 pass.
- `tests/manual/caption-and-snap.md` passes.
- The spike is recorded.
- `validation-report.md` has a "Phase 1" section with date, commit, Windows build, each check above with pass or fail, and links to logs and screenshots (recorded by T030).

---

## Phase 3: User Story 1 — Launch and use a translucent Explorer window (Priority: P1) 🎯 MVP

**Goal**: The window opens with a supported backdrop (Mica by default) and switches
between Acrylic, Mica and Solid without restarting. It falls back honestly when a backdrop
is unavailable or when system settings require opacity, and it keeps every surface
legible (FR-002, FR-003, FR-008; SC-001, SC-004).

**Independent Test**:

1. Using a Debug build, launch with `--backdrop=mica`, then `acrylic`, then `solid`, on
   build 22621 or later.
2. Toggle Windows transparency effects and high contrast while the app runs.
3. Run V-1a to V-1e. The mode can be switched at runtime with the Debug-only `--backdrop`
   handoff from T037, which works before US2's picker exists.

### Tests for User Story 1

- [X] T031 [P] [US1] Write the failing test `tests/unit/ThemeResolveTests.cpp`. It covers the full DM "EffectiveAppearance" resolution truth table: high contrast beats transparency-off, which beats unsupported, which beats apply-failed, which beats requested. It also asserts that:
  - `tintAlpha == 0` and `surfaceAlpha == 1` when the applied mode is Solid
  - in Acrylic or Mica, `surfaceAlpha == surfaceOpacity` and `tintAlpha == tintOpacity`, and changing one input never changes the other output (constitution Principle VII)
  - `opacityControlEnabled == false` in Solid or high contrast
  - `"accent"` resolves to `capabilities.accent`
  - `baseColor` is `#202020` in dark mode and `#F3F3F3` in light mode
  - applied Acrylic in dark mode with `surfaceOpacity = 0` gives `textScrimAlpha > 0`; applied Solid gives `textScrimAlpha == 0`
- [X] T032 [P] [US1] Write the failing test `tests/unit/ContrastTests.cpp`. It covers WCAG relative luminance (sRGB linearization), with contrast ratios for known pairs: black on white = 21.0, `#767676` on white ≈ 4.54. It also covers the legibility guard from research R-05:
  - `Composite(under, over, alpha)` for alpha 0, 0.5 and 1.
  - `PickTextColors` takes the backdrop extremes for the applied mode and chooses the text color with the highest *minimum* contrast across them.
  - **Acrylic over a bright wallpaper**: dark mode, Acrylic, `surfaceAlpha = 0`, no tint, extremes black and white. The result must have `textScrimAlpha > 0`, and the resulting contrast against the white extreme must be at least 4.5:1.
  - The same case with `surfaceAlpha = 0.9` needs no extra scrim (`textScrimAlpha == 0`).
  - Mica, dark mode, `surfaceAlpha = 0`: contrast is checked against both `#202020` and `#202020` moved 25% toward white.
  - `textScrimAlpha`, when non-zero, is never below `surfaceAlpha`, and is a multiple of 0.05.
  - **Text on selected rows** (`PickSelectedRowColors`): with a mid-gray accent `#808080` in dark mode (Mica, `surfaceAlpha = 0`), the chosen `selectedText` must reach 4.5:1 against every `Composite(textAreaComposite, selection, selectionAlpha)`. It does this by switching text color or by raising `selectionAlpha` above 0.40.
  - With a dark accent such as `#003E92` in dark mode, `selectionAlpha` stays at 0.40 and `selectedText == text`.
  - `selectionAlpha` is always a multiple of 0.05 between 0.40 and 1.0. The selection fill still reaches 3:1 against the unselected surface.

### Implementation for User Story 1

- [X] T033 [P] [US1] Implement `include/te/appearance/Contrast.h` and `src/appearance/Contrast.cpp` (R-05):
  - `double RelativeLuminance(Rgb)`
  - `double ContrastRatio(Rgb, Rgb)`
  - `Rgb Composite(Rgb under, Rgb over, float alpha)`, meaning `over` drawn at `alpha` on top of `under`
  - `BackdropExtremeSet BackdropExtremes(BackdropMode applied, bool darkMode, Rgb base)`, as the table in R-05. The set holds one or two colors and converts to `std::span<const Rgb>`; a plain span return would dangle, because the Mica extremes depend on `base`. Declared in namespace `te::Contrast` (T032)
  - `struct TextChoice { Rgb text; Rgb secondary; float scrimAlpha; } PickTextColors(std::span<const Rgb> extremes, Rgb base, float surfaceAlpha, Rgb tint, float tintAlpha)`, which composes each extreme as `Composite(Composite(extreme, base, surfaceAlpha), tint, tintAlpha)`, picks the text color with the best minimum contrast, and, if that is below 4.5:1, finds the smallest scrim alpha (steps of 0.05, at least `surfaceAlpha`) that reaches it. When neither color passes without a scrim, it picks the one that needs the smaller scrim (R-05 step 3). `secondary` is the text faded up to 30% toward the other color, as far as 4.5:1 still holds
  - `struct SelectionChoice { float selectionAlpha; Rgb selectedText; } PickSelectedRowColors(std::span<const Rgb> textAreaComposites, Rgb selection, Rgb text, Rgb otherText)`, which follows the R-05 "Text on selected rows" steps: start at alpha 0.40 with the normal text color, then try the other text color, then raise the alpha in steps of 0.05 up to 1.0 until one of them reaches 4.5:1 against every `Composite(textAreaComposite, selection, alpha)`
  Make T032 pass.
- [X] T034 [US1] Implement `IThemeManager` in `src/appearance/ThemeManager.cpp` (R-05, DM "RenderingCapabilities"):
  - `QueryCapabilities` reads C++/WinRT `winrt::Windows::UI::ViewManagement::UISettings` (`AdvancedEffectsEnabled`, `GetColorValue(Foreground)` luminance for dark mode, `GetColorValue(Accent)`, `TextScaleFactor`), `SystemParametersInfoW(SPI_GETHIGHCONTRAST)` and `SPI_GETCLIENTAREAANIMATION`. `systemBackdropSupported` is filled in by the caller from `IBackdropManager::ProbeSystemBackdrop`.
  - `Resolve` is the pure function under test in T031. It uses `Contrast::BackdropExtremes` and `PickTextColors` for the text colors and `textScrimAlpha`, checks selection and focus against the same extremes at 3:1, uses `PickSelectedRowColors` to fill `selectionAlpha` and `selectedText`, and fills `typicalSurfaceColor` (R-05). In high contrast it uses `GetSysColor(COLOR_WINDOW, COLOR_WINDOWTEXT, COLOR_HIGHLIGHT, COLOR_HOTLIGHT)`, with `COLOR_HIGHLIGHTTEXT` as `selectedText` and `selectionAlpha = 1.0`.
  - Subscribe to `ColorValuesChanged`, `AdvancedEffectsEnabledChanged` and `TextScaleFactorChanged`. Each handler posts `WM_TE_SETTINGS_CHANGED` to the main window; there is no UI work on WinRT threads. (Implemented as `ThemeManager::Subscribe(HWND)`; posts are coalesced until the owner calls `ThemeManager::NotifyChanged()` on the UI thread for `WM_TE_SETTINGS_CHANGED` / `WM_SETTINGCHANGE`, which raises the changed callback. T037 wires both.)
  - Make T031 pass.
- [X] T035 [US1] Implement `IBackdropManager` in `src/appearance/BackdropManager.cpp` (R-01, R-04):
  - `ProbeSystemBackdrop` sets `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW` and reports whether the call returned `SUCCEEDED`.
  - `Apply` does three things:
    - Sets `DWMWA_USE_IMMERSIVE_DARK_MODE` from `darkMode`. (`EffectiveAppearance` has no `darkMode` field, so it is derived from the luminance of `base`; in high contrast that follows `COLOR_WINDOW`.)
    - Sets `DWMWA_SYSTEMBACKDROP_TYPE`: Mica = `DWMSBT_MAINWINDOW`, Acrylic = `DWMSBT_TRANSIENTWINDOW`, Solid = `DWMSBT_NONE`.
    - In Solid mode only, sets `DWMWA_CAPTION_COLOR` and `DWMWA_BORDER_COLOR` to the base color; otherwise resets them with `DWMWA_COLOR_DEFAULT`.
  - Return the backdrop `HRESULT`.
  - Never call `DWMWA_MICA_EFFECT` or `SetWindowCompositionAttribute`. Add a comment citing TC-010.
- [X] T036 [US1] Create `src/render/SurfacePainter.cpp` and `include/te/render/SurfacePainter.h`: `PaintSurfaces(ID2D1DeviceContext*, const MainLayout&, const EffectiveAppearance&)`.
  - Clear to transparent (`D2D1::ColorF(0, 0)`).
  - In Solid mode, fill every region with the opaque `baseColor`.
  - In Acrylic or Mica, paint two independent layers over the caption, toolbar (including the address bar), navigation pane, file-list and status-bar regions (FR-003, R-04): first the surface layer, `baseColor × surfaceAlpha`; then the tint layer, `tint × tintAlpha`. Both are premultiplied.
  - Leave the caption-button rectangle at zero alpha in every mode. (The rectangle is `MainLayout::captionButtons`, in DIPs, filled by `MainWindow::UpdateLayout` from `CaptionLayout::captionButtons` via `MainLayout::ToDip`.)
  - Text areas use a surface-opacity **floor**, not an extra layer (R-05). When `textScrimAlpha > 0`, the surface layer inside each text area (each visible file row, the address text, navigation entries, status text and the caption title) is drawn at `max(surfaceAlpha, textScrimAlpha)` *instead of* `surfaceAlpha`. The layer order is always the same, inside and outside text areas: backdrop → surface layer → tint layer → text. This matches the composite that T033 checks. The rest of the surface keeps the user's `surfaceAlpha`.
  - Provide this as `SurfacePainter::PaintTextScrim(ID2D1DeviceContext*, D2D1_RECT_F, const EffectiveAppearance&)`. It replaces the surface layer inside the rectangle and redraws the tint layer there, so the result is the same as if the whole region had been painted at the floor opacity. It does nothing when `textScrimAlpha <= surfaceAlpha`. The tasks that draw text areas (T039, T060, T061, T063) call it for each area before drawing text.
  - In high contrast, use only system colors and draw no overlay.
- [X] T037 [US1] Wire the appearance pipeline into `src/app/MainWindow.cpp`:
  - On `WM_CREATE`: `ProbeSystemBackdrop` → `QueryCapabilities` → `Resolve` → `Apply`. If `Apply` fails, post `WM_TE_BACKDROP_FAILED`.
  - On `WM_SETTINGCHANGE` (`lParam` `"ImmersiveColorSet"` or `SPI_SETHIGHCONTRAST`), `WM_DWMCOMPOSITIONCHANGED`, `WM_THEMECHANGED` and `WM_TE_SETTINGS_CHANGED`: re-query, re-resolve, re-apply and invalidate.
  - On `WM_TE_BACKDROP_FAILED`: set `capabilities.backdropApplyFailed = true` and re-`Resolve`, which gives Solid with `FallbackReason::BackdropApplyFailed` (rule 4). Clear the flag when the user picks another mode.
  - Hold an in-memory `AppearanceSettings` initialized from `Defaults()` (R-11), with the requested mode overridden by `--backdrop` in Debug builds. (Until T043 defines `Defaults()`, the `AppearanceSettings` member defaults are used; they are the same R-11 values. T047 switches this to the loaded settings.)
  - Add `MainWindow::SetRequestedMode(BackdropMode)`, which re-applies without recreating the window and keeps the layout and future navigation state (US1-2).
  - In Debug builds only (`#ifdef _DEBUG`): forward a second launch's `--backdrop` to the running window through `WM_COPYDATA`, found with a named mutex and `FindWindowExW` on the class, then exit the second process. This lets runtime switching be tested before US2. Release builds contain no forwarding code, and every launch opens its own window (UI §4 "Command line").
- [X] T038 [P] [US1] Implement `IStatusBar` in `src/ui/StatusBar.cpp` and `include/te/ui/StatusBar.h`. It renders with DirectWrite in the status region:
  - left text: the item count and selection (empty until US3)
  - right text: the applied mode, surface opacity and tint strength, for example "Mica · Surface 0% · Tint 20%" (only the mode is shown in Solid)
  - "(fallback: <reason>)" when `applied != requested`, using the `STRINGTABLE` reason strings (UI §3)
  - `SetTransientMessage(text, ms)`, which later stories use. (The expiry uses a `WM_TIMER` on the owner window, id `StatusBar::kTransientTimerId`; the owner forwards `WM_TIMER` to `StatusBar::OnTimer`. Strings come from `StatusBar::Strings::Load(instance, StringIds)`, filled from `resource.h` in `wWinMain`; English defaults match the STRINGTABLE. `Automation()` returns `nullptr` until T078/T079. T039 wires all three into `MainWindow`.)
- [X] T039 [US1] Render the caption title text and status bar in `effective.text` and `effective.secondaryText` from `src/window/CustomTitleBar.cpp` and `src/ui/StatusBar.cpp`, and call `SurfacePainter` first in the `MainWindow` render pass. Before drawing the caption title and the status text, call `SurfacePainter::PaintTextScrim` for each text area (T036, R-05). Verify the text is legible in dark, light and high contrast. (Verified by `tests/unit/LegibilityTests.cpp`: dark, light and high contrast × Acrylic/Mica/Solid × surface and tint levels, rendered with `SurfacePainter` and measured over every backdrop extreme; live light-mode screenshots in `validation/us1-light-{mica,acrylic,solid}.png`. The title's text area spans the whole caption strip up to the drag region's right edge.)
- [X] T040 [US1] Create the manual checklist `tests/manual/themes.md` for V-1a (including the worst-case legibility steps: Acrylic, 0% surface, 0% tint, over a white wallpaper in dark mode and a black one in light mode), V-1b (via `--backdrop` for now), V-1c (transparency toggle; a pre-22621 VM if available), and V-1e (high contrast on and off while running). Then run it, and add a "US1" section to `specs/001-translucent-explorer/validation-report.md` with the results and the T031/T032 test results (SC-011).

**Checkpoint**: V-1a to V-1e pass, T031 and T032 pass, US1 is demonstrable on its own, and
T040 has recorded the evidence.

---

## Phase 4: User Story 2 — Change color from the title bar (Priority: P1)

**Goal**: A title-bar picker immediately to the left of Minimize opens an accessible
popup. The popup offers presets, a custom color, a preview, mode switching, surface
opacity, tint strength and Reset. All settings persist across restarts, with recovery from corrupt or invalid
settings (FR-004 to FR-007, FR-022; SC-002, SC-003).

**Independent Test**: Run V-2a to V-2h: placement at normal, maximized and narrow sizes;
pointer and keyboard activation; live, independent changes; Solid disabling both opacity
controls;
restart persistence; corrupt and invalid settings files; dismissal without side effects.

### Tests for User Story 2

- [X] T041 [P] [US2] Write the failing test `tests/unit/SettingsTests.cpp` for `SettingsManager`, using a temp directory injected through the constructor. Cover:
  - missing file → defaults (Mica, `"accent"`, tint 0.20, surface 0.00, 16 × `#FFFFFF`)
  - a round-trip of `Save` then `Load`, including `surfaceOpacity`
  - `tintOpacity: 5` clamps to 0.80 while a valid `backdropMode` is kept
  - `tintOpacity: 0.33` rounds to 0.35
  - `surfaceOpacity: 1.5` clamps to 0.90, and `surfaceOpacity: -1` clamps to 0.00, without changing `tintOpacity`
  - a bad `tintColor` falls back to default only for that field
  - `customColors` of length 3 is padded to 16
  - `{not json` gives `fileWasCorrupt == true` and a `settings.corrupt-*.json` in the directory
  - `schemaVersion: 2` loads defaults and does not overwrite the file
  - `Save` writes through `settings.json.tmp` and leaves no `.tmp` behind

### Implementation for User Story 2

- [X] T042 [P] [US2] Complete `include/te/appearance/AppearanceSettings.h` and add `include/te/appearance/Palette.h`:
  - `AppearanceSettings` as in CI.
  - `constexpr std::array<NamedColor, 12> kPresetPalette` with names and hex values from R-11 (Blue `#0078D4`, Navy `#0063B1`, Teal `#00B7C3`, Sea green `#00B294`, Green `#107C10`, Gold `#FFB900`, Orange `#F7630C`, Red `#E81123`, Rose `#EA005E`, Purple `#8764B8`, Slate `#515C6B`, Graphite `#4C4A48`).
  - `kTintMin = 0.0`, `kTintMax = 0.80`, `kSurfaceMin = 0.0`, `kSurfaceMax = 0.90`, `kOpacityStep = 0.05`. (Also `SnapOpacity(value, min, max)`, shared by T043 validation and the T045 sliders. `tests/unit/PaletteTests.cpp` checks every preset is legible at 80% tint; that check made the dark text candidate pure black instead of `#1B1B1B` — see R-05.)
- [X] T043 [US2] Implement `ISettingsStore` in `src/settings/SettingsManager.cpp` and `include/te/settings/SettingsManager.h` (R-12, [contracts/settings.schema.json](./contracts/settings.schema.json)):
  - The directory comes from `SHGetKnownFolderPath(FOLDERID_LocalAppData)` + `\TranslucentExplorer`, overridable for tests.
  - Parse with nlohmann/json and validate each field.
  - A corrupt file is renamed to `settings.corrupt-<yyyyMMddHHmmss>.json`.
  - Atomic save: write `.tmp`, `FlushFileBuffers`, then `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
  - Reset keeps `customColors`.
  - Make T041 pass.
- [X] T044 [US2] Add the appearance popup `IDD_APPEARANCE` `DIALOGEX` to `resources/TranslucentExplorer.rc`, with its control IDs in `resources/resource.h`. Style is `WS_POPUP | WS_BORDER | DS_MODALFRAME` off, with `DS_CONTROL` and `DS_SHELLFONT` ("Segoe UI" 9). It contains:
  - a "Mode" group with three `AUTORADIOBUTTON`s: `IDC_MODE_ACRYLIC`, `IDC_MODE_MICA`, `IDC_MODE_SOLID`
  - `IDC_MODE_REASON`, a static
  - 12 `BS_OWNERDRAW` preset buttons, `IDC_SWATCH_0`…`IDC_SWATCH_11`, with `WS_TABSTOP` only on the first
  - `IDC_SWATCH_ACCENT`, a button
  - `IDC_CUSTOM`, a push button "Custom…"
  - `IDC_SURFACE`, a `msctls_trackbar32`, with `IDC_SURFACE_LABEL` ("Surface opacity")
  - `IDC_TINT`, a `msctls_trackbar32`, with `IDC_TINT_LABEL` ("Tint strength")
  - `IDC_OPACITY_REASON`, a static shared by both trackbars
  - `IDC_PREVIEW`, an `SS_OWNERDRAW` static
  - `IDC_RESET`, a push button "Reset to defaults"
  Also add the accelerator `Alt+Shift+C` → `IDM_OPEN_APPEARANCE` to `IDR_ACCEL`.
  (Done without `DS_CONTROL`: it strips `WS_CAPTION`, which includes `WS_BORDER`, so the popup lost its border; a top-level popup does not need it for `IsDialogMessageW`. `WS_EX_CONTROLPARENT` is kept. Also added `IDC_MODE_GROUP` and `IDC_PREVIEW_LABEL`, which gives the preview its accessible name.)
- [X] T045 [US2] Implement `IColorPicker` in `src/appearance/ColorPicker.cpp` and `include/te/appearance/ColorPicker.h` (R-10, UI §3):
  - Create the popup with `CreateDialogParamW(IDD_APPEARANCE, owner)` as modeless, and register it with `Application` for `IsDialogMessageW`.
  - Position it under the anchor and clamp it to `GetMonitorInfoW(MonitorFromWindow(owner)).rcWork`.
  - Owner-drawn swatches show the fill, a focus ring and a check mark on the selected swatch. The window text is the color name, for UIA. Arrow keys move within the 4×3 grid through `WM_GETDLGCODE` / `DLGC_WANTARROWS`.
  - `IDC_CUSTOM` opens `ChooseColorW` with `CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR` and `lpCustColors` backed by `settings.customColors`.
  - `IDC_SURFACE` has range 0–18 and `IDC_TINT` has range 0–16 (both × 5%), with `TBM_SETPAGESIZE 2` and `TBM_SETTICFREQ 2`. Each writes only its own field (`surfaceOpacity` or `tintOpacity`). Both are disabled when `!effective.opacityControlEnabled`; `IDC_OPACITY_REASON` then reads "Solid mode is fully opaque".
  - Mode radios are disabled when unsupported, with reason text from the `STRINGTABLE` (FR-022).
  - Each change updates the preview and calls the changed callback.
  - Dismissal: `IDCANCEL` / Escape, `WM_ACTIVATE(WA_INACTIVE)` (unless focus moved to its own `ChooseColor` dialog), or a second click on the picker. Hide the popup with `ShowWindow(SW_HIDE)`, not by destroying it. A click that dismisses the popup is not passed to the main window: set a flag to swallow the next `WM_LBUTTONDOWN` / `WM_LBUTTONUP` in the file list. (Implemented as `ColorPicker::ShouldSwallowClick(msg)`, which the owner calls for button messages; it swallows only the pair that follows a deactivation within 500 ms, which also makes a second click on the picker button close the popup. The concrete class adds `SetCapabilities` (which modes are available and why) and `Update` (refresh while open). Popup IDs live in `include/te/appearance/AppearanceIds.h`, included by `resource.h`; the swatch grid was re-laid out as 4×3; the test executables embed the `.rc`. Screenshot: `validation/us2-popup.png`.)
- [X] T046 [US2] Add the picker button to `src/window/CustomTitleBar.cpp`:
  - Render it in `layout.picker`: a 14-DIP circle filled with the effective tint, a 1-DIP ring in `effective.text` at 40% alpha, and hover and pressed fills that match the caption buttons (track the pointer with `TrackMouseEvent`).
  - Handle `WM_LBUTTONDOWN` / `WM_LBUTTONUP` inside the picker rectangle to toggle `IColorPicker`, anchored at the picker's screen rectangle.
  - `IDM_OPEN_APPEARANCE` from the accelerator does the same.
  - Keep `WM_NCHITTEST` ordering unchanged (T021).
  - (Done: hover = `effective.text` at 10%, pressed at 6%; the click is press-and-release inside the picker with mouse capture. `MainWindow` owns the `ColorPicker`, checks `ShouldSwallowClick` before any button message, and applies each popup change immediately; T047 adds loading, the debounced save and the corrupt-file notice. Screenshots: `validation/us2-picker-{hover,open,purple}.png`.)
- [X] T047 [US2] Integrate settings and the picker in `src/app/MainWindow.cpp`:
  - `ISettingsStore::Load()` runs before the first `ShowWindow`. In Debug builds, `--backdrop` overrides the requested mode for that session only and is not saved.
  - If `fileWasCorrupt`, call `IStatusBar::SetTransientMessage` with the string "Appearance settings were reset".
  - The picker's changed callback updates the in-memory settings, re-resolves and applies immediately, and schedules a 100 ms debounced save with `SetTimer(ID_TIMER_SAVE)`.
  - Reset applies `Defaults()` but keeps `customColors`.
  - `WM_DESTROY` flushes any pending save.
  - Opening or closing the picker must not touch the navigation or selection state.
  - (Done: `MainWindow::Options::settingsStore` is injected — `Application` passes a `SettingsManager`, tests pass a fake or `nullptr`, so tests never touch the user's file. The Debug `WM_COPYDATA` handoff is also session-only. Verified end to end with the Release build: change → restart restores it (`validation/us2-restart.png`); corrupt file → renamed, notice shown (`validation/us2-corrupt.png`). `SnapOpacity` now returns exact decimals, so the file stores `0.3`, not `0.30000000000000004`.)
- [X] T048 [US2] Create the manual checklist `tests/manual/picker.md` with V-2a to V-2h from quickstart. Include placement at maximized and narrow widths, and at 100%, 150% and 200% scaling. Then run it, and add a "Phase 2 (US2)" section to `specs/001-translucent-explorer/validation-report.md` with the V-2a to V-2h and T041 results (SC-011).

**Checkpoint**: US1 and US2 both pass on their own, and T048 has recorded the evidence.
This is the MVP: a translucent native window with a working title-bar color picker
(constitution phase 2 exit).

---

## Phase 5: User Story 3 — Navigate and inspect files (Priority: P2)

**Goal**: The address bar, Back/Forward/Up/Refresh, a navigation pane of quick locations,
and a custom translucent Direct2D file list with the Name, Date modified, Type and Size
columns, icons, sorting, selection and open. Enumeration runs asynchronously, can be
cancelled and never shows stale results (FR-009 to FR-011, FR-018 to FR-020; SC-005).

**Independent Test**: Run `tools/New-TestData.ps1`, then V-3a to V-3h: navigation through
`nested`, history correctness, sorting, opening files with and without associations, an
inaccessible or disconnected location, rapid alternation between `10k` and `nested`, and
Unicode and long-path folders.

### Tests for User Story 3

- [X] T049 [P] [US3] Write the failing test `tests/unit/NavigationHistoryTests.cpp` (DM "NavigationHistory"), using an injected comparable `Location` stand-in. Cover:
  - navigating to the same location does nothing
  - Navigate after Back truncates the forward entries
  - A → B → C → Back → Back gives A
  - then Forward gives B
  - Up pushes the parent, and Back returns to the child
  - a cap of 100 entries drops the oldest
  - Back and Forward are disabled at the ends
- [X] T050 [P] [US3] Write the failing test `tests/unit/SortModelTests.cpp` (DM "SortState"). Cover:
  - folders first in both directions
  - `StrCmpLogicalW` natural order (`file2` before `file10`)
  - missing size or date sorts last
  - a stable tie-break on name
  - selection kept by identity across a re-sort (`SelectionModel`)
- [X] T051 [P] [US3] Write the failing test `tests/unit/GenerationGuardTests.cpp`. Payloads with `gen != current` are rejected and freed (use a counting deleter), and a current payload is accepted.
- [X] T052 [P] [US3] Write the failing integration test `tests/integration/DirectoryEnumeratorTests.cpp` against `%TEMP%\te-test`. Skip with `GTEST_SKIP` if the folder is missing, and tell the tester to run `tools/New-TestData.ps1`. Use a message-only window (`HWND_MESSAGE`) to receive posts. Cover:
  - enumerating `nested\a` delivers the expected names, types and sizes, then `EnumDone` with `S_OK`
  - `readonly-acl` delivers `EnumDone` with `E_ACCESSDENIED`
  - `Start` for gen 2 during gen 1 means no gen-1 batch arrives after gen 2's first batch is accepted by the guard

### Implementation for User Story 3

- [X] T053 [P] [US3] Implement the `ShellLocation` class declared in `include/te/shell/ShellTypes.h` (T016) in `src/shell/ShellLocation.cpp`, as DM "Location":
  - The PIDL is owned by `wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>`, created with `ILCloneFull`.
  - Equality uses `ILIsEqual`.
  - `Parent()` uses `ILRemoveLastID` on a clone, and returns empty for the Desktop root.
  - Lazy `IShellItem` via `SHCreateItemFromIDList`.
  - `displayName` (`SIGDN_NORMALDISPLAY`), `parsingPath` (`SIGDN_DESKTOPABSOLUTEPARSING`) and `isFileSystem` (`SFGAO_FILESYSTEM`).
  - (Done. Copies clone the PIDL and strings but never the cached `IShellItem`, so a copy handed to a worker creates its own item in its own apartment. Tests: `tests/integration/ShellLocationTests.cpp`, 7/7.)
- [X] T054 [P] [US3] Implement `src/ui/NavigationHistory.cpp` and `include/te/ui/NavigationHistory.h`, templated on the location type or taking a comparator, as DM "NavigationHistory". Make T049 pass. (T049 fixed the interface: `NavigationHistory<Location, Equal = std::equal_to<Location>>`, header-only because it is a template, with `Navigate(L)`, `Up(std::optional<L> parent)`, `Back`, `Forward`, `CanBack`, `CanForward`, `Current`, `Entries`, `Index`, `kMaxEntries = 100`. The `.cpp` is only needed for an explicit instantiation for `te::Location`, if any.)
- [X] T055 [P] [US3] Implement `src/ui/SortModel.cpp` / `include/te/ui/SortModel.h` (the comparator over the `SortState` from `include/te/ui/SortTypes.h`, T016) and `src/ui/SelectionModel.cpp` / `include/te/ui/SelectionModel.h`, as DM "SortState" and "SelectionModel" (anchor, focus, range and toggle; cleared on navigation). Make T050 pass. (T050 fixed the interfaces: `SortModel::Less(a, b, state)` and `SortModel::Sort(items, state)`, which returns the permutation `oldIndexAt[newIndex]`; `SelectionModel` with `Reset`, `Grow`, `Select`, `Toggle`, `ExtendTo`, `Focus`, `SelectAll`, `Clear` and `ApplyPermutation`.)
- [X] T056 [P] [US3] Implement display formatting for the `FileItem` declared in `include/te/ui/FileItem.h` (T016), in `src/ui/FileItemFormat.cpp` and `include/te/ui/FileItemFormat.h`:
  - size via `StrFormatByteSizeEx(SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT)`
  - date via `FileTimeToLocalFileTime`, `GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE)` and `GetTimeFormatEx(TIME_NOSECONDS)`
  - empty strings for missing values
  - (Done as `te::FileItemFormat::Size` / `Modified`, tests in `tests/unit/FileItemFormatTests.cpp`. Dates are converted with `FileTimeToSystemTime` + `SystemTimeToTzSpecificLocalTime` instead of `FileTimeToLocalFileTime`, which applies today's daylight-saving bias to every date and so shows summer dates an hour off in winter; this matches Explorer.)
- [X] T057 [US3] Implement `IShellNavigator` (without the context menu, which comes in US4) in `src/shell/ShellNavigator.cpp`:
  - `Parse` uses `SHParseDisplayName` after `ExpandEnvironmentStringsW`.
  - `Parent` is as described.
  - `InitialLocation` tries the command-line path, then `FOLDERID_ComputerFolder`, then `FOLDERID_Profile` (R-11).
  - `Open` uses `ShellExecuteExW` with `SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI` and `lpIDList`. On `ERROR_NO_ASSOCIATION` / `SE_ERR_NOASSOC`, it shows a `TaskDialogIndirect` "Windows can't open this file" with an "Open with…" command link that re-invokes with `lpVerb = L"openas"` (UI §6). Folders navigate instead of opening. (Done in `ShellNavigator`. Found: on Windows 11, `ShellExecuteExW` with `SEE_MASK_FLAG_NO_UI` does not report `ERROR_NO_ASSOCIATION`; it succeeds and starts the system "How do you want to open this file?" window. So `Open` first checks `AssocQueryStringW(ASSOCSTR_COMMAND)` for file-system items and shows the app's own dialog when nothing is registered. `Open` returns `S_FALSE` for folders; the caller navigates. The prompt is injectable, so `tests/integration/ShellNavigatorTests.cpp` (10 tests) shows no UI.)
- [X] T058 [US3] Implement `IDirectoryEnumerator` in `src/shell/DirectoryEnumerator.cpp` on a `StaWorker` (R-07):
  - `Start` cancels the previous request's `stop_source` and posts a task.
  - The task calls `BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&IEnumShellItems))` and loops `Next(64)`, checking `stop_token` between calls.
  - For each item, read `IShellItem2` properties (`PKEY_ItemTypeText`, `PKEY_Size`, `PKEY_DateModified`), `SFGAO` attributes and the child PIDL via `SHGetIDListFromObject` + `ILFindLastID` clone.
  - Honor `SHGetSetSettings(SSF_SHOWALLOBJECTS)` for hidden items.
  - Post `EnumBatch` every 256 items through `PostOwned`, and finish with `EnumDone { hr, cancelled }`.
  - `Shutdown` requests stop and joins.
  - Make T052 pass. (T052 added `include/te/shell/DirectoryEnumerator.h` — `DirectoryEnumerator` with `kBatchSize = 256` — and placeholder `src/shell/DirectoryEnumerator.cpp` / `src/shell/ShellLocation.cpp` for T058 / T053 to replace. Its extra tests: 10,000 files in batches of at most 256, and `CancelAll` ending with `cancelled`.)
- [X] T059 [US3] Implement a basic `IIconProvider` in `src/shell/IconProvider.cpp` on its own `StaWorker`:
  - `IShellItemImageFactory::GetImage({size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK)` → `IconReady` with `wil::unique_hbitmap`.
  - `CancelOlderThan(gen)` drops queued requests. LIFO prioritization is added in US6 (T087).
  - (Done. `Request` gained an `itemKey` parameter — `IconReady` carries the row's key, and the old signature had no way to receive it; contract updated. A failed extraction still posts `IconReady` with a null bitmap so the row can show a fallback. Tests: `tests/integration/IconProviderTests.cpp`, 5/5.)
- [X] T060 [US3] Implement `IFileView` in `src/ui/FileView.cpp` and `include/te/ui/FileView.h`, rendering with Direct2D in the list region:
  - A header row with 4 columns and resizable dividers. Clicking a header sorts; clicking again toggles the direction and shows an arrow glyph. `Ctrl+Shift+1`, `2`, `3` and `4` sort by Name, Date modified, Type and Size the same way (UI §4).
  - 28-DIP rows with the icon (16 DIP × DPI), name, date, type and size (right-aligned), in `effective.text` / `secondaryText`. Before drawing each visible row and the header row, call `SurfacePainter::PaintTextScrim` for that row's rectangle (T036, R-05).
  - The selection fill is `effective.selection` at `effective.selectionAlpha` (normally 40%), drawn after `PaintTextScrim` and before the text. Text on selected rows uses `effective.selectedText` (R-05 "Text on selected rows"). The focus rectangle is 2 DIP in `effective.focus` whenever the list has keyboard focus.
  - A vertical scrollbar that handles `WM_MOUSEWHEEL` and scrollbar dragging, drawn custom and translucent.
  - Mouse: click, Ctrl+click, Shift+click and double-click to open.
  - Keyboard: arrows, Home, End, PgUp and PgDn, with Shift and Ctrl variants, plus `Ctrl+Space`, `Ctrl+A` and Enter (UI §4).
  - Icons are converted from `HBITMAP` with WIC `CreateBitmapFromHBITMAP(WICBitmapUsePremultipliedAlpha)` → `CreateBitmapFromWicBitmap`.
  - `BeginLocation` clears items and selection. `AppendItems` merges each batch into sorted order.
  - (Done. `FileView` is windowless: `MainWindow` forwards input in DIPs through `OnPointerDown/Move/Up`, `OnDoubleClick`, `OnWheel`, `OnKeyDown`, `SetFocused`, and gets `open`, `counts` and `invalidate` callbacks. The view assigns item keys; `ForEachVisibleWithoutIcon` hands out icon requests for rows on screen; `IconFromHBitmap` does the WIC conversion. Header labels are English defaults in `FileView::Strings` (no STRINGTABLE entries yet). Tests: `tests/unit/FileViewTests.cpp`, 16; offscreen render: `validation/us3-file-view-offscreen.png`. UI Automation stays for T078.)
- [X] T061 [US3] Implement `IAddressBar` in `src/ui/AddressBar.cpp` and `include/te/ui/AddressBar.h`:
  - Display mode draws `Location.displayName` / `parsingPath` over the translucent toolbar, on the same surface and tint layers as the rest of the toolbar. Before drawing the text, call `SurfacePainter::PaintTextScrim` for the address field's rectangle (T036, R-05). With the layered-edit approach, the same area stays painted while editing, so the text behind the transparent edit background keeps its contrast.
  - Clicking it, or pressing `Ctrl+L` / `Alt+D` / `F4`, shows a child `EDIT` (`ES_AUTOHSCROLL`) covering the field, selects all, and calls `SHAutoComplete(SHACF_FILESYS_DIRS | SHACF_URLHISTORY)`. If T027(b) failed, use the owned-popup variant instead.
  - Make the edit translucent (R-02, constitution Principle II). **T027 result: (d) passed** ([spike-rendering.md](./spike-rendering.md)), so use the layered, colour-keyed edit: create it with `WS_EX_LAYERED` and `SetLayeredWindowAttributes(key, 0, LWA_COLORKEY)`, where the key colour is `effective.typicalSurfaceColor`. From `WM_CTLCOLOREDIT` return a brush in the key colour, with `SetBkColor(key)` and `effective.text` as the text colour. Re-apply the key and recreate the brush whenever the effective appearance changes; a surface-colour key keeps anti-aliased text edges free of fringes. Confirm the caret manually in V-3a (the spike could not capture it). The tinted opaque fallback and its sign-off are not needed.
  - Enter parses and navigates. Escape restores the path and returns to display mode. An invalid path shows the inline message below the field and keeps the current folder (UI §6, US3-6).
  - (Done in `AddressBar`. The owner forwards `WM_CTLCOLOREDIT` (`HandleCtlColor`) and clicks (`OnPointerDown`), calls `ApplyAppearance` on changes, and draws `RenderOverlay` after the file list so the error callout is not covered. Enter with unchanged text just leaves edit mode (a virtual folder's display name such as "This PC" does not parse); focus loss cancels like Escape. File-system folders show their path, virtual folders their name. Tests: `tests/integration/AddressBarTests.cpp`, 10. Caret visibility still needs the manual V-3a check.)
- [X] T062 [US3] Create `src/ui/Toolbar.cpp` and `include/te/ui/Toolbar.h`: Back, Forward, Up and Refresh glyph buttons ("Segoe Fluent Icons", falling back to "Segoe MDL2 Assets") drawn with Direct2D, with hover, pressed and disabled states driven by `NavigationHistory` and `Location::Parent()`. Add tooltips with a `TOOLTIPS_CLASSW` window using `TTF_SUBCLASS` rectangles. (Done: windowless `Toolbar` with `SetState(canBack, canForward, canUp)` from the owner, a click callback, and `ButtonsWidth()` for placing the address bar; tooltip rectangles follow bounds and DPI. Tooltip texts are English defaults in `Toolbar::Strings`. Tests: `tests/integration/ToolbarTests.cpp`, 7.)
- [X] T063 [US3] Implement `INavigationPane` in `src/ui/NavigationPane.cpp` and `include/te/ui/NavigationPane.h`:
  - Quick locations: This PC (`FOLDERID_ComputerFolder`), Desktop, Documents, Downloads, Pictures, Music, Videos (`SHGetKnownFolderIDList`), then fixed and removable drives from `GetLogicalDriveStringsW` + `GetDriveTypeW`, with Shell icons.
  - Clicking or pressing Enter navigates, the current location is highlighted, and up/down keys move between entries.
  - Before drawing each entry's text, call `SurfacePainter::PaintTextScrim` for that entry's rectangle (T036, R-05).
  - Refresh the drive list on `WM_DEVICECHANGE` (`DBT_DEVICEARRIVAL` / `DBT_DEVICEREMOVECOMPLETE`).
  - (Done: windowless `NavigationPane`; the owner forwards `OnDeviceChange(wParam)`, pointer, wheel and key input. The ~12 icons are extracted on the UI thread in `Refresh`/`SetDpi` and converted with `FileView::IconFromHBitmap` on first render (recreated after a device change). A known folder that cannot be resolved is skipped. Tests: `tests/integration/NavigationPaneTests.cpp`, 9.)
- [X] T064 [US3] Add the navigation controller to `src/app/MainWindow.cpp`:
  - `NavigateTo(Location, HistoryMode)` increments `currentGeneration`, calls `IDirectoryEnumerator::Start`, `IIconProvider::CancelOlderThan` and `IFileView::BeginLocation` (deferred until the first batch, so a failure keeps the previous view).
  - Commit to `NavigationHistory` on the first `EnumBatch` or a successful `EnumDone` only.
  - `WM_TE_ENUM_BATCH` / `WM_TE_ENUM_DONE` / `WM_TE_ICON_READY` go through the generation guard (T051). Use `te::GenerationGuard` (`include/te/core/GenerationGuard.h`; implemented early, in T058, because T052's supersede test needs real generations; T051 passes); `NavigateTo` calls `Advance()` to get the new generation.
  - On `EnumDone` failure, show `HresultMessage` in the status bar and keep the previous view (FR-020).
  - Status text: "N items" and "M selected".
  - The window title is "Translucent Explorer — <displayName>".
  - Accelerators: `Alt+Left` / `Backspace` (only when the focus is not in an `EDIT`), `Alt+Right`, `Alt+Up`, `F5`.
  - The initial location comes from `IShellNavigator::InitialLocation`.
  - (Done. Also: F6 / Shift+F6 cycle address bar → navigation pane → file list (the picker is reached with Alt+Shift+C); Backspace is handled in `WM_KEYDOWN`, so it never reaches the address edit's text; the navigation commands live in `include/te/app/CommandIds.h`, included by `resource.h`. `GenerationGuard::Rewind` restores the displayed generation after a failed navigation, so that folder's icons are still accepted. `ShellNavigator::Parse` now resolves `.`/`..` in file-system paths (`PathAllocCanonicalize`), and the address bar says "can't find" only for not-found errors, otherwise "Can't open '<path>': <system text>". `MainWindow::Options::startShell` is false by default, so tests never touch the Shell; `Application` turns it on. Verified live: `validation/us3-{start,address-edit,invalid-path,access-denied}.png`. The window-lifecycle leak test (T029) still runs with the shell off; a navigation leak check belongs to US6.)
- [X] T065 [P] [US3] Run the `IExplorerBrowser` spike (R-06, constitution Principle IV). In a throwaway branch, or behind `#ifdef TE_SPIKE_EXPLORERBROWSER` in `src/shell/ExplorerBrowserSpike.cpp`, host `IExplorerBrowser::Initialize` in the list region with `EBO_NOBORDER` and `FVM_DETAILS`, over Mica. Record whether the view composites translucently, with screenshots and the conclusion, in `specs/001-translucent-explorer/spike-explorerbrowser.md`. Then remove the spike code from the default build.
  - Done 2026-09-28: the view is opaque (list 255,255,255 vs Mica 249,241,237 in both variants; IVisualProperties had no effect) and GDI item text washes out over the frame. The custom Direct2D view stays. The spike ran as a standalone program in git-ignored `out/spike/`, which was then deleted; it was never in the product build. See `spike-explorerbrowser.md`.
- [X] T066 [US3] Create the manual checklist `tests/manual/navigation.md` with V-3a to V-3h. V-3h covers listing, sorting and opening items in `%TEMP%\te-test\unicode` and navigating to the deepest folder of `%TEMP%\te-test\longpath` (constitution Principle IX); file operations there are V-4h. Then run it, and add a "Phase 3 (US3)" section to `specs/001-translucent-explorer/validation-report.md` with the results, the T049 to T052 results and a link to the T065 spike (SC-011).
  - Done 2026-09-28: `tests/manual/navigation.md` Part A 12/12 automated (new `tests/integration/MainWindowNavigationTests.cpp`, 6 tests, plus the live Release app with screenshots); Part B 9 manual rows pending (mouse and keyboard, caret, opening files with applications, 100%/200%). "Phase 3 (US3)" added to `validation-report.md`. Fixed on the way: keys stuck on the closed address field (`MainWindow`), typed paths past MAX_PATH (`ShellNavigator` drops the `\\?\` prefix), 8.3 names in the address (`AddressBar`), monochrome emoji (color fonts). 318/318 tests in Debug and Release.

**Checkpoint**: V-3a to V-3h pass, T049 to T052 pass, the spike is documented, US3 works on
its own, and T066 has recorded the evidence (constitution phase 3 exit).

---

## Phase 6: User Story 4 — Manage files safely (Priority: P2)

**Goal**: Copy, move, rename, recycle and permanent delete through `IFileOperation` in an
STA, with the Shell's conflict and confirmation UI. Plus clipboard copy, cut and paste,
native context menus, truthful results and no silent data loss (FR-012 to FR-014,
FR-020; SC-006).

**Independent Test**: V-4a to V-4h against `%TEMP%\te-test` (Unicode and long-path folders, conflicts, `readonly-acl`,
`10k` cancel, Recycle Bin check, Shift+Delete cancel, context menu Properties).

### Tests for User Story 4

- [X] T067 [P] [US4] Write the failing integration test `tests/integration/FileOperationServiceTests.cpp`. Create a fresh temp tree per test and use a message-only notify window. Cover:
  - (a) Rename `a.txt` → `b.txt` succeeds and posts `FileOpItem { hr = S_OK, newName = "b.txt" }` and then `FileOpDone(Succeeded)`.
  - (b) Rename to `"bad:name"` is rejected by `ValidateNewName` before submission.
  - (c) Copy into a directory with a deny-write ACL gives `PartiallySucceeded` or `Failed`, with an `E_ACCESSDENIED`-class `hr` for the item.
  - (d) The static `FileOperationService::FlagsFor(kind)` never contains `FOF_NOCONFIRMATION`, `FOF_NOERRORUI` or `FOF_RENAMEONCOLLISION`; `Recycle` includes `FOF_ALLOWUNDO` and `DeletePermanent` does not; all include `FOFX_ADDUNDORECORD`.
  - (e) Long paths and Unicode names (constitution Principle IX): rename a file whose name contains emoji, CJK and right-to-left characters to another Unicode name, and copy it to a second folder; both succeed and the names round-trip exactly. Copy a file out of a folder deeper than 260 characters; the result is either success with identical content, or a failure `hr` reported for the item. It is never a silent truncation.
  - The tests exercise only operations that do not show conflict UI.
  - (Done 2026-09-28: 6 tests, all failing against the placeholder as intended; the other 318 still pass. T067 fixed the interface in `include/te/shell/FileOperationService.h`: `FileOperationService(Factory)` where `Factory = std::function<HRESULT(IFileOperation**)>` (empty = `CoCreateInstance(CLSID_FileOperation)`), static `DWORD FlagsFor(FileOpKind)`, static `bool ValidateNewName(std::wstring_view)` (trims spaces first). The placeholder `src/shell/FileOperationService.cpp` posts `FileOpDone(Failed, E_NOTIMPL)`; T068 replaces it. The test factory (research R-14) wraps the real `IFileOperation` and checks that the service calls `SetOperationFlags` exactly once with `FlagsFor(kind)` and calls `SetOwnerWindow`. In (c) and the long-path copy only, it also adds `FOF_NOERRORUI` on the wrapped object, because the Shell's access-denied dialog, or an elevation prompt for an administrator, would block an automated run; the service's own flags are unchanged. (c) denies `FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY` to the current user with an explicit ACE, removed again in teardown. Each test uses a fresh `%TEMP%\te-fileop-<guid>` tree, deleted afterwards.)

### Implementation for User Story 4

- [X] T068 [US4] Implement `IFileOperationService` in `src/shell/FileOperationService.cpp` and `include/te/shell/FileOperationService.h` on a dedicated `StaWorker` (R-08):
  - `ValidateNewName`: not empty after trimming, and none of `\/:*?"<>|`.
  - `FlagsFor(kind)` as in T067(d).
  - Per request: `CoCreateInstance(CLSID_FileOperation)`, `SetOperationFlags`, `SetOwnerWindow(notifyHwnd)`, `Advise` an `IFileOperationProgressSink` implementation (a class in the same file).
  - Operations: `CopyItems` / `MoveItems` with an `IShellItemArray` from PIDLs (`SHCreateShellItemArrayFromIDLists`) or an `IDataObject`; `RenameItem`; `DeleteItems`.
  - Call `PerformOperations`, then `GetAnyOperationsAborted`.
  - `PostCopyItem`, `PostMoveItem`, `PostRenameItem` and `PostDeleteItem` each post a `FileOpItem`.
  - The final state (Succeeded, PartiallySucceeded, Cancelled or Failed) is posted as `FileOpDone` (DM "FileOperationRequest").
  - Make T067 pass.
  - (Done 2026-09-28: T067 passes, 6/6, plus a new Move test, 7/7; 325/325 in Debug and Release. Notes:
    - Flags: `FOFX_ADDUNDORECORD` for every kind; Recycle adds `FOF_ALLOWUNDO | FOF_WANTNUKEWARNING`, so the Shell warns instead of silently deleting an item the Recycle Bin cannot take (Principle IX).
    - One `WM_TE_FILEOP_ITEM` is posted only for each item the request names (matched by PIDL). Nested items of a copied folder are counted, and their failures listed, but not posted, so a large tree cannot exceed the thread's posted-message quota. The error list is capped at 200 entries.
    - A rename reports `COPYENGINE_S_DONT_PROCESS_CHILDREN` on success; the sink reports it as `S_OK`. An item the user skipped in the conflict dialog (`COPYENGINE_S_USER_IGNORED`) counts as neither done nor failed and gets no `newName`.
    - Final state: item failures decide first (`Failed` if none succeeded, else `PartiallySucceeded`), then an abort or cancel code gives `Cancelled`, then a failed `PerformOperations` gives `Failed`. `Failed` and `PartiallySucceeded` always carry at least one error.
    - A paste's `IDataObject` is marshalled from the UI apartment to the worker (`CoMarshalInterThreadInterfaceInStream`), and passed to `CopyItems` / `MoveItems` as is.
    - `Shutdown` lets the running operation finish and drops queued ones (nothing has touched their files).
    - Observed: (c) ends `Failed` with `COPYENGINE_E_ACCESS_DENIED_DEST` (0x80270022); the copy from 344 characters deep succeeds with identical content.)
- [X] T069 [P] [US4] Implement `src/shell/ContextMenu.cpp` and `include/te/shell/ContextMenu.h`, and wire them into `IShellNavigator::ShowContextMenu` / `HandleMenuMessage` in `src/shell/ShellNavigator.cpp` (R-08):
  - Get `IContextMenu` for the items with `SHCreateShellItemArrayFromIDLists` → `BindToHandler(BHID_SFUIObject)`. For the folder background, use `IShellFolder::CreateViewObject(IID_IContextMenu)`.
  - `QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_CANRENAME | (extended ? CMF_EXTENDEDVERBS : 0))`.
  - `TrackPopupMenuEx(TPM_RETURNCMD | TPM_RIGHTBUTTON)`.
  - If `GetCommandString(GCS_VERBW)` is `"rename"`, return a sentinel so the caller starts inline rename. Otherwise, call `InvokeCommand` with `CMINVOKECOMMANDINFOEX` (`CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE`, plus `CMIC_MASK_SHIFT_DOWN` / `CMIC_MASK_CONTROL_DOWN` from the key state).
  - While the menu is open, keep `IContextMenu2` / `IContextMenu3` and forward `WM_INITMENUPOPUP`, `WM_DRAWITEM`, `WM_MEASUREITEM` and `WM_MENUCHAR`.
  - (Done 2026-09-28: `te::ContextMenu` with `Load` / `Populate` / `VerbOf` / `Invoke` / `Show` / `HandleMenuMessage`. The rename sentinel is `te::kContextMenuRename` (`MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_ITF, 0x201)`, in `IShellNavigator.h`); a dismissed menu returns `S_FALSE`. `ShellNavigator` keeps the open menu for the whole `ShowContextMenu` call, including the command it runs, and `SetMenuTracker` replaces `TrackPopupMenuEx` for tests. New `tests/integration/ContextMenuTests.cpp`, 6 tests: the Shell's verbs for a file (open, cut, copy, delete, rename, properties), the folder background menu, extended verbs keep every normal verb, invalid input, Rename returns the sentinel without renaming, and a dismissed menu runs nothing while a submenu's `WM_INITMENUPOPUP` is handled by the Shell. No menu is shown and no verb is invoked in tests; the on-screen menu and invoking verbs (Properties, delete) are V-4 manual checks. The Shell declines `WM_INITMENUPOPUP` for the top-level menu and handles only its own submenus, so `HandleMenuMessage` returns false then, correctly. 331/331 in Debug and Release.)
- [X] T070 [P] [US4] Implement `src/shell/Clipboard.cpp` and `include/te/shell/Clipboard.h`:
  - `CopyToClipboard(items, bool cut)` uses `SHCreateDataObject(parentPidl, n, childPidls, nullptr, IID_PPV_ARGS(&IDataObject))`. For cut, it sets `CFSTR_PREFERREDDROPEFFECT` to `DROPEFFECT_MOVE` as an `HGLOBAL` `DWORD`, then calls `OleSetClipboard` and `OleFlushClipboard` on exit.
  - `GetPasteRequest(destination)` uses `OleGetClipboard`, reads `CFSTR_PREFERREDDROPEFFECT`, and returns `FileOpRequest{ Copy | Move, dataObject }`.
  - (Done 2026-09-28: `te::Clipboard` with static `CreateDataObject(folder, items, cut, IDataObject**)` and `PasteRequestFrom(IDataObject*, destination, FileOpRequest*)`, which touch no clipboard, plus the thin `CopyToClipboard`, `GetPasteRequest`, `CanPaste` and `FlushOnExit` (`OleFlushClipboard` only if the clipboard still holds this app's object; T072 calls it at exit). Copy sets `DROPEFFECT_COPY | DROPEFFECT_LINK`, cut `DROPEFFECT_MOVE`, as Explorer does; paste moves only when the effect is move without copy, so a missing effect never moves files. Non-file data gives `DV_E_FORMATETC`. New `tests/integration/ClipboardTests.cpp`, 7 tests; they never use the system clipboard, so the tester's clipboard and Win+V history are untouched (real Ctrl+C / Ctrl+V is V-4a). Two T068 defects found through the paste path and fixed: (1) the worker released its proxy to the data object after posting `FileOpDone`, a call into the UI apartment, which deadlocked when the UI then stopped pumping; everything is now released before the final message. (2) `Shutdown` during a running paste deadlocked in `Join`; new `StaWorker::JoinServingComCalls` waits with `CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS)`. Test `ShutdownDuringAPasteServesTheDataObjectAndFinishes` hangs with a plain `Join` (mutation check) and passes 10/10 with the fix. 338/338 in Debug and Release.)
- [X] T071 [US4] Add inline rename to `src/ui/FileView.cpp`:
  - `F2` (or the rename sentinel from T069) overlays a child `EDIT` on the name cell. Select the name without its extension for files, or the whole name for folders. Create it layered and colour-keyed exactly like the address edit (T061): a plain GDI `EDIT` in the glass area draws transparent text (spike-rendering.md, finding b).
  - Enter validates with `ValidateNewName`. An invalid name shows the balloon tip "A file name can't contain any of the following characters: \ / : * ? \" < > |" (`EM_SHOWBALLOONTIP`) and stays in edit mode.
  - Escape cancels.
  - On a valid name, submit a `Rename` request, but do **not** change the displayed name until `ApplyRename` is called from a successful `FileOpItem` (US4-3).
  - (Done 2026-09-28: the edit is `te::RenameEdit` (`include/te/ui/RenameEdit.h`, control ID `0x524E`), owned by `FileView`, which adds `AttachWindow`, `SetDpi`, `BeginRename()` / `BeginRename(index)`, `CancelRename`, `IsRenaming`, `RenameField`, `NameCellRect`, the `Callbacks::rename(item, newName)` callback and `Strings::badName`. The edit follows the row when the list scrolls or re-sorts and cancels when the row leaves the view, the item disappears or the folder changes; the list does not draw the name under it. Focus loss commits a valid, changed name (as Explorer does) and drops an invalid one.
    - Found by experiment: `IFileOperation::RenameItem` takes the raw file name (renaming `a.txt` to `c` gives `c`), and with "hide extensions" on, the Shell's display and editing names have no extension. So the edit starts from the real file name: new `ShellItemInfo::editName`, filled by `DirectoryEnumerator` with `SIGDN_PARENTRELATIVEPARSING` for file-system items (correct past `MAX_PATH` too) and `SIGDN_PARENTRELATIVEEDITING` otherwise. The extension is left unselected.
    - `MainWindow` plumbing done here: `AttachWindow`, `WM_CTLCOLOREDIT` forwarding and DPI changes. Submitting the request and calling `ApplyRename` on a successful `FileOpItem` is T072; until then a committed name is not submitted.
    - New `tests/integration/FileViewRenameTests.cpp`, 8 tests; `MainWindowNavigationTests` checks `editName` on real files. Live check in the Release app ([edit](./validation/us4-rename-edit.png), [invalid name](./validation/us4-rename-invalid.png)); no file was renamed. 346/346 in Debug and Release.)
- [X] T072 [US4] Wire file operations into `src/app/MainWindow.cpp`:
  - Accelerators and list keys: `Delete` → `Recycle`; `Shift+Delete` → `DeletePermanent` (the Shell shows the confirmation); `Ctrl+C` / `Ctrl+X` / `Ctrl+V` → `Clipboard` + `Submit`; `Shift+F10`, `VK_APPS` and right-click → `ShowContextMenu` (folder background when nothing is selected, extended verbs with Shift).
  - Forward menu messages to `HandleMenuMessage` before other handling.
  - When an operation is submitted, show a persistent status-bar message for it, for example "Copying 3 items…", "Moving 12 items…" or "Deleting 1 item…", and keep the window responsive. Replace the message with the final result on `WM_TE_FILEOP_DONE` (spec US4-6).
  - `WM_TE_FILEOP_ITEM`: `ApplyRename` on a successful rename; remove, add or refresh affected rows when the current folder is the source or the destination.
  - `WM_TE_FILEOP_DONE`: status messages from UI §6 ("N items copied", "N of M items completed", "Operation cancelled — N items completed before cancellation"). For `PartiallySucceeded` or `Failed`, show a `TaskDialogIndirect` listing each failed item's name and `HresultMessage`.
  - Refresh the current folder with a new generation after operations that affect it.
  - (Done 2026-09-28:
    - Keys: `Delete`, `Shift+Delete`, `Ctrl+C`, `Ctrl+X` and `Ctrl+V` are handled in `MainWindow::OnFileListKey` when the file list has the focus, not as accelerators, which would take `Ctrl+C` / `Ctrl+V` from the address and rename edits. Right-click, `Shift+F10` and the Menu key all arrive as `WM_CONTEXTMENU` (from `DefWindowProc`). `WM_RBUTTONDOWN` first selects the row under the pointer, or clears the selection over empty space (the folder background menu).
    - Context menu: Rename starts inline rename (T071). After any other command the folder is refreshed; Shell verbs run synchronously because `CMIC_MASK_ASYNCOK` is not set.
    - Status: `SubmitFileOperation` shows "Copying N items…" and the like as a persistent operation message. When an operation ends, the next running one's message shows, or the result from UI §6. Items the user skipped in a conflict (`COPYENGINE_S_USER_IGNORED`) are not counted as done, so the status then reads "N of M items completed". New pure `te::FileOpText` (`include/te/app/FileOpText.h`, strings `IDS_OP_*` loaded through `Application`), unit-tested in `tests/unit/FileOpTextTests.cpp`, 5 tests.
    - Rename: the row shows the new name when the item's `FileOpItem` succeeds. When Explorer hides the extension it shows the new name the same way; new `FileView::ApplyRename(key, displayName, realName)` keeps the real name for the next rename.
    - Refresh: after an operation whose source folder or destination is on screen, with a new generation. `RefreshKeepingSelection` restores the selection and focus by real name, mapping a rename to its new name (new `FileView::RestoreSelection`, `SelectForContextMenu`).
    - A successful paste of this app's cut empties the clipboard (`Clipboard::ClearIfCurrent`). At exit the service shuts down before queued messages are drained, and `Clipboard::FlushOnExit` keeps a copy pasteable.
    - Failure dialog: `TaskDialogIndirect` with up to 10 items inline, the full list under details; injectable via `Options::showOperationFailures`. `Options::fileOperations` and `Options::contextMenuTracker` are test seams.
    - New `tests/integration/MainWindowFileOpsTests.cpp`, 10 tests, with a fake service so nothing is deleted or moved, plus one real rename end to end. Clipboard keys are not automated (they would replace the tester's clipboard); they are V-4a. Live check of the real context menu, dismissed with Escape, no file changed: [screenshot](./validation/us4-context-menu.png). 361/361 in Debug and Release.
    - Not in any task: the list does not watch the folder (`SHChangeNotifyRegister`), so changes made by other programs show only after F5 or the next navigation.)
- [X] T073 [US4] Create the manual checklist `tests/manual/file-operations.md` with V-4a to V-4h (V-4h: rename and copy in `unicode\`; rename and copy in the deepest `longpath\` folder). Include hash comparison commands (`Get-FileHash`) for the conflict-skip check, a Recycle Bin check, and a row confirming the "Copying N items…" status appears during V-4f. Then run it, and add a "Phase 4 (US4)" section to `specs/001-translucent-explorer/validation-report.md` with the results and the T067 results (SC-011).
  - (Done 2026-09-28: `tests/manual/file-operations.md` Part A 16/16 automated, Part B 11 manual rows pending (the system clipboard, real mouse and keyboard, 100%/200%). New `tests/integration/FileOpsShellUiValidation.cpp`: 6 `DISABLED_` tests that raise the Shell's own dialogs (conflict, permanent-delete confirmation, access denied, progress cancel, Properties) and answer them through UI Automation, plus a Recycle Bin check that removes its own item; run with `--gtest_also_run_disabled_tests --gtest_filter=FileOpsShellUi.*`, 6/6 in Release and Debug. New permanent tests: a real three-file copy through the window (V-4a status) and a rename 344 characters deep (V-4h). Found and fixed: the Shell reports each conflicting item first as `COPYENGINE_S_PENDING`, which the service counted as done, so skipped files would have read as moved (`FileOperationService` now ignores pending reports). "Phase 4 (US4)" added to `validation-report.md` with the T067 results. 363/363 in Debug and Release.)

**Checkpoint**: V-4a to V-4h and T067 pass, US4 works on its own, and T073 has recorded the
evidence (constitution phase 4 exit).

---

## Phase 7: User Story 5 — Use native Windows interaction and accessibility (Priority: P2)

**Goal**: Full keyboard operation with visible focus, a UI Automation tree for every
custom-drawn element, correct layout at 100%, 150% and 200% and across monitor changes,
and live adaptation to theme, text-scale, high-contrast and reduced-motion changes
(FR-015 to FR-017; SC-004, SC-007, SC-008).

**Independent Test**: V-5a to V-5e: keyboard-only runs of V-3b, V-2c and V-4a; Narrator
announcements; zero FastPass failures in Accessibility Insights; three scale factors plus
a monitor move; live OS setting changes.

### Implementation for User Story 5

- [X] T074 [P] [US5] Implement `src/a11y/UiaRoot.cpp` and `include/te/a11y/UiaRoot.h` (R-09, UI §5):
  - A class implementing `IRawElementProviderSimple`, `IRawElementProviderFragment` and `IRawElementProviderFragmentRoot` for the main window.
  - `HostRawElementProvider` uses `UiaHostProviderFromHwnd`.
  - `ElementProviderFromPoint` and `GetFocus` delegate to the child providers.
  - Handle `WM_GETOBJECT` with `lParam == UiaRootObjectId` in `src/app/MainWindow.cpp` → `UiaReturnRawElementProvider`.
  - On `WM_DESTROY`, call `UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)` and `UiaDisconnectProvider`.
  - (Done 2026-09-28: `te::UiaRoot` (WRL, `ProviderOptions_ServerSideProvider | UseComThreading`) takes two callbacks from `MainWindow`: the children in UI §5 order (null entries skipped, so T075–T078 only have to return their providers from the components' `Automation()`) and the focused component. Children navigate to their siblings with `UiaRoot::Sibling`. Hit testing and focus go one level deeper through an app-defined `te::IUiaFragmentExtension` (`HitTest`, `FocusedDescendant`) when a child implements it, for example for a file-list row. `WM_GETOBJECT` creates the root on the first `UiaRootObjectId` request only (MSAA requests such as `OBJID_CLIENT` are left to the system), and `OnDestroy` returns null to UIA and disconnects before the components go. After `Disconnect` every call returns `UIA_E_ELEMENTNOTAVAILABLE`. New `tests/integration/UiaRootTests.cpp`, 6 tests, through the real UIA client from a helper MTA thread: tree order under the system's native TitleBar, parent and siblings, hit testing with delegation, focus delegation, disconnection, and the main window's `WM_GETOBJECT` / `WM_DESTROY`, with the native Minimize, Maximize and Close buttons still reported. 369/369 in Debug and Release.)
- [X] T075 [P] [US5] Implement `src/a11y/UiaTitleBarButton.cpp`: a Button control type named "Appearance and color", with `IInvokeProvider` and `IExpandCollapseProvider` (state reflects `IColorPicker::IsOpen`) and a `BoundingRectangle` from `layout.picker` in screen coordinates. Raise `UIA_ExpandCollapseExpandCollapseStatePropertyId` property changes when it opens or closes. (Fallback providers for the native caption buttons, if the spike needs them, are added in T078.)
  - (Done 2026-09-28: `te::UiaTitleBarButton` (`include/te/a11y/UiaTitleBarButton.h`), the root's first child: Button "Appearance and color" (new localizable `IDS_A11Y_PICKER_NAME`), AutomationId `AppearanceButton`, AcceleratorKey "Alt+Shift+C", bounds from `CustomTitleBar::PickerScreenRect`. Invoke, Expand and Collapse post `IDM_OPEN_APPEARANCE`, so the UIA call returns at once; Expand and Collapse act only when the state differs. New `ColorPicker::SetOpenChangedCallback` fires from `Show` and `Hide`, which every close path uses (Escape, Cancel, deactivation, the button), and drives `NotifyOpenChanged`, which raises the ExpandCollapseState property change when clients listen. Disconnected with the root on `WM_DESTROY`. Not keyboard-focusable yet: it joins the F6 ring in T080. New `tests/integration/UiaTitleBarButtonTests.cpp`, 4 tests through the real UIA client. Two test-environment effects found: a popup in a non-foreground test process may close itself at once through its deactivation rule, and an in-process UIA client receives each property event twice although the provider raises it once (confirmed with a temporary probe); the tests account for both. 373/373 in Debug and Release.)
- [X] T076 [US5] Implement `src/a11y/UiaFileList.cpp`: the DataGrid control type named "Items", with `ISelectionProvider` (`CanSelectMultiple = TRUE`), `IGridProvider`, `ITableProvider` (column headers Name, Date modified, Type, Size as HeaderItem children whose `IInvokeProvider` sorts) and `IScrollProvider`, backed by `IFileView`.
  - (Done 2026-09-28: `te::UiaFileList` (`include/te/a11y/UiaFileList.h`) plus internal `UiaHeader` (control type Header, the list's first child) and `UiaHeaderItem` (Name / Date modified / Type / Size from `FileView::Strings`; Invoke = `FileView::SortByColumn`, repeating reverses; `ItemStatus` "Sorted ascending/descending" on the sort column). Headers are created on demand and hold the list, never the reverse, so there are no reference cycles. Grid `RowCount` = items, `ColumnCount` = 4; Table row-major with the four column headers; Selection `CanSelectMultiple` TRUE; Scroll vertical only, one row per small step, a page per large step, `SetScrollPercent` in percent of the scroll range. Rows and cells come from T077 through `Host::row` / `Host::cell`: until then `GetSelection` is empty, `GetItem` returns `E_NOTIMPL`, and hit tests and focus below the header resolve to the list itself. New `FileView` accessors `Bounds`, `HeaderCellRect`, `ContentHeightDip`, `ViewportHeightDip`, `SetScrollOffset`, `SortByColumn`, `ColumnNames`; `MainWindow::DipToScreen` / `ScreenToDip`; shared helpers in `include/te/a11y/UiaCommon.h`; localizable name `IDS_A11Y_FILE_LIST` "Items". New `tests/integration/UiaFileListTests.cpp`, 6 tests through the real UIA client over a 60-file folder. 379/379 in Debug and Release.)
- [X] T077 [US5] Implement `src/a11y/UiaFileItem.cpp`: the DataItem control type, `Name = FileItem.name`, with `ISelectionItemProvider`, `IInvokeProvider` (open), `IScrollItemProvider` (scroll into view) and `IGridItemProvider` (row and column). Cell Text children expose the date, type and size. Providers are keyed by item identity plus generation, and a stale provider returns `UIA_E_ELEMENTNOTAVAILABLE`.
  - (Done 2026-09-28: `src/a11y/UiaFileItem.cpp` with `UiaFileItem` (DataItem, Name = the display name; SelectionItem Select / Add / Remove on `FileView::SelectOnly` / `SetSelected`; Invoke; ScrollItem on `EnsureVisible`; GridItem row n, column 0, column span 4; `IsOffscreen`; bounds clipped to the rows area) and `UiaFileCell` (Text cells Name / Date modified / Type / Size, with GridItem and TableItem whose column header is the HeaderItem). Both are keyed by the item's key plus the listing generation and report `UIA_E_ELEMENTNOTAVAILABLE` once the folder is listed again or the item is gone. The list now builds its rows itself (`UiaFileList::MakeRow` / `MakeCell`) instead of T076's `Host::row` / `Host::cell` hooks; the row before the first is the Header; hit tests resolve to the cell under the point; the focused row is the focus. Invoke posts the new `WM_TE_UIA_OPEN_ITEM` (key, generation), so nothing opens inside the UIA call (opening can navigate or show a modal dialog); the window opens the item only if it is still in the current listing. New `FileView` helpers `IndexOfKey`, `CellRect`, `IsRowVisible`, `EnsureVisible`, `FocusRow`, `SelectOnly`, `SetSelected`. 6 new tests in `tests/integration/UiaFileListTests.cpp`. Found: sending `WM_GETOBJECT` directly from a test made UIA stop asking later windows of the process for their providers, so the tests now create the root through a real client request; and UIA tests must run process-isolated, as ctest does, because a reused window handle can carry state between tests of one process (documented in the three UIA test files; the app has one window per process). The Invoke test first checked from the client thread and was flaky (about 1 in 6); it now calls Invoke on the UI thread and checks the queued `WM_TE_UIA_OPEN_ITEM`, 10/10. 385/385 in Debug and Release.)
- [X] T078 [US5] Implement `src/a11y/UiaChrome.cpp`:
  - The ToolBar "Navigation", with Button providers for Back, Forward, Up and Refresh (`IInvokeProvider`; `IsEnabled` mirrors the toolbar state).
  - The Edit "Address", with `IValueProvider` in display mode. In edit mode, the native `EDIT` is used.
  - The List "Navigation pane", with ListItem children (`ISelectionItemProvider`, `IInvokeProvider`).
  - The StatusBar, with `UIA_LiveSettingPropertyId = Polite`.
  - **Not needed — T027(e) passed** (the system exposes Minimize, Maximize and Close with the Invoke pattern; spike-rendering.md). Kept for reference: only if the native caption buttons were missing from the UIA tree: Button providers "Minimize", "Maximize"/"Restore" and "Close", with `BoundingRectangle` from `DWMWA_CAPTION_BUTTON_BOUNDS` and `IInvokeProvider::Invoke` posting `WM_SYSCOMMAND` with `SC_MINIMIZE`, `SC_MAXIMIZE`/`SC_RESTORE` or `SC_CLOSE` (R-09).
  - (Done 2026-09-28: `src/a11y/UiaChrome.cpp` / `include/te/a11y/UiaChrome.h`, sharing a `ChromeFragment` base for the boilerplate. The ToolBar "Navigation" holds Buttons named from the tooltip texts (text before the parenthesis: "Back", "Forward", "Up to the parent folder", "Refresh"; the parenthesis is the AcceleratorKey, the full text the HelpText). `IsEnabled` mirrors the toolbar; Invoke posts the command, and a disabled button returns `UIA_E_ELEMENTNOTENABLED`. The Edit "Address" has Value in display mode; `SetValue` posts a navigation. While editing, the native `EDIT` is the Address element: the root implements `IRawElementProviderHwndOverride` and returns a property-override provider (`ProviderOptions_OverrideProvider`) that names it "Address", and the toolbar's own Address steps aside. A first design (our fragment hosted by the EDIT) listed the EDIT twice and, with the override, hung in a resolution loop. The List "Navigation pane" has ListItems keyed by location: Select and Invoke post a navigation, AddToSelection is `UIA_E_INVALIDOPERATION`, and Selection is the current place. The StatusBar is named by the status text, with LiveSetting Polite and the appearance summary as FullDescription. Navigation from UIA goes through the new `WM_TE_UIA_NAVIGATE`. Localizable names `IDS_A11Y_TOOLBAR` / `_ADDRESS` / `_NAV_PANE`. Root children now: picker, toolbar, pane, file list, status bar (UI §5). New `tests/integration/UiaChromeTests.cpp`, 9 tests. For T083: the contract calls the button "Up", but its name is "Up to the parent folder" (the localized tooltip); and the inline-rename EDIT is still listed as an unnamed native Edit under the window while renaming. 394/394 in Debug and Release.)
- [X] T079 [US5] Raise UIA events from `src/ui/FileView.cpp`, `src/ui/StatusBar.cpp` and `src/app/MainWindow.cpp`:
  - `UIA_AutomationFocusChangedEventId` on focus moves.
  - `UIA_SelectionItem_ElementSelectedEventId`, `ElementAddedToSelection` and `ElementRemovedFromSelection`.
  - `UiaRaiseStructureChangedEvent(StructureChangeType_ChildrenInvalidated)` on `BeginLocation` and after batches, throttled to at most one per 250 ms.
  - `UIA_LiveRegionChangedEventId` when a status message is set.
  - (Done 2026-09-28: `MainWindow::UpdateAutomationState` compares a snapshot (focus owner, window focus, address editing, focused row key, pane focus, listing generation and count, selected row keys) after every message that can change them, and after `FileView`'s counts callback, which also covers changes made through UIA. From the difference it raises: focus changed on the root's deepest focused element (not for the address EDIT or the popup, which raise their own); ElementSelected when the selection becomes that one row, otherwise AddedToSelection / RemovedFromSelection per row, or one `Selection_Invalidated` above 20 changes; `StructureChanged(ChildrenInvalidated)` on a new listing or item count, at most one per 250 ms plus a trailing one through `kUiaStructureTimerId`. `StatusBar::SetMessageCallback` raises `LiveRegionChanged` when a transient or operation message is set (not for item counts). All only while `UiaClientsAreListening()`. Two defects found by the tests and fixed: the first change after a client connected was only recorded, not announced (the snapshot is now taken when the root is created); and `StructureChanged` passed the short `{UiaAppendRuntimeId, n}` runtime ID, which handlers registered on the list itself never matched (the full ID now comes from `UiaNodeFromProvider` / `UiaGetRuntimeId`). New `tests/integration/UiaEventsTests.cpp`, 4 tests with real UIA event handlers. 398/398 in Debug and Release.)
- [X] T080 [US5] Add keyboard focus management to `src/app/MainWindow.cpp`:
  - A focus ring in this order: picker → address bar → navigation pane → file list. `F6` / `Shift+F6` cycle through it (UI §4).
  - The picker takes Enter and Space to open.
  - Each custom component draws a 2-DIP focus indicator, contrast-checked against its background to at least 3:1 with `Contrast`.
  - Keep `WM_UPDATEUISTATE` / `UISF_HIDEFOCUS` handling consistent with Windows: always show focus after keyboard input.
  - (Done 2026-09-28: `MainWindow::Focus` is now public (`Picker`, `Address`, `Places`, `Files`) with `KeyboardFocus()` and `FocusCuesVisible()`; `CycleFocus` runs picker → address → pane → files and wraps both ways. On the picker, Enter and Space call `TogglePicker`; `CustomTitleBar::SetPickerFocused` draws a 2-DIP rounded ring around the button. The picker's UIA button is now keyboard-focusable, reports `HasKeyboardFocus`, `SetFocus` moves the ring to it, and `AutomationFocus` returns it. New `te::FocusIndicator` (`include/te/render/FocusIndicator.h`, `kWidthDip = 2`, `ColorOver(effective, onSelection)`) over new `Contrast::PickIndicatorColor(preferred, backgrounds)`, which keeps the theme focus color when it reaches 3:1 against every background it sits on, else picks black or white. It fixed a real defect: the ring on a selected row had the same color as the selection fill and was invisible. `FileView`, `NavigationPane` and `CustomTitleBar` draw through it; `AddressBar` draws a ring while editing. `SetFocusVisible` on each component follows `UISF_HIDEFOCUS`: `WM_UPDATEUISTATE` runs `DefWindowProc` then `UpdateFocusCues`; `WM_KEYDOWN`, `WM_SYSKEYDOWN` and accelerator `WM_COMMAND` (HIWORD 1) send `WM_CHANGEUISTATE(UIS_CLEAR)`; a mouse click does not, as in Windows. Defect found by the tests and fixed: F6 inside the address EDIT did nothing, because the edit had the keyboard; `AddressBar`'s edit procedure now forwards F6 / Shift+F6 to the window. New `tests/unit/FocusIndicatorTests.cpp` (5) and `tests/integration/MainWindowFocusTests.cpp` (6). Screenshots `validation/us5-focus-row.png`, `us5-focus-picker.png`, `us5-focus-address.png`, `us5-focus-pane.png`. 409/409 in Debug and Release.)
- [X] T081 [US5] Harden DPI handling in `src/app/MainWindow.cpp`, `src/window/CaptionHitTesting.cpp`, `src/render/TextFormats.cpp`, `src/ui/FileView.cpp` and `src/appearance/ColorPicker.cpp`:
  - On `WM_DPICHANGED`, apply the suggested rectangle, then recompute the caption layout, rebuild the text formats, re-request icons at the new pixel size (new generation for icons only), resize the render target, and reposition an open picker popup inside the new monitor's work area.
  - Handle `WM_GETDPISCALEDSIZE` for exact scaling.
  - Verify at 100%, 150% and 200%, and when moving between monitors with different scales (V-5d).
  - (Done 2026-09-28: `MainWindow::OnDpiChanged` now gives every component the new DPI (toolbar, address bar, file list with its rename edit, navigation pane, which extracts its icons again) and rebuilds the text formats before `DpiManager::OnDpiChanged` stores the DPI and applies the suggested rectangle, so the `WM_SIZE` from the move already lays out at the new scale. It then reloads the title bar and taskbar icons at the new size (new `LoadAppIcons`), gives the file icons a new generation, and calls `OnSize` (caption layout through `CustomTitleBar::UpdateLayout` / `CaptionHitTester`, render target and layout), also when the move left the pixel size unchanged. An open appearance popup moves with the picker button, inside the new monitor's work area: new `ColorPicker::Reposition`; the popup's own `WM_DPICHANGED` (the dialog manager rescales it) posts a second placement once its new size is known. Icons for the file list only: new `GenerationGuard::Issue` hands out a number without changing the current listing generation; `FileView::ResetIcons(iconGen)` / `IconGeneration()` mark every row to be requested again (the old bitmap stays on screen, scaled, until its successor arrives), `IconProvider::CancelOlderThan` drops requests queued at the old size, and `OnIconReady` / `ConvertPendingIcons` accept icons by the icon generation; the listing, its generation, selection and scroll stay. Side fix found on the way: `NavigateTo` cancelled the icons of the folder still on screen, so if the navigation failed (FR-020) its unloaded rows never got icons; icons now get a fresh generation when a navigation commits. `WM_GETDPISCALEDSIZE` (in `CustomTitleBar`): new `DpiManager::ScaledWindowSize` scales the client area by newDpi / oldDpi and adds `CustomTitleBar::FrameSizeForDpi` (`AdjustWindowRectExForDpi`, without the caption, which WM_NCCALCSIZE gives to the client area), so the client area keeps its DIPs; maximized and minimized windows keep Windows' own answer. `TextFormats` needed no change: formats are in DIPs, and `Rebuild` already takes the text scale. New `tests/unit/DpiScalingTests.cpp` (4); new tests in `GenerationGuardTests` (1), `FileViewTests` (1), `ColorPickerTests` (1); new `tests/integration/MainWindowDpiTests.cpp` (5, plus `DISABLED_CaptureAtEachScale`), which sends the real message sequence (`WM_GETDPISCALEDSIZE`, then `WM_DPICHANGED`) to the window: frame model, exact size at ×1.5 / ×2 / ×⅔, everything rescaled with the listing kept, a ×2 round trip without drift, the open popup following the button. Found while testing: a window at its minimum size grows at a higher scale (the minimum is in DIPs), and Windows caps a window at the screen size, so a round trip is exact only between those limits; a chain of non-integer ratios (150 → 100 → 200 → 150 %) can differ by a pixel. Screenshots `validation/us5-dpi-1-144.png`, `us5-dpi-2-96.png`, `us5-dpi-3-192.png`, `us5-dpi-4-144.png` (simulated scales on this 150 % monitor; the caption buttons stay at the real scale because the DWM draws them). This PC has one monitor (1920 × 1200 at 150 %), and the display settings are not changed by tests, so real 100 % / 200 % runs and monitor moves are left to the manual `tests/manual/dpi.md` (T083). 421/421 in Debug and Release.)
- [X] T082 [US5] Adapt live to OS settings in `src/appearance/ThemeManager.cpp`, `src/render/TextFormats.cpp` and `src/app/MainWindow.cpp`:
  - On `TextScaleFactorChanged`, rebuild the formats and relayout rows (row height scales with text).
  - On a `SPI_GETCLIENTAREAANIMATION` change, re-read the setting into `RenderingCapabilities`. The app has no animations (R-05): hover and pressed states change instantly. Add a code comment and a review check that any animation added later must be skipped when `animationsEnabled` is false.
  - On a light/dark change, update `DWMWA_USE_IMMERSIVE_DARK_MODE` and the colors.
  - On high contrast on or off, re-resolve (T037 path).
  - All of this must work without a restart (V-5e).
  - (Done 2026-09-28: `MainWindow::RefreshAppearance`, which runs for every `TextScaleFactorChanged` / `ColorValuesChanged` / `AdvancedEffectsEnabledChanged` event (`WM_TE_SETTINGS_CHANGED`) and every relevant `WM_SETTINGCHANGE`, now also calls new `ApplyTextScale`: it rebuilds the formats at the new scale (clamped to 1–2.25), then gives the scale to `FileView::SetTextScale` (rows and header grow, the top row stays on top, the rename edit gets the new font), `NavigationPane::SetTextScale`, `CustomTitleBar::SetTextScale` (the minimum height keeps five rows of the larger text; re-applied at once through `SetWindowPos`) and `AddressBar::OnTextScaleChanged` (new font; the field grows), and lays out again; `MainLayout::Compute` takes the text scale for the toolbar and status bar heights. Startup previously hard-coded 1.0 and nothing rebuilt the formats; `UiaFileList` scrolling and `UiaFileItem` bounds use the scaled heights. Found in the 225 % capture and fixed: the title was clipped, because the caption strip keeps the height Windows gives it; `CustomTitleBar` now lays the title out and draws it smaller when it is taller than the strip. Icons keep their size, as in Windows (Text size changes text only). `WM_SETTINGCHANGE(SPI_SETCLIENTAREAANIMATION)` now re-reads `RenderingCapabilities::animationsEnabled`; the app still has no animations (R-05), and the code comments in `AppearanceSettings.h` and `RefreshAppearance` and a new `docs/review-checklist.md` (Motion) require any future animation to show its end state at once when it is false. Light/dark (`DWMWA_USE_IMMERSIVE_DARK_MODE` and colors) and high contrast (T037 path) already re-resolved live; they are now covered by tests. Test hook `MainWindow::Options::adjustCapabilities` changes what the window reads, so no user setting is touched; `Capabilities()` accessor. New `tests/integration/MainWindowLiveSettingsTests.cpp` (5, plus `DISABLED_CaptureAtEachTextSize`), `tests/unit/MainLayoutTests.cpp` (3), one new `FileViewTests` test; the screen-capture helper moved to `tests/integration/ScreenCapture.h`. Screenshots `validation/us5-text-100.png`, `us5-text-150.png`, `us5-text-225.png`. The real Settings switches (V-5e) are in the manual `tests/manual/dpi.md` (T083). 430/430 in Debug and Release.)
- [X] T083 [US5] Create the manual checklists `tests/manual/accessibility.md` (V-5a to V-5c, with the expected Narrator announcements and a FastPass result table) and `tests/manual/dpi.md` (V-5d, V-5e). Then run them, and add a "US5" section to `specs/001-translucent-explorer/validation-report.md` with the results (SC-011).
  - (Done 2026-09-28: `tests/manual/accessibility.md` (Part A 13 automated rows; Part B: keyboard only B1–B4, expected Narrator announcements B5–B13, FastPass result table C1–C2) and `tests/manual/dpi.md` (Part A 12 automated rows; Part B: 100 / 150 / 200 %, a scale change while running, two monitors, and the Settings switches for dark mode, text size, animation effects, contrast themes and transparency, B1–B11). Part A of both passes; Part B is pending: Accessibility Insights is not installed here, Narrator was not started, real scale and settings changes would change the tester's settings, and the PC has one monitor. In its place, new `tests/integration/UiaAuditTests.cpp` (4): an audit through the real UIA client with checks modelled on FastPass's automated rules (Axe.Windows: names, control types, bounds contained in the parent, required patterns, parent types, keyboard focusability, ambiguous focusable siblings, the focused element), over the main window idle, while editing the address, during an inline rename, and over the appearance popup. It found two defects, both fixed: (1) while editing, UIA listed a typeless "Address" element under the address EDIT, level after level, made from the T078 override provider (`IRawElementProviderHwndOverride`); the EDIT is now named with Dynamic Annotation (new `te::AnnotateHwnd` / `ClearHwndAnnotation` in `src/a11y/HwndAnnotation.cpp`, `IAccPropServices::SetHwndPropStr` for Name and AutomationId, cleared on `WM_NCDESTROY`), and `MakeUiaAddressOverride` was removed; (2) the inline-rename EDIT had no name (the T078 open item): now "Name" (new `IDS_A11Y_RENAME` 1305, `MainWindow::Options::renameAutomationName`, AutomationId `RenameEdit`). The title bar is exempt from the audit's name rule: Windows' own non-client element has an empty name for every Win32 window (checked against a plain window). "Phase 5 (US5)" section in `validation-report.md`. Still open for the project owner: the Up button's UIA name "Up to the parent folder" vs "Up" in UI §5. 434/434 in Debug and Release.)

**Checkpoint**: V-5a to V-5e pass, FastPass reports zero failures, US5 works on its own,
and T083 has recorded the evidence.

---

## Phase 8: User Story 6 — Remain responsive with large folders (Priority: P3)

**Goal**: 10,000+ item folders, icon storms and rapid navigation stay interactive and
correct. Closing during enumeration is safe, and repeated window lifecycles do not leak
(FR-018, FR-019; SC-009, SC-010).

**Independent Test**: V-3f, V-3g and V-6a to V-6c: interaction while `10k` loads, rapid
alternation, integration tests for 10k, cancellation and close-during-enumeration, 50
lifecycle cycles at baseline, and Application Verifier with no stops.

### Tests for User Story 6

- [X] T084 [US6] Extend `tests/integration/DirectoryEnumeratorTests.cpp`:
  - (a) Enumerating `10k` delivers exactly 10,000 items across batches of 256 or fewer, and the UI-side guard accepts all of them.
  - (b) Calling `CancelAll()` after the first batch gives `EnumDone { cancelled = true }` within 2 s, and no further batches arrive.
  - (c) `Shutdown()` while enumerating `10k` returns, and every posted payload is freed (counting allocator hook).
  - (Done 2026-09-28: (a) `TenThousandFilesArriveInBatchesOfAtMost256` now takes every batch through a `GenerationGuard`: 10,000 items accepted, none rejected, every batch 1–256 items. (b) new `CancelAllAfterTheFirstBatchStopsTheBatches`: after the first batch, `CancelAll()`; the batches already queued are taken off, and what follows must be the `EnumDone` alone, `cancelled`, within 2 s (measured: at once). To make "no further batches" a guarantee rather than a race, `DirectoryEnumerator` now checks the stop tokens and posts each batch under a `postLock` that `CancelAll`, `Start` (superseding) and `Shutdown` also take, so once they return a cancelled request posts no batch; before, the check ran only between `Next(64)` calls. (c) new `ShutdownWhileEnumeratingFreesEveryPayload`: `Shutdown()` mid-enumeration returns (measured 0 ms, 256 items delivered), and after the queue is drained the live counts of `EnumBatch` and `EnumDone` are back to where they started. The counting hook is a new `LiveCounted<T>` base on every `WM_TE_*` payload in `include/te/core/Messages.h` (one relaxed atomic increment/decrement; `Live()`), usable by T085/T088. New `DrainFreesEveryPayloadType`: `MainWindow::DrainPendingMessages` frees all five payload types. Mutation checks: making the drain leak `EnumDone` fails `DrainFreesEveryPayloadType`; making the worker leak a batch it declines to post after a cancel was not caught, because in these runs the cancel always landed between fetches, so that path (the new lock's) is not exercised by the tests. DirectoryEnumerator 8/8; 437/437 in Debug and Release.)
- [X] T085 [US6] Extend `tests/integration/WindowLifecycleTests.cpp`: repeat the 50-cycle test while navigating to `%TEMP%\te-test\10k` and destroying the window before `EnumDone`. Assert the GDI count (`GR_GDIOBJECTS`), the USER count (`GR_USEROBJECTS`, a separate `GetGuiResources` call) and the handle count each return to baseline.
  - (Done 2026-09-28: new `WindowLifecycle.DestroyingWhileEnumeratingTenThousandItemsDoesNotLeak`: 2 warm-up and 50 measured cycles, each creating the window with the Shell started on `10k`, pumping one message at a time until the first batch is on screen (icons requested), then destroying it. It asserts that all 50 windows were destroyed with the listing incomplete, that GDI, USER and handle counts equal the baseline, and that the live count of every `WM_TE_*` payload (`LiveCounted`, T084) is back where it was. Result, 3 runs each in Debug and Release: GDI 47 → 47, USER 6 → 6, handles 473 → 473, payloads 0 → 0, 50/50 destroyed mid-listing. A first version pumped all queued messages before checking, so only 17 of 50 windows were destroyed mid-listing; in that run the handle count ended one above the baseline (one handle over 50 cycles, so not one per window). A per-cycle trace of the corrected test showed no drift. (Identified in T086, when it came back: a Section handle mapping the Windows component-catalog cache, `C:\ProgramData\Microsoft\Windows\Caches\cversions.2.ro`, which COM and the Shell map again at random moments when the catalog version changes; not the application's. Both lifecycle tests now count those sections separately and compare every other handle exactly.) The current shutdown order already passes; T088 still hardens it. 438/438 in Debug and Release.)

### Implementation for User Story 6

- [X] T086 [US6] Virtualize `src/ui/FileView.cpp`:
  - Render and hit-test only the visible row range (computed from scroll offset and row height).
  - Cache `IDWriteTextLayout` per visible row, and invalidate on scroll, resize or DPI change.
  - `AppendItems` uses `std::merge` of each sorted batch into the existing sorted vector, instead of re-sorting everything.
  - Keep the selection and focus by item identity across merges.
  - (Done 2026-09-28: rendering and hit-testing already used only the visible range (`Render` loops from `floor(scroll / RowHeight())`, `RowAt` computes the index), and icons are requested for visible rows only. New: (1) a text-layout cache: `FileView::LayoutsFor` builds one `IDWriteTextLayout` per cell (4 per row) with the column width and row height, keyed by item key and tagged with a layout epoch; each frame keeps the layouts of the rows it drew and drops the rest, so scrolling builds only the rows that came into view. `InvalidateLayouts` bumps the epoch on a column resize, a size change (`SetBounds`), `SetDpi`, `SetTextScale`, new text formats and a new body format; `ApplyRename` drops that row's layouts; `BeginLocation` clears them. Rows are drawn with `DrawTextLayout`. (2) New `SortModel::MergeBatch`: the batch alone is stable-sorted and merged in with `std::merge`, whose tie rule (existing first) gives exactly the order a full stable sort of the appended list would; `AppendItems` uses it, and its permutation keeps selection and focus on their items (`SelectionModel::ApplyPermutation`) and rebuilds the key index. Tests: `SortModel.MergingBatchesMatchesAFullSort` (600 random items with many ties, batches of 1–90, all 4 fields in both directions, checking the permutation too), `MergingIntoAnEmptyListSortsTheBatch`, `FileViewTest.MergedBatchesKeepTheSelectionAndFocusOnTheirItems`, `FileViewRender.TextLayoutsAreCachedForTheVisibleRows` (a repaint builds none; one row of scroll builds 4; a column resize, a resize, a text-scale and a DPI change rebuild the visible rows). Mutation: merging with the batch first on ties fails the equivalence test for all 8 field/direction pairs. Render capture checked: alignment, ellipsis and right-aligned sizes unchanged. Two test fixes from the full runs: the 10k lifecycle test (T085) showed +1 handle again, now traced to the component-catalog cache mapping (see T085) and counted separately; `UiaChromeTest.HitTestingFindsButtonsAndPlaces` failed once because `ElementFromPoint` hit-tests the real screen, so it and the two other hit-test tests (`UiaFileListTests`, `UiaRootTests`) keep their window topmost while they hit-test. 442/442 in Debug and Release.)
- [X] T087 [US6] Upgrade `src/shell/IconProvider.cpp` (R-07):
  - A LIFO queue using `StaWorker::PostFront`, fed by `FileView` requesting icons only for visible rows plus a 1-screen look-ahead.
  - `CancelOlderThan(gen)` drops stale requests.
  - Cache results by the system image-list index from `SHGetFileInfoW(pidl, SHGFI_PIDL | SHGFI_SYSICONINDEX)`, so items that share an icon (for example all `.txt` files) reuse one `ID2D1Bitmap1` on the UI side.
  - Limit WIC → Direct2D conversions to 32 per frame, deferring the rest to the next `WM_PAINT`.
  - (Done 2026-09-28: `IconProvider::Request` posts with `StaWorker::PostFront` (LIFO). `FileView::ForEachVisibleWithoutIcon` requests the visible rows plus one screen ahead, the look-ahead first and the visible rows last, bottom to top, so the top visible row is served first. `CancelOlderThan` unchanged. The worker reads each item's system image-list index with `SHGetFileInfoW(SHGFI_PIDL | SHGFI_SYSICONINDEX)` and remembers the (index, size) pairs it has sent with a bitmap; a repeat comes back as `IconReady { shared = true }` without extracting. `IconReady` gained `imageIndex`, `sizePx` and `shared`; `Request` gained `forceExtract`. New `te::IconCache` (`include/te/ui/IconCache.h`, `src/ui/IconCache.cpp`) keeps one `ID2D1Bitmap1` per (index, size) and drains the window's queue (now a `std::deque`) each frame: results for another icon generation are dropped, cached and shared icons are delivered without a conversion, and at most `kMaxConversionsPerFrame` = 32 HBITMAPs are converted; the rest stay queued and the window invalidates itself for the next `WM_PAINT`. A shared icon the cache no longer has (after a Direct2D device loss) makes the row ask again with `forceExtract` (`FileView::RequestIconAgain`). The file list previously kept bitmaps from a lost device; now `IconCache::SetDevice` notices a new device, and the window drops every row's bitmap and requests them again, forced (`ResetIcons(gen, false)`). Tests: `tests/unit/IconCacheTests.cpp` (7: sharing, the 32 budget and order, cached icons outside the budget, stale generations, a shared miss, a device change, sizes kept apart), `IconProviderTests` +2 (shared unless forced or another size; the newest request served first, the rest in LIFO order), `FileViewTests` (look-ahead and request order; request-again and forced reset), and new `tests/integration/MainWindowIconTests.cpp`: over `10k` (10,000 .txt files) every visible row, at the top and 5,000 rows down, draws the same bitmap, and the window converted exactly one. 453/453 in Debug and Release.)
- [X] T088 [US6] Harden the shutdown order in `src/app/MainWindow.cpp` (CI "Cross-thread message contract"). `WM_CLOSE` and `WM_DESTROY` run, in this order:
  - `IDirectoryEnumerator::CancelAll` and `IIconProvider::CancelOlderThan(UINT64_MAX)`
  - `IFileOperationService::Shutdown`, which waits for a running `IFileOperation` whose Shell UI is modal
  - `IDirectoryEnumerator::Shutdown` and `IIconProvider::Shutdown`
  - drain the `WM_TE_*` messages
  - release the UIA providers
  - release Direct2D, DirectComposition and Direct3D
  Make T084(c) and T085 pass.
  - (Done 2026-09-28: `MainWindow::OnDestroy` rewritten in the contract order; before, it released UI Automation first, then shut the workers down before the file-operation service, and never released the graphics devices explicitly. Now: `WM_CLOSE` (new) and step 1 of `WM_DESTROY` call `CancelBackgroundWork` once: `IDirectoryEnumerator::CancelAll` and `IIconProvider::CancelOlderThan(UINT64_MAX)`. Then (2) `IFileOperationService::Shutdown`, dropping queued operations and flushing the clipboard; (3) `DirectoryEnumerator::Shutdown` and `IconProvider::Shutdown`; (4) the icon queue and `DrainPendingMessages`; (5) the UI Automation providers, disconnected and released; (6) every bitmap made on the device (`IconCache`, the file list's icons, new `NavigationPane::ReleaseDeviceResources`, new `CustomTitleBar::ReleaseDeviceResources`), then new `RenderDevice::ReleaseDevice` (DirectComposition, Direct2D, swap chain, Direct3D). Settings are saved and the popup destroyed before the first wait. New test hook `Options::shutdownTrace`. New `tests/integration/MainWindowShutdownTests.cpp` (2): closing while `10k` is listed, with a UI Automation client connected and a fake file-operation service that posts one last result from its `Shutdown`, gives the trace cancel → fileops → (service) → workers → drain → uia → graphics, cancel only once, every payload freed (including that last result), UIA and the device released and no icon bitmap left; `DestroyWindow` without `WM_CLOSE` runs the same steps. Mutation: draining before the file-operation service fails both the order and the payload count (the service's last result leaks). T084(c) and T085 pass (the 10k lifecycle test: GDI, USER, handles and payloads at baseline). 455/455 in Debug and Release.)
- [X] T089 [US6] Create the manual checklist `tests/manual/performance.md`, then run it and add a "US6" section to `specs/001-translucent-explorer/validation-report.md` with its results and the T084/T085 results (SC-011). The checklist covers:
  - V-3f and V-3g
  - repeated navigation between a local folder and a network share (`\\localhost\c$` or a real share), 20 round trips
  - disconnecting a USB drive while it is being listed
  - running V-3 and V-4 under Application Verifier (Basics: Handles, Heaps, Locks) with the command `appverif /verify TranslucentExplorer.exe`
  - recording watchdog output from DebugView
  - Stating the note: no latency target is asserted.
  - (Done 2026-09-28: `tests/manual/performance.md`: Part A, 9 rows (8 pass; the Debug CRT leak report is pending T093, which adds it), and Part B, 6 manual rows: V-3f / V-3g by hand, 20 local ↔ network-share round trips, a USB drive unplugged while listed, Application Verifier (`appverif /verify TranslucentExplorer.exe`, Basics, with `appverif /n` to switch it off) and DebugView watchdog lines, with the no-latency-target note. New `tests/integration/MainWindowResponsivenessTests.cpp` automates V-3f: while `10k` loads, wheel scroll, two resizes, End / Home and the popup, each taking effect, every message timed and the Debug watchdog's lines captured through its test sink; 3 runs each: Debug, all 4 actions during loading, longest handler 25 ms, none over 50 ms, no watchdog line; Release, 2 of 4 during loading (listing done in about 550 ms), longest 17 ms. V-3g was automated in T066. Not run here: `\\localhost\c$` is not open to this account and there is no other share (creating one would change the system); no removable drive; Application Verifier is installed but enabling it writes machine settings for the executable. "Phase 6 (US6)" section in `validation-report.md` with the T084–T088 results and findings. 456/456 in Debug and Release.)

**Checkpoint**: V-3f, V-3g and V-6a to V-6c pass, US6 works on its own, and T089 has
recorded the evidence.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Optional FR-021 aids, release gates and documentation (constitution phase 5
exit).

- [X] T090 [P] Optional (FR-021, SHOULD): add a folder tree mode to `src/ui/NavigationPane.cpp`. Expand nodes lazily with `IDirectoryEnumerator`-style enumeration on the enumeration worker, folders only (`SHCONTF_FOLDERS`). Nodes use UIA TreeItem with `IExpandCollapseProvider` in `src/a11y/UiaChrome.cpp`, and the tree is translucent like the pane.
  - (Done 2026-09-28: every place in the navigation pane is a tree node with a chevron; collapsed, the pane looks and works as before, so no separate mode switch was added. The rows on screen stay one flat vector in display order with a depth per row (`Entry::id`, `depth`, `expandable`, `expanded`, `loading`), so scrolling, hit-testing and focus keep working on indices; new tree navigation `ParentOf` / `FirstChildOf` / `LastChildOf` / `NextSiblingOf` / `PreviousSiblingOf` / `IndexOfNode`. Expanding a node the first time asks new `te::FolderTreeLoader` (`src/shell/FolderTreeLoader.cpp`, its own STA worker, so a node never waits behind a large folder's listing): `IShellFolder::EnumObjects(SHCONTF_FOLDERS)` (hidden ones only when Explorer shows them), zip folders left out (`SFGAO_STREAM`), `SFGAO_HASSUBFOLDER` for the chevron, each child's icon extracted on the worker, folders by name in natural order then drive roots; the result comes back as new `WM_TE_TREE_CHILDREN` (`FolderChildren` payload, live-counted, freed by the drain; `WM_TE_LAST` moved to it). A collapsed node keeps its rows (with their own open state) for the next expansion; a folder with no subfolders becomes a leaf; expanded folders are remembered and reopened after a refresh (a drive arriving or leaving). Input: a click on the chevron expands or collapses without navigating, elsewhere on the row navigates; Right expands, then goes to the first child; Left collapses, then goes to the parent. Drawn over the pane's translucent surface with the same scrim, selection and focus ring; indentation 16 DIP per level. UI Automation: the pane is now a Tree (was a List) and each row a TreeItem keyed by node id (the same folder can appear twice, e.g. Desktop at the top and under This PC), nested as the tree is, with ExpandCollapse (Collapsed / Expanded / PartiallyExpanded while loading / LeafNode), SelectionItem and Invoke; Expand / Collapse post new `WM_TE_UIA_EXPAND`, and rows added or removed raise `StructureChanged` on the pane (`RaisePaneStructureChanged`, sharing a new `FullRuntimeId` helper with the file list). Shutdown (T088): the loader is cancelled in step 1 and joined in step 3. Test hook `NavigationPane::SetPlacesForTesting`. New `tests/integration/FolderTreeTests.cpp` (8: the loader lists folders only, sorted, with icons; expand, collapse and re-expand without a second listing; an empty folder becomes a leaf; Left / Right; the chevron toggles and the name navigates; open folders reopen after a refresh; the current folder is highlighted inside the tree; UI Automation Tree / TreeItem, nested, expanded through ExpandCollapse), plus `DISABLED_CaptureTheTree`; `UiaChromeTests` and `UiaAuditTests` updated to Tree / TreeItem, and the audit gained the TreeItem parent and ExpandCollapse rules. Screenshot `validation/polish-folder-tree.png`. For the project owner: UI contract §5 still shows `List "Navigation pane"` with ListItems. Not done: the tree does not expand itself to the current folder, as Explorer's does. 464/464 in Debug and Release.)
- [X] T091 [P] Optional (FR-021, SHOULD): add a breadcrumb display mode to `src/ui/AddressBar.cpp`: segments from `Location` ancestors, where clicking a segment navigates and a chevron lists child folders. Clicking empty space enters edit mode (existing behavior).
  - (Done 2026-09-28: in display mode the address shows `Crumbs()`: the location and its ancestors through `ShellLocation::Parent()`, root first, the namespace root (the Desktop) left out, so a system folder reads "This PC > Windows (C:) > Windows > System32" and a folder inside the profile starts at the user's own folder, as the Shell's hierarchy has it. `LayoutCrumbs` measures each name with DirectWrite (on location, bounds, text-format and text-size changes); when they do not fit, the leading segments move behind an overflow button («) whose menu lists them, nearest first, and the last segment always shows (cut with an ellipsis if needed); 32 DIP at the right end stay empty for editing. A click on a segment navigates (new `SetLocationCallback`, wired to `NavigateTo(..., Push)`); a click on its chevron lists the subfolders with a `FolderTreeLoader` (T090) off the UI thread, and the menu opens when they arrive (node ids tagged `AddressBar::kNodeTag`, so `MainWindow` routes `WM_TE_TREE_CHILDREN` to the address bar or the navigation pane; a listing for an earlier location or chevron is ignored); choosing an item navigates; empty space starts editing, as before. Hover highlights segments, chevrons and the overflow button (`OnPointerMove` / `OnPointerLeave`, forwarded by the window); the chevron points down while its menu loads. Menus go through `ContextMenu::Track` (new `MainWindow::Options::breadcrumbMenuTracker`), so tests pick items without a modal menu. Without text formats nothing is laid out and the plain path shows (the existing address-bar tests). The loader is cancelled and joined in the T088 shutdown order. UI Automation is unchanged: the Address element keeps the path as its Value; the segments are not separate elements (editing gives keyboard and screen-reader users the same navigation). Found while testing: `%TEMP%` paths begin at the user's folder, not This PC, and show the user's full name, so the screenshots use the system folder. New `tests/integration/AddressBarBreadcrumbTests.cpp` (8: segments from the root child, a system folder from This PC, segment click, chevron menu and choice, dismissed and stale menus, overflow, empty space and editing, the real window), plus `DISABLED_CaptureTheBreadcrumbs`. Screenshots `validation/polish-breadcrumbs-wide.png`, `polish-breadcrumbs-narrow.png`. 472/472 in Debug and Release.)
- [X] T092 [P] Optional (FR-021, SHOULD): add a filter box in the toolbar to `src/ui/Toolbar.cpp` and `src/ui/FileView.cpp`. `Ctrl+F` focuses it, and it filters the current folder's items by name substring (case-insensitive, `FindNLSStringEx` with `LINGUISTIC_IGNORECASE`). Escape clears it. Full Windows Search integration is out of scope.
  - (Done 2026-09-28: the box is its own component, new `te::FilterBox` (`include/te/ui/FilterBox.h`, `src/ui/FilterBox.cpp`), rather than part of `Toolbar.cpp`, which stays the four buttons; it sits at the right end of the toolbar row (200 DIP, at most a third of the row; the address bar gives up that width) and works like the address bar: drawn with Direct2D over the translucent toolbar (the filter text, or the placeholder "Filter", new `IDS_FILTER_PLACEHOLDER` 1306), and while it has the keyboard a layered, colour-keyed EDIT takes the typing, named "Filter" (AutomationId `FilterBox`) for UI Automation with Dynamic Annotation. Ctrl+F (new accelerator `IDM_FOCUS_FILTER` 40015) or a click gives it the keyboard; every change (`EN_CHANGE`, routed by `MainWindow`'s `WM_COMMAND`) filters the list at once; Enter keeps the filter and gives the keyboard back to the list; Escape clears it and does the same; F6 still moves on in the focus ring; a new folder clears it. `FileView::SetFilter` keeps the items that do not match in `m_hidden`, sorted like the list: changing the filter merges both back (`std::merge`) and splits them again, the selection and focus stay on items that remain (hidden ones come back unselected), later batches are split as they arrive (hidden ones merged with `SortModel::MergeBatch`), a new sort sorts the hidden ones too, and a pending icon request of a hidden row is asked again when it shows. Matching: `FindNLSStringEx(FIND_FROMSTART | LINGUISTIC_IGNORECASE)` on the shown name or the real one (`editName`) - found by the window test: with known extensions hidden, ".txt" matched nothing until the real name was included. The status bar counts the listed items. Not done: the box is not in the F6 ring (Ctrl+F reaches it), and it is a UI Automation element only while it has the keyboard. New `tests/unit/FileViewFilterTests.cpp` (6: matching, hide and restore in order, the real name with hidden extensions, the selection, later batches and sorting, a new folder) and `tests/integration/MainWindowFilterTests.cpp` (4: Ctrl+F and live filtering, Enter and Escape, a new folder clears it, the edit's UIA name), plus `DISABLED_CaptureTheFilteredList`. Screenshot `validation/polish-filter.png` (the system folder filtered by "shell"). 482/482 in Debug and Release.)
- [X] T093 Add the Debug CRT leak check in `src/app/wWinMain.cpp`: `_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF)` under `_DEBUG`. Run V-3 and V-4, and fix any reported leaks (SC-010).
  - (Done 2026-09-28: `EnableLeakCheck()` at the start of `wWinMain` under `_DEBUG` adds `_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF` to the CRT flags, so blocks still allocated at CRT shutdown are reported to the debugger output; when the environment variable `TE_CRT_REPORT` names a file, the report also goes there (`_CRTDBG_MODE_FILE`; the file stays open because the report is written after `wWinMain` returns). New `tools/Invoke-CrtLeakRun.ps1` runs the Debug build through V-3 (address, Enter into a folder, Back / Forward / Up / Refresh, `10k` with End / Home, five rapid Back / Forward, `unicode`, a bad path, the filter, the appearance popup, the folder tree) and V-4 (F2 rename, Delete to the Recycle Bin, in a scratch folder; the one recycled file is removed from the Recycle Bin afterwards) through window messages only, no injected input and no clipboard, closes the window and prints the report: empty, exit code 0 - no leaks to fix. The report path was proven with a planted `new char[42]`: "Detected memory leaks! {207} normal block ..., 42 bytes long", then removed. V-4's copy and paste were left out of the scripted run because they need the system clipboard (the service's copy and move paths run in `FileOperationServiceTests` and `ClipboardTests` under the same Debug CRT). New regression test `WindowLifecycle.TheCrtHeapReturnsToBaselineAfterBrowsing` (Debug; skipped in Release): two warm-up windows, then `_CrtMemCheckpoint` around five windows that list `nested`, filter, expand a tree node, go up and refresh: 0 normal blocks more; on a leak it dumps the blocks with their allocation numbers. Mutation: a `new int` in `FilterBox::Clear` failed it with one 4-byte block per cycle listed. `tests/manual/performance.md` A9 and the validation report's V-6b row updated. 483/483 in Debug and Release.)
- [X] T094 Run the Release build with `cmake --build --preset x64-release`, and confirm zero warnings in application code with `/W4 /WX`. Fix any Release-only warnings, for example unused variables in `assert`s, in the affected `src/**` files (SC-010).
  - (Done 2026-09-28: `cmake --build --preset x64-release --clean-first` rebuilt all 200 translation units (te_core, the application, both test executables) with exit code 0 and no line containing "warning" in the whole log - no compiler, linker (`LNK4xxx`) or resource-compiler warning either, which `/WX` would not have turned into errors. The generated projects carry `/W4` (WarningLevel Level4) and `/WX` (TreatWarningAsError) for all four configurations of te_core, TranslucentExplorer, te_unit_tests and te_integration_tests; third-party headers are compiled at `/external:W0` by design (R-13). The code has no `assert`, so no assert-only variables can warn in Release. Nothing to fix. Release tests on the clean build: 483/483.)
- [ ] T095 Run Application Verifier (Basics) on the Debug build across the quickstart V-3 and V-4 scenarios. Record the results in `tests/manual/performance.md` and fix any stops.
- [ ] T096 Run the Accessibility Insights for Windows FastPass on the main window and the appearance popup. Record the results in `tests/manual/accessibility.md`, and fix any failures in `src/a11y/*` or `resources/TranslucentExplorer.rc`.
  - (Deferred to manual release testing, 2026-09-28, by the project owner's decision: Accessibility Insights is not installed on the development machine, and downloading it or its Axe.Windows engine was declined. The run stays as rows C1–C2 in `tests/manual/accessibility.md`; until then, `tests/integration/UiaAuditTests.cpp` (T083, extended in T090) checks FastPass-style rules on every test run.)
- [X] T097 [P] Update `README.md` with a project summary, prerequisites, build, test and run commands (from quickstart), links to `specs/001-translucent-explorer/`, and the constitution. (Done 2026-09-28: README keeps the original sentence verbatim and adds the summary, prerequisites, build / test / run and test-data commands from quickstart, keys, the CRT leak run, links to the constitution, every feature document, the manual checklists and the review checklist, and a folder layout.)
- [X] T098 Re-run the constitution check after implementation. Update the "Post-design" column in `specs/001-translucent-explorer/plan.md` "Constitution Check" to "Post-implementation" with evidence links, and record any new deviations in its Complexity Tracking. (Done 2026-09-28: all ten principles pass after implementation, with manual rows, T095 and T096 still open for T099; the column is now "Post-implementation" with evidence per principle; the governance gates show the release gates as open until T099; four new deviations are added to Complexity Tracking: opaque transient menus, Dynamic Annotation for native EDITs, test hooks in production classes, and a folder filter as the search facility; ui-contract §5 drift (List vs Tree, breadcrumbs and filter missing) is recorded for the project owner.)
- [ ] T099 Run the full quickstart.md validation (V-1 to V-6, including V-3h and V-4h) on Debug and Release, on build 22621 or later at 100%, 150% and 200%. Add the "Phase 5 (release)" section to `specs/001-translucent-explorer/validation-report.md` with pass/fail per scenario and for SC-001 to SC-011, and confirm that the earlier phase sections are all present and passing (SC-011). Confirm the "Sign-offs" section has no open items. In particular, if the address-edit fallback was used (T061), the project owner's approval must be recorded there before release. (In progress 2026-09-28: automated part done: clean Debug and Release rebuilds with 0 warnings, 483/483 tests in each, CRT leak report empty; "Phase 5 (release)" section added to validation-report.md with every scenario and SC-001 to SC-011; all earlier sections present; no open sign-offs. Left open: the manual Part B rows at 100%, 150% and 200% with two monitors, T095 Application Verifier and T096 FastPass. Verdict: not yet releasable. Re-run 2026-09-30 on build 26300: automated part unchanged and passing, 483/483 in each configuration, 0 warnings, CRT report empty; manual rows, T095 and T096 still open.)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies; start immediately.
- **Foundational (Phase 2)**: Depends on Setup, and blocks every user story. T027 (the
  rendering spike) must finish before T061 (the address bar).
- **US1 (Phase 3)**: Depends on Foundational.
- **US2 (Phase 4)**: Depends on Foundational. It uses US1's `ThemeManager`,
  `BackdropManager` and `SurfacePainter` (T034 to T036) to apply changes, so it starts
  after T037.
- **US3 (Phase 5)**: Depends on Foundational. It reads `EffectiveAppearance` for colors,
  so it needs T034; before that it can use a stub `Resolve`. It can run in parallel with
  US2.
- **US4 (Phase 6)**: Depends on US3 (it needs the file list, selection, `ShellLocation`
  and `ShellNavigator`).
- **US5 (Phase 7)**: Depends on US2 (the picker UIA) and US3 (the file list UIA).
  T080 to T082 can start after US2.
- **US6 (Phase 8)**: Depends on US3. T088 also depends on US4 (`FileOperationService`
  shutdown).
- **Polish (Phase 9)**: Depends on the stories being released.

### User Story Dependencies

Each story can start once everything it lists is complete:

| Story | Needs |
|-------|-------|
| US1 (P1) | Foundational |
| US2 (P1) | Foundational, US1 |
| US3 (P2) | Foundational, US1's `ThemeManager` (T034) |
| US4 (P2) | US3 |
| US5 (P2) | US2, US3 |
| US6 (P3) | US3; T088 also needs US4 |
| Polish | All stories being released |

### Within Each User Story

- Test tasks come before the implementation tasks they validate, and must fail first.
- Entities and pure logic (history, sort, contrast, settings) come before services
  (enumerator, file operations, theme).
- Services come before UI components (file view, address bar, picker).
- UI components come before `MainWindow` wiring.
- Each story ends with its manual checklist and a checkpoint validation.

### Parallel Opportunities

- **Setup**: T007 to T010.
- **Foundational**: T011, T013, T015, T016, T017, T019, T020, T026 and T028 (all
  different files). T012 follows T016, because the messages use the shared shell types.
  T014 → T018 → T021 → T022 → T023 → T024 → T025 are sequential.
- **US1**: T031, T032, T033 and T038 in parallel, then T034 → T035 → T036 → T037 → T039.
- **US2**: T041 and T042 in parallel, then T043. T044 → T045 → T046 → T047.
- **US3**: T049 to T056 in parallel (tests and models), then T057 → T058 → T059 → T060 →
  T061 → T062 → T063 → T064. T065 can run at any time in the phase.
- **US4**: T067, T069 and T070 in parallel, then T068 → T071 → T072.
- **US5**: T074 and T075 in parallel, then T076 → T077 → T078 → T079. T080 to T082 are
  independent of the UIA work.
- **Evidence tasks run last in their phase and are never parallel**: T030 (Phase 1 exit),
  T040 (US1), T048 (US2), T066 (US3), T073 (US4), T083 (US5), T089 (US6), T099 (release).
- **Across stories**: once US1 is done, US2 and US3 can be staffed in parallel.

---

## Parallel Example: User Story 3

```text
# Tests first (all different files):
Task: "T049 [US3] NavigationHistory tests in tests/unit/NavigationHistoryTests.cpp"
Task: "T050 [US3] Sort/selection tests in tests/unit/SortModelTests.cpp"
Task: "T051 [US3] Generation guard tests in tests/unit/GenerationGuardTests.cpp"
Task: "T052 [US3] Enumerator integration tests in tests/integration/DirectoryEnumeratorTests.cpp"

# Then the models in parallel:
Task: "T053 [US3] ShellLocation in src/shell/ShellLocation.cpp"
Task: "T054 [US3] NavigationHistory in src/ui/NavigationHistory.cpp"
Task: "T055 [US3] SortModel/SelectionModel in src/ui/SortModel.cpp, src/ui/SelectionModel.cpp"
Task: "T056 [US3] FileItem formatting in src/ui/FileItemFormat.cpp"

# Independent at any time:
Task: "T065 [US3] IExplorerBrowser spike → spike-explorerbrowser.md"
```

## Parallel Example: User Story 1

```text
Task: "T031 [US1] Theme resolution tests in tests/unit/ThemeResolveTests.cpp"
Task: "T032 [US1] Contrast tests in tests/unit/ContrastTests.cpp"
Task: "T033 [US1] Contrast utilities in src/appearance/Contrast.cpp"
Task: "T038 [US1] Status bar in src/ui/StatusBar.cpp"
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2, both P1)

1. Complete Phase 1 (Setup) and Phase 2 (Foundational), and pass the constitution phase 1
   exit.
2. Complete Phase 3 (US1). **STOP and VALIDATE** with V-1a to V-1e: a translucent native
   window with honest fallbacks.
3. Complete Phase 4 (US2). **STOP and VALIDATE** with V-2a to V-2h. This is the MVP: the
   product's identity (a translucent window plus the title-bar color picker), as the
   constitution's "transparent UI and picker before advanced file management" rule
   requires.

### Incremental Delivery

1. MVP (US1 + US2): demo.
2. Add US3: a browsable Explorer, then validate V-3.
3. Add US4: safe file management, then validate V-4.
4. Add US5: accessibility, keyboard and DPI, then validate V-5.
5. Add US6: responsiveness at scale, then validate V-6.
6. Polish: optional FR-021 aids and the release gates (T093 to T099).

### Parallel Team Strategy

1. The whole team does Setup and Foundational.
2. Developer A: US1 → US2 → US5 (appearance and accessibility track).
3. Developer B: US3 → US4 → US6 (Shell and file track).
4. They converge on Polish and the validation report.

---

## Notes

- **[P]** tasks touch different files and have no dependency on unfinished tasks.
- **[USx]** labels map to the spec's user stories for traceability. Section 8 of the spec
  maps the stories back to the constitution's principles.
- Never add `FOF_NOCONFIRMATION`, undocumented DWM attributes or registry theme keys.
  Those would violate constitution Principles IV, VII and IX.
- Commit after each task or logical group, and stop at every checkpoint to validate the
  story on its own.
- Any deviation found during implementation must be recorded in plan.md Complexity
  Tracking (constitution Governance).
