// SortModel and SelectionModel (T050; data-model: SortState, SelectionModel): folders
// first in both directions, natural name order, missing values last, a stable name
// tie-break, and the selection kept by item identity across a re-sort.

#include <te/ui/SelectionModel.h>
#include <te/ui/SortModel.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace
{

struct Spec
{
    const wchar_t* name;
    bool folder = false;
    std::optional<std::uint64_t> size;
    std::optional<std::uint64_t> modified; // FILETIME ticks
    const wchar_t* type = L"File";
};

std::vector<te::FileItem> Items(std::initializer_list<Spec> specs)
{
    std::vector<te::FileItem> items;
    std::size_t key = 0;
    for (const Spec& s : specs)
    {
        te::FileItem item;
        item.key = key++;
        item.info.name = s.name;
        item.info.isFolder = s.folder;
        item.info.size = s.size;
        item.info.typeText = s.type;
        if (s.modified)
        {
            item.info.modified =
                FILETIME{static_cast<DWORD>(*s.modified & 0xFFFFFFFF), static_cast<DWORD>(*s.modified >> 32)};
        }
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<std::wstring> Names(const std::vector<te::FileItem>& items)
{
    std::vector<std::wstring> names;
    for (const te::FileItem& item : items)
    {
        names.push_back(item.info.name);
    }
    return names;
}

te::SortState By(te::SortField field, te::SortDirection direction = te::SortDirection::Ascending)
{
    return {field, direction};
}

constexpr auto Asc = te::SortDirection::Ascending;
constexpr auto Desc = te::SortDirection::Descending;

// ---------------------------------------------------------------------------
// SortModel
// ---------------------------------------------------------------------------

TEST(SortModel, FoldersFirstInBothDirections)
{
    for (const auto direction : {Asc, Desc})
    {
        for (const auto field :
             {te::SortField::Name, te::SortField::Size, te::SortField::Type, te::SortField::DateModified})
        {
            auto items = Items(
                {{L"b.txt", false, 10, 5}, {L"Zeta", true}, {L"a.txt", false, 20, 6}, {L"Alpha", true}});
            te::SortModel::Sort(items, By(field, direction));
            EXPECT_TRUE(items[0].info.isFolder && items[1].info.isFolder)
                << "field " << static_cast<int>(field) << " direction " << static_cast<int>(direction);
            EXPECT_FALSE(items[2].info.isFolder || items[3].info.isFolder);
        }
    }
}

TEST(SortModel, NamesUseNaturalOrder)
{
    auto items = Items({{L"file10"}, {L"file2"}, {L"File1"}, {L"file20"}});
    te::SortModel::Sort(items, By(te::SortField::Name));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"File1", L"file2", L"file10", L"file20"}));

    te::SortModel::Sort(items, By(te::SortField::Name, Desc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"file20", L"file10", L"file2", L"File1"}));
}

TEST(SortModel, FoldersSortByNameWithinTheirGroupInBothDirections)
{
    auto items = Items({{L"dir10", true}, {L"b"}, {L"dir2", true}, {L"a"}});
    te::SortModel::Sort(items, By(te::SortField::Name, Desc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"dir10", L"dir2", L"b", L"a"}));
}

TEST(SortModel, SizeOrderWithMissingSizesLastInBothDirections)
{
    auto items = Items(
        {{L"big", false, 300}, {L"virtual", false, std::nullopt}, {L"small", false, 1}, {L"mid", false, 20}});
    te::SortModel::Sort(items, By(te::SortField::Size, Asc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"small", L"mid", L"big", L"virtual"}));

    te::SortModel::Sort(items, By(te::SortField::Size, Desc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"big", L"mid", L"small", L"virtual"}));
}

TEST(SortModel, DateOrderWithMissingDatesLastInBothDirections)
{
    auto items =
        Items({{L"new", false, 1, 300}, {L"nodate", false, 1, std::nullopt}, {L"old", false, 1, 100}});
    te::SortModel::Sort(items, By(te::SortField::DateModified, Asc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"old", L"new", L"nodate"}));

    te::SortModel::Sort(items, By(te::SortField::DateModified, Desc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"new", L"old", L"nodate"}));
}

TEST(SortModel, TypeOrderThenName)
{
    auto items = Items({{L"b.txt", false, 1, 1, L"Text Document"},
                        {L"a.png", false, 1, 1, L"PNG File"},
                        {L"a.txt", false, 1, 1, L"Text Document"}});
    te::SortModel::Sort(items, By(te::SortField::Type));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"a.png", L"a.txt", L"b.txt"}));
}

// Ties on the chosen field are broken by name, ascending and natural, whatever the
// direction, so equal sizes or dates list in a predictable order.
TEST(SortModel, TiesBreakOnNameAscending)
{
    auto items = Items({{L"file10", false, 5}, {L"file2", false, 5}, {L"file1", false, 5}});
    te::SortModel::Sort(items, By(te::SortField::Size, Asc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"file1", L"file2", L"file10"}));
    te::SortModel::Sort(items, By(te::SortField::Size, Desc));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"file1", L"file2", L"file10"}));
}

