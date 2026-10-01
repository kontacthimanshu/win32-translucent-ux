#pragma once

// Sort order for the file list (data-model: SortState). The comparator is
// implemented in SortModel (T055).

namespace te
{

enum class SortField
{
    Name,
    DateModified,
    Type,
    Size,
};

enum class SortDirection
{
    Ascending,
    Descending,
};

struct SortState
{
    SortField field = SortField::Name;
    SortDirection direction = SortDirection::Ascending;

    friend constexpr bool operator==(const SortState&, const SortState&) = default;
};

} // namespace te
