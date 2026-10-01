#pragma once

// The navigation pane (T063; UI contract §1): This PC, the user's known folders, then
// fixed and removable drives, each with its Shell icon. Windowless: MainWindow forwards
// input in DIPs and draws it in its render pass.
//
// Folder tree (T090; FR-021, SHOULD): every place is a tree node with an expander. The
// first expansion lists the node's subfolders on a FolderTreeLoader worker
// (WM_TE_TREE_CHILDREN -> OnChildren); a collapsed node keeps its rows for the next time.
// The rows on screen are kept flat in display order (Entries()), each with its depth, so
// scrolling, hit-testing and the keyboard work on row indices. Expanded folders are
// remembered and expanded again after a refresh (a drive arrives or leaves).

#include <te/render/TextFormats.h>
#include <te/shell/FolderTreeLoader.h>
#include <te/ui/INavigationPane.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace te
{

class NavigationPane final : public INavigationPane
{
  public:
    static constexpr float kRowHeightDip = 28.0f;
    static constexpr float kIconSizeDip = 16.0f;
    static constexpr float kPaddingDip = 12.0f;
    static constexpr float kIconGapDip = 8.0f;
    static constexpr float kGroupGapDip = 8.0f; // between known folders and drives
    static constexpr float kFocusWidthDip = 2.0f;
    static constexpr float kExpanderDip = 16.0f; // the chevron's column (T090)
    static constexpr float kIndentDip = 16.0f;   // per tree level (T090)
    static constexpr int kWheelRows = 3;

    struct Entry
    {
        ShellLocation location;
        bool isDrive = false;            // a top-level drive (after the group gap)
        wil::unique_hbitmap iconSource;  // from IShellItemImageFactory, converted on render
        wil::com_ptr<ID2D1Bitmap1> icon; // for the current Direct2D device
        // Tree (T090).
        std::uint64_t id = 0;   // unique while the pane lives; the key for results and UIA
        int depth = 0;          // 0 for the places, 1 for their subfolders, ...
        bool expandable = true; // false once it turned out to have no subfolders
        bool expanded = false;
        bool loading = false; // its subfolders are being listed
    };

    NavigationPane() = default;

    void Attach(HWND parent, UINT dpi) noexcept; // repaints and loader results go to `parent`
    void SetDpi(UINT dpi);                       // re-extracts icons at the new size
    // Drops the Direct2D icons made on the current device (WM_DESTROY, T088).
    void ReleaseDeviceResources() noexcept;
    // Stops listing subfolders: CancelAll first, Shutdown when the workers stop (T088).
    void CancelLoads();
    void ShutdownLoader();
    // The Windows text size (1 to 2.25, T082): rows grow with the text.
    void SetTextScale(float scale);
    [[nodiscard]] float RowHeight() const noexcept
    {
        return kRowHeightDip * m_textScale;
    }
    void SetTextFormats(const TextFormats* formats) noexcept;

    // INavigationPane
    void Refresh() override;
    void SetCurrent(const ShellLocation& location) override;
    void SetNavigateCallback(std::function<void(const ShellLocation&)> callback) override;
    void SetBounds(const D2D1_RECT_F& bounds) override;
    void Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective) override;
    IRawElementProviderFragment* Automation() override; // T078

    // WM_DEVICECHANGE: refreshes on drive arrival and removal. Returns true if it did.
    bool OnDeviceChange(WPARAM event);

    // Input in DIPs; each returns true if the pane used it. A click on a row's chevron
    // expands or collapses it; anywhere else on the row navigates.
    bool OnPointerDown(D2D1_POINT_2F point);
    bool OnPointerMove(D2D1_POINT_2F point);
    void OnPointerLeave();
    bool OnWheel(int wheelDelta);
    // Up / Down / Home / End move; Right expands, then enters; Left collapses, then goes to
    // the parent; Enter / Space navigate.
    bool OnKeyDown(UINT vk);
    void SetFocused(bool focused);
    // Whether keyboard focus cues are shown (T080).
    void SetFocusVisible(bool visible);
    [[nodiscard]] bool Focused() const noexcept
    {
        return m_focused;
    }

    // Tests only: these places instead of This PC, the known folders and the drives, so a
    // test builds a tree over folders it created (T090). Behaves as Refresh otherwise.
    void SetPlacesForTesting(std::vector<ShellLocation> places);

    // Tree (T090). Expanding a node the first time lists its subfolders asynchronously.
    void Expand(std::size_t index);
    void Collapse(std::size_t index);
    // WM_TE_TREE_CHILDREN: the owner hands the result over (it frees the payload).
    void OnChildren(FolderChildren& result);
    // Called after rows were added or removed (UI Automation's structure event).
    void SetTreeChangedCallback(std::function<void()> callback);

    [[nodiscard]] const std::vector<Entry>& Entries() const noexcept
    {
        return m_entries;
    }
    [[nodiscard]] std::optional<std::size_t> CurrentIndex() const noexcept
    {
        return m_current;
    }
    [[nodiscard]] std::optional<std::size_t> FocusIndex() const noexcept
    {
        return m_focus;
    }
    [[nodiscard]] D2D1_RECT_F EntryRect(std::size_t index) const noexcept;
    // The chevron's area of a row (T090).
    [[nodiscard]] D2D1_RECT_F ExpanderRect(std::size_t index) const noexcept;
    [[nodiscard]] const D2D1_RECT_F& Bounds() const noexcept
    {
        return m_bounds;
    }
    [[nodiscard]] std::optional<std::size_t> EntryAt(D2D1_POINT_2F point) const noexcept;

    // Tree navigation over the visible rows (T090; UI Automation).
    [[nodiscard]] std::optional<std::size_t> IndexOfNode(std::uint64_t id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> ParentOf(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> FirstChildOf(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> LastChildOf(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> NextSiblingOf(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> PreviousSiblingOf(std::size_t index) const noexcept;

  private:
    void ExtractIcons();
    // The top-level rows are replaced (Refresh, SetPlacesForTesting); open folders reopen.
    void ResetPlaces(std::vector<Entry> places);
    void Navigate(std::size_t index);
    void EnsureVisible(std::size_t index);
    void ClampScroll();
    void Invalidate() const;
    [[nodiscard]] float ContentHeight() const noexcept;
    [[nodiscard]] int IconPx() const noexcept;
    // One past the last row of index's subtree.
    [[nodiscard]] std::size_t SubtreeEnd(std::size_t index) const noexcept;
    // Row indices after rows were inserted at `at` (count > 0) or removed from it (< 0).
    void ShiftIndices(std::size_t at, std::ptrdiff_t count);
    void InsertRows(std::size_t at, std::vector<Entry>&& rows);
    void UpdateFirstDrive() noexcept;
    void TreeChanged();
    [[nodiscard]] bool Remembered(const ShellLocation& location) const;
    void Remember(const ShellLocation& location, bool expanded);

    HWND m_parent = nullptr;
    UINT m_dpi = USER_DEFAULT_SCREEN_DPI;
    const TextFormats* m_formats = nullptr;
    wil::com_ptr<IDWriteInlineObject> m_ellipsis;
    IDWriteTextFormat* m_ellipsisFormat = nullptr;
    wil::com_ptr<ID2D1Device> m_iconDevice; // icons are recreated after a device change

    std::vector<Entry> m_entries; // the rows on screen, in display order
    std::optional<std::size_t> m_current;
    std::optional<std::size_t> m_focus;
    std::optional<std::size_t> m_hot;
    std::optional<ShellLocation> m_currentLocation; // re-matched after Refresh
    std::function<void(const ShellLocation&)> m_navigate;
    std::function<void()> m_treeChanged;

    // Tree (T090).
    FolderTreeLoader m_loader;
    std::uint64_t m_nextId = 1;
    std::size_t m_firstDrive = SIZE_MAX; // row of the first top-level drive (group gap)
    // The rows under a collapsed node, kept for its next expansion, by node id.
    std::unordered_map<std::uint64_t, std::vector<Entry>> m_collapsed;
    std::vector<ShellLocation> m_expandedLocations; // expanded again after a refresh

    D2D1_RECT_F m_bounds{};
    float m_scroll = 0.0f;
    float m_textScale = 1.0f;
    bool m_focused = false;
    bool m_focusVisible = true;
};

} // namespace te
