#include <te/ui/SortModel.h>

#include <shlwapi.h>

#include <algorithm>
#include <iterator>
#include <numeric>

namespace te::SortModel
{

namespace
{

int Sign(int value)
{
    return (value > 0) - (value < 0);
}

// Natural order, as Explorer: "file2" < "file10", case-insensitive.
int CompareNames(const std::wstring& a, const std::wstring& b)
{
    return Sign(StrCmpLogicalW(a.c_str(), b.c_str()));
}

// -1 / 0 / +1 on the chosen field, in the chosen direction. A missing value is after a
// present one in both directions, so only present-vs-present results are flipped.
template <class T, class Compare>
int CompareOptional(const std::optional<T>& a, const std::optional<T>& b, Compare compare, bool descending)
{
    if (a && b)
    {
        const int result = compare(*a, *b);
        return descending ? -result : result;
    }
    if (a != std::nullopt || b != std::nullopt)
    {
        return a ? -1 : 1;
    }
    return 0;
}

int CompareField(const ShellItemInfo& a, const ShellItemInfo& b, const SortState& state)
{
    const bool descending = state.direction == SortDirection::Descending;
    switch (state.field)
    {
    case SortField::Name: {
        const int result = CompareNames(a.name, b.name);
        return descending ? -result : result;
    }
    case SortField::Type: {
        const int result = CompareNames(a.typeText, b.typeText);
        return descending ? -result : result;
    }
    case SortField::Size:
        return CompareOptional(
            a.size, b.size, [](std::uint64_t x, std::uint64_t y) { return (x > y) - (x < y); }, descending);
    case SortField::DateModified:
        return CompareOptional(
            a.modified, b.modified,
            [](const FILETIME& x, const FILETIME& y) { return Sign(CompareFileTime(&x, &y)); }, descending);
    }
    return 0;
}

} // namespace

bool Less(const ShellItemInfo& a, const ShellItemInfo& b, const SortState& state)
{
    // 1. Folders before files, in both directions.
    if (a.isFolder != b.isFolder)
    {
        return a.isFolder;
    }
    // 2. The chosen field (missing values last).
    if (const int result = CompareField(a, b, state); result != 0)
    {
        return result < 0;
    }
    // 3. Ties: name, ascending and natural, whatever the direction. (Already decided for
    //    the Name field, where equal names stay in their current order.)
    if (state.field != SortField::Name)
    {
        return CompareNames(a.name, b.name) < 0;
    }
    return false;
}

namespace
{

// Moves items into the order oldIndexAt describes (result[newIndex] = old index).
void Reorder(std::vector<FileItem>& items, const std::vector<std::size_t>& oldIndexAt)
{
    std::vector<FileItem> ordered;
    ordered.reserve(items.size());
    for (const std::size_t oldIndex : oldIndexAt)
    {
        ordered.push_back(std::move(items[oldIndex]));
    }
    items = std::move(ordered);
}

} // namespace

std::vector<std::size_t> MergeBatch(std::vector<FileItem>& items, std::size_t sortedCount,
                                    const SortState& state)
{
    sortedCount = std::min(sortedCount, items.size());
    const auto less = [&](std::size_t x, std::size_t y) { return Less(items[x].info, items[y].info, state); };

    std::vector<std::size_t> added(items.size() - sortedCount);
    std::iota(added.begin(), added.end(), sortedCount);
    std::stable_sort(added.begin(), added.end(), less);

    std::vector<std::size_t> existing(sortedCount);
    std::iota(existing.begin(), existing.end(), std::size_t{0});
    std::vector<std::size_t> oldIndexAt;
    oldIndexAt.reserve(items.size());
    // std::merge takes from the first range on ties, so existing items stay ahead of new
    // ones that compare equal, as a stable sort of the appended vector would keep them.
    std::merge(existing.begin(), existing.end(), added.begin(), added.end(), std::back_inserter(oldIndexAt),
               less);
    Reorder(items, oldIndexAt);
    return oldIndexAt;
}

std::vector<std::size_t> Sort(std::vector<FileItem>& items, const SortState& state)
{
    std::vector<std::size_t> oldIndexAt(items.size());
    std::iota(oldIndexAt.begin(), oldIndexAt.end(), std::size_t{0});
    std::stable_sort(oldIndexAt.begin(), oldIndexAt.end(),
                     [&](std::size_t x, std::size_t y) { return Less(items[x].info, items[y].info, state); });
    Reorder(items, oldIndexAt);
    return oldIndexAt;
}

} // namespace te::SortModel
