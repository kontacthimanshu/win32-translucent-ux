#pragma once

// The translucent Direct2D file list (T060; research R-06; UI contract §1, §4; FR-010,
// FR-011). A windowless component: MainWindow forwards pointer and keyboard input in DIPs
// and draws it inside its render pass, after the surface layers.
//
// Layout, top to bottom: a header row (Name, Date modified, Type, Size; click to sort,
// click again to reverse; drag a divider to resize), then 28-DIP rows, with a custom
// translucent scrollbar on the right.

#include <te/render/TextFormats.h>
#include <te/ui/IFileView.h>
#include <te/ui/RenameEdit.h>
#include <te/ui/SelectionModel.h>

#include <wincodec.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

namespace te
{

class FileView final : public IFileView
{
  public:
    static constexpr float kRowHeightDip = 28.0f;
    static constexpr float kHeaderHeightDip = 28.0f;
    static constexpr float kIconSizeDip = 16.0f;
    static constexpr float kCellPaddingDip = 12.0f;
    static constexpr float kIconGapDip = 8.0f;
    static constexpr float kScrollbarWidthDip = 10.0f;
    static constexpr float kDividerGripDip = 4.0f;
    static constexpr float kMinColumnWidthDip = 48.0f;
    static constexpr float kFocusWidthDip = 2.0f;
    static constexpr int kWheelRows = 3;

    enum class Column
    {
        Name,
        DateModified,
        Type,
        Size,
    };

    struct Strings
    {
        std::wstring name = L"Name";
        std::wstring dateModified = L"Date modified";
        std::wstring type = L"Type";
        std::wstring size = L"Size";
        // Balloon tip for an invalid new name (IDS_ERR_BAD_NAME).
        std::wstring badName =
            L"A file name can't contain any of the following characters:\r\n\\ / : * ? \" < > |";
    };

    struct Callbacks
    {
        std::function<void(const FileItem&)> open;                           // Enter / double-click
        std::function<void(std::size_t items, std::size_t selected)> counts; // status bar
        std::function<void()> invalidate;                                    // repaint needed
        // Inline rename committed a valid, changed name (T071). The owner submits a Rename
        // request; the row keeps its old name until ApplyRename (US4-3).
        std::function<void(const FileItem&, const std::wstring& newName)> rename;
    };

    explicit FileView(Strings strings = {});

    void SetCallbacks(Callbacks callbacks);
    void SetTextFormats(const TextFormats* formats) noexcept;
    // The window that hosts the inline-rename edit (T071), and its DPI.
    void AttachWindow(HWND parent, UINT dpi);
    void SetDpi(UINT dpi);
    // The Windows text size (1 to 2.25, T082): rows and the header grow with the text,
    // the top row on screen stays the top row, and an open rename edit gets the new font.
    void SetTextScale(float scale);
    [[nodiscard]] float RowHeight() const noexcept
    {
        return kRowHeightDip * m_textScale;
    }
    [[nodiscard]] float HeaderHeight() const noexcept
    {
        return kHeaderHeightDip * m_textScale;
    }

    // Inline rename of the focused item (F2, or the context menu's Rename): an edit over
    // its name cell. False if nothing is focused or the item cannot be renamed.
    bool BeginRename();
    bool BeginRename(std::size_t index);
    void CancelRename();
    [[nodiscard]] bool IsRenaming() const noexcept
    {
        return m_renameKey.has_value();
    }
    // The edit, for WM_CTLCOLOREDIT / appearance forwarding and tests.
    [[nodiscard]] RenameEdit& RenameField() noexcept
    {
        return m_rename;
    }
    // Right-click (T072): over a row that is not selected, select just that row; over the
    // empty part of the list, clear the selection (the menu is then the folder's). Returns
    // false outside the rows area (header, scrollbar, outside the list).
    bool SelectForContextMenu(D2D1_POINT_2F point);
    // After a refresh (T072): selects the items `selected` accepts and focuses the first
    // one `focused` accepts, as before the refresh.
    void RestoreSelection(const std::function<bool(const FileItem&)>& selected,
                          const std::function<bool(const FileItem&)>& focused);

