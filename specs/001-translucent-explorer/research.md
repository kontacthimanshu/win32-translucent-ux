# Phase 0 Research: Translucent Explorer

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-09-28

Each entry records a decision, the reason for it, and the alternatives that were rejected.
Every open item from the plan's Technical Context is resolved here. Items marked **Spike**
are decided now but must be confirmed by a short experiment in the delivery phase named.
If the experiment fails, the fallback is already chosen.

---

## R-01 Backdrop API and OS capability detection

**Decision**: Apply backdrops with `DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, …)`:

| Mode | Value |
|------|-------|
| Mica | `DWMSBT_MAINWINDOW` |
| Acrylic | `DWMSBT_TRANSIENTWINDOW` |
| Solid | `DWMSBT_NONE`, plus an opaque fill drawn by the app |

Detect support by capability probing, not by version checks alone:

1. Call `DwmSetWindowAttribute`. If it returns a failure `HRESULT` (for example
   `E_INVALIDARG` on builds earlier than 22621), mark the backdrop unsupported.
2. Also call `VerifyVersionInfoW` against build 22621, only so the UI can report why the
   mode is unavailable.

The app manifest declares the Windows 10/11 `supportedOS` GUID, so version APIs return
true values.

**Rationale**: `DWMWA_SYSTEMBACKDROP_TYPE` is the documented system backdrop API that
constitution Principle II names for build 22621 and later. Probing the real `HRESULT`
catches every case where the call cannot take effect, including policy and remote
sessions.

**Alternatives considered**:

- `DWMWA_MICA_EFFECT` (1029) on builds 22000–22620. Rejected: undocumented, which
  Principle IV and TC-010 forbid. Those builds fall back to Solid.
- `SetWindowCompositionAttribute` (acrylic blur). Rejected: undocumented.

---

## R-02 Per-pixel translucent client rendering

**Decision**:

1. Extend the DWM frame over the whole client area with `DwmExtendFrameIntoClientArea`
   and margins `{-1,-1,-1,-1}`.
2. Render all custom content with Direct2D into a DXGI flip-model swap chain created by
   `IDXGIFactory2::CreateSwapChainForComposition` with `DXGI_ALPHA_MODE_PREMULTIPLIED`.
3. Present that swap chain through a DirectComposition visual bound to the main HWND with
   `IDCompositionDevice::CreateTargetForHwnd(hwnd, topmost = FALSE, …)`.
4. The window's GDI redirection surface is filled with `BLACK_BRUSH` on `WM_PAINT`. Black
   is zero-alpha inside an extended frame, so the backdrop shows through. Native child
   controls (the address `EDIT`) still paint normally into the redirection surface, which
   sits above the non-topmost DirectComposition visual.

**Rationale**: Only a premultiplied-alpha composition swap chain lets Direct2D draw
partly transparent pixels, such as tint overlays and anti-aliased text, over a DWM system
backdrop. `ID2D1HwndRenderTarget` and GDI cannot produce correct per-pixel alpha.
DirectComposition, Direct3D 11 and DXGI are native, documented Windows APIs, not
application frameworks, so Principle I allows them. The plan's Complexity Tracking table
records why the stack goes beyond the constitution's preferred-technology list.

**Spike (Phase 1)**: Confirm these things in one prototype window:

- The DWM caption buttons stay visible where the visual leaves alpha 0.
- A child `EDIT` paints above the visual.
- Text renders correctly in both light and dark themes.
- The native caption buttons are still exposed to UI Automation (see R-09).
- Whether the address `EDIT` can be truly translucent while editing (see below).

**Fallback**: If the child `EDIT` cannot render in this setup, host the edit control in a
borderless owned popup placed over the address bar while editing.

**Spike result (T027, [spike-rendering.md](./spike-rendering.md))**: the caption buttons stay
visible and clickable; Direct2D text is correct in light and dark mode; a child `EDIT` is drawn
above the visual only when the main window has `WS_CLIPCHILDREN`, and even then a plain `EDIT`'s
black GDI text is transparent in the extended frame. The layered, colour-keyed edit (approach 1
below) works, with the key set to the surface colour, so approach 1 is used.

**Address box while editing (constitution Principle II, NON-NEGOTIABLE)**: The address bar
is part of the "navigation area", which must be translucent. Two approaches, in order of
preference:

