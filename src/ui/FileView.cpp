#include <te/ui/FileView.h>

#include <te/render/FocusIndicator.h>
#include <te/render/SurfacePainter.h>
#include <te/render/TextHalo.h>
#include <te/ui/FileItemFormat.h>
#include <te/ui/SortModel.h>

#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace te
{

namespace
{

D2D1_COLOR_F ToColor(Rgb rgb, float alpha = 1.0f)
{
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
}

bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

constexpr SortField kFieldOf[] = {SortField::Name, SortField::DateModified, SortField::Type, SortField::Size};

} // namespace

FileView::FileView(Strings strings) : m_strings(std::move(strings)) {}

void FileView::SetCallbacks(Callbacks callbacks)
{
    m_callbacks = std::move(callbacks);
}

void FileView::SetTextFormats(const TextFormats* formats) noexcept
{
    m_formats = formats;
    InvalidateLayouts();
    m_rename.SetTextFormats(formats);
    m_ellipsis.reset();
    m_ellipsisFormat = nullptr;
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

void FileView::AttachWindow(HWND parent, UINT dpi)
{
    m_rename.Attach(parent, dpi);
    m_rename.SetBadNameText(m_strings.badName);
}

void FileView::SetDpi(UINT dpi)
{
    InvalidateLayouts();
    m_rename.SetDpi(dpi);
    FollowRename();
}

void FileView::SetTextScale(float scale)
{
    scale = std::max(1.0f, scale);
    if (scale == m_textScale)
    {
        return;
    }
    m_scroll *= scale / m_textScale; // the same row stays at the top
    m_textScale = scale;
    InvalidateLayouts();
    m_ellipsis.reset(); // made for the previous body format
    m_ellipsisFormat = nullptr;
    ClampScroll();
    m_rename.UpdateFont();
    FollowRename();
    Invalidate();
}

D2D1_RECT_F FileView::NameCellRect(std::size_t index) const
{
    const D2D1_RECT_F row = RowRect(index);
    const float nameLeft = ColumnLeft(Column::Name);
    // Where the name text starts (after the icon), to the end of the column.
    const float left = nameLeft + kCellPaddingDip + kIconSizeDip + kIconGapDip - 3.0f;
    return D2D1::RectF(left, row.top + 2.0f, nameLeft + ColumnWidth(Column::Name) - kCellPaddingDip / 2,
                       row.bottom - 2.0f);
}

D2D1_RECT_F FileView::HeaderCellRect(Column column) const noexcept
{
    const D2D1_RECT_F header = HeaderRect();
    const float left = ColumnLeft(column);
    return D2D1::RectF(left, header.top, std::min(left + ColumnWidth(column), header.right), header.bottom);
}

std::optional<std::size_t> FileView::IndexOfKey(std::size_t key) const
{
    const auto it = m_indexOfKey.find(key);
    if (it == m_indexOfKey.end())
    {
        return std::nullopt;
    }
    return it->second;
}

D2D1_RECT_F FileView::CellRect(std::size_t index, Column column) const noexcept
{
    const D2D1_RECT_F row = RowRect(index);
    const float left = ColumnLeft(column);
    return D2D1::RectF(left, row.top, std::min(left + ColumnWidth(column), row.right), row.bottom);
}

bool FileView::IsRowVisible(std::size_t index) const noexcept
{
    const D2D1_RECT_F row = RowRect(index);
    return index < m_items.size() && row.bottom > m_bounds.top + HeaderHeight() && row.top < m_bounds.bottom;
}

void FileView::EnsureVisible(std::size_t index)
{
    if (index < m_items.size())
    {
        ScrollIntoView(index);
        Invalidate();
    }
}

void FileView::FocusRow(std::size_t index)
{
    if (index < m_items.size())
    {
        m_selection.Focus(index);
        ScrollIntoView(index);
        NotifyCounts(); // the owner also tracks focus from here (UIA events, T079)
        Invalidate();
    }
}

void FileView::SelectOnly(std::size_t index)
{
    if (index < m_items.size())
    {
        m_selection.Select(index);
        NotifyCounts();
        Invalidate();
    }
}

void FileView::SetSelected(std::size_t index, bool selected)
{
    if (index < m_items.size() && m_selection.IsSelected(index) != selected)
    {
        m_selection.Toggle(index);
        NotifyCounts();
        Invalidate();
    }
}

void FileView::SetScrollOffset(float offsetDip)
{
    const float before = m_scroll;
    m_scroll = offsetDip;
    ClampScroll();
    if (m_scroll != before)
    {
        Invalidate();
    }
}

void FileView::SortByColumn(Column column)
{
    SortBy(kFieldOf[static_cast<std::size_t>(column)]);
}

bool FileView::SelectForContextMenu(D2D1_POINT_2F point)
{
    if (!Contains(m_bounds, point) || Contains(HeaderRect(), point) || Contains(ScrollbarRect(), point))
    {
        return false;
    }
    SetFocused(true);
    if (const auto row = RowAt(point))
    {
        if (!m_selection.IsSelected(*row))
        {
            m_selection.Select(*row);
        }
        else
        {
            m_selection.Focus(*row);
        }
    }
    else
    {
        m_selection.Clear();
    }
    NotifyCounts();
    Invalidate();
    return true;
}

void FileView::RestoreSelection(const std::function<bool(const FileItem&)>& selected,
                                const std::function<bool(const FileItem&)>& focused)
{
    std::optional<std::size_t> focus;
    bool any = false;
    for (std::size_t i = 0; i < m_items.size(); ++i)
    {
        if (selected && selected(m_items[i]))
        {
            if (!any)
            {
                m_selection.Select(i); // replaces whatever the refresh left
                any = true;
            }
            else
            {
                m_selection.Toggle(i);
            }
        }
        if (!focus && focused && focused(m_items[i]))
        {
            focus = i;
        }
    }
    if (focus)
    {
        m_selection.Focus(*focus);
        ScrollIntoView(*focus);
    }
    NotifyCounts();
    Invalidate();
}

bool FileView::BeginRename()
{
    const auto focus = m_selection.FocusIndex();
    return focus && BeginRename(*focus);
}

bool FileView::BeginRename(std::size_t index)
{
    CancelRename(); // one at a time
    if (index >= m_items.size() || !m_items[index].info.canRename)
    {
        return false;
    }
    ScrollIntoView(index);
    const FileItem& item = m_items[index];
    const std::size_t key = item.key;
    const std::wstring& name = item.info.editName.empty() ? item.info.name : item.info.editName;
    RenameEdit::Callbacks callbacks;
    callbacks.commit = [this, key](const std::wstring& newName) {
        const auto it = m_indexOfKey.find(key);
        if (it != m_indexOfKey.end() && m_callbacks.rename)
        {
            m_callbacks.rename(m_items[it->second], newName);
        }
    };
    callbacks.ended = [this] {
        m_renameKey.reset();
        Invalidate();
    };
    if (!m_rename.Begin(name, item.info.isFolder, NameCellRect(index), std::move(callbacks)))
    {
        return false;
    }
    m_renameKey = key;
    Invalidate();
    return true;
}

void FileView::CancelRename()
{
    m_rename.Cancel();
    m_renameKey.reset();
}

void FileView::FollowRename()
{
    if (!m_renameKey)
    {
        return;
    }
    const auto it = m_indexOfKey.find(*m_renameKey);
    if (it == m_indexOfKey.end())
    {
        CancelRename();
        return;
    }
    const D2D1_RECT_F cell = NameCellRect(it->second);
    const float viewportTop = m_bounds.top + HeaderHeight();
    if (cell.top < viewportTop || cell.bottom > m_bounds.bottom)
    {
        CancelRename(); // scrolled out of view
        return;
    }
    m_rename.Move(cell);
}

void FileView::BeginLocation(Generation gen)
{
    CancelRename();
    m_gen = gen;
    m_iconGen = gen;
    m_items.clear();
    m_hidden.clear();
    m_filter.clear(); // a new folder starts unfiltered (T092)
    m_indexOfKey.clear();
    m_layouts.clear();
    m_selection.Reset(0);
    m_scroll = 0.0f;
    NotifyCounts();
    Invalidate();
}

void FileView::AppendItems(Generation gen, std::vector<FileItem>&& items)
{
    if (gen != m_gen)
    {
        return; // a batch for a folder the user already left
    }
    const std::size_t sortedCount = m_items.size();
    const std::size_t hiddenCount = m_hidden.size();
    m_items.reserve(m_items.size() + items.size());
    for (FileItem& item : items)
    {
        item.key = m_nextKey++;
        item.generation = gen;
        item.icon = {};
        // With a filter, the items it hides wait aside (T092).
        (Passes(item.info) ? m_items : m_hidden).push_back(std::move(item));
    }
    if (m_hidden.size() > hiddenCount)
    {
        SortModel::MergeBatch(m_hidden, hiddenCount, m_sort);
    }
    m_selection.Grow(m_items.size());
    // Each batch is sorted on its own and merged into the sorted list (T086); the
    // selection and focus follow their items through the permutation.
    m_selection.ApplyPermutation(SortModel::MergeBatch(m_items, sortedCount, m_sort));
    RebuildKeyIndex();
    FollowRename();
    NotifyCounts();
    Invalidate();
}

void FileView::SetIcon(Generation gen, std::size_t itemKey, wil::com_ptr<ID2D1Bitmap1> bitmap)
{
    if (gen != m_iconGen)
    {
        return;
    }
    const auto it = m_indexOfKey.find(itemKey);
    if (it == m_indexOfKey.end())
    {
        return;
    }
    IconSlot& icon = m_items[it->second].icon;
    icon.state = bitmap ? IconSlot::State::Ready : IconSlot::State::Failed;
    icon.bitmap = std::move(bitmap);
    icon.forceExtract = false;
    Invalidate();
}

void FileView::ApplyRename(std::size_t itemKey, std::wstring newName)
{
    std::wstring realName = newName;
    ApplyRename(itemKey, std::move(newName), std::move(realName));
}

void FileView::ApplyRename(std::size_t itemKey, std::wstring displayName, std::wstring realName)
{
    const auto it = m_indexOfKey.find(itemKey);
    if (it == m_indexOfKey.end())
    {
        return;
    }
    ShellItemInfo& info = m_items[it->second].info;
    info.editName = std::move(realName);
    info.name = std::move(displayName);
    m_layouts.erase(itemKey); // its name changed
    Resort();
    Invalidate();
}

void FileView::SetSort(SortState state)
{
    m_sort = state;
    Resort();
    if (const auto focus = m_selection.FocusIndex())
    {
        ScrollIntoView(*focus);
    }
    Invalidate();
}

void FileView::SortBy(SortField field)
{
    SortState next{field, SortDirection::Ascending};
    if (m_sort.field == field)
    {
        next.direction = m_sort.direction == SortDirection::Ascending ? SortDirection::Descending
                                                                      : SortDirection::Ascending;
    }
    SetSort(next);
}

std::vector<const FileItem*> FileView::Selection() const
{
    std::vector<const FileItem*> selected;
    for (const std::size_t index : m_selection.SelectedIndices())
    {
        selected.push_back(&m_items[index]);
    }
    return selected;
}

bool FileView::Passes(const ShellItemInfo& info) const
{
    // The name as shown, or the real one: with known extensions hidden, "report.txt" shows
    // as "report", and ".txt" still finds it, as Explorer's search does.
    return Matches(info.name, m_filter) || (!info.editName.empty() && Matches(info.editName, m_filter));
}

bool FileView::Matches(const std::wstring& name, const std::wstring& filter)
{
    if (filter.empty())
    {
        return true;
    }
    // A substring anywhere in the name, ignoring case the linguistic way (T092).
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE, name.c_str(),
                           static_cast<int>(name.size()), filter.c_str(), static_cast<int>(filter.size()),
                           nullptr, nullptr, nullptr, 0) >= 0;
}

void FileView::SetFilter(std::wstring text)
{
    if (text == m_filter)
    {
        return;
    }
    CancelRename();
    // The selection and focus, by item, to find them again after the change.
    std::unordered_set<std::size_t> selected;
    for (const std::size_t index : m_selection.SelectedIndices())
    {
        selected.insert(m_items[index].key);
    }
    const auto focusIndex = m_selection.FocusIndex();
    const std::optional<std::size_t> focusKey =
        focusIndex && *focusIndex < m_items.size() ? std::optional(m_items[*focusIndex].key) : std::nullopt;

    // Both lists are sorted: merged back into one, then split by the new filter, in order.
    std::vector<FileItem> all;
    all.reserve(m_items.size() + m_hidden.size());
    std::merge(std::make_move_iterator(m_items.begin()), std::make_move_iterator(m_items.end()),
               std::make_move_iterator(m_hidden.begin()), std::make_move_iterator(m_hidden.end()),
               std::back_inserter(all), [this](const FileItem& a, const FileItem& b) {
                   return SortModel::Less(a.info, b.info, m_sort);
               });
    m_items.clear();
    m_hidden.clear();
    m_filter = std::move(text);
    for (FileItem& item : all)
    {
        if (Passes(item.info))
        {
            m_items.push_back(std::move(item));
        }
        else
        {
            if (item.icon.state == IconSlot::State::Pending)
            {
                item.icon.state = IconSlot::State::NotRequested; // asked again when it shows
            }
            m_hidden.push_back(std::move(item));
        }
    }
    m_selection.Reset(m_items.size());
    RebuildKeyIndex();
    m_scroll = 0.0f; // the list starts from the top, as a new result does
    RestoreSelection([&](const FileItem& item) { return selected.contains(item.key); },
                     [&](const FileItem& item) { return focusKey && item.key == *focusKey; });
    ClampScroll();
    NotifyCounts();
    Invalidate();
}

void FileView::Resort()
{
    SortModel::Sort(m_hidden, m_sort); // the hidden ones keep the same order (T092)
    const std::vector<std::size_t> oldIndexAt = SortModel::Sort(m_items, m_sort);
    m_selection.ApplyPermutation(oldIndexAt);
    RebuildKeyIndex();
    FollowRename();
}

void FileView::RebuildKeyIndex()
{
    m_indexOfKey.clear();
    m_indexOfKey.reserve(m_items.size());
    for (std::size_t i = 0; i < m_items.size(); ++i)
    {
        m_indexOfKey.emplace(m_items[i].key, i);
    }
}

void FileView::NotifyCounts() const
{
    if (m_callbacks.counts)
    {
        m_callbacks.counts(m_items.size(), m_selection.SelectedCount());
    }
}

void FileView::Invalidate() const
{
    if (m_callbacks.invalidate)
    {
        m_callbacks.invalidate();
    }
}

void FileView::ResetIcons(Generation iconGen, bool keepBitmaps)
{
    m_iconGen = iconGen;
    for (FileItem& item : m_items)
    {
        item.icon.state = IconSlot::State::NotRequested; // the bitmap stays until replaced
        if (!keepBitmaps)
        {
            item.icon.bitmap.reset();
            item.icon.forceExtract = true; // the worker's "already sent" no longer holds
        }
    }
    Invalidate();
}

void FileView::RequestIconAgain(Generation gen, std::size_t itemKey)
{
    if (gen != m_iconGen)
    {
        return;
    }
    if (const auto it = m_indexOfKey.find(itemKey); it != m_indexOfKey.end())
    {
        IconSlot& icon = m_items[it->second].icon;
        icon.state = IconSlot::State::NotRequested;
        icon.forceExtract = true;
        Invalidate();
    }
}

void FileView::ForEachVisibleWithoutIcon(const std::function<void(const FileItem&)>& request)
{
    if (m_items.empty())
    {
        return;
    }
    const auto first = static_cast<std::size_t>(std::floor(m_scroll / RowHeight()));
    const auto last = std::min(
        m_items.size(), static_cast<std::size_t>(std::ceil((m_scroll + ViewportHeight()) / RowHeight())));
    const std::size_t ahead = std::min(m_items.size(), last + RowsPerPage()); // one screen ahead
    const auto visit = [&](std::size_t i) {
        if (m_items[i].icon.state == IconSlot::State::NotRequested)
        {
            m_items[i].icon.state = IconSlot::State::Pending;
            request(m_items[i]);
        }
    };
    // Requested in reverse priority: the LIFO worker serves the last request first.
    for (std::size_t i = ahead; i > last; --i)
    {
        visit(i - 1);
    }
    for (std::size_t i = last; i > first; --i)
    {
        visit(i - 1);
    }
}

HRESULT FileView::IconFromHBitmap(ID2D1DeviceContext* dc, HBITMAP bitmap, ID2D1Bitmap1** result)
{
    RETURN_HR_IF(E_POINTER, result == nullptr);
    *result = nullptr;
    RETURN_HR_IF(E_INVALIDARG, dc == nullptr || bitmap == nullptr);
    wil::com_ptr<IWICImagingFactory> wic;
    RETURN_IF_FAILED(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
    // IShellItemImageFactory returns 32-bpp bitmaps with premultiplied alpha (R-06).
    wil::com_ptr<IWICBitmap> wicBitmap;
    RETURN_IF_FAILED(
        wic->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha, &wicBitmap));
    return dc->CreateBitmapFromWicBitmap(wicBitmap.get(), nullptr, result);
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void FileView::SetBounds(const D2D1_RECT_F& bounds)
{
    if (bounds.right - bounds.left != m_bounds.right - m_bounds.left ||
        bounds.bottom - bounds.top != m_bounds.bottom - m_bounds.top)
    {
        InvalidateLayouts(); // a resize
    }
    m_bounds = bounds;
    ClampScroll();
}

float FileView::ViewportHeight() const noexcept
{
    return std::max(0.0f, m_bounds.bottom - m_bounds.top - HeaderHeight());
}

float FileView::ContentHeight() const noexcept
{
    return static_cast<float>(m_items.size()) * RowHeight();
}

void FileView::ClampScroll()
{
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, ContentHeight() - ViewportHeight()));
    FollowRename();
}