    // The filter box (T092): only items whose name contains `text` (case-insensitive,
    // linguistic) stay listed; the others wait aside, in order, and come back when the filter
    // changes. The selection and focus stay on items that remain listed. Later batches are
    // split the same way; a new folder starts unfiltered (BeginLocation).
    void SetFilter(std::wstring text);
    [[nodiscard]] const std::wstring& Filter() const noexcept
    {
        return m_filter;
    }
    // Items hidden by the filter.
    [[nodiscard]] std::size_t HiddenCount() const noexcept
    {
        return m_hidden.size();
    }
    // FindNLSStringEx(LINGUISTIC_IGNORECASE); an empty filter matches everything.
    [[nodiscard]] static bool Matches(const std::wstring& name, const std::wstring& filter);
    // The current filter matches the shown name or the real one (with its extension).
    [[nodiscard]] bool Passes(const ShellItemInfo& info) const;

    // For UI Automation (T076): geometry in client DIPs, scrolling and sorting.
    [[nodiscard]] const D2D1_RECT_F& Bounds() const noexcept
    {
        return m_bounds;
    }
    [[nodiscard]] D2D1_RECT_F HeaderCellRect(Column column) const noexcept;
    [[nodiscard]] float ContentHeightDip() const noexcept
    {
        return ContentHeight();
    }
    [[nodiscard]] float ViewportHeightDip() const noexcept
    {
        return ViewportHeight();
    }
    // Sets the scroll offset (clamped) and repaints.
    void SetScrollOffset(float offsetDip);
    // Sorts by a column as a header click does: repeating reverses the direction.
    void SortByColumn(Column column);
    [[nodiscard]] const Strings& ColumnNames() const noexcept
    {
        return m_strings;
    }
    // For the rows' UI Automation providers (T077).
    [[nodiscard]] std::optional<std::size_t> IndexOfKey(std::size_t key) const;
    [[nodiscard]] D2D1_RECT_F CellRect(std::size_t index, Column column) const noexcept;
    // True if any part of the row is inside the rows area (below the header).
    [[nodiscard]] bool IsRowVisible(std::size_t index) const noexcept;
    void EnsureVisible(std::size_t index);
    // Moves the focus to a row without changing the selection (UIA SetFocus).
    void FocusRow(std::size_t index);
    // Selects only this row and focuses it (SelectionItem.Select).
    void SelectOnly(std::size_t index);
    // Adds a row to, or removes it from, the selection (SelectionItem.Add/RemoveFromSelection).
    void SetSelected(std::size_t index, bool selected);

    // The name cell of a row in DIPs (where the rename edit goes).
    [[nodiscard]] D2D1_RECT_F NameCellRect(std::size_t index) const;

    // IFileView
    void BeginLocation(Generation gen) override;
    void AppendItems(Generation gen, std::vector<FileItem>&& items) override;
    void SetIcon(Generation gen, std::size_t itemKey, wil::com_ptr<ID2D1Bitmap1> bitmap) override;
    void ApplyRename(std::size_t itemKey, std::wstring newName) override;
    // As above, when the shown name differs from the real one (Explorer hides the
    // extension): `displayName` is shown, `realName` is what the next rename starts from.
    void ApplyRename(std::size_t itemKey, std::wstring displayName, std::wstring realName);
    void SetSort(SortState state) override;
    [[nodiscard]] std::vector<const FileItem*> Selection() const override;
    void SetBounds(const D2D1_RECT_F& bounds) override;
    void Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective) override;
    IRawElementProviderFragment* Automation() override; // UI Automation: T078