1. **Translucent edit**: make the child `EDIT` a layered child window
   (`WS_EX_LAYERED`, supported for child windows since Windows 8) with
   `SetLayeredWindowAttributes(LWA_COLORKEY)`. Its background brush is the key color, so
   the background is transparent and the text is opaque. The spike must confirm three
   things: the backdrop and tint show through, ClearType text has no key-color fringes
   (use grayscale anti-aliasing if needed), and the caret and selection render correctly.
   If all three hold, use this approach.
2. **Tinted opaque edit**: if approach 1 fails, handle `WM_CTLCOLOREDIT` and return a
   solid brush in the **typical surface color** from R-05
   (`Composite(Composite(base, base, surfaceAlpha), tint, tintAlpha)`). The field then
   blends with the surface. It is opaque only while the user is typing and returns to the
   translucent display mode when editing ends. This interpretation — that an active
   text-entry control is not one of the "surfaces" Principle II names — is recorded in
   plan.md Complexity Tracking and needs explicit sign-off before release by the project
   owner (the repository maintainer), recorded in the "Sign-offs" section of
   `validation-report.md`.

**Alternatives considered**:

- `WS_EX_NOREDIRECTIONBITMAP` with DirectComposition only. Rejected: child HWNDs then
  cannot render.
- `WS_EX_LAYERED` + `UpdateLayeredWindow`. Rejected: disables system backdrops and is
  CPU-bound.
- `ID2D1HwndRenderTarget`. Rejected: no premultiplied alpha to the compositor.

---

## R-03 Custom frame, title-bar picker placement and hit testing

**Decision**: Follow Microsoft's documented "Custom Window Frame Using DWM" pattern.

- **Frame**: `WM_NCCALCSIZE` (`wParam == TRUE`) keeps the left, right and bottom resize
  borders and removes the standard caption. The DWM keeps drawing the native caption
  buttons (Minimize, Maximize/Restore, Close) in the extended frame.
- **Caption height**:

  ```text
  GetSystemMetricsForDpi(SM_CYCAPTION, dpi)
    + GetSystemMetricsForDpi(SM_CYFRAME, dpi)
    + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
  ```

- **Picker position**: from `DwmGetWindowAttribute(DWMWA_CAPTION_BUTTON_BOUNDS)`:
  - `picker.right = captionButtons.left - 8 DIP`
  - width 40 DIP, height = caption-button height, vertically aligned with the buttons.
- **`WM_NCHITTEST` order**:
  1. `DwmDefWindowProc`. If it handles the message, return its result. This covers
     `HTMINBUTTON`, `HTMAXBUTTON` (Snap Layouts) and `HTCLOSE`.
  2. Top resize band → `HTTOP`, `HTTOPLEFT` or `HTTOPRIGHT`. This band is not used when
     maximized.
  3. Picker rectangle → `HTCLIENT`.
  4. Rest of the caption strip → `HTCAPTION`.
  5. Everything else → `HTCLIENT`.
- **Maximized windows** (found in Phase 1 validation): `WM_NCCALCSIZE` must not push the
  client area down when maximized. DWM only hit-tests its caption buttons when the client
  area starts at the window's top edge. The top rows are then above the screen, and the
  layout offsets content by `contentTopPx` (frame + padding).
- **Recompute layout on**: `WM_DPICHANGED`, `WM_SIZE`, `WM_DWMCOMPOSITIONCHANGED` and
  `WM_SETTINGCHANGE`.
- **Narrow windows**: `WM_GETMINMAXINFO` sets the minimum width to
  `captionButtons.width + picker + margins + 160 DIP`. The picker and caption buttons are
  never overlaid.

**Rationale**: Principle III requires `DwmDefWindowProc` to hit-test first. Using the
native caption buttons keeps Snap Layouts, the system menu (`HTCAPTION` right-click and
`Alt+Space`) and accessibility of those buttons working for free.

**Alternatives considered**:

- Drawing custom caption buttons. Rejected: loses Snap Layouts unless reimplemented, and
  the constitution prefers the native buttons.

---

## R-04 Tint and opacity model (Principle VII, FR-006, FR-022)

**Decision**: Keep four independent settings. Tint and opacity are implemented
separately, as constitution Principle VII requires where the rendering technology
supports it (Direct2D composition does).

- **`backdropMode`**: Acrylic, Mica or Solid.
- **`tintColor`**: sRGB, or `"accent"` to follow the Windows accent color.
- **`tintOpacity`**: 0.00–0.80, in steps of 0.05. How strongly the tint color shows.
- **`surfaceOpacity`**: 0.00–0.90, in steps of 0.05. How opaque the app's surfaces are
  over the system backdrop. This is the independent opacity setting.