TEST(SortModel, SortIsStableForFullTies)
{
    auto items = Items({{L"same", false, 1, 1}, {L"same", false, 1, 1}, {L"same", false, 1, 1}});
    te::SortModel::Sort(items, By(te::SortField::Size));
    EXPECT_EQ(items[0].key, 0u);
    EXPECT_EQ(items[1].key, 1u);
    EXPECT_EQ(items[2].key, 2u);
}

TEST(SortModel, SortReturnsThePermutation)
{
    auto items = Items({{L"c"}, {L"a"}, {L"b"}});
    const std::vector<std::size_t> oldIndexAt = te::SortModel::Sort(items, By(te::SortField::Name));
    EXPECT_EQ(oldIndexAt, (std::vector<std::size_t>{1, 2, 0}));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"a", L"b", L"c"}));
}

TEST(SortModel, LessIsAStrictWeakOrdering)
{
    const auto items = Items({{L"a", false, 1}, {L"b", true}, {L"a", false, 1}, {L"c", false, std::nullopt}});
    for (const auto field :
         {te::SortField::Name, te::SortField::Size, te::SortField::Type, te::SortField::DateModified})
    {
        for (const auto direction : {Asc, Desc})
        {
            const te::SortState state = By(field, direction);
            for (const auto& x : items)
            {
                EXPECT_FALSE(te::SortModel::Less(x.info, x.info, state)) << "irreflexive";
                for (const auto& y : items)
                {
                    EXPECT_FALSE(te::SortModel::Less(x.info, y.info, state) &&
                                 te::SortModel::Less(y.info, x.info, state))
                        << "asymmetric";
                }
            }
        }
    }
    EXPECT_TRUE(te::SortModel::Less(items[1].info, items[0].info, By(te::SortField::Name))) << "folder first";
}

// ---------------------------------------------------------------------------
// SelectionModel
// ---------------------------------------------------------------------------

TEST(SelectionModel, SelectionIsKeptByIdentityAcrossAReSort)
{
    auto items = Items({{L"c"}, {L"a"}, {L"b"}, {L"d"}});
    te::SelectionModel selection;
    selection.Reset(items.size());
    selection.Select(1); // "a"
    selection.Toggle(3); // + "d"; focus and anchor on "d"
    ASSERT_EQ(selection.SelectedCount(), 2u);

    const auto oldIndexAt = te::SortModel::Sort(items, By(te::SortField::Name, Desc)); // d c b a
    selection.ApplyPermutation(oldIndexAt);

    std::vector<std::wstring> selected;
    for (const std::size_t i : selection.SelectedIndices())
    {
        selected.push_back(items[i].info.name);
    }
    EXPECT_EQ(selected, (std::vector<std::wstring>{L"d", L"a"}));
    ASSERT_TRUE(selection.FocusIndex().has_value());
    EXPECT_EQ(items[*selection.FocusIndex()].info.name, L"d");
    ASSERT_TRUE(selection.AnchorIndex().has_value());
    EXPECT_EQ(items[*selection.AnchorIndex()].info.name, L"d");
}

TEST(SelectionModel, ClickSelectsOnlyThatRow)
{
    te::SelectionModel selection;
    selection.Reset(5);
    selection.Select(1);
    selection.Select(3);
    EXPECT_EQ(selection.SelectedIndices(), (std::vector<std::size_t>{3}));
    EXPECT_EQ(selection.FocusIndex(), 3u);
    EXPECT_EQ(selection.AnchorIndex(), 3u);
}

TEST(SelectionModel, ToggleAddsAndRemoves)
{
    te::SelectionModel selection;
    selection.Reset(5);
    selection.Select(0);
    selection.Toggle(2);
    EXPECT_EQ(selection.SelectedIndices(), (std::vector<std::size_t>{0, 2}));
    selection.Toggle(0);
    EXPECT_EQ(selection.SelectedIndices(), (std::vector<std::size_t>{2}));
    EXPECT_EQ(selection.FocusIndex(), 0u);
}

TEST(SelectionModel, ShiftExtendsFromTheAnchorInEitherDirection)
{
    te::SelectionModel selection;
    selection.Reset(10);
    selection.Select(4);
    selection.ExtendTo(7);
    EXPECT_EQ(selection.SelectedIndices(), (std::vector<std::size_t>{4, 5, 6, 7}));
    selection.ExtendTo(2); // the range is replaced, not added to
    EXPECT_EQ(selection.SelectedIndices(), (std::vector<std::size_t>{2, 3, 4}));
    EXPECT_EQ(selection.AnchorIndex(), 4u);
    EXPECT_EQ(selection.FocusIndex(), 2u);
}

TEST(SelectionModel, FocusMovesWithoutChangingTheSelection)
{
    te::SelectionModel selection;
    selection.Reset(4);
    selection.Select(1);
    selection.Focus(3);
    EXPECT_EQ(selection.SelectedIndices(), (std::vector<std::size_t>{1}));
    EXPECT_EQ(selection.FocusIndex(), 3u);
}

