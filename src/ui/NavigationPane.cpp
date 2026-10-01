#include <te/ui/NavigationPane.h>

#include <te/render/FocusIndicator.h>
#include <te/render/SurfacePainter.h>
#include <te/render/TextHalo.h>
#include <te/ui/FileView.h>

#include <dbt.h>
#include <shlobj.h>

#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace te
{

namespace
{

// Quick locations, in Explorer's order (UI contract §1).
const KNOWNFOLDERID* const kKnownFolders[] = {
    &FOLDERID_ComputerFolder, &FOLDERID_Desktop, &FOLDERID_Documents, &FOLDERID_Downloads,
    &FOLDERID_Pictures,       &FOLDERID_Music,   &FOLDERID_Videos,
};

D2D1_COLOR_F ToColor(Rgb rgb, float alpha = 1.0f)
{
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
}

bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

std::optional<ShellLocation> FromIdList(PCIDLIST_ABSOLUTE pidl)
{
    ShellLocation location;
    if (pidl && SUCCEEDED(ShellLocation::FromIdList(pidl, &location)))
    {
        return location;
    }
    return std::nullopt;
}

} // namespace

void NavigationPane::Attach(HWND parent, UINT dpi) noexcept
{
    m_parent = parent;
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
}

void NavigationPane::SetDpi(UINT dpi)
{
    m_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    ExtractIcons();
    Invalidate();
}

void NavigationPane::ReleaseDeviceResources() noexcept
{
    for (Entry& entry : m_entries)
    {
        entry.icon.reset();
    }
    for (auto& [id, rows] : m_collapsed)
    {
        for (Entry& entry : rows)
        {
            entry.icon.reset();
        }
    }
    m_iconDevice.reset();
}

void NavigationPane::CancelLoads()
{
    m_loader.CancelAll();
}

void NavigationPane::ShutdownLoader()
{
    m_loader.Shutdown();
}

void NavigationPane::SetTextScale(float scale)
{
    scale = std::max(1.0f, scale);
    if (scale == m_textScale)
    {
        return;
    }
    m_scroll *= scale / m_textScale; // the same row stays at the top
    m_textScale = scale;
    ClampScroll();
    Invalidate();
}

void NavigationPane::SetTextFormats(const TextFormats* formats) noexcept
{
    m_formats = formats;
    m_ellipsis.reset();
    m_ellipsisFormat = nullptr;
}

void NavigationPane::SetNavigateCallback(std::function<void(const ShellLocation&)> callback)
{
    m_navigate = std::move(callback);
}

void NavigationPane::SetTreeChangedCallback(std::function<void()> callback)
{
    m_treeChanged = std::move(callback);
}

void NavigationPane::Invalidate() const
{
    if (m_parent)
    {
        InvalidateRect(m_parent, nullptr, FALSE);
    }
}

int NavigationPane::IconPx() const noexcept
{
    return static_cast<int>(std::lround(kIconSizeDip * static_cast<float>(m_dpi) / USER_DEFAULT_SCREEN_DPI));
}

// ---------------------------------------------------------------------------
// Entries
// ---------------------------------------------------------------------------

void NavigationPane::Refresh()
{
    std::vector<Entry> entries;
    for (const KNOWNFOLDERID* id : kKnownFolders)
    {
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        // A folder that does not exist on this machine (e.g. redirected or removed) is skipped.
        const HRESULT hr = SHGetKnownFolderIDList(*id, KF_FLAG_DEFAULT, nullptr, wil::out_param(pidl));
        if (SUCCEEDED(hr))
        {
            if (auto location = FromIdList(pidl.get()))
            {
                Entry entry;
                entry.location = std::move(*location);
                entries.push_back(std::move(entry));
            }
        }
    }

    // Fixed and removable drives, in drive-letter order.
    wchar_t drives[4 * 26 + 1]{};
    const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    for (const wchar_t* root = drives; length > 0 && *root; root += wcslen(root) + 1)
    {
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
        {
            continue;
        }
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        const HRESULT hr = SHParseDisplayName(root, nullptr, wil::out_param(pidl), 0, nullptr);
        if (SUCCEEDED(hr))
        {
            if (auto location = FromIdList(pidl.get()))
            {
                Entry entry;
                entry.location = std::move(*location);
                entry.isDrive = true;
                entries.push_back(std::move(entry));
            }
        }
    }

    ResetPlaces(std::move(entries));
}

void NavigationPane::SetPlacesForTesting(std::vector<ShellLocation> places)
{
    std::vector<Entry> entries;
    for (ShellLocation& place : places)
    {
        Entry entry;
        entry.location = std::move(place);
        entries.push_back(std::move(entry));
    }
    ResetPlaces(std::move(entries));
}

void NavigationPane::ResetPlaces(std::vector<Entry> entries)
{
    // A new tree: loads still queued for the old one are dropped, and their results no
    // longer find their node.
    m_loader.CancelAll();
    m_collapsed.clear();
    for (Entry& entry : entries)
    {
        entry.id = m_nextId++;
    }
    m_entries = std::move(entries);
    UpdateFirstDrive();
    m_hot.reset();
    if (m_focus && *m_focus >= m_entries.size())
    {
        m_focus.reset();
    }
    ExtractIcons();
    if (m_currentLocation)
    {
        SetCurrent(*m_currentLocation);
    }
    ClampScroll();
    // The folders that were open are opened again (their subfolders load again).
    for (std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if (Remembered(m_entries[i].location))
        {
            Expand(i);
        }
    }
    TreeChanged();
}

void NavigationPane::ExtractIcons()
{
    // The places, on the UI thread (a handful); subfolders come with their icons from the
    // loader's worker.
    const int size = IconPx();
    for (Entry& entry : m_entries)
    {
        if (entry.depth > 0)
        {
            continue;
        }
        entry.icon.reset();
        entry.iconSource.reset();
        wil::com_ptr<IShellItem> item;
        if (FAILED(entry.location.Item(&item)))
        {
            continue;
        }
        if (auto factory = item.try_query<IShellItemImageFactory>())
        {
            HBITMAP bitmap = nullptr;
            if (SUCCEEDED(
                    factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)))
            {
                entry.iconSource.reset(bitmap);
            }
        }
    }
}

