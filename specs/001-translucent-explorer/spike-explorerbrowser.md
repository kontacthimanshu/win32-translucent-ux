# Spike: `IExplorerBrowser` over Mica (R-06, T065)

**Date**: 2026-09-28
**Machine**: Windows 11 Pro 10.0.26200, light app mode, transparency effects on
**Question**: Can the Shell-hosted view (`IExplorerBrowser`, which hosts DefView) composite
translucently over the system backdrop, so it could replace the custom Direct2D file view
and still meet FR-003 (full-surface translucency)?

**Answer**: No. The DefView list paints an opaque background. The custom Direct2D file view
stays, as R-06 decided. This spike meets the constitution's Principle IV requirement that
`IExplorerBrowser` SHOULD be evaluated.

## Setup

A throwaway Win32 program, set up the same way as the application's composition:

- `DwmExtendFrameIntoClientArea` with margins `{-1, -1, -1, -1}` (the whole client area is
  frame).
- `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW` (Mica).
- `WM_PAINT` fills the parent with a black GDI brush, which is transparent in the extended
  frame. `WM_ERASEBKGND` returns 1, and the window has `WS_CLIPCHILDREN`.
- `CoCreateInstance(CLSID_ExplorerBrowser)`, then `IExplorerBrowser::Initialize` with
  `FOLDERSETTINGS{FVM_DETAILS, 0}`, then `SetOptions(EBO_NOBORDER)`, then
  `BrowseToIDList(FOLDERID_Windows, SBSP_ABSOLUTE)`.
- The browser fills the client area except a 220 DIP strip on the left. The strip shows bare
  Mica for comparison.
- Per-monitor-v2 DPI aware, with `OleInitialize` on the UI thread.

There were two variants:

| Variant | Change |
|---------|--------|
| a | The default view, as above |
| b | As a, and also `IFolderView2` → `IVisualProperties::SetColor(VPCF_BACKGROUND, RGB(0,0,0))` and `SetColor(VPCF_TEXT, RGB(255,255,255))`. This is the classic "black is transparent on glass" trick. |

Each window was shown in the foreground for 2.5 s. It was then captured from the screen,
using its DWM extended frame bounds (`DWMWA_EXTENDED_FRAME_BOUNDS`), and closed with
`WM_CLOSE`. No input was injected.

## Results

| Variant | Screenshot | Bare Mica strip (RGB) | List background (RGB) |
|---------|------------|-----------------------|-----------------------|
| a | [variant-a.png](spike-explorerbrowser/variant-a.png) | 249, 241, 237 | 255, 255, 255 |
| b | [variant-b.png](spike-explorerbrowser/variant-b.png) | 249, 241, 237 | 255, 255, 255 |

Both samples come from the same row, at 85% of the window height. The strip sample is at
8% of the width, and the list sample at 70% (the empty area to the right of the names).

Observations:

1. **The list is opaque.** The Mica strip shows the tinted wallpaper (249, 241, 237). The
   list area is pure white (255, 255, 255) and does not change with the backdrop. The
   header, the list body and the scroll bars are all opaque.
2. **Item text is damaged.** In both variants the folder names render very faint and
   washed out, because DefView draws them with GDI. GDI leaves the alpha channel at 0, and
   over the extended frame the DWM treats those pixels as transparent. The names are close
   to illegible, which would also fail the legibility requirement (US1 acceptance scenario 5, R-05).
3. **`IVisualProperties` has no visible effect.** Variant b looks the same as variant a:
   the background is still white and the text is not white. The colors were set right
   after `BrowseToIDList`. DefView appears to ignore them, or to replace the view after
   the call. Setting them later, from `IExplorerBrowserEvents::OnViewCreated`, was not
   tried. Even if it worked, a black GDI background would only give "fully transparent or
   opaque", with no surface-opacity or tint control (R-04) and the same GDI-text alpha
   problem.

## Conclusion

- `IExplorerBrowser` / DefView cannot provide a translucent file view over the system
  backdrop. It fails FR-003, and its text fails the legibility requirement (US1 scenario 5, R-05) over the extended frame.
- The custom Direct2D file view (`te::FileView`, T053–T055) remains the file view. R-06's
  expected outcome is confirmed. There is no change to plan.md or research.md.
- Things the Shell view would give for free, and that the custom view must provide itself,
  are already planned in other tasks: context menus (US4, T069), drag-drop and file
  operations (US4), and UI Automation (US6).

## Spike code

The spike was a standalone program in `out/spike/explorerbrowser/`. That folder is
git-ignored and was deleted after the run. It was never part of the CMake product build or
of `te_core`, so there was nothing to remove from the default build. The essential code:

```cpp
const MARGINS glass{-1, -1, -1, -1};
DwmExtendFrameIntoClientArea(hwnd, &glass);
const DWM_SYSTEMBACKDROP_TYPE mica = DWMSBT_MAINWINDOW;
DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &mica, sizeof(mica));

CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&browser));
FOLDERSETTINGS settings{FVM_DETAILS, 0};
browser->Initialize(hwnd, &rect, &settings);
browser->SetOptions(EBO_NOBORDER);
browser->BrowseToIDList(windowsFolderPidl, SBSP_ABSOLUTE);

// Variant b only
browser->GetCurrentView(IID_PPV_ARGS(&view));             // IFolderView2
view->QueryInterface(IID_PPV_ARGS(&visual));              // IVisualProperties
visual->SetColor(VPCF_BACKGROUND, RGB(0, 0, 0));
visual->SetColor(VPCF_TEXT, RGB(255, 255, 255));

// WM_PAINT of the host: black GDI = transparent in the extended frame
FillRect(dc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
```