TEST(SelectionModel, SelectAllAndClear)
{
    te::SelectionModel selection;
    selection.Reset(3);
    selection.SelectAll();
    EXPECT_EQ(selection.SelectedCount(), 3u);
    selection.Clear();
    EXPECT_EQ(selection.SelectedCount(), 0u);
}

TEST(SelectionModel, NavigationClearsEverything)
{
    te::SelectionModel selection;
    selection.Reset(3);
    selection.Select(2);
    selection.Reset(7);
    EXPECT_EQ(selection.Count(), 7u);
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_FALSE(selection.FocusIndex().has_value());
    EXPECT_FALSE(selection.AnchorIndex().has_value());
}

TEST(SelectionModel, NewBatchesArriveUnselected)
{
    te::SelectionModel selection;
    selection.Reset(2);
    selection.Select(1);
    selection.Grow(5);
    EXPECT_EQ(selection.Count(), 5u);
    EXPECT_EQ(selection.SelectedIndices(), (std::vector<std::size_t>{1}));
}

// T086: merging each batch into the sorted list gives exactly the order a full stable sort
// of everything appended so far gives, for every field and direction, with many ties.
struct RandomSpec
{
    std::size_t key = 0;
    std::wstring name;
    bool folder = false;
    std::wstring type;
    std::optional<std::uint64_t> size;
    std::optional<std::uint64_t> modified;
};

te::FileItem Make(const RandomSpec& spec)
{
    te::FileItem item;
    item.key = spec.key;
    item.info.name = spec.name;
    item.info.isFolder = spec.folder;
    item.info.typeText = spec.type;
    item.info.size = spec.size;
    if (spec.modified)
    {
        item.info.modified = FILETIME{static_cast<DWORD>(*spec.modified), 0};
    }
    return item;
}

std::vector<std::size_t> Keys(const std::vector<te::FileItem>& items)
{
    std::vector<std::size_t> keys;
    for (const te::FileItem& item : items)
    {
        keys.push_back(item.key);
    }
    return keys;
}

TEST(SortModel, MergingBatchesMatchesAFullSort)
{
    std::mt19937 random(20260928); // fixed: the same data every run
    const wchar_t* names[] = {L"a", L"b", L"file2", L"file10", L"Report", L"report", L"z", L"\u00e9t\u00e9"};
    std::vector<RandomSpec> specs;
    for (std::size_t key = 0; key < 600; ++key)
    {
        RandomSpec spec;
        spec.key = key;
        spec.name = names[random() % std::size(names)];
        spec.folder = random() % 5 == 0;
        spec.type = spec.folder ? L"File folder" : (random() % 2 == 0 ? L"File" : L"Text Document");
        if (!spec.folder && random() % 4 != 0)
        {
            spec.size = random() % 4; // many equal sizes
        }
        if (random() % 5 != 0)
        {
            spec.modified = 1000 + random() % 3;
        }
        specs.push_back(std::move(spec));
    }

    for (const te::SortField field :
         {te::SortField::Name, te::SortField::DateModified, te::SortField::Type, te::SortField::Size})
    {
        for (const te::SortDirection direction : {Asc, Desc})
        {
            const te::SortState state{field, direction};
            std::vector<te::FileItem> merged;
            std::size_t next = 0;
            while (next < specs.size())
            {
                const std::size_t count = std::min<std::size_t>(1 + random() % 90, specs.size() - next);
                const std::size_t sortedCount = merged.size();
                for (std::size_t i = 0; i < count; ++i)
                {
                    merged.push_back(Make(specs[next + i]));
                }
                next += count;

                const std::vector<std::size_t> before = Keys(merged);
                const std::vector<std::size_t> oldIndexAt =
                    te::SortModel::MergeBatch(merged, sortedCount, state);
                ASSERT_EQ(oldIndexAt.size(), merged.size());
                for (std::size_t i = 0; i < merged.size(); ++i)
                {
                    ASSERT_EQ(merged[i].key, before[oldIndexAt[i]]) << "the returned permutation";
                }
            }
            // Everything appended in arrival order, then sorted in one go.
            std::vector<te::FileItem> sorted;
            for (const RandomSpec& spec : specs)
            {
                sorted.push_back(Make(spec));
            }
            te::SortModel::Sort(sorted, state);
            EXPECT_EQ(Keys(merged), Keys(sorted))
                << "field " << static_cast<int>(field) << ", direction " << static_cast<int>(direction);
        }
    }
}

TEST(SortModel, MergingIntoAnEmptyListSortsTheBatch)
{
    std::vector<te::FileItem> items = Items({{L"c"}, {L"a"}, {L"b", true}});
    const std::vector<std::size_t> oldIndexAt = te::SortModel::MergeBatch(items, 0, By(te::SortField::Name));
    EXPECT_EQ(Names(items), (std::vector<std::wstring>{L"b", L"a", L"c"}));
    EXPECT_EQ(oldIndexAt, (std::vector<std::size_t>{2, 1, 0}));
}

} // namespace