D2D1_RECT_F FileView::HeaderRect() const noexcept
{
    return D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right,
                       std::min(m_bounds.bottom, m_bounds.top + HeaderHeight()));
}

D2D1_RECT_F FileView::ScrollbarRect() const noexcept
{
    return D2D1::RectF(m_bounds.right - kScrollbarWidthDip, m_bounds.top + HeaderHeight(), m_bounds.right,
                       m_bounds.bottom);
}

std::optional<D2D1_RECT_F> FileView::ThumbRect() const noexcept
{
    const float content = ContentHeight();
    const float viewport = ViewportHeight();
    if (content <= viewport || viewport <= 0.0f)
    {
        return std::nullopt;
    }
    const D2D1_RECT_F track = ScrollbarRect();
    const float trackHeight = track.bottom - track.top;
    const float thumbHeight = std::max(24.0f, trackHeight * viewport / content);
    const float top = track.top + (trackHeight - thumbHeight) * (m_scroll / (content - viewport));
    return D2D1::RectF(track.left, top, track.right, top + thumbHeight);
}

D2D1_RECT_F FileView::RowRect(std::size_t index) const
{
    const float top = m_bounds.top + HeaderHeight() + static_cast<float>(index) * RowHeight() - m_scroll;
    return D2D1::RectF(m_bounds.left, top, m_bounds.right - kScrollbarWidthDip, top + RowHeight());
}

