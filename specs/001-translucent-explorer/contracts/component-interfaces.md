# Contract: Component Interfaces and Cross-Thread Messages

**Feature**: [spec.md](../spec.md) | **Plan**: [plan.md](../plan.md)

This file defines the explicit interfaces that constitution Principle VI and TC-009
require. Components depend only on these interfaces, never on each other's
implementations.

- The declarations below are signatures, not implementations.
- They live in `include/te/<area>/I*.h`.
- Every method is called on the UI thread unless marked otherwise.

## Common types (`include/te/core/`)

```cpp
namespace te {
using Generation = std::uint64_t;
enum class BackdropMode { Acrylic, Mica, Solid };
enum class FallbackReason { None, HighContrast, TransparencyOff, BackdropUnsupported, BackdropApplyFailed };
struct Rgb { std::uint8_t r, g, b; };
struct Status { HRESULT hr; std::wstring message; };   // user-presentable
}
```

## Window area (`include/te/window/`)

```cpp
struct CaptionLayout {
    int captionHeightPx;
    RECT captionButtons;  // client coords, from DWMWA_CAPTION_BUTTON_BOUNDS
    RECT picker;          // right edge = captionButtons.left - 8 DIP
    RECT dragRegion;      // caption strip minus picker and caption buttons
    int resizeBandPx;     // top resize band height; 0 when maximized
    int contentTopPx;     // rows above the screen when maximized (frame + padding); 0 otherwise
};

class ICaptionHitTester {
public:
    virtual ~ICaptionHitTester() = default;
    // Computes the layout for the current DPI, window size and show state.
    virtual CaptionLayout Compute(HWND hwnd, UINT dpi) = 0;
    // Order: DwmDefWindowProc -> top resize band -> picker (HTCLIENT)
    //        -> drag region (HTCAPTION) -> HTCLIENT.
    // Returns true and sets *result if handled.
    virtual bool HitTest(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                         const CaptionLayout& layout, LRESULT* result) = 0;
};

// The concrete CaptionHitTester (include/te/window/CaptionHitTester.h) takes an
// ICaptionMetricsProvider, so the layout can be unit-tested with injected
// CaptionMetrics (system metrics, caption-button bounds, client size, maximized).

class IDpiManager {
public:
    virtual ~IDpiManager() = default;
    virtual UINT Dpi() const = 0;
    virtual float Scale() const = 0;          // dpi / 96
    virtual int ToPx(float dip) const = 0;
    virtual void OnDpiChanged(HWND hwnd, UINT newDpi, const RECT& suggested) = 0;
};
```

## Appearance area (`include/te/appearance/`)

```cpp
struct AppearanceSettings {
    BackdropMode backdropMode;
    std::variant<std::monostate /*accent*/, Rgb> tintColor;
    double tintOpacity;       // 0.00–0.80, tint layer strength
    double surfaceOpacity;    // 0.00–0.90, surface layer opacity, independent of tint
    std::array<Rgb, 16> customColors;
};

struct RenderingCapabilities {
    bool systemBackdropSupported;
    bool transparencyEffectsEnabled;
    bool highContrast;
    bool darkMode;
    bool animationsEnabled;
    double textScaleFactor;
    Rgb accent;
    bool backdropApplyFailed;   // set on WM_TE_BACKDROP_FAILED; resolution rule 4
};

struct EffectiveAppearance {
    BackdropMode requested;
    BackdropMode applied;
    FallbackReason reason;
    Rgb tint;
    float tintAlpha;
    float surfaceAlpha;       // 1.0 in Solid
    Rgb base, text, secondaryText, selection, focus;
    float selectionAlpha;     // 0.40 unless raised so selected-row text reaches 4.5:1
    Rgb selectedText;         // text color on selected rows (research R-05)
    float textScrimAlpha;     // surface-opacity floor inside text areas only; 0 = not needed (research R-05)
    Rgb typicalSurfaceColor;  // opaque stand-in for the surface (address-edit fallback)
    bool opacityControlEnabled;
};

class IThemeManager {
public:
    virtual ~IThemeManager() = default;
    virtual RenderingCapabilities QueryCapabilities() = 0;
    // Pure function. Unit-tested as a truth table (data-model: EffectiveAppearance).
    virtual EffectiveAppearance Resolve(const AppearanceSettings&, const RenderingCapabilities&) const = 0;
    // Raised on WM_SETTINGCHANGE / UISettings events, marshalled to the UI thread.
    virtual void SetChangedCallback(std::function<void()>) = 0;
};
// The concrete ThemeManager adds Subscribe(HWND notifyWindow), which posts a coalesced
// WM_TE_SETTINGS_CHANGED from the UISettings events, and NotifyChanged(), which the
// owner calls on the UI thread to raise the changed callback (T034).

class IBackdropManager {
public:
    virtual ~IBackdropManager() = default;
    // Applies DWMWA_SYSTEMBACKDROP_TYPE, DWMWA_USE_IMMERSIVE_DARK_MODE and,
    // in Solid mode only, DWMWA_CAPTION_COLOR / DWMWA_BORDER_COLOR.
    // Returns the HRESULT of the backdrop call so the caller can fall back
    // (FallbackReason::BackdropApplyFailed).
    virtual HRESULT Apply(HWND hwnd, const EffectiveAppearance&) = 0;
    virtual bool ProbeSystemBackdrop(HWND hwnd) = 0;
};

class IColorPicker {
public:
    virtual ~IColorPicker() = default;
    virtual void Show(HWND owner, const RECT& anchorScreen, const AppearanceSettings& current,
                      const EffectiveAppearance& effective) = 0;
    virtual void Hide() = 0;
    virtual bool IsOpen() const = 0;
    // Called for each live change and on Reset. The owner persists (debounced)
    // and re-resolves.
    virtual void SetChangedCallback(std::function<void(const AppearanceSettings&)>) = 0;
};
```