    // Input, in DIPs relative to the window's client area. Each returns true if the view
    // used the event (the caller then stops routing it).
    bool OnPointerDown(D2D1_POINT_2F point, bool ctrl, bool shift);
    bool OnPointerMove(D2D1_POINT_2F point);
    bool OnPointerUp(D2D1_POINT_2F point);
    bool OnDoubleClick(D2D1_POINT_2F point);
    bool OnWheel(int wheelDelta);
    bool OnKeyDown(UINT vk, bool ctrl, bool shift);
    void SetFocused(bool focused);
    // Whether keyboard focus cues are shown (WM_UPDATEUISTATE / UISF_HIDEFOCUS, T080).
    void SetFocusVisible(bool visible);
    [[nodiscard]] bool Focused() const noexcept
    {
        return m_focused;
    }
    // True while a divider or the scrollbar thumb is being dragged (the owner captures
    // the mouse).
    [[nodiscard]] bool Dragging() const noexcept
    {
        return m_drag != Drag::None;
    }

    // Icons for the rows on screen and one screen ahead (T087): calls request(item) once
    // for each such item whose icon is not requested yet, and marks it pending. The
    // look-ahead is requested first and the visible rows last, bottom to top, so a LIFO
    // worker serves the top visible row first.
    void ForEachVisibleWithoutIcon(const std::function<void(const FileItem&)>& request);
    // The worker skipped a shared icon the UI no longer has: ask for it again, extracted.
    void RequestIconAgain(Generation gen, std::size_t itemKey);
    // Icons are requested and accepted under their own generation, so they can be
    // requested again without restarting the listing (a DPI change, T081). BeginLocation
    // starts it at the listing's generation. ResetIcons switches to `iconGen` (newer than
    // any issued so far) and marks every icon to be requested again; the old bitmaps stay
    // on screen, scaled, until the new ones arrive.
    // `keepBitmaps` false drops them too (they were made on a Direct2D device that is gone).
    void ResetIcons(Generation iconGen, bool keepBitmaps = true);
    [[nodiscard]] Generation IconGeneration() const noexcept
    {
        return m_iconGen;
    }
    // HBITMAP (32 bpp, premultiplied alpha, from IShellItemImageFactory) to a Direct2D
    // bitmap for `dc`, through WIC.
    static HRESULT IconFromHBitmap(ID2D1DeviceContext* dc, HBITMAP bitmap, ID2D1Bitmap1** result);

    // The text-layout cache (T086): layouts kept for the rows drawn last, and how many
    // have been created since the view was made. For tests.
    [[nodiscard]] std::size_t CachedRowLayouts() const noexcept
    {
        return m_layouts.size();
    }
    [[nodiscard]] std::uint64_t CreatedTextLayouts() const noexcept
    {
        return m_layoutsCreated;
    }

    // State, for the owner and tests.
    [[nodiscard]] Generation CurrentGeneration() const noexcept
    {
        return m_gen;
    }
    [[nodiscard]] const std::vector<FileItem>& Items() const noexcept
    {
        return m_items;
    }
    [[nodiscard]] const SelectionModel& SelectionState() const noexcept
    {
        return m_selection;
    }
    [[nodiscard]] SortState Sort() const noexcept
    {
        return m_sort;
    }
    [[nodiscard]] float ScrollOffset() const noexcept
    {
        return m_scroll;
    }
    [[nodiscard]] float ColumnWidth(Column column) const noexcept
    {
        return m_widths[static_cast<std::size_t>(column)];
    }
    // Row under a point (DIPs), if any.
    [[nodiscard]] std::optional<std::size_t> RowAt(D2D1_POINT_2F point) const;
    // Rectangle of a row in DIPs (may be outside the visible area).
    [[nodiscard]] D2D1_RECT_F RowRect(std::size_t index) const;

  private:
    enum class Drag
    {
        None,
        Divider,
        Thumb,
    };