The app composes each surface in this order, from back to front:

1. The DWM system backdrop (Mica or Acrylic), which the app does not control.
2. A **surface layer**: the theme base color (`#202020` dark, `#F3F3F3` light) at
   `surfaceOpacity`. At 0.00 the backdrop shows fully; at 0.90 the surface is nearly
   opaque.
3. A **tint layer**: `tintColor` at `tintOpacity`.
4. Content: text, icons, selection and focus, always fully opaque.

How the settings are presented and applied:

- The UI labels the controls **"Surface opacity"** and **"Tint strength"**. Both describe
  the app's own layers. Neither claims to control the opacity of the system material
  (FR-022).
- `DWMWA_CAPTION_COLOR` and `DWMWA_BORDER_COLOR` are set only in Solid mode, to match the
  solid color.
- In Solid mode, `tintColor` is the opaque background. Both sliders are disabled, with
  the explanation "Solid mode is fully opaque". Their stored values are kept.
- The whole window, including text, is never made translucent. A true window-opacity
  mode (`WS_EX_LAYERED`) is out of scope, per the spec.

**Rationale**: The DWM does not expose opacity control for system backdrops, so app-owned
layers are the only honest, controllable way to vary opacity. Keeping the surface layer
separate from the tint layer makes opacity independent of tint color, as Principle VII
requires. The caps (0.90 surface, 0.80 tint) keep the backdrop visible.

**Alternatives considered**:

- A layered-window alpha. Rejected: breaks backdrops and makes the whole window
  translucent, including text.
- A single "tint strength" that doubles as opacity. Rejected: opacity would then depend
  on the tint, which conflicts with Principle VII.

---

## R-05 Legibility guard and system preferences

**Decision**: Resolve the effective appearance on every change. `ThemeManager` holds the
priority rules below. The first rule that matches decides the mode.

| Priority | Condition | Effective appearance |
|----------|-----------|----------------------|
| 1 | High contrast on (`SystemParametersInfoW(SPI_GETHIGHCONTRAST)`, `HCF_HIGHCONTRASTON`) | Solid, using system colors (`GetSysColor(COLOR_WINDOW / COLOR_WINDOWTEXT / COLOR_HIGHLIGHT / COLOR_HOTLIGHT)`); tint ignored |
| 2 | Transparency effects off (`winrt::Windows::UI::ViewManagement::UISettings::AdvancedEffectsEnabled() == false`) | Solid |
| 3 | Backdrop unsupported (R-01) | Solid, with reason reported |

**Legibility guard**: the app cannot read the pixels the DWM composites behind it, so it
checks text contrast against a *range* of possible backdrops for the applied mode, not a
single guess. The base color is the theme base: `#202020` in dark mode, `#F3F3F3` in light
mode.

| Applied mode | Backdrop extremes checked | Why |
|--------------|---------------------------|-----|
| Mica | `base`, and `Composite(base, oppositeExtreme, 0.25)` | Mica is heavily blurred and pulled toward the theme base, but a bright or dark wallpaper still shifts it somewhat |
| Acrylic | `#000000` and `#FFFFFF` | Acrylic shows the content behind the window, which can be any color |
| Solid | `base` only (the backdrop is fully covered) | — |

`oppositeExtreme` is `#FFFFFF` in dark mode and `#000000` in light mode.

For each extreme, the composite behind text is built in the same order as the rendering
(R-04), using `Composite(under, over, alpha)`:

```text
composite = Composite(Composite(backdropExtreme, base, surfaceAlpha), tint, tintAlpha)
```

Then:

1. Choose the text color (white-ish or black-ish) whose *minimum* contrast across all the
   extremes is highest.
2. If that minimum is at least 4.5:1, no further action is needed.
3. Otherwise, find the smallest **text scrim alpha**, in steps of 0.05, at which the
   minimum contrast reaches 4.5:1 (found in T033: if neither color passes at the user's
   opacity, use the color that needs the smaller scrim, so dark text is never kept over a
   dark base that no scrim can fix), using the same formula with the scrim alpha in place
   of `surfaceAlpha`. The scrim alpha is a **floor for the surface layer inside text
   areas only** (file rows, address text, navigation entries, status text, caption
   title). It is not an extra layer. Inside those areas the surface layer is drawn at
   `max(surfaceAlpha, textScrimAlpha)` *instead of* `surfaceAlpha`, and the layer order
   stays backdrop → surface layer → tint layer → text. So what is drawn is exactly what
   was checked. The value is never lower than the user's `surfaceOpacity`, and at 1.0 the
   surface is fully opaque, which always passes: the text candidates are pure white and
   pure black, and for any composite color one of them reaches at least 4.58:1. (Fluent's
   near-black `#1B1B1B` was used at first; T042 found that Navy or Slate at 80% tint over
   the light base then left neither color at 4.5:1.)