## Settings area (`include/te/settings/`)

```cpp
struct LoadResult {
    AppearanceSettings settings;
    std::vector<std::wstring> fieldsDefaulted;   // for diagnostics
    bool fileWasCorrupt;                         // original renamed to settings.corrupt-<ts>.json
};

class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;
    virtual LoadResult Load() = 0;                         // never throws; always returns usable settings
    virtual HRESULT Save(const AppearanceSettings&) = 0;   // atomic replace (research R-12)
    static AppearanceSettings Defaults();                  // research R-11
};
```

## Shell area (`include/te/shell/`)

```cpp
// Both types are defined in include/te/shell/ShellTypes.h, which is created in the
// Foundational phase so that Messages.h and the interfaces compile before US3.
class ShellLocation;    // data-model: Location (owns the PIDL)
struct ShellItemInfo;   // data-model: FileItem fields without the UI state

class IShellNavigator {
public:
    virtual ~IShellNavigator() = default;
    virtual HRESULT Parse(std::wstring_view text, ShellLocation* out) = 0;  // SHParseDisplayName
    virtual std::optional<ShellLocation> Parent(const ShellLocation&) = 0;
    virtual ShellLocation InitialLocation(std::optional<std::wstring_view> cmdLinePath) = 0;
    virtual HRESULT Open(HWND owner, const ShellLocation& folder, const ShellItemInfo& item) = 0;  // ShellExecuteExW
    virtual HRESULT ShowContextMenu(HWND owner, POINT screenPt, const ShellLocation& folder,
                                    std::span<const ShellItemInfo* const> items, bool extended) = 0;
    // Must be forwarded from the window procedure while a menu is open
    // (IContextMenu2/3).
    virtual bool HandleMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result) = 0;
};

class IDirectoryEnumerator {
public:
    virtual ~IDirectoryEnumerator() = default;
    // Starts enumeration on the STA enumeration worker. Cancels any earlier
    // request. Results arrive as WM_TE_ENUM_BATCH / WM_TE_ENUM_DONE posted
    // to notifyHwnd.
    virtual void Start(HWND notifyHwnd, Generation gen, const ShellLocation& loc) = 0;
    virtual void CancelAll() = 0;             // request_stop; non-blocking
    virtual void Shutdown() = 0;              // request_stop + join; called from WM_DESTROY
};

class IIconProvider {
public:
    virtual ~IIconProvider() = default;
    // Thread-safe. Queued LIFO so the visible rows are served first.
    virtual void Request(HWND notifyHwnd, Generation gen, std::size_t itemKey, const ShellLocation& folder,
                         const ShellItemInfo& item, int sizePx) = 0;
    virtual void CancelOlderThan(Generation gen) = 0;
    virtual void Shutdown() = 0;
};

enum class FileOpKind { Copy, Move, Rename, Recycle, DeletePermanent };
enum class FileOpFinalState { Succeeded, PartiallySucceeded, Cancelled, Failed };  // FileOpDone::state

struct FileOpRequest {
    std::uint64_t id;
    FileOpKind kind;
    std::vector<ShellLocation> sources;          // or dataObject for paste
    wil::com_ptr<IDataObject> dataObject;
    std::optional<ShellLocation> destination;
    std::optional<std::wstring> newName;
};

class IFileOperationService {
public:
    virtual ~IFileOperationService() = default;
    // Runs IFileOperation on the STA file-op worker. Owner window = mainHwnd.
    // Flags are fixed by research R-08 and cannot be overridden by the caller.
    // Progress and results arrive as WM_TE_FILEOP_ITEM / WM_TE_FILEOP_DONE.
    virtual void Submit(HWND notifyHwnd, FileOpRequest request) = 0;
    virtual void Shutdown() = 0;   // waits for the running operation; Shell UI stays modal to the owner
};
```

## UI area (`include/te/ui/`)

