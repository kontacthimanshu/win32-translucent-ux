# Data Model: Translucent Explorer

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-09-28

This file describes the entities from the spec's Key Entities section, plus four supporting
types needed by the design:

- `EffectiveAppearance`
- `RenderingCapabilities`
- `SortState`
- `SelectionModel`

Types are shown in C++ notation for clarity only. They are field-level contracts, not
finished code.

## Entity relationships

```text
AppearanceSettings ──(resolved with RenderingCapabilities)──▶ EffectiveAppearance
          ▲                                                        │
          │ persisted by ISettingsStore                            ▼ applied by IBackdropManager
WindowState ─ owns ─▶ RenderingCapabilities, caption/picker layout

NavigationHistory ─ 1..* ─▶ Location
Location ─(navigate)─▶ DirectoryRequest ─ yields 0..* ─▶ FileItem
FileItem ─ 1..* ─▶ FileOperationRequest (sources)
Location ─ 0..1 ─▶ FileOperationRequest (destination)
SortState, SelectionModel ─ apply to ─▶ current FileItem list
```

---

## AppearanceSettings (persisted)

The user's requested appearance. It is stored in `settings.json`; the schema is
[contracts/settings.schema.json](./contracts/settings.schema.json).
`schemaVersion` sits at the top level of the file, and every other field is inside the
`appearance` object.

| Field | Type | Default | Validation |
|-------|------|---------|------------|
| `schemaVersion` | `uint32` | `1` | Must be `1`. An unknown higher version is read as defaults, and the file is not overwritten until the user makes a change. |
| `backdropMode` | enum `{Acrylic, Mica, Solid}` | `Mica` | Any other value → default |
| `tintColor` | `"accent"` \| `#RRGGBB` | `"accent"` | Must match the regex `^#[0-9A-Fa-f]{6}$` or equal `"accent"` |
| `tintOpacity` | `double` | `0.20` | 0.00–0.80, rounded to the nearest 0.05; out of range → clamp |
| `surfaceOpacity` | `double` | `0.00` | 0.00–0.90, rounded to the nearest 0.05; out of range → clamp. Opacity of the app's surface layer over the backdrop (research R-04). |
| `customColors` | `array<#RRGGBB>`, length 16 | 16 × `#FFFFFF` | Invalid entries → `#FFFFFF`; the array is padded or truncated to 16 |

**Rules**:

- Backdrop mode, tint color, tint opacity and surface opacity are independent (FR-006,
  constitution Principle VII). Changing one never changes another.
- `tintOpacity` and `surfaceOpacity` are ignored while the effective mode is Solid, but
  they are kept.
- Each field is validated on its own: one invalid field does not reset the others
  (FR-007).
- Reset restores all fields except `customColors`, which keeps the user's custom palette.

## RenderingCapabilities (runtime)

What the OS and session support right now. It is re-evaluated on:

- `WM_SETTINGCHANGE`
- `WM_DWMCOMPOSITIONCHANGED`
- `WM_DPICHANGED`
- `UISettings` events

| Field | Source |
|-------|--------|
| `systemBackdropSupported` | Result of the `DWMWA_SYSTEMBACKDROP_TYPE` probe (research R-01) |
| `osBuild` | `VerifyVersionInfoW` bracket, used for messages only |
| `transparencyEffectsEnabled` | `UISettings::AdvancedEffectsEnabled` |
| `highContrast` | `SPI_GETHIGHCONTRAST` |
| `darkMode` | Luminance of `UISettings` foreground |
| `animationsEnabled` | `SPI_GETCLIENTAREAANIMATION` |
| `textScaleFactor` | `UISettings::TextScaleFactor` |
| `accentColor` | `UISettings::GetColorValue(UIColorType::Accent)` |
| `backdropApplyFailed` | Set by `MainWindow` on `WM_TE_BACKDROP_FAILED` (the `IBackdropManager::Apply` `HRESULT` failed); cleared when the user picks another mode. It lets the pure `Resolve` apply rule 4. |

## EffectiveAppearance (derived, not persisted)

This is the output of `ThemeManager::Resolve(AppearanceSettings, RenderingCapabilities)`.