bool NavigationPane::OnDeviceChange(WPARAM event)
{
    if (event != DBT_DEVICEARRIVAL && event != DBT_DEVICEREMOVECOMPLETE)
    {
        return false;
    }
    Refresh();
    return true;
}

void NavigationPane::SetCurrent(const ShellLocation& location)
{
    m_currentLocation = location;
    std::optional<std::size_t> current;
    for (std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].location == location)
        {
            current = i;
            break;
        }
    }
    if (current != m_current)
    {
        m_current = current;
        Invalidate();
    }
}

void NavigationPane::Navigate(std::size_t index)
{
    if (index < m_entries.size() && m_navigate)
    {
        m_navigate(m_entries[index].location);
    }
}

// ---------------------------------------------------------------------------
// Tree (T090)
// ---------------------------------------------------------------------------

bool NavigationPane::Remembered(const ShellLocation& location) const
{
    return std::find(m_expandedLocations.begin(), m_expandedLocations.end(), location) !=
           m_expandedLocations.end();
}

void NavigationPane::Remember(const ShellLocation& location, bool expanded)
{
    const auto it = std::find(m_expandedLocations.begin(), m_expandedLocations.end(), location);
    if (expanded && it == m_expandedLocations.end())
    {
        m_expandedLocations.push_back(location);
    }
    else if (!expanded && it != m_expandedLocations.end())
    {
        m_expandedLocations.erase(it);
    }
}

std::size_t NavigationPane::SubtreeEnd(std::size_t index) const noexcept
{
    std::size_t end = index + 1;
    while (end < m_entries.size() && m_entries[end].depth > m_entries[index].depth)
    {
        ++end;
    }
    return end;
}

void NavigationPane::UpdateFirstDrive() noexcept
{
    m_firstDrive = SIZE_MAX;
    for (std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].depth == 0 && m_entries[i].isDrive)
        {
            m_firstDrive = i;
            return;
        }
    }
}