std::optional<std::size_t> FileView::RowAt(D2D1_POINT_2F point) const
{
    if (!Contains(m_bounds, point) || point.y < m_bounds.top + HeaderHeight() ||
        point.x >= m_bounds.right - kScrollbarWidthDip)
    {
        return std::nullopt;
    }
    const float offset = point.y - (m_bounds.top + HeaderHeight()) + m_scroll;
    const auto index = static_cast<std::size_t>(offset / RowHeight());
    return index < m_items.size() ? std::optional<std::size_t>(index) : std::nullopt;
}

float FileView::ColumnLeft(Column column) const noexcept
{
    float left = m_bounds.left;
    for (std::size_t i = 0; i < static_cast<std::size_t>(column); ++i)
    {
        left += m_widths[i];
    }
    return left;
}

std::optional<FileView::Column> FileView::DividerAt(D2D1_POINT_2F point) const
{
    if (!Contains(HeaderRect(), point))
    {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < m_widths.size(); ++i)
    {
        const auto column = static_cast<Column>(i);
        const float edge = ColumnLeft(column) + m_widths[i];
        if (std::fabs(point.x - edge) <= kDividerGripDip)
        {
            return column;
        }
    }
    return std::nullopt;
}

std::optional<FileView::Column> FileView::HeaderColumnAt(D2D1_POINT_2F point) const
{
    if (!Contains(HeaderRect(), point))
    {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < m_widths.size(); ++i)
    {
        const float left = ColumnLeft(static_cast<Column>(i));
        if (point.x >= left && point.x < left + m_widths[i])
        {
            return static_cast<Column>(i);
        }
    }
    return std::nullopt;
}

