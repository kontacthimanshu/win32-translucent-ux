// NavigationHistory (T049; data-model: NavigationHistory, spec US3-3). Uses a move-only
// Location stand-in with an injected comparator, like te::Location (a PIDL compared with
// ILIsEqual): here paths compare case-insensitively, as Windows paths do.

#include <te/ui/NavigationHistory.h>

#include <gtest/gtest.h>

#include <cwctype>
#include <memory>
#include <optional>
#include <string>

namespace
{

// Move-only, like a Location that owns its PIDL.
struct FakeLocation
{
    std::wstring path;
    std::unique_ptr<int> owned = std::make_unique<int>(0);

    explicit FakeLocation(std::wstring p) : path(std::move(p)) {}
};

struct SamePlace
{
    bool operator()(const FakeLocation& a, const FakeLocation& b) const
    {
        if (a.path.size() != b.path.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.path.size(); ++i)
        {
            if (std::towlower(a.path[i]) != std::towlower(b.path[i]))
            {
                return false;
            }
        }
        return true;
    }
};

using History = te::NavigationHistory<FakeLocation, SamePlace>;

FakeLocation At(const wchar_t* path)
{
    return FakeLocation(path);
}

std::wstring Current(const History& history)
{
    const FakeLocation* current = history.Current();
    return current ? current->path : L"<none>";
}

TEST(NavigationHistory, StartsEmptyWithBothDirectionsDisabled)
{
    History history;
    EXPECT_EQ(history.Current(), nullptr);
    EXPECT_FALSE(history.CanBack());
    EXPECT_FALSE(history.CanForward());
    EXPECT_FALSE(history.Back());
    EXPECT_FALSE(history.Forward());
}

TEST(NavigationHistory, FirstNavigateBecomesCurrent)
{
    History history;
    EXPECT_TRUE(history.Navigate(At(L"C:\\A")));
    EXPECT_EQ(Current(history), L"C:\\A");
    EXPECT_EQ(history.Size(), 1u);
    EXPECT_FALSE(history.CanBack());
    EXPECT_FALSE(history.CanForward());
}

TEST(NavigationHistory, NavigatingToTheSameLocationDoesNothing)
{
    History history;
    history.Navigate(At(L"C:\\A"));
    history.Navigate(At(L"C:\\B"));
    EXPECT_FALSE(history.Navigate(At(L"c:\\b"))) << "the injected comparator decides equality";
    EXPECT_EQ(history.Size(), 2u);
    EXPECT_EQ(Current(history), L"C:\\B");
    EXPECT_FALSE(history.CanForward());
}

TEST(NavigationHistory, BackBackThenForward)
{
    History history;
    history.Navigate(At(L"A"));
    history.Navigate(At(L"B"));
    history.Navigate(At(L"C"));

    EXPECT_TRUE(history.Back());
    EXPECT_EQ(Current(history), L"B");
    EXPECT_TRUE(history.Back());
    EXPECT_EQ(Current(history), L"A");
    EXPECT_FALSE(history.CanBack());

    EXPECT_TRUE(history.Forward());
    EXPECT_EQ(Current(history), L"B");
    EXPECT_TRUE(history.CanBack());
    EXPECT_TRUE(history.CanForward());
    EXPECT_EQ(history.Size(), 3u) << "Back and Forward never change the entries";
}

TEST(NavigationHistory, NavigateAfterBackTruncatesForwardEntries)
{
    History history;
    history.Navigate(At(L"A"));
    history.Navigate(At(L"B"));
    history.Navigate(At(L"C"));
    history.Back();
    history.Back(); // at A; B and C are forward entries

    EXPECT_TRUE(history.Navigate(At(L"D")));
    EXPECT_EQ(Current(history), L"D");
    EXPECT_FALSE(history.CanForward());
    ASSERT_EQ(history.Size(), 2u);
    EXPECT_EQ(history.Entries()[0].path, L"A");
    EXPECT_EQ(history.Entries()[1].path, L"D");
}

TEST(NavigationHistory, NavigatingToTheCurrentEntryAfterBackKeepsForwardEntries)
{
    History history;
    history.Navigate(At(L"A"));
    history.Navigate(At(L"B"));
    history.Back(); // at A
    EXPECT_FALSE(history.Navigate(At(L"a"))) << "a refresh of the same place";
    EXPECT_TRUE(history.CanForward()) << "nothing was truncated";
}

TEST(NavigationHistory, UpPushesTheParentAndBackReturnsToTheChild)
{
    History history;
    history.Navigate(At(L"C:\\Users\\me\\Docs"));
    EXPECT_TRUE(history.Up(At(L"C:\\Users\\me")));
    EXPECT_EQ(Current(history), L"C:\\Users\\me");
    EXPECT_EQ(history.Size(), 2u);

    EXPECT_TRUE(history.Back());
    EXPECT_EQ(Current(history), L"C:\\Users\\me\\Docs");
    EXPECT_TRUE(history.Forward());
    EXPECT_EQ(Current(history), L"C:\\Users\\me");
}

TEST(NavigationHistory, UpAtTheRootDoesNothing)
{
    History history;
    history.Navigate(At(L"Desktop"));
    EXPECT_FALSE(history.Up(std::nullopt));
    EXPECT_EQ(history.Size(), 1u);
    EXPECT_EQ(Current(history), L"Desktop");
}

TEST(NavigationHistory, UpNeverDuplicatesTheCurrentEntry)
{
    History history;
    history.Navigate(At(L"X"));
    EXPECT_FALSE(history.Up(At(L"x"))) << "a parent equal to the current entry is not pushed";
    EXPECT_EQ(history.Size(), 1u);
}

TEST(NavigationHistory, CapOf100DropsTheOldest)
{
    History history;
    for (int i = 0; i < 105; ++i)
    {
        history.Navigate(FakeLocation(L"L" + std::to_wstring(i)));
    }
    ASSERT_EQ(history.Size(), History::kMaxEntries);
    EXPECT_EQ(history.Entries().front().path, L"L5");
    EXPECT_EQ(history.Entries().back().path, L"L104");
    EXPECT_EQ(Current(history), L"L104");
    EXPECT_EQ(history.Index(), History::kMaxEntries - 1);

    // All 99 Back steps still work, and stop at the oldest kept entry.
    int steps = 0;
    while (history.Back())
    {
        ++steps;
    }
    EXPECT_EQ(steps, 99);
    EXPECT_EQ(Current(history), L"L5");
}

TEST(NavigationHistory, BackAndForwardAreDisabledAtTheEnds)
{
    History history;
    history.Navigate(At(L"A"));
    history.Navigate(At(L"B"));
    EXPECT_TRUE(history.CanBack());
    EXPECT_FALSE(history.CanForward());
    EXPECT_FALSE(history.Forward());
    EXPECT_EQ(Current(history), L"B");

    history.Back();
    EXPECT_FALSE(history.CanBack());
    EXPECT_TRUE(history.CanForward());
    EXPECT_FALSE(history.Back());
    EXPECT_EQ(Current(history), L"A");
}

TEST(NavigationHistory, DefaultComparatorWorksForComparableTypes)
{
    te::NavigationHistory<std::wstring> history;
    EXPECT_TRUE(history.Navigate(L"A"));
    EXPECT_FALSE(history.Navigate(L"A"));
    EXPECT_TRUE(history.Navigate(L"a")) << "std::equal_to is case-sensitive";
    EXPECT_EQ(history.Size(), 2u);
}

} // namespace
