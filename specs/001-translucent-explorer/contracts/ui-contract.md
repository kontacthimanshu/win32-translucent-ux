# Contract: User Interface, Keyboard and Accessibility

**Feature**: [spec.md](../spec.md) | **Plan**: [plan.md](../plan.md)

This is the externally visible contract of the application: what a user, a tester or an
assistive technology can rely on. The manual test suites in `tests/manual/` check against
this file.

## 1. Window layout (all sizes in DIP; scaled by the current monitor DPI)

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ [app icon] Translucent Explorer — <folder>         [🎨]  │  ─  │  ▢  │  ✕  │  ← caption strip
├──────────────────────────────────────────────────────────────────────────────┤
│ [←][→][↑][⟳]  [ address bar ...................................... ]     │  ← toolbar, 40 DIP
├──────────────┬───────────────────────────────────────────────────────────────┤
│ Navigation   │ Name ▲            Date modified     Type          Size       │
│ pane         │ 📁 Documents      2026-09-01 10:22  File folder              │
│ (This PC,    │ 📄 report.docx    2026-09-27 17:05  Word Document   24 KB    │
│  drives,     │ ...                                                          │
│  known       │                                                              │
│  folders)    │                                                              │
├──────────────┴───────────────────────────────────────────────────────────────┤
│ 1,204 items   3 selected   Mica · Surface 0% · Tint 20%                      │  ← status bar, 24 DIP
└──────────────────────────────────────────────────────────────────────────────┘
```

| Region | Rule |
|--------|------|
| Caption strip | Height = `SM_CYCAPTION + SM_CYFRAME + SM_CXPADDEDBORDER` at the current DPI. The app draws only the icon, title and picker. The DWM draws Minimize, Maximize/Restore and Close. |
| Picker button `[🎨]` | 40 × caption-button height. Right edge = Minimize left edge − 8 DIP. It shows the current tint as a filled circle (14 DIP) with a 1 DIP contrast ring. Hover and pressed states follow the caption-button styling. |
| Drag region | The whole caption strip except the picker and the caption buttons: title-bar drag, double-click to maximize/restore, right-click for the system menu. |
| Translucent surfaces | Caption strip, toolbar including the address bar, navigation pane, file list and status bar are all drawn over the backdrop: surface layer, then tint layer, then content (FR-003, research R-04). While the address field is being edited, it is a translucent layered edit if the Phase 1 spike confirms it works; otherwise it is filled with the current composite color (research R-02). |
| Title text | Truncated with an ellipsis when the window is too narrow. It never overlaps the picker or the caption buttons. |
| Minimum window size | Width = caption buttons + picker + margins + 160 DIP; height = caption + toolbar + 5 rows + status bar. |

## 2. Hit-test contract (FR-004, FR-008, TC-006)

Evaluated in this order on `WM_NCHITTEST`:

1. `DwmDefWindowProc`: if it handles the point, its result is returned (`HTMINBUTTON`,
   `HTMAXBUTTON`, `HTCLOSE`). Snap Layouts appear when hovering Maximize.
2. The top resize band (not maximized) → `HTTOP`, `HTTOPLEFT` or `HTTOPRIGHT`.
3. The picker rectangle → `HTCLIENT`.
4. The drag region → `HTCAPTION`.
5. Everywhere else → `HTCLIENT`. The side and bottom borders stay non-client, handled by
   `DefWindowProc`.

**Guarantee**: At every DPI and in every show state, no pixel of the caption-button area
returns `HTCLIENT` for the picker.

## 3. Appearance popup (FR-005, FR-006, FR-022)

The popup opens anchored under the picker button, inside the monitor work area.

| Control | Behavior |
|---------|----------|
| Mode: Acrylic / Mica / Solid (radio) | Applies immediately. An unsupported option is disabled, and the reason text reads "Requires Windows 11 build 22621 or later", "Transparency effects are turned off in Windows settings" or "High contrast is on". |
| 12 preset swatches + "Accent color" | Applies immediately. Accessible name = the color name, for example "Teal". Arrow keys move within the grid. |
| Custom… | Opens the Windows color dialog. The selection applies on OK. |
| Surface opacity (0–90%, step 5) | Applies immediately. Sets how opaque the app's surfaces are over the backdrop, independent of the tint. Disabled in Solid mode and high contrast, with the text "Solid mode is fully opaque". It is never described as the opacity of the window or of the system backdrop. |
| Tint strength (0–80%, step 5) | Applies immediately. Sets how strongly the tint color shows. Disabled in Solid mode and high contrast, with the same text. |
| Preview | Shows the tint over the current theme base. |
| Reset to defaults | Mica, accent tint, tint strength 20%, surface opacity 0%. Custom colors are kept. |
| Dismiss | Escape, clicking outside, or the picker button again. The click that dismisses the popup is not passed to the window underneath. |

The status bar always shows the **applied** mode. If the applied mode differs from the
requested mode, it adds "(fallback: <reason>)" (spec US1-3).

## 4. Keyboard contract (FR-015)

| Keys | Action | Scope |
|------|--------|-------|
| `Alt+Left`, `Backspace` | Back | Window, except while editing text |
| `Alt+Right` | Forward | Window |
| `Alt+Up` | Up | Window |
| `Ctrl+L`, `Alt+D`, `F4` | Focus the address bar and select all | Window |
| `Enter` / `Esc` (in the address bar) | Navigate / cancel the edit and restore the path | Address bar |
| `F5` | Refresh | Window |
| `F6` / `Shift+F6` | Cycle focus: picker → address bar → navigation pane → file list | Window |
| `Alt+Shift+C` | Open the appearance popup | Window |
| Arrows, `Home`, `End`, `PgUp`, `PgDn` | Move focus | File list |
| `Shift+` movement / `Ctrl+Space` / `Ctrl+A` | Extend / toggle / select all | File list |
| `Enter` | Open the focused or selected items | File list |
| `F2` | Inline rename | File list |
| `Delete` / `Shift+Delete` | Recycle / permanent delete with Shell confirmation | File list |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut / paste | File list |
| `Shift+F10`, `Menu` key | Context menu for the selection, or the folder background if nothing is selected | File list |
| `Alt+Space` | System menu | Window |
| `Alt+F4` | Close | Window |
| Clicking a column header / `Ctrl+Shift+1`, `2`, `3`, `4` | Sort by Name / Date modified / Type / Size; repeating toggles the direction | File list |

**Command line**:

| Argument | Build | Behavior |
|----------|-------|----------|
| `<folder path>` | All | Opens that folder instead of This PC, if valid (research R-11). |
| `--backdrop=acrylic\|mica\|solid` | **Debug only** | Overrides the requested mode for the session, for testing US1 before the picker exists. In Debug, if an instance is already running, the value is forwarded to it with `WM_COPYDATA` and the new process exits. Release builds ignore this argument, and every launch opens its own window. |

**Focus visibility**: The focused element always shows a 2 DIP focus indicator whose
contrast against its background is at least 3:1.

## 5. UI Automation tree (FR-016)

```text
Window "Translucent Explorer — <folder>"
├── TitleBar (native)
│   ├── Button "Appearance and color"        ExpandCollapse (Expanded while the popup is open)
│   ├── Button "Minimize" / "Maximize" / "Close"   (native, from DWM)
├── ToolBar "Navigation"
│   ├── Button "Back" / "Forward" / "Up" / "Refresh"   Invoke; IsEnabled reflects history
│   └── Edit "Address"                        Value = current path
├── List "Navigation pane"
│   └── ListItem "<location>" …               SelectionItem, Invoke
├── DataGrid "Items"                          Selection (multiple), Grid, Table, Scroll
│   ├── Header: HeaderItem "Name" | "Date modified" | "Type" | "Size"  (Invoke = sort)
│   └── DataItem "<name>" …                   SelectionItem, Invoke, ScrollItem, GridItem
│       └── (cells exposed as Text children: name, date, type, size)
└── StatusBar                                 Name = status text
```

**Events**: `AutomationFocusChanged` on focus moves; `SelectionItem_ElementSelected`,
`ElementAddedToSelection` and `ElementRemovedFromSelection`; `StructureChanged` when the
folder contents are replaced; `LiveRegionChanged` on the status bar for operation results.

## 6. Error presentation (FR-013, FR-020)

| Situation | Presentation |
|-----------|--------------|
| Invalid path in the address bar | Inline message under the address bar and a status bar message. The previous folder stays shown. |
| Access denied, drive not ready, or disconnected network location | The status bar shows the message and the list keeps the previous or fallback folder. It never crashes and the window stays responsive. |
| No file association | `TaskDialogIndirect`: "Windows can't open this file" with "Open with…", which invokes the Shell's "openas" verb. |
| File operation partly failed | `TaskDialogIndirect` listing each failed item with its system error text. The status bar shows "N of M items completed". |
| Operation cancelled | Status bar: "Operation cancelled — N items completed before cancellation". |
| Settings file corrupt | A one-time status bar notice: "Appearance settings were reset". |
| Backdrop failed at runtime | Falls back to Solid, with the status bar showing "(fallback: backdrop unavailable)". |