4. The user's `surfaceOpacity` setting is not changed. The floor applies only inside text
   areas where it is needed, so the rest of the surface stays as translucent as the user
   chose.

Selection and focus colors come from the accent color. They are checked the same way
against the same extremes, with 3:1 as the target for non-text indicators.

**Text on selected rows** is checked separately. A selected row adds the selection fill
between the tint layer and the text, so its composite for each backdrop extreme is:

```text
selectedComposite = Composite(textAreaComposite, selection, selectionAlpha)
```

Here `textAreaComposite` is the text-area composite above, using the floor opacity. The
guard picks the values for selected rows in this order:

1. Start with `selectionAlpha = 0.40` and the normal text color. If the minimum contrast
   across the extremes is at least 4.5:1, use them.
2. Otherwise, try the other text color (white-ish or black-ish) for selected rows only.
   If it reaches 4.5:1, use it as `selectedTextColor`.
3. Otherwise, raise `selectionAlpha` in steps of 0.05, up to 1.0, and repeat steps 1 and 2
   at each value. At 1.0 the fill is opaque, so one of the two text colors always reaches
   4.5:1 against it.

The selection fill must also still reach 3:1 against the unselected surface, so the
highlight stays visible (Principle II).

The **typical surface color**, used where one opaque color must stand in for the surface
(for example the tinted address-edit fallback in R-02), uses `base` as the backdrop:
`Composite(Composite(base, base, surfaceAlpha), tint, tintAlpha)`.

Other system signals:

- **Dark/light mode**: `UISettings::GetColorValue(UIColorType::Foreground)` luminance,
  which is Microsoft's documented Win32 approach. Applied with
  `DWMWA_USE_IMMERSIVE_DARK_MODE`.
- **Change notifications**: `WM_SETTINGCHANGE` with `lParam == L"ImmersiveColorSet"`, and
  `UISettings::ColorValuesChanged`.
- **Text scale**: `UISettings::TextScaleFactor` and `TextScaleFactorChanged`.
- **Reduced motion**: `SPI_GETCLIENTAREAANIMATION`. The app has no animations when it is
  off.

**Rationale**: Principles II and X require legibility and respect for OS preferences.
Using C++/WinRT `UISettings` from Win32 is the documented approach. It is a WinRT API, not
a UI framework, so no WinUI dependency is introduced.

**Alternatives considered**:

- Reading `HKCU\...\Themes\Personalize\AppsUseLightTheme`. Rejected: an undocumented
  registry contract.

---

## R-06 File view: Shell-hosted vs custom Direct2D (TC-005, TC-007)

**Decision**: Build a custom Direct2D file view as the primary path.

A Phase 3 **Spike** will evaluate `IExplorerBrowser` hosted in the client area:

- Measure whether its view can be translucent.
- Record the result in `specs/001-translucent-explorer/spike-explorerbrowser.md`.

The expected outcome is that it is opaque. `IExplorerBrowser` hosts DefView, whose list
paints an opaque background, which fails FR-003. The custom view then remains, as the
constitution permits.

The custom view uses these interfaces:

| Need | Interface |
|------|-----------|
| Enumeration | `IShellItem::BindToHandler(BHID_EnumItems, IID_PPV_ARGS(&IEnumShellItems))` |
| Columns | `IShellItem2::GetString(PKEY_ItemTypeText)`, `GetUInt64(PKEY_Size)`, `GetFileTime(PKEY_DateModified)` |
| Attributes | `SFGAO_FOLDER \| SFGAO_STREAM \| SFGAO_HIDDEN` |
| Icons | `IShellItemImageFactory::GetImage(size, SIIGBF_ICONONLY \| SIIGBF_BIGGERSIZEOK)` → `HBITMAP` → WIC (`CreateBitmapFromHBITMAP`) → `ID2D1Bitmap1`, cached per system image-list index where available |