void NavigationPane::ShiftIndices(std::size_t at, std::ptrdiff_t count)
{
    const auto shift = [&](std::optional<std::size_t>& index) {
        if (!index || *index < at)
        {
            return;
        }
        if (count < 0 && *index < at + static_cast<std::size_t>(-count))
        {
            index = at > 0 ? std::optional<std::size_t>(at - 1) : std::nullopt; // inside the removed rows
            return;
        }
        index = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(*index) + count);
    };
    shift(m_focus);
    shift(m_hot);
    m_hot.reset(); // re-found on the next pointer move
    m_current.reset();
    if (m_currentLocation)
    {
        for (std::size_t i = 0; i < m_entries.size(); ++i)
        {
            if (m_entries[i].location == *m_currentLocation)
            {
                m_current = i;
                break;
            }
        }
    }
}

void NavigationPane::InsertRows(std::size_t at, std::vector<Entry>&& rows)
{
    if (rows.empty())
    {
        return;
    }
    const auto count = static_cast<std::ptrdiff_t>(rows.size());
    m_entries.insert(m_entries.begin() + static_cast<std::ptrdiff_t>(at),
                     std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    UpdateFirstDrive();
    ShiftIndices(at, count);
    ClampScroll();
}

void NavigationPane::Expand(std::size_t index)
{
    if (index >= m_entries.size())
    {
        return;
    }
    Entry& node = m_entries[index];
    if (!node.expandable || node.expanded)
    {
        return;
    }
    node.expanded = true;
    Remember(node.location, true);
    if (const auto kept = m_collapsed.find(node.id); kept != m_collapsed.end())
    {
        std::vector<Entry> rows = std::move(kept->second);
        m_collapsed.erase(kept);
        InsertRows(index + 1, std::move(rows));
        TreeChanged();
    }
    else if (!node.loading && m_parent)
    {
        node.loading = true;
        m_loader.Request(m_parent, node.id, node.location, IconPx());
    }
    Invalidate();
}

void NavigationPane::Collapse(std::size_t index)
{
    if (index >= m_entries.size() || !m_entries[index].expanded)
    {
        return;
    }
    Entry& node = m_entries[index];
    node.expanded = false;
    Remember(node.location, false);
    const std::size_t end = SubtreeEnd(index);
    if (end > index + 1)
    {
        // The rows below are kept, with their own expanded state, for the next expansion.
        std::vector<Entry> rows(
            std::make_move_iterator(m_entries.begin() + static_cast<std::ptrdiff_t>(index) + 1),
            std::make_move_iterator(m_entries.begin() + static_cast<std::ptrdiff_t>(end)));
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                        m_entries.begin() + static_cast<std::ptrdiff_t>(end));
        m_collapsed[node.id] = std::move(rows);
        UpdateFirstDrive();
        ShiftIndices(index + 1, -static_cast<std::ptrdiff_t>(end - index - 1));
        ClampScroll();
        TreeChanged();
    }
    Invalidate();
}

void NavigationPane::OnChildren(FolderChildren& result)
{
    const auto index = IndexOfNode(result.nodeId);
    Entry* node = index ? &m_entries[*index] : nullptr;
    if (!node)
    {
        // Inside a collapsed subtree, or from before a refresh.
        for (auto& [id, rows] : m_collapsed)
        {
            for (Entry& row : rows)
            {
                if (row.id == result.nodeId)
                {
                    node = &row;
                }
            }
        }
    }
    if (!node)
    {
        return;
    }
    node->loading = false;
    std::vector<Entry> rows;
    rows.reserve(result.children.size());
    for (FolderChildren::Child& child : result.children)
    {
        Entry row;
        row.location = std::move(child.location);
        row.iconSource = std::move(child.icon);
        row.id = m_nextId++;
        row.depth = node->depth + 1;
        row.expandable = child.hasSubfolders;
        rows.push_back(std::move(row));
    }
    if (rows.empty())
    {
        // No subfolders (or they could not be listed): the node is a leaf.
        node->expandable = false;
        node->expanded = false;
        Invalidate();
        return;
    }
    if (!index || !node->expanded)
    {
        m_collapsed[node->id] = std::move(rows); // shown when it is expanded
        node->expanded = false;
        return;
    }
    const std::size_t at = *index + 1;
    std::vector<std::uint64_t> reopen; // children that were open before (a refresh)
    for (const Entry& row : rows)
    {
        if (row.expandable && Remembered(row.location))
        {
            reopen.push_back(row.id);
        }
    }
    InsertRows(at, std::move(rows));
    for (const std::uint64_t id : reopen)
    {
        if (const auto child = IndexOfNode(id))
        {
            Expand(*child);
        }
    }
    TreeChanged();
    Invalidate();
}

