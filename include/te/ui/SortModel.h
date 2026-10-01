#pragma once

// File-list ordering (T055; data-model: SortState):
// 1. folders before files, in both directions, as in Explorer;
// 2. then the chosen field in the chosen direction (names with StrCmpLogicalW, so
//    "file2" sorts before "file10");
// 3. items missing the field's value (no size, no date) after those that have it, in
//    both directions;
// 4. ties broken by name, ascending, with StrCmpLogicalW; the sort is stable.

#include <te/shell/ShellTypes.h>
#include <te/ui/FileItem.h>
#include <te/ui/SortTypes.h>

#include <cstddef>
#include <vector>

namespace te::SortModel
{

// Strict weak ordering: true if `a` is listed before `b`.
bool Less(const ShellItemInfo& a, const ShellItemInfo& b, const SortState& state);

// Stable sort of `items` in place. Returns the permutation used: result[newIndex] is
// the item's old index, for SelectionModel::ApplyPermutation.
std::vector<std::size_t> Sort(std::vector<FileItem>& items, const SortState& state);

// A new batch (T086): items[0, sortedCount) are already in order, the rest were just
// appended. Sorts only the new items (stably) and merges them in with std::merge, which
// keeps an existing item before a new one it ties with: the result is exactly what Sort
// would give, in O(n + k log k) instead of O((n + k) log(n + k)). Returns the permutation,
// as Sort does.
std::vector<std::size_t> MergeBatch(std::vector<FileItem>& items, std::size_t sortedCount,
                                    const SortState& state);

} // namespace te::SortModel