**Rationale**: Only a custom view can meet full-surface translucency (Principle II,
FR-003). The spike still meets the constitution's "SHOULD be evaluated" requirement for
`IExplorerBrowser`.

**Alternatives considered**:

- `SysListView32` with custom draw. Rejected: GDI-opaque, same problem.
- `IShellView` hosted directly. Rejected: same DefView limitation.

---

## R-07 Threading and COM apartments (Principle VIII, TC-008)

**Decision**:

| Thread | Apartment | Work | Notes |
|--------|-----------|------|-------|
| UI thread | STA (`OleInitialize`, needed for clipboard and drag-drop) | Owns all HWNDs, Direct2D/DirectComposition objects and UIA providers | — |
| Enumeration worker | STA (`CoInitializeEx(COINIT_APARTMENTTHREADED)`) | Runs one `DirectoryRequest` at a time | `std::jthread`; cooperative cancellation via `std::stop_token`, checked between `IEnumShellItems::Next` batches of 64 |
| Icon worker | STA | Serves a LIFO queue that favors visible rows | Requests are tagged with a generation |
| File-operation worker | STA (required by `IFileOperation`) | Runs each `FileOperationRequest` | Has its own message pump; `IFileOperation::SetOwnerWindow(mainHwnd)` keeps Shell progress and conflict UI modal to the main window without blocking its message loop |

**Stale-result guard**:

- Each navigation increments `currentGeneration`.
- Workers post `WM_APP_*` messages carrying an owning heap payload that includes the
  generation.
- The UI thread discards and frees any payload where `payload.generation !=
  currentGeneration`.
- On `WM_DESTROY`, all workers get `request_stop()` and are joined before the window
  finishes destroying. Payloads that are still queued are drained and freed.

**Rationale**: This meets FR-018 and FR-019 and the constitution's STA rule for
`IFileOperation`. Posting messages keeps every UI mutation on the UI thread.

**Alternatives considered**:

- An MTA thread pool. Rejected: many Shell folders are STA-only.
- `std::async`. Rejected: no control over the apartment.

---

## R-08 File operations, safety and clipboard (FR-012–014, Principle IX)

**Decision**:

- Every operation goes through `IFileOperation` on the file-operation worker:
  - Copy and move: `CopyItems` and `MoveItems`.
  - Rename: `RenameItem`.
  - Delete: `DeleteItems`.
- **Flags**:
  - Always set `FOFX_ADDUNDORECORD`.
  - Set `FOF_ALLOWUNDO` for delete (Recycle Bin).
  - Shift+Delete removes `FOF_ALLOWUNDO`, which triggers the Shell's
    permanent-delete confirmation.
  - Never set `FOF_NOCONFIRMATION`, `FOF_NOERRORUI` or `FOF_RENAMEONCOLLISION`. The Shell
    shows its own conflict and confirmation UI.
- **Result reporting**:
  - `IFileOperationProgressSink::PostCopyItem`, `PostMoveItem`, `PostRenameItem` and
    `PostDeleteItem` collect per-item `HRESULT`s.
  - `GetAnyOperationsAborted` distinguishes a cancellation.
  - Results are posted to the UI, which shows a status-bar message and, on failure, a
    `TaskDialogIndirect` listing the failed items.
- **Rename**: The list updates the item name only after `PostRenameItem` reports success
  (spec US4-3).
- **Clipboard (Ctrl+C / Ctrl+X / Ctrl+V)**:
  - Copy/cut: `SHCreateDataObject` from the selection's PIDLs, then `OleSetClipboard`.
    Cut adds `CFSTR_PREFERREDDROPEFFECT = DROPEFFECT_MOVE`.
  - Paste: `OleGetClipboard`, then `IFileOperation::CopyItems` or `MoveItems` with the
    `IDataObject`.
- **Context menus**:
  - `IShellItemArray::BindToHandler(BHID_SFUIObject, IID_IContextMenu)`.
  - `QueryContextMenu(CMF_NORMAL | CMF_CANRENAME | (shift ? CMF_EXTENDEDVERBS : 0))`.
  - `TrackPopupMenuEx`, then `InvokeCommand` (`CMINVOKECOMMANDINFOEX` with `ptInvoke` and
    `fMask |= CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE`).
  - `WM_INITMENUPOPUP`, `WM_DRAWITEM`, `WM_MEASUREITEM` and `WM_MENUCHAR` are forwarded to
    `IContextMenu2` and `IContextMenu3`.
  - The "rename" verb is intercepted and routed to inline rename.