void NavigationPane::TreeChanged()
{
    if (m_treeChanged)
    {
        m_treeChanged();
    }
}

std::optional<std::size_t> NavigationPane::IndexOfNode(std::uint64_t id) const noexcept
{
    for (std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].id == id)
        {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> NavigationPane::ParentOf(std::size_t index) const noexcept
{
    if (index >= m_entries.size() || m_entries[index].depth == 0)
    {
        return std::nullopt;
    }
    for (std::size_t i = index; i-- > 0;)
    {
        if (m_entries[i].depth < m_entries[index].depth)
        {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> NavigationPane::FirstChildOf(std::size_t index) const noexcept
{
    if (index + 1 < m_entries.size() && m_entries[index + 1].depth == m_entries[index].depth + 1)
    {
        return index + 1;
    }
    return std::nullopt;
}

std::optional<std::size_t> NavigationPane::LastChildOf(std::size_t index) const noexcept
{
    std::optional<std::size_t> last;
    for (auto child = FirstChildOf(index); child; child = NextSiblingOf(*child))
    {
        last = child;
    }
    return last;
}

std::optional<std::size_t> NavigationPane::NextSiblingOf(std::size_t index) const noexcept
{
    if (index >= m_entries.size())
    {
        return std::nullopt;
    }
    const std::size_t next = SubtreeEnd(index);
    if (next < m_entries.size() && m_entries[next].depth == m_entries[index].depth)
    {
        return next;
    }
    return std::nullopt;
}

std::optional<std::size_t> NavigationPane::PreviousSiblingOf(std::size_t index) const noexcept
{
    if (index >= m_entries.size())
    {
        return std::nullopt;
    }
    for (std::size_t i = index; i-- > 0;)
    {
        if (m_entries[i].depth == m_entries[index].depth)
        {
            return i;
        }
        if (m_entries[i].depth < m_entries[index].depth)
        {
            return std::nullopt; // reached the parent
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Geometry and input
// ---------------------------------------------------------------------------

void NavigationPane::SetBounds(const D2D1_RECT_F& bounds)
{
    m_bounds = bounds;
    ClampScroll();
}

D2D1_RECT_F NavigationPane::EntryRect(std::size_t index) const noexcept
{
    float top = m_bounds.top + static_cast<float>(index) * RowHeight() - m_scroll;
    if (index >= m_firstDrive)
    {
        top += kGroupGapDip; // the drives, and the rows under them
    }
    return D2D1::RectF(m_bounds.left, top, m_bounds.right, top + RowHeight());
}

D2D1_RECT_F NavigationPane::ExpanderRect(std::size_t index) const noexcept
{
    const D2D1_RECT_F row = EntryRect(index);
    const float depth = index < m_entries.size() ? static_cast<float>(m_entries[index].depth) : 0.0f;
    const float left = row.left + kPaddingDip / 3.0f + depth * kIndentDip;
    return D2D1::RectF(left, row.top, left + kExpanderDip, row.bottom);
}

std::optional<std::size_t> NavigationPane::EntryAt(D2D1_POINT_2F point) const noexcept
{
    if (!Contains(m_bounds, point) || m_entries.empty())
    {
        return std::nullopt;
    }
    // Rows are evenly spaced (the group gap aside): compute the candidate, then check it.
    const float offset = point.y - m_bounds.top + m_scroll;
    const auto guess = static_cast<std::size_t>(std::max(0.0f, offset / RowHeight()));
    for (std::size_t i = guess > 0 ? guess - 1 : 0; i < std::min(m_entries.size(), guess + 2); ++i)
    {
        if (Contains(EntryRect(i), point))
        {
            return i;
        }
    }
    return std::nullopt;
}

float NavigationPane::ContentHeight() const noexcept
{
    return static_cast<float>(m_entries.size()) * RowHeight() +
           (m_firstDrive != SIZE_MAX ? kGroupGapDip : 0.0f);
}

void NavigationPane::ClampScroll()
{
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, ContentHeight() - (m_bounds.bottom - m_bounds.top)));
}

void NavigationPane::EnsureVisible(std::size_t index)
{
    const D2D1_RECT_F r = EntryRect(index);
    if (r.top < m_bounds.top)
    {
        m_scroll -= m_bounds.top - r.top;
    }
    else if (r.bottom > m_bounds.bottom)
    {
        m_scroll += r.bottom - m_bounds.bottom;
    }
    ClampScroll();
}

void NavigationPane::SetFocusVisible(bool visible)
{
    if (visible != m_focusVisible)
    {
        m_focusVisible = visible;
        if (m_parent)
        {
            InvalidateRect(m_parent, nullptr, FALSE);
        }
    }
}

void NavigationPane::SetFocused(bool focused)
{
    if (focused != m_focused)
    {
        m_focused = focused;
        if (focused && !m_focus && !m_entries.empty())
        {
            m_focus = m_current.value_or(0);
        }
        Invalidate();
    }
}

bool NavigationPane::OnPointerDown(D2D1_POINT_2F point)
{
    if (!Contains(m_bounds, point))
    {
        return false;
    }
    SetFocused(true);
    if (const auto index = EntryAt(point))
    {
        m_focus = index;
        const Entry& entry = m_entries[*index];
        if (entry.expandable && Contains(ExpanderRect(*index), point))
        {
            entry.expanded ? Collapse(*index) : Expand(*index); // the chevron: no navigation
        }
        else
        {
            Navigate(*index);
        }
    }
    Invalidate();
    return true;
}

bool NavigationPane::OnPointerMove(D2D1_POINT_2F point)
{
    const auto hot = EntryAt(point);
    if (hot != m_hot)
    {
        m_hot = hot;
        Invalidate();
    }
    return hot.has_value();
}

void NavigationPane::OnPointerLeave()
{
    if (m_hot)
    {
        m_hot.reset();
        Invalidate();
    }
}

bool NavigationPane::OnWheel(int wheelDelta)
{
    const float before = m_scroll;
    m_scroll -= static_cast<float>(wheelDelta) / WHEEL_DELTA * kWheelRows * RowHeight();
    ClampScroll();
    if (m_scroll != before)
    {
        Invalidate();
    }
    return true;
}

bool NavigationPane::OnKeyDown(UINT vk)
{
    if (m_entries.empty())
    {
        return false;
    }
    const std::size_t last = m_entries.size() - 1;
    const std::size_t focus = m_focus.value_or(m_current.value_or(0));
    std::optional<std::size_t> next;
    switch (vk)
    {
    case VK_UP:
        next = m_focus ? (focus > 0 ? focus - 1 : 0) : focus;
        break;
    case VK_DOWN:
        next = m_focus ? std::min(focus + 1, last) : focus;
        break;
    case VK_HOME:
        next = 0;
        break;
    case VK_END:
        next = last;
        break;
    case VK_RIGHT:
        // As in Explorer's tree: expand; if already expanded, go to the first child.
        if (m_entries[focus].expandable && !m_entries[focus].expanded)
        {
            m_focus = focus;
            Expand(focus);
            return true;
        }
        next = FirstChildOf(focus).value_or(focus);
        break;
    case VK_LEFT:
        // Collapse; if already collapsed, go to the parent.
        if (m_entries[focus].expanded)
        {
            m_focus = focus;
            Collapse(focus);
            return true;
        }
        next = ParentOf(focus).value_or(focus);
        break;
    case VK_RETURN:
    case VK_SPACE:
        Navigate(focus);
        return true;
    default:
        return false;
    }
    m_focus = next;
    EnsureVisible(*next);
    Invalidate();
    return true;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void NavigationPane::Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective)
{
    IDWriteTextFormat* format = m_formats ? m_formats->Body() : nullptr;
    if (!dc || !format || m_bounds.right <= m_bounds.left || m_bounds.bottom <= m_bounds.top)
    {
        return;
    }
    // Icons belong to one Direct2D device: recreate them after a device change.
    wil::com_ptr<ID2D1Device> device;
    dc->GetDevice(device.put());
    if (device != m_iconDevice)
    {
        ReleaseDeviceResources();
        m_iconDevice = std::move(device);
    }
    if (m_ellipsisFormat != format && m_formats->Factory())
    {
        m_ellipsis.reset();
        LOG_IF_FAILED(m_formats->Factory()->CreateEllipsisTrimmingSign(format, &m_ellipsis));
        m_ellipsisFormat = format;
    }
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    format->SetTrimming(&trimming, m_ellipsis.get());
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(dc->CreateSolidColorBrush(ToColor(effective.text), &brush)))
    {
        return;
    }
    dc->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_ALIASED);
    for (std::size_t i = 0; i < m_entries.size(); ++i)
    {
        Entry& entry = m_entries[i];
        const D2D1_RECT_F row = EntryRect(i);
        if (row.bottom <= m_bounds.top || row.top >= m_bounds.bottom)
        {
            continue;
        }
        // Legibility floor first (R-05), then the current / hover fill, then content. The
        // tree's rows are drawn over the same translucent surface as the pane (FR-003).
        SurfacePainter::PaintTextScrim(dc, row, effective);
        const bool current = m_current == i;
        if (current)
        {
            brush->SetColor(ToColor(effective.selection, effective.selectionAlpha));
            dc->FillRectangle(row, brush.get());
        }
        else if (m_hot == i)
        {
            brush->SetColor(ToColor(effective.text, 0.06f));
            dc->FillRectangle(row, brush.get());
        }

        // The chevron: pointing right when collapsed, down when expanded (T090).
        const float middle = (row.top + row.bottom) / 2.0f;
        const D2D1_RECT_F expander = ExpanderRect(i);
        if (entry.expandable)
        {
            const float cx = (expander.left + expander.right) / 2.0f;
            constexpr float kHalf = 3.5f;
            brush->SetColor(ToColor(current ? effective.selectedText : effective.secondaryText,
                                    entry.loading ? 0.5f : 1.0f));
            const TextHalo::Line down[] = {
                {D2D1::Point2F(cx - kHalf, middle - kHalf / 2.0f), D2D1::Point2F(cx, middle + kHalf / 2.0f)},
                {D2D1::Point2F(cx, middle + kHalf / 2.0f), D2D1::Point2F(cx + kHalf, middle - kHalf / 2.0f)},
            };
            const TextHalo::Line right[] = {
                {D2D1::Point2F(cx - kHalf / 2.0f, middle - kHalf), D2D1::Point2F(cx + kHalf / 2.0f, middle)},
                {D2D1::Point2F(cx + kHalf / 2.0f, middle), D2D1::Point2F(cx - kHalf / 2.0f, middle + kHalf)},
            };
            TextHalo::DrawLines(dc, entry.expanded ? std::span<const TextHalo::Line>(down) : right,
                                brush.get(), 1.2f, effective);
        }

        const float iconLeft = expander.right + 2.0f;
        if (!entry.icon && entry.iconSource)
        {
            LOG_IF_FAILED(FileView::IconFromHBitmap(dc, entry.iconSource.get(), &entry.icon));
        }
        if (entry.icon)
        {
            dc->DrawBitmap(entry.icon.get(),
                           D2D1::RectF(iconLeft, middle - kIconSizeDip / 2.0f, iconLeft + kIconSizeDip,
                                       middle + kIconSizeDip / 2.0f),
                           1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        }
        const std::wstring& name = entry.location.DisplayName();
        brush->SetColor(ToColor(current ? effective.selectedText : effective.text));
        TextHalo::DrawTextW(
            dc, name.c_str(), static_cast<UINT32>(name.size()), format,
            D2D1::RectF(iconLeft + kIconSizeDip + kIconGapDip, row.top, row.right - kPaddingDip, row.bottom),
            brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, effective);

        if (m_focused && m_focusVisible && m_focus == i)
        {
            // 3:1 against the row, also on the current place's fill (T080).
            brush->SetColor(ToColor(FocusIndicator::ColorOver(effective, current)));
            const float inset = kFocusWidthDip / 2.0f;
            dc->DrawRectangle(
                D2D1::RectF(row.left + inset, row.top + inset, row.right - inset, row.bottom - inset),
                brush.get(), kFocusWidthDip);
        }
    }
    dc->PopAxisAlignedClip();
}

IRawElementProviderFragment* NavigationPane::Automation()
{
    return nullptr; // T078
}

} // namespace te
