// The file list's filter (T092; FR-021): names are matched as a case-insensitive,
// linguistic substring (FindNLSStringEx, LINGUISTIC_IGNORECASE); items that do not match
// wait aside in order and come back when the filter changes; the selection and focus stay
// on items that remain; later batches and a new sort respect the filter; a new folder
// starts unfiltered.

#include <te/ui/FileView.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace
{

constexpr D2D1_RECT_F kBounds{0.0f, 0.0f, 800.0f, 28.0f + 10 * 28.0f};

std::vector<te::FileItem> Batch(std::initializer_list<const wchar_t*> names)
{
    std::vector<te::FileItem> items;
    for (const wchar_t* name : names)
    {
        te::FileItem item;
        item.info.name = name;
        item.info.size = 1;
        item.info.typeText = L"File";
        items.push_back(std::move(item));
    }
    return items;
}

class FileViewFilterTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_view.SetBounds(kBounds);
        m_view.SetCallbacks({{}, [this](std::size_t items, std::size_t) { m_count = items; }, {}, {}});
        m_view.BeginLocation(1);
    }

    std::vector<std::wstring> Names() const
    {
        std::vector<std::wstring> names;
        for (const te::FileItem& item : m_view.Items())
        {
            names.push_back(item.info.name);
        }
        return names;
    }
    std::vector<std::wstring> Selected() const
    {
        std::vector<std::wstring> names;
        for (const te::FileItem* item : m_view.Selection())
        {
            names.push_back(item->info.name);
        }
        return names;
    }
    void Click(std::size_t row, bool ctrl = false)
    {
        const D2D1_RECT_F r = m_view.RowRect(row);
        m_view.OnPointerDown(D2D1::Point2F(100.0f, (r.top + r.bottom) / 2.0f), ctrl, false);
        m_view.OnPointerUp(D2D1::Point2F(100.0f, (r.top + r.bottom) / 2.0f));
    }

    te::FileView m_view;
    std::size_t m_count = 0;
};

TEST(FileViewFilter, MatchesAreCaseInsensitiveLinguisticSubstrings)
{
    EXPECT_TRUE(te::FileView::Matches(L"Annual Report.docx", L"report"));
    EXPECT_TRUE(te::FileView::Matches(L"annual report.docx", L"REPORT"));
    EXPECT_TRUE(te::FileView::Matches(L"notes.txt", L"")) << "an empty filter matches everything";
    EXPECT_TRUE(te::FileView::Matches(L"Café menu.txt", L"CAFÉ")) << "accented letters, any case";
    EXPECT_FALSE(te::FileView::Matches(L"notes.txt", L"report"));
    EXPECT_FALSE(te::FileView::Matches(L"a", L"ab"));
}

TEST_F(FileViewFilterTest, TheFilterHidesAndRestoresItemsInOrder)
{
    m_view.AppendItems(1, Batch({L"beta.txt", L"alpha.log", L"Gamma.TXT", L"delta.md"}));
    ASSERT_EQ(Names(), (std::vector<std::wstring>{L"alpha.log", L"beta.txt", L"delta.md", L"Gamma.TXT"}));

    m_view.SetFilter(L"txt");
    EXPECT_EQ(Names(), (std::vector<std::wstring>{L"beta.txt", L"Gamma.TXT"}));
    EXPECT_EQ(m_view.HiddenCount(), 2u);
    EXPECT_EQ(m_count, 2u) << "the status bar counts what is listed";
    for (std::size_t i = 0; i < m_view.Items().size(); ++i)
    {
        EXPECT_EQ(m_view.IndexOfKey(m_view.Items()[i].key), i) << "keys find their rows";
    }

    m_view.SetFilter(L"");
    EXPECT_EQ(Names(), (std::vector<std::wstring>{L"alpha.log", L"beta.txt", L"delta.md", L"Gamma.TXT"}));
    EXPECT_EQ(m_view.HiddenCount(), 0u);
    EXPECT_EQ(m_count, 4u);
}

TEST_F(FileViewFilterTest, TheRealNameMatchesWhenExtensionsAreHidden)
{
    // Explorer set to hide known extensions: "report.txt" is shown as "report".
    std::vector<te::FileItem> items = Batch({L"report", L"notes"});
    items[0].info.editName = L"report.txt";
    items[1].info.editName = L"notes.md";
    m_view.AppendItems(1, std::move(items));
    m_view.SetFilter(L".TXT");
    EXPECT_EQ(Names(), (std::vector<std::wstring>{L"report"}));
}

TEST_F(FileViewFilterTest, TheSelectionStaysOnItemsThatRemain)
{
    m_view.AppendItems(1, Batch({L"a.txt", L"b.log", L"c.txt"}));
    Click(0);       // a.txt
    Click(1, true); // + b.log
    ASSERT_EQ(Selected(), (std::vector<std::wstring>{L"a.txt", L"b.log"}));
    m_view.SetFilter(L".txt");
    EXPECT_EQ(Selected(), (std::vector<std::wstring>{L"a.txt"})) << "b.log is hidden, so no longer selected";
    m_view.SetFilter(L"");
    EXPECT_EQ(Selected(), (std::vector<std::wstring>{L"a.txt"})) << "hidden items come back unselected";
}

TEST_F(FileViewFilterTest, LaterBatchesAndSortingRespectTheFilter)
{
    m_view.SetFilter(L"x");
    m_view.AppendItems(1, Batch({L"box", L"cat", L"axe"}));
    m_view.AppendItems(1, Batch({L"fox", L"dog", L"ant"}));
    EXPECT_EQ(Names(), (std::vector<std::wstring>{L"axe", L"box", L"fox"}));
    EXPECT_EQ(m_view.HiddenCount(), 3u);

    m_view.SetSort({te::SortField::Name, te::SortDirection::Descending});
    EXPECT_EQ(Names(), (std::vector<std::wstring>{L"fox", L"box", L"axe"}));
    m_view.SetFilter(L"");
    EXPECT_EQ(Names(), (std::vector<std::wstring>{L"fox", L"dog", L"cat", L"box", L"axe", L"ant"}))
        << "the hidden items were sorted too";
}

TEST_F(FileViewFilterTest, ANewFolderStartsUnfiltered)
{
    m_view.AppendItems(1, Batch({L"one", L"two"}));
    m_view.SetFilter(L"one");
    ASSERT_EQ(m_view.HiddenCount(), 1u);
    m_view.BeginLocation(2);
    EXPECT_TRUE(m_view.Filter().empty());
    EXPECT_EQ(m_view.HiddenCount(), 0u);
    m_view.AppendItems(2, Batch({L"three", L"four"}));
    EXPECT_EQ(m_view.Items().size(), 2u);
}

} // namespace