- **Open**: `ShellExecuteExW` with `SEE_MASK_INVOKEIDLIST` and `lpIDList`. On failure
  (for example `SE_ERR_NOASSOC`), a clear message is shown and the window stays responsive.
- **Long paths**: The manifest sets `longPathAware`. Operations that the Shell API
  rejects because of path length surface the `HRESULT` text.

**Rationale**: This meets Principle IX: no silent overwrite and no silent permanent
delete. Relying on the Shell's own UI gives Windows-consistent behavior.

**Alternatives considered**:

- `CopyFileExW` / `SHFileOperationW`. Rejected: the constitution mandates
  `IFileOperation`, and `SHFileOperationW` is legacy.

---

## R-09 Accessibility: UI Automation for custom content (FR-015, FR-016)

**Decision**: The main window answers `WM_GETOBJECT` (`UiaRootObjectId`) with
`UiaReturnRawElementProvider` for a fragment root. The fragment tree has these elements:

| Element | UIA control type | Patterns / behavior |
|---------|------------------|---------------------|
| `ColorPickerButton` | Button | `IExpandCollapseProvider`; name "Appearance and color" |
| `AddressBar` | Edit, when in display mode | — |
| `NavigationPane` | List | — |
| `FileList` | DataGrid | `ISelectionProvider`, `IGridProvider`, `ITableProvider`, `IScrollProvider` |
| `FileItem` | DataItem | `ISelectionItemProvider`, `IInvokeProvider`, `IScrollItemProvider`, `IGridItemProvider` |
| `StatusBar` | StatusBar | — |

- `UiaRaiseAutomationEvent` is raised for focus and selection changes.
- `UiaRaiseStructureChangedEvent` is raised on folder changes.
- The child `EDIT` exposes UIA natively.
- **Native caption buttons: verified by the T027 spike** — UI Automation finds Minimize, Maximize and Close with the Invoke pattern, so no extra providers are needed. Original note: With the standard caption removed by
  `WM_NCCALCSIZE`, the system's title-bar accessibility proxy may no longer report the
  DWM-drawn Minimize, Maximize/Restore and Close buttons. The Phase 1 rendering spike
  checks this with Accessibility Insights. If they are missing, the fragment root adds
  Button providers for them. Their `BoundingRectangle` comes from
  `DWMWA_CAPTION_BUTTON_BOUNDS`, and `IInvokeProvider::Invoke` posts
  `WM_SYSCOMMAND` with `SC_MINIMIZE`, `SC_MAXIMIZE`/`SC_RESTORE` or `SC_CLOSE`.
- The picker popup is a `DIALOGEX` of standard controls (R-10), so it is accessible by
  default.

**Rationale**: Principle X requires custom-rendered items to expose UIA. Most of the tree
is only needed because the file view is custom.

**Alternatives considered**:

- `IAccessible` (MSAA). Rejected: legacy, and it lacks grid and table semantics.

---

## R-10 Color picker popup UI

**Decision**: When the title-bar button is activated (click, Enter or Space, or `Alt+Shift+C`), the app opens a `DIALOGEX`-based owned popup with these features:

- **Placement**: anchored below the picker button and clamped to
  `MonitorFromWindow` → `GetMonitorInfoW(rcWork)` (edge case "mixed-DPI").
- **Dismissal**: closes on Escape, on loss of activation, or by toggling the picker button.
  No click passes through to the file list (edge case "dismissed without selection").
- **Keyboard**: Tab and arrow navigation via `IsDialogMessageW`.

The popup contains:

- **Mode**: three auto radio buttons — Acrylic, Mica, Solid. Unsupported modes are
  disabled, and a static text gives the reason.
- **Presets**: 12 preset swatches as `BS_OWNERDRAW` buttons, each with a window text set to
  the color name for UIA. They are arranged in a grid with arrow-key navigation.
- **Accent**: an "Accent color" swatch (follows Windows).
- **Custom…**: opens `ChooseColorW` (`CC_FULLOPEN | CC_RGBINIT`), with the 16 custom colors
  persisted.
- **Surface opacity**: a trackbar (`TRACKBAR_CLASSW`) from 0 to 90 in steps of 5.
- **Tint strength**: a trackbar from 0 to 80 in steps of 5.
- Both trackbars are disabled in Solid mode and in high contrast, with the explanation
  text (FR-022).