| Field | Meaning |
|-------|---------|
| `requestedMode` | From the settings |
| `appliedMode` | The mode actually shown |
| `fallbackReason` | `None` \| `HighContrast` \| `TransparencyOff` \| `BackdropUnsupported` \| `BackdropApplyFailed` |
| `tintRgb` | Resolved sRGB value; `"accent"` is resolved here |
| `tintAlpha` | `0` if `appliedMode == Solid`, otherwise `tintOpacity` |
| `surfaceAlpha` | `1` if `appliedMode == Solid` (fully opaque base), otherwise `surfaceOpacity` |
| `baseColor` | `#202020` (dark) or `#F3F3F3` (light); the solid color in Solid mode; `COLOR_WINDOW` in high contrast |
| `textColor`, `secondaryTextColor`, `selectionColor`, `focusColor` | Text colors chosen for the highest *minimum* contrast across the backdrop extremes of the applied mode (Mica: base and base 25% toward the opposite extreme; Acrylic: black and white; Solid: base), reaching at least 4.5:1 together with `textScrimAlpha`. Selection and focus reach at least 3:1 (research R-05). |
| `selectionAlpha` | Opacity of the selection fill on selected rows. Starts at `0.40`, and is raised in steps of 0.05 only if text on selected rows cannot otherwise reach 4.5:1 (research R-05). |
| `selectedTextColor` | The text color on selected rows. Usually equal to `textColor`; switched to the other text color if that is what reaches 4.5:1 over the selection fill (research R-05). |
| `textScrimAlpha` | The minimum opacity of the surface layer inside text areas only: a floor, not an extra layer. Inside text areas the surface layer is drawn at `max(surfaceAlpha, textScrimAlpha)` instead of `surfaceAlpha`, with the tint layer and text on top in the usual order. `0` when the chosen text color already reaches 4.5:1 against every backdrop extreme for the applied mode; otherwise the smallest value, in steps of 0.05, that reaches it (research R-05). Never below `surfaceAlpha` when non-zero; `1` always passes. |
| `typicalSurfaceColor` | One opaque color standing in for the surface, with `baseColor` as the backdrop estimate: `Composite(Composite(base, base, surfaceAlpha), tint, tintAlpha)`. Used by the tinted address-edit fallback (research R-02). |
| `opacityControlEnabled` | `appliedMode != Solid && !highContrast` |

**Resolution rules** (the first matching rule wins):

1. `highContrast` → `appliedMode = Solid`, system colors, `fallbackReason = HighContrast`.
2. `!transparencyEffectsEnabled` → `appliedMode = Solid`, `fallbackReason = TransparencyOff`.
3. `requestedMode != Solid && !systemBackdropSupported` → Solid, `BackdropUnsupported`.
4. `requestedMode != Solid && backdropApplyFailed` (the backdrop apply `HRESULT` failed at
   runtime) → Solid, `BackdropApplyFailed`. The UI
   status reports it (FR-022; spec US1-3).
5. Otherwise, `appliedMode = requestedMode`.

## WindowState (runtime)

| Field | Type | Notes |
|-------|------|-------|
| `hwnd` | `HWND` | Owned by `MainWindow` (`wil::unique_hwnd` semantics on the owner) |
| `dpi` | `UINT` | From `GetDpiForWindow`; updated on `WM_DPICHANGED` |
| `bounds` | `RECT` | Window rectangle in physical pixels |
| `showState` | enum `{Normal, Minimized, Maximized}` | — |
| `captionHeight` | `int` (px) | Research R-03 formula at the current DPI |
| `captionButtonBounds` | `RECT` | From `DWMWA_CAPTION_BUTTON_BOUNDS`, in client coordinates |
| `pickerRect` | `RECT` | `right = captionButtonBounds.left - 8 DIP`; width 40 DIP |
| `capabilities` | `RenderingCapabilities` | — |
| `effective` | `EffectiveAppearance` | — |

**Invariants**:

- `pickerRect ∩ captionButtonBounds = ∅`.
- `pickerRect` lies inside the client caption strip.
- In Normal state, the top resize band (`SM_CYFRAME + SM_CXPADDEDBORDER`) is excluded from
  `pickerRect`.

## Location

| Field | Type | Notes |
|-------|------|-------|
| `pidl` | `wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>` | The canonical identity; comparison uses `ILIsEqual` |
| `item` | `wil::com_ptr<IShellItem>` | Created lazily from the `pidl` |
| `displayName` | `std::wstring` | `SIGDN_NORMALDISPLAY` |
| `parsingPath` | `std::optional<std::wstring>` | `SIGDN_DESKTOPABSOLUTEPARSING`; also filled for virtual folders |
| `isFileSystem` | `bool` | `SFGAO_FILESYSTEM` |

**Rules**:

- The parent is computed with `ILRemoveLastID` on a clone.
- The root (Desktop) has no parent, and Up is disabled there.
- Address-bar text is parsed with `SHParseDisplayName`. On failure, the location is
  unchanged and an error is shown (spec US3-6).

## NavigationHistory

| Field | Type |
|-------|------|
| `entries` | `std::vector<Location>` (at most 100; the oldest entry is dropped) |
| `index` | `size_t`; the current position |

**Operations and state changes**:

| Operation | Behavior |
|-----------|----------|
| `Navigate(loc)` | If `loc` equals `entries[index]`, do nothing (for example, a refresh of the same place). Otherwise, remove entries after `index`, append `loc`, and set `index` to the last entry. |
| `Up()` | `Navigate(parent(current))`. This pushes an ordinary entry, so Back returns to the child, as Windows Explorer does. It never duplicates the current entry (spec US3-3). |
| `Back()` | Allowed when `index > 0`; decrements `index`. Does not change `entries`. |
| `Forward()` | Allowed when `index + 1 < entries.size()`; increments `index`. |
| Failed navigation | Nothing is committed to history. The entry is committed only when the `DirectoryRequest` reaches `Enumerating`. |

## DirectoryRequest