    void Resort();
    void RebuildKeyIndex();
    void ClampScroll();
    void ScrollIntoView(std::size_t index);
    void MoveFocus(std::size_t index, bool ctrl, bool shift);
    void NotifyCounts() const;
    void Invalidate() const;
    [[nodiscard]] float ViewportHeight() const noexcept;
    [[nodiscard]] float ContentHeight() const noexcept;
    [[nodiscard]] D2D1_RECT_F HeaderRect() const noexcept;
    [[nodiscard]] D2D1_RECT_F ScrollbarRect() const noexcept;
    [[nodiscard]] std::optional<D2D1_RECT_F> ThumbRect() const noexcept;
    [[nodiscard]] float ColumnLeft(Column column) const noexcept;
    [[nodiscard]] std::optional<Column> DividerAt(D2D1_POINT_2F point) const;
    [[nodiscard]] std::optional<Column> HeaderColumnAt(D2D1_POINT_2F point) const;
    [[nodiscard]] std::size_t RowsPerPage() const noexcept;
    void SortBy(SortField field);
    // Keeps the rename edit on its row after scrolling, sorting or resizing; cancels the
    // rename when the row leaves the view or the item is gone.
    void FollowRename();

    void DrawText(ID2D1DeviceContext* dc, IDWriteTextFormat* format, const std::wstring& text,
                  const D2D1_RECT_F& rect, DWRITE_TEXT_ALIGNMENT alignment, ID2D1Brush* brush,
                  const EffectiveAppearance& effective);

    // The text layouts of one row's four cells (T086), built with the body format for
    // the column widths and row height of `epoch`.
    struct RowLayouts
    {
        std::uint64_t epoch = 0;
        std::array<wil::com_ptr<IDWriteTextLayout>, 4> cells;
    };
    // Every layout built before this call is out of date: column widths, the view's size,
    // the text formats, the text scale, the DPI or an item's text changed.
    void InvalidateLayouts() noexcept
    {
        ++m_layoutEpoch;
    }
    // The cached layouts of `item` for this frame, built if missing or out of date.
    RowLayouts& LayoutsFor(const FileItem& item, IDWriteTextFormat* body,
                           std::unordered_map<std::size_t, RowLayouts>& frame);

    Strings m_strings;
    Callbacks m_callbacks;
    const TextFormats* m_formats = nullptr;
    wil::com_ptr<IDWriteInlineObject> m_ellipsis;
    IDWriteTextFormat* m_ellipsisFormat = nullptr; // the format m_ellipsis was made for

    Generation m_gen = 0;
    Generation m_iconGen = 0; // what SetIcon accepts (T081)
    std::vector<FileItem> m_items;
    std::vector<FileItem> m_hidden; // filtered out (T092), sorted like m_items
    std::wstring m_filter;
    std::unordered_map<std::size_t, std::size_t> m_indexOfKey;
    // Layouts of the rows drawn in the last frame, by item key (T086). Rebuilt every frame
    // from the rows on screen, so rows that scrolled away are dropped.
    std::unordered_map<std::size_t, RowLayouts> m_layouts;
    std::uint64_t m_layoutEpoch = 1;
    std::uint64_t m_layoutsCreated = 0;
    std::size_t m_nextKey = 1;
    SelectionModel m_selection;
    SortState m_sort;

    D2D1_RECT_F m_bounds{};
    std::array<float, 4> m_widths{320.0f, 170.0f, 170.0f, 100.0f};
    float m_scroll = 0.0f;
    float m_textScale = 1.0f;
    bool m_focused = false;
    bool m_focusVisible = true;

    Drag m_drag = Drag::None;
    Column m_dragColumn = Column::Name;
    float m_dragOrigin = 0.0f;     // pointer x (divider) or y (thumb) at drag start
    float m_dragStartValue = 0.0f; // column width or scroll offset at drag start
    bool m_thumbHot = false;

    RenameEdit m_rename;
    std::optional<std::size_t> m_renameKey; // item being renamed
};

} // namespace te