- **Preview**: an owner-drawn swatch showing the tint over the current theme base.
- **Reset to defaults**: a push button.

Changes apply live, and are debounced to 100 ms before they are persisted.

**Rationale**: Standard controls give keyboard, high-contrast and UIA support without
custom work. The popup is transient, and it is not one of the surfaces that FR-003 requires
to be translucent.

**Alternatives considered**:

- A fully custom Direct2D popup. Rejected: duplicates the UIA work for little benefit.

---

## R-11 Deferred product decisions from spec Assumptions

**Decision**:

**Defaults, restored by Reset**:

- `backdropMode = Mica`. If Mica is unsupported, the effective mode is Solid; the stored
  preference stays Mica so it applies when support appears.
- `tintColor = "accent"`
- `tintOpacity = 0.20`
- `surfaceOpacity = 0.00` (the backdrop shows fully, as in Windows 11 Explorer)

**Preset palette** (from the Windows accent palette):

| Color | Hex |
|-------|-----|
| Blue | `#0078D4` |
| Navy | `#0063B1` |
| Teal | `#00B7C3` |
| Sea green | `#00B294` |
| Green | `#107C10` |
| Gold | `#FFB900` |
| Orange | `#F7630C` |
| Red | `#E81123` |
| Rose | `#EA005E` |
| Purple | `#8764B8` |
| Slate | `#515C6B` |
| Graphite | `#4C4A48` |

**Opacity ranges**: surface opacity 0–90% and tint strength 0–80%, both in steps of 5%;
both disabled in Solid mode.

**Initial launch directory**:

1. A path given on the command line, if valid.
2. Otherwise "This PC" (`FOLDERID_ComputerFolder`).
3. If that fails, `FOLDERID_Profile`.

The last-visited location is not persisted in this release.

**Rationale**: These defaults match Windows 11 Explorer conventions and keep the first run
legible. The spec explicitly leaves these to the plan.

---

## R-12 Settings persistence

**Decision**: Store settings as JSON at
`%LOCALAPPDATA%\TranslucentExplorer\settings.json`. The folder is found with
`SHGetKnownFolderPath(FOLDERID_LocalAppData)`. The schema is
[contracts/settings.schema.json](./contracts/settings.schema.json).

- The format is UTF-8 without a BOM and includes `schemaVersion: 1`.
- **Writes**: write to `settings.json.tmp`, call `FlushFileBuffers`, then
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
- **Reads**:
  - Parse with nlohmann/json.
  - Each field is validated independently. An invalid field falls back to its default
    while the valid fields are kept.
  - An unparsable file is renamed to `settings.corrupt-<timestamp>.json` and replaced with
    defaults (edge case "Corrupted settings file").

**Rationale**: The constitution prefers "native C++ with JSON". Atomic replacement means a
crash cannot corrupt the file. Validating each field meets FR-007.

**Alternatives considered**:

- The registry (`HKCU`). Rejected: less inspectable and not the constitution's preferred
  format.
- `Windows.Data.Json`. Rejected: needs WinRT string plumbing throughout, and is harder to
  unit-test.

---

## R-13 Build, dependencies and toolchain

**Decision**:

- **Build system**: CMake 3.28+ with `CMakePresets.json` and a Visual Studio generator for
  the x64 architecture. The `x64-debug` and `x64-release` presets use "Visual Studio 18
  2026" (the development machine's toolset); `vs2022-x64-debug` and `vs2022-x64-release`
  use "Visual Studio 17 2022" for machines that only have Visual Studio 2022. Both are
  allowed by the constitution ("Visual Studio 2022 or later").
- **Dependencies**: managed by vcpkg manifest mode (`vcpkg.json`, baseline pinned):
  - `wil` — RAII for handles and COM (`wil::com_ptr`, `wil::unique_hwnd`, …)
  - `nlohmann-json`
  - `gtest`
- **Windows SDK**: 10.0.22621.0 or later. It supplies the DWM, Shell, Direct2D,
  DirectComposition, UIA and C++/WinRT headers (`cppwinrt`).
- **Compiler options for app targets**: `/std:c++20 /W4 /WX /permissive- /utf-8 /EHsc
  /Zc:__cplusplus /guard:cf /sdl`. Release adds `/O2 /GL` with `/LTCG`.
- **Third-party headers**: included with `/external:anglebrackets /external:W0`, so `/WX`
  applies only to application code, as "new application-code warnings" requires.