std::size_t FileView::RowsPerPage() const noexcept
{
    return std::max<std::size_t>(1, static_cast<std::size_t>(ViewportHeight() / RowHeight()));
}

void FileView::ScrollIntoView(std::size_t index)
{
    const float top = static_cast<float>(index) * RowHeight();
    if (top < m_scroll)
    {
        m_scroll = top;
    }
    else if (top + RowHeight() > m_scroll + ViewportHeight())
    {
        m_scroll = top + RowHeight() - ViewportHeight();
    }
    ClampScroll();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void FileView::SetFocusVisible(bool visible)
{
    if (visible != m_focusVisible)
    {
        m_focusVisible = visible;
        Invalidate();
    }
}

void FileView::SetFocused(bool focused)
{
    if (focused != m_focused)
    {
        m_focused = focused;
        Invalidate();
    }
}

bool FileView::OnPointerDown(D2D1_POINT_2F point, bool ctrl, bool shift)
{
    if (!Contains(m_bounds, point))
    {
        return false;
    }
    SetFocused(true);

    if (const auto divider = DividerAt(point))
    {
        m_drag = Drag::Divider;
        m_dragColumn = *divider;
        m_dragOrigin = point.x;
        m_dragStartValue = ColumnWidth(*divider);
        return true;
    }
    if (const auto column = HeaderColumnAt(point))
    {
        SortBy(kFieldOf[static_cast<std::size_t>(*column)]);
        return true;
    }
    if (Contains(ScrollbarRect(), point))
    {
        if (const auto thumb = ThumbRect(); thumb && Contains(*thumb, point))
        {
            m_drag = Drag::Thumb;
            m_dragOrigin = point.y;
            m_dragStartValue = m_scroll;
        }
        else if (thumb)
        {
            // Click on the track: one page towards the pointer.
            m_scroll += (point.y < thumb->top ? -1.0f : 1.0f) * ViewportHeight();
            ClampScroll();
            Invalidate();
        }
        return true;
    }

    if (const auto row = RowAt(point))
    {
        if (ctrl)
        {
            m_selection.Toggle(*row);
        }
        else if (shift)
        {
            m_selection.ExtendTo(*row);
        }
        else
        {
            m_selection.Select(*row);
        }
    }
    else if (!ctrl)
    {
        m_selection.Clear(); // a click on empty space deselects, as in Explorer
    }
    NotifyCounts();
    Invalidate();
    return true;
}

bool FileView::OnPointerMove(D2D1_POINT_2F point)
{
    switch (m_drag)
    {
    case Drag::Divider:
        InvalidateLayouts();
        m_widths[static_cast<std::size_t>(m_dragColumn)] =
            std::max(kMinColumnWidthDip, m_dragStartValue + (point.x - m_dragOrigin));
        Invalidate();
        return true;
    case Drag::Thumb:
        if (const auto thumb = ThumbRect())
        {
            const D2D1_RECT_F track = ScrollbarRect();
            const float travel = (track.bottom - track.top) - (thumb->bottom - thumb->top);
            const float range = ContentHeight() - ViewportHeight();
            if (travel > 0.0f)
            {
                m_scroll = m_dragStartValue + (point.y - m_dragOrigin) * range / travel;
                ClampScroll();
                Invalidate();
            }
        }
        return true;
    case Drag::None:
        break;
    }
    const auto thumb = ThumbRect();
    const bool hot = thumb && Contains(*thumb, point);
    if (hot != m_thumbHot)
    {
        m_thumbHot = hot;
        Invalidate();
    }
    return false;
}

bool FileView::OnPointerUp(D2D1_POINT_2F)
{
    if (m_drag == Drag::None)
    {
        return false;
    }
    m_drag = Drag::None;
    return true;
}

bool FileView::OnDoubleClick(D2D1_POINT_2F point)
{
    const auto row = RowAt(point);
    if (!row)
    {
        return false;
    }
    m_selection.Select(*row);
    NotifyCounts();
    Invalidate();
    if (m_callbacks.open)
    {
        m_callbacks.open(m_items[*row]);
    }
    return true;
}

bool FileView::OnWheel(int wheelDelta)
{
    const float rows = static_cast<float>(wheelDelta) / WHEEL_DELTA * kWheelRows;
    const float before = m_scroll;
    m_scroll -= rows * RowHeight();
    ClampScroll();
    if (m_scroll != before)
    {
        Invalidate();
    }
    return true;
}

void FileView::MoveFocus(std::size_t index, bool ctrl, bool shift)
{
    if (shift)
    {
        m_selection.ExtendTo(index);
    }
    else if (ctrl)
    {
        m_selection.Focus(index); // Ctrl+arrow: move focus, keep the selection
    }
    else
    {
        m_selection.Select(index);
    }
    ScrollIntoView(index);
    NotifyCounts();
    Invalidate();
}

bool FileView::OnKeyDown(UINT vk, bool ctrl, bool shift)
{
    if (vk == VK_F2 && !ctrl && !shift)
    {
        return BeginRename(); // UI §4: inline rename
    }
    // Ctrl+Shift+1..4: sort by Name, Date modified, Type, Size (UI §4).
    if (ctrl && shift && vk >= '1' && vk <= '4')
    {
        SortBy(kFieldOf[vk - '1']);
        return true;
    }
    if (m_items.empty())
    {
        return false;
    }
    const std::size_t last = m_items.size() - 1;
    const std::optional<std::size_t> focus = m_selection.FocusIndex();
    const std::size_t current = focus.value_or(0);
    const std::size_t page = RowsPerPage();

    switch (vk)
    {
    case VK_UP:
        MoveFocus(focus ? (current > 0 ? current - 1 : 0) : 0, ctrl, shift);
        return true;
    case VK_DOWN:
        MoveFocus(focus ? std::min(current + 1, last) : 0, ctrl, shift);
        return true;
    case VK_HOME:
        MoveFocus(0, ctrl, shift);
        return true;
    case VK_END:
        MoveFocus(last, ctrl, shift);
        return true;
    case VK_PRIOR:
        MoveFocus(current > page ? current - page : 0, ctrl, shift);
        return true;
    case VK_NEXT:
        MoveFocus(std::min(current + page, last), ctrl, shift);
        return true;
    case VK_SPACE:
        if (ctrl)
        {
            m_selection.Toggle(current);
            NotifyCounts();
            Invalidate();
            return true;
        }
        return false;
    case 'A':
        if (ctrl)
        {
            m_selection.SelectAll();
            NotifyCounts();
            Invalidate();
            return true;
        }
        return false;
    case VK_RETURN:
        if (focus && m_callbacks.open)
        {
            m_callbacks.open(m_items[*focus]);
        }
        return focus.has_value();
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void FileView::DrawText(ID2D1DeviceContext* dc, IDWriteTextFormat* format, const std::wstring& text,
                        const D2D1_RECT_F& rect, DWRITE_TEXT_ALIGNMENT alignment, ID2D1Brush* brush,
                        const EffectiveAppearance& effective)
{
    if (text.empty() || rect.right <= rect.left)
    {
        return;
    }
    format->SetTextAlignment(alignment);
    // Color fonts: emoji in file names draw in color, as in Explorer (V-3h).
    TextHalo::DrawTextW(dc, text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, effective);
}

FileView::RowLayouts& FileView::LayoutsFor(const FileItem& item, IDWriteTextFormat* body,
                                           std::unordered_map<std::size_t, RowLayouts>& frame)
{
    RowLayouts& layouts = frame[item.key];
    if (layouts.epoch == m_layoutEpoch)
    {
        return layouts; // already built this frame
    }
    if (const auto cached = m_layouts.find(item.key);
        cached != m_layouts.end() && cached->second.epoch == m_layoutEpoch)
    {
        layouts = std::move(cached->second);
        return layouts;
    }
    layouts = {};
    layouts.epoch = m_layoutEpoch;
    IDWriteFactory3* factory = m_formats ? m_formats->Factory() : nullptr;
    if (!factory)
    {
        return layouts;
    }
    const std::wstring texts[] = {item.info.name, FileItemFormat::Modified(item.info.modified),
                                  item.info.typeText, FileItemFormat::Size(item.info.size)};
    for (std::size_t i = 0; i < layouts.cells.size(); ++i)
    {
        const auto column = static_cast<Column>(i);
        float width = ColumnWidth(column) - 2.0f * kCellPaddingDip;
        if (column == Column::Name)
        {
            width = ColumnWidth(column) - 2.0f * kCellPaddingDip - kIconSizeDip - kIconGapDip;
        }
        wil::com_ptr<IDWriteTextLayout> layout;
        if (SUCCEEDED_LOG(factory->CreateTextLayout(texts[i].c_str(), static_cast<UINT32>(texts[i].size()),
                                                    body, std::max(0.0f, width), RowHeight(), &layout)))
        {
            layout->SetTextAlignment(column == Column::Size ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                            : DWRITE_TEXT_ALIGNMENT_LEADING);
            layouts.cells[i] = std::move(layout);
            ++m_layoutsCreated;
        }
    }
    return layouts;
}

void FileView::Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    m_rename.ApplyAppearance(effective); // colour key and text colour of the rename edit
    IDWriteTextFormat* body = m_formats ? m_formats->Body() : nullptr;
    IDWriteTextFormat* header = m_formats ? m_formats->Header() : nullptr;
    if (!dc || !body || !header || m_bounds.right <= m_bounds.left || m_bounds.bottom <= m_bounds.top)
    {
        return;
    }

    // Long names end in an ellipsis; the sign belongs to one format, so it is remade
    // when the formats are rebuilt (text scale).
    if (m_ellipsisFormat != body && m_formats->Factory())
    {
        InvalidateLayouts(); // layouts made with the previous format
        m_ellipsis.reset();
        LOG_IF_FAILED(m_formats->Factory()->CreateEllipsisTrimmingSign(body, &m_ellipsis));
        m_ellipsisFormat = body;
    }
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    body->SetTrimming(&trimming, m_ellipsis.get());
    for (IDWriteTextFormat* format : {body, header})
    {
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(dc->CreateSolidColorBrush(ToColor(effective.text), &brush)))
    {
        return;
    }
    dc->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_ALIASED);

    // Header: legibility floor, labels, sort arrow, dividers.
    const D2D1_RECT_F headerRect = HeaderRect();
    SurfacePainter::PaintTextScrim(dc, headerRect, effective);
    const std::wstring* labels[] = {&m_strings.name, &m_strings.dateModified, &m_strings.type,
                                    &m_strings.size};
    for (std::size_t i = 0; i < m_widths.size(); ++i)
    {
        const auto column = static_cast<Column>(i);
        const float left = ColumnLeft(column);
        const float right = left + m_widths[i];
        const D2D1_RECT_F cell =
            D2D1::RectF(left + kCellPaddingDip, headerRect.top, right - kCellPaddingDip, headerRect.bottom);
        brush->SetColor(ToColor(effective.secondaryText));
        const bool sizeColumn = column == Column::Size;
        DrawText(dc, header, *labels[i], cell,
                 sizeColumn ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING, brush.get(),
                 effective);
        if (kFieldOf[i] == m_sort.field)
        {
            const std::wstring arrow = m_sort.direction == SortDirection::Ascending ? L"▴" : L"▾";
            const D2D1_RECT_F arrowRect = D2D1::RectF(left, headerRect.top, right, headerRect.top + 10.0f);
            DrawText(dc, header, arrow, arrowRect, DWRITE_TEXT_ALIGNMENT_CENTER, brush.get(), effective);
        }
        brush->SetColor(ToColor(effective.secondaryText, 0.3f));
        dc->FillRectangle(D2D1::RectF(right - 1.0f, headerRect.top + 6.0f, right, headerRect.bottom - 6.0f),
                          brush.get());
    }
    brush->SetColor(ToColor(effective.secondaryText, 0.3f));
    dc->FillRectangle(D2D1::RectF(m_bounds.left, headerRect.bottom - 1.0f, m_bounds.right, headerRect.bottom),
                      brush.get());

    // Rows.
    const D2D1_RECT_F rowsArea =
        D2D1::RectF(m_bounds.left, headerRect.bottom, m_bounds.right, m_bounds.bottom);
    dc->PushAxisAlignedClip(rowsArea, D2D1_ANTIALIAS_MODE_ALIASED);
    if (!m_items.empty())
    {
        const auto first = static_cast<std::size_t>(std::floor(m_scroll / RowHeight()));
        const auto end =
            std::min(m_items.size(),
                     static_cast<std::size_t>(std::ceil((m_scroll + ViewportHeight()) / RowHeight())) + 1);
        const std::optional<std::size_t> focus = m_selection.FocusIndex();
        // Only the rows on screen are laid out and drawn; their text layouts are kept for
        // the next frame, and those of rows no longer on screen are dropped (T086).
        std::unordered_map<std::size_t, RowLayouts> frame;
        frame.reserve(end > first ? end - first : 0);
        for (std::size_t i = first; i < end; ++i)
        {
            const FileItem& item = m_items[i];
            RowLayouts& layouts = LayoutsFor(item, body, frame);
            constexpr D2D1_DRAW_TEXT_OPTIONS textOptions =
                D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;
            const auto drawCell = [&](Column column, float left) {
                if (const auto& layout = layouts.cells[static_cast<std::size_t>(column)])
                {
                    TextHalo::DrawTextLayout(dc, D2D1::Point2F(left, RowRect(i).top), layout.get(),
                                             brush.get(), textOptions, effective);
                }
            };
            const D2D1_RECT_F row = RowRect(i);
            // Floor first, then the selection fill, then content (R-05 layer order).
            SurfacePainter::PaintTextScrim(dc, row, effective);
            const bool selected = m_selection.IsSelected(i);
            if (selected)
            {
                brush->SetColor(ToColor(effective.selection, effective.selectionAlpha));
                dc->FillRectangle(row, brush.get());
            }
            const Rgb primary = selected ? effective.selectedText : effective.text;
            const Rgb secondary = selected ? effective.selectedText : effective.secondaryText;

            // Name column: icon, then the name.
            const float nameLeft = ColumnLeft(Column::Name);
            const float iconLeft = nameLeft + kCellPaddingDip;
            const float middle = (row.top + row.bottom) / 2.0f;
            if (item.icon.bitmap) // also a previous DPI's bitmap while its successor loads
            {
                dc->DrawBitmap(item.icon.bitmap.get(),
                               D2D1::RectF(iconLeft, middle - kIconSizeDip / 2.0f, iconLeft + kIconSizeDip,
                                           middle + kIconSizeDip / 2.0f),
                               1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            }
            const float textLeft = iconLeft + kIconSizeDip + kIconGapDip;
            brush->SetColor(ToColor(primary));
            // While it is renamed, the edit shows the name instead.
            if (!m_renameKey || *m_renameKey != item.key)
            {
                drawCell(Column::Name, textLeft);
            }

            brush->SetColor(ToColor(secondary));
            for (const Column column : {Column::DateModified, Column::Type, Column::Size})
            {
                drawCell(column, ColumnLeft(column) + kCellPaddingDip);
            }

            if (m_focused && m_focusVisible && focus == i)
            {
                // 3:1 against the row, also when it is selected (T080).
                brush->SetColor(ToColor(FocusIndicator::ColorOver(effective, m_selection.IsSelected(i))));
                const float inset = kFocusWidthDip / 2.0f;
                dc->DrawRectangle(
                    D2D1::RectF(row.left + inset, row.top + inset, row.right - inset, row.bottom - inset),
                    brush.get(), kFocusWidthDip);
            }
        }
        m_layouts = std::move(frame);
    }
    else
    {
        m_layouts.clear();
    }
    dc->PopAxisAlignedClip();

    // Scrollbar: a thin translucent thumb, wider while hovered or dragged.
    if (const auto thumb = ThumbRect())
    {
        const bool active = m_thumbHot || m_drag == Drag::Thumb;
        const float width = active ? kScrollbarWidthDip - 2.0f : 4.0f;
        const float center = (thumb->left + thumb->right) / 2.0f;
        brush->SetColor(ToColor(effective.text, active ? 0.55f : 0.35f));
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(center - width / 2.0f, thumb->top + 2.0f,
                                                               center + width / 2.0f, thumb->bottom - 2.0f),
                                                   width / 2.0f, width / 2.0f),
                                 brush.get());
    }

    dc->PopAxisAlignedClip();
    body->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    header->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_rename.Render(dc, effective); // the rename text in Transparent (EditHalo)
}

IRawElementProviderFragment* FileView::Automation()
{
    return nullptr; // T078
}

} // namespace te