| Field | Type | Notes |
|-------|------|-------|
| `generation` | `uint64_t` | Monotonic; the UI owns `currentGeneration` |
| `location` | `Location` | — |
| `stop` | `std::stop_source` | Cancelled on a new navigation, on window close, or on refresh |
| `state` | enum | See the state table below |
| `error` | `HRESULT` | Set when `state == Failed` |
| `itemsDelivered` | `size_t` | — |

**States**:

```text
Pending ──▶ Enumerating ──▶ Completed
   │             │
   │             ├──▶ Cancelled   (stop requested; posted payloads discarded)
   └─────────────┴──▶ Failed      (e.g., E_ACCESSDENIED,
                                   HRESULT_FROM_WIN32(ERROR_NOT_READY))
```

**Rules**:

- The worker posts `ItemBatch` payloads of 256 items or fewer, and a final `Completed`
  or `Failed`.
- The UI accepts a payload only if its generation equals `currentGeneration` (FR-019).
- `Failed` keeps the previous view and shows a recoverable error (FR-020).

## FileItem

| Field | Type | Notes |
|-------|------|-------|
| `childPidl` | `wil::unique_cotaskmem_ptr<ITEMID_CHILD>` | Identity within the parent folder |
| `name` | `std::wstring` | `SIGDN_NORMALDISPLAY` for the parent folder |
| `typeText` | `std::wstring` | `PKEY_ItemTypeText` |
| `size` | `std::optional<uint64_t>` | `PKEY_Size`; empty for folders and virtual items |
| `modified` | `std::optional<FILETIME>` | `PKEY_DateModified` |
| `isFolder` | `bool` | `SFGAO_FOLDER` and not `SFGAO_STREAM` (zip files count as files) |
| `isHidden` | `bool` | `SFGAO_HIDDEN`. Shown only if Explorer is set to show hidden files (`SHGetSetSettings` with `SSF_SHOWALLOBJECTS`). |
| `canRename`, `canDelete`, `canCopy`, `canMove` | `bool` | From `SFGAO_CANRENAME`, `SFGAO_CANDELETE`, `SFGAO_CANCOPY`, `SFGAO_CANMOVE` |
| `icon` | `IconSlot` | `{state: NotRequested \| Pending \| Ready \| Failed, bitmap: com_ptr<ID2D1Bitmap1>}` |
| `generation` | `uint64_t` | The generation of the request that produced it |

**Rule**: Icons arrive in `IconResult` payloads that carry `(generation, childPidl
hash)`. The UI drops any result whose generation is not current.

## SortState

| Field | Type | Default |
|-------|------|---------|
| `field` | enum `{Name, DateModified, Type, Size}` | `Name` |
| `direction` | enum `{Ascending, Descending}` | `Ascending` |

**Comparator**:

1. Folders come before files, in both directions, as in Explorer.
2. Items are then compared by the chosen field.
3. Ties are broken by name using `StrCmpLogicalW`, ascending in both directions (T050).

Missing values (for example, a folder's size) sort last, in both directions. Sorting is stable
(`std::stable_sort`). It is re-applied incrementally as batches arrive.

## SelectionModel

| Field | Type |
|-------|------|
| `selected` | `std::vector<bool>` or a bitset indexed by display order |
| `focusIndex` | `std::optional<size_t>` |
| `anchorIndex` | `std::optional<size_t>`, for Shift range selection |

**Rules**:

- The selection is cleared on navigation.
- On re-sort, the selection is kept by item identity.
- Opening or closing the color picker does not change the selection (spec US2-4; edge
  case "dismissed without selection").

## FileOperationRequest

| Field | Type | Notes |
|-------|------|-------|
| `id` | `uint64_t` | — |
| `kind` | enum `{Copy, Move, Rename, Recycle, DeletePermanent}` | `DeletePermanent` only from `Shift+Delete` or an explicit Shell verb |
| `sources` | `std::vector<pidl>` or `IDataObject` (paste) | Must not be empty |
| `destination` | `std::optional<Location>` | Required for Copy and Move |
| `newName` | `std::optional<std::wstring>` | Required for Rename. Must be non-empty and contain none of `\/:*?"<>\|`. Leading and trailing spaces are trimmed. |
| `state` | enum | See the state table below |
| `itemResults` | `std::vector<{pidl, HRESULT}>` | From `IFileOperationProgressSink` |
| `aborted` | `bool` | `GetAnyOperationsAborted` |

**States**:

```text
Queued ──▶ Running ──▶ Succeeded
                  ├──▶ PartiallySucceeded   (some itemResults failed)
                  ├──▶ Cancelled            (aborted == true, e.g. user cancelled
                  │                          the Shell conflict dialog)
                  └──▶ Failed               (PerformOperations failed before any item)
```

**Rules**:

- The UI reflects changes only from `PostXxxItem` success callbacks (spec US4-3).
- Every final state other than `Succeeded` shows a status message.
- `Failed` and `PartiallySucceeded` also list the item errors (FR-013, SC-006).
- The production flags are fixed as described in research R-08. There is no code path
  that sets `FOF_NOCONFIRMATION`.