- **Manifest**:
  - `PerMonitorV2` DPI awareness
  - `longPathAware`
  - Common Controls v6
  - `supportedOS` Windows 10/11
  - `activeCodePage` UTF-8, for any narrow-string use
- **Link libraries**: `dwmapi`, `d2d1`, `dwrite`, `d3d11`, `dxgi`, `dcomp`,
  `windowscodecs`, `uiautomationcore`, `shlwapi`, `comctl32`, `propsys`, `runtimeobject`.

**Rationale**: This meets TC-001 and TC-011. WIL is Microsoft's own RAII library for
exactly these handle and COM types (Principle I, VIII).

**Alternatives considered**:

- A hand-written `.vcxproj`. Rejected: the constitution prefers CMake.
- CMake `FetchContent`. Rejected: vcpkg gives cached, versioned binaries.

---

## R-14 Testing strategy and quality gates

**Decision**:

- **Unit tests** (GoogleTest, run by CTest) cover pure logic, with no windows:
  - settings parse, validate and serialize
  - `NavigationHistory`
  - generation guard
  - sort comparators (`StrCmpLogicalW` for names; folders first)
  - caption and picker layout math at 96, 144 and 192 DPI
  - WCAG contrast calculation
  - effective-appearance resolution truth table
- **Integration tests** (GoogleTest, marked as integration, running on real temp folders
  under `%TEMP%`):
  - `DirectoryEnumerator` over 10,000 generated files
  - cancellation mid-enumeration
  - stale-generation discard
  - `FileOperationService` rename and copy on a temp tree, using a test-only
    `IFileOperation` factory that leaves the production flags unchanged. Conflict UI is
    covered manually.
- **Resource checks**:
  - The Debug CRT (`_CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF)`) runs on exit.
  - A lifecycle test creates and destroys the main window 50 times and asserts that the
    GDI object count, the USER object count and the handle count all return to baseline,
    within a tolerance of 0. `GR_GDIOBJECTS` and `GR_USEROBJECTS` are separate values, not
    combinable flags, so call `GetGuiResources` once for each. Use
    `GetProcessHandleCount` for handles.
  - Application Verifier (Basics: Handles, Heaps, Locks) runs on the manual suite before a
    release.
- **Manual suites** (checklists under `tests/manual/`):
  - title bar, caption and Snap Layouts
  - DPI (100/150/200% and monitor transitions)
  - file operations (conflicts, access denied, cancel, delete confirmation)
  - accessibility (Narrator, keyboard-only, Accessibility Insights for Windows FastPass)
  - theme and high contrast
- **Responsiveness**: A Debug-only UI-thread watchdog logs any message-handler duration
  over 50 ms with `OutputDebugStringW`. This is a diagnostic aid, not a published latency
  guarantee (spec Success Criteria note).

**Rationale**: Each spec success criterion (SC-001 to SC-011) maps to an automated or
manual check. Per-phase validation is reproducible, as the constitution's Stage Validation
section requires.

---

## R-15 Keyboard model (FR-015)

**Decision**: Use Explorer-compatible bindings, handled with an accelerator table
(`CreateAcceleratorTableW`) and list key handling.

| Action | Keys |
|--------|------|
| Back | `Alt+Left`, `Backspace` |
| Forward | `Alt+Right` |
| Up | `Alt+Up` |
| Focus address bar | `Ctrl+L`, `Alt+D`, `F4` |
| Refresh | `F5` |
| Cycle panes (title-bar picker → address bar → navigation pane → file list) | `F6` / `Shift+F6` |
| Open color picker | `Alt+Shift+C` |
| Open | `Enter` |
| Rename | `F2` |
| Delete (Recycle Bin) | `Delete` |
| Permanent delete (Shell confirmation) | `Shift+Delete` |
| Copy / Cut / Paste | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` |
| Select all | `Ctrl+A` |
| Sort by Name / Date modified / Type / Size (repeat toggles direction) | `Ctrl+Shift+1` / `2` / `3` / `4`, or click the column header |
| Context menu | `Shift+F10`, `Menu` key |
| Move in list | arrows, `Home`, `End`, `PgUp`, `PgDn` |
| Extend selection | `Shift+` navigation keys |
| Toggle selection | `Ctrl+Space` |

The focus rectangle is always drawn when the file list has keyboard focus (2 DIP, accent
color, contrast-checked).

**Rationale**: Familiar conventions (Principle V) and keyboard access for all essential
functions.