```cpp
class IFileView {
public:
    virtual ~IFileView() = default;
    virtual void BeginLocation(Generation gen) = 0;                  // clears items and selection
    virtual void AppendItems(Generation gen, std::vector<FileItem>&&) = 0;
    virtual void SetIcon(Generation gen, std::size_t itemKey, wil::com_ptr<ID2D1Bitmap1>) = 0;
    virtual void ApplyRename(std::size_t itemKey, std::wstring newName) = 0;  // only after PostRenameItem success
    virtual void SetSort(SortState) = 0;
    virtual std::vector<const FileItem*> Selection() const = 0;
    virtual void SetBounds(const D2D1_RECT_F& bounds) = 0;          // layout rectangle in DIPs (MainLayout)
    virtual void Render(ID2D1DeviceContext*, const EffectiveAppearance&) = 0;
    virtual IRawElementProviderFragment* Automation() = 0;           // UIA (research R-09)
};
```

`itemKey` is `FileItem::key`: an index assigned when the item is appended, stable within
one generation.

The other UI components follow the same pattern. Each has `SetBounds`, `Render` and
`Automation`, plus:

| Interface | Methods |
|-----------|---------|
| `IAddressBar` | `SetLocation(const ShellLocation&)`, `BeginEdit()`, `CancelEdit()`, `IsEditing()`, `SetNavigateCallback(std::function<void(std::wstring_view)>)`, `ShowError(std::wstring_view)` |
| `INavigationPane` | `Refresh()`, `SetCurrent(const ShellLocation&)`, `SetNavigateCallback(std::function<void(const ShellLocation&)>)` |
| `IStatusBar` | `SetItemCounts(size_t items, size_t selected)`, `SetAppearance(const AppearanceSettings&, const EffectiveAppearance&)`, `SetTransientMessage(std::wstring, UINT ms)`, `SetOperationMessage(std::wstring)`, `ClearOperationMessage()` |

The shared data types these use (`ShellLocation`, `ShellItemInfo`, `FileItem`,
`IconSlot`, `SortState`) are in `include/te/shell/ShellTypes.h`,
`include/te/ui/FileItem.h` and `include/te/ui/SortTypes.h`.

## Render area (`include/te/render/`)

```cpp
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    // Creates a D3D11 device + DXGI composition swap chain
    // (DXGI_ALPHA_MODE_PREMULTIPLIED) + DComp target (topmost = FALSE).
    virtual HRESULT Initialize(HWND hwnd) = 0;
    virtual HRESULT Resize(UINT widthPx, UINT heightPx, UINT dpi) = 0;
    virtual ID2D1DeviceContext* BeginDraw() = 0;
    virtual HRESULT EndDrawAndPresent() = 0;      // handles D2DERR_RECREATE_TARGET / device-lost by re-initializing
    virtual IDWriteFactory3* DWrite() = 0;
};
```

## Cross-thread message contract

- Worker threads communicate with the UI thread **only** through `PostMessageW(notifyHwnd,
  WM_TE_*, 0, reinterpret_cast<LPARAM>(payload.release()))`.
- The UI thread takes ownership immediately: `std::unique_ptr<T>{reinterpret_cast<T*>(lParam)}`.
- If `PostMessageW` fails, for example because the window was destroyed, the worker keeps
  ownership and frees the payload.
- On `WM_DESTROY`, workers are shut down first. `PeekMessageW(PM_REMOVE)` then drains the
  remaining `WM_TE_*` messages so their payloads are freed. No application-owned
  resources leak (SC-010).

| Message (`WM_APP +`) | Payload | Generation check | UI action |
|----------------------|---------|------------------|-----------|
| `WM_TE_ENUM_BATCH` (+1) | `EnumBatch { Generation gen; std::vector<ShellItemInfo> items; }` | Discard if `gen != current` | `IFileView::AppendItems` |
| `WM_TE_ENUM_DONE` (+2) | `EnumDone { Generation gen; HRESULT hr; bool cancelled; }` | Discard if stale | Commit history on first batch or success; show an error on failure |
| `WM_TE_ICON_READY` (+3) | `IconReady { Generation gen; size_t itemKey; wil::unique_hbitmap bmp; }` | Discard if stale | WIC → Direct2D bitmap, then `SetIcon` |
| `WM_TE_FILEOP_ITEM` (+4) | `FileOpItem { uint64_t opId; FileOpKind kind; ShellLocation item; HRESULT hr; std::optional<std::wstring> newName; }` | n/a | Update the view for successful items only |
| `WM_TE_FILEOP_DONE` (+5) | `FileOpDone { uint64_t opId; FinalState state; std::vector<Status> errors; bool aborted; }` | n/a | Status bar; `TaskDialogIndirect` on failure; refresh if the current folder is affected |
| `WM_TE_SETTINGS_CHANGED` (+6) | none | n/a | Re-query capabilities, `Resolve`, `Apply`, re-render |
| `WM_TE_BACKDROP_FAILED` (+7) | `HRESULT` in `wParam` | n/a | `FallbackReason::BackdropApplyFailed` → Solid, status notice |
