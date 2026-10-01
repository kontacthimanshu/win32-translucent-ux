// Inline rename in the file list (T071; research R-02, R-08; UI contract §4; spec US4-3;
// quickstart V-4c) with a real parent window and the layered, colour-keyed EDIT: F2, the
// initial selection, Enter with an invalid and a valid name, Escape, focus loss, the old
// name kept until ApplyRename, and cancellation when the row goes away.

#include <te/ui/FileView.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace
{

te::FileItem Item(const wchar_t* name, bool folder = false, bool canRename = true)
{
    te::FileItem item;
    item.info.name = name;
    item.info.editName = name;
    item.info.isFolder = folder;
    item.info.canRename = canRename;
    if (!folder)
    {
        item.info.size = 1;
    }
    return item;
}

class FileViewRenameTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_parent = CreateWindowExW(0, L"STATIC", L"parent", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 100, 100,
                                   900, 600, nullptr, nullptr, nullptr, nullptr);
        ASSERT_NE(m_parent, nullptr);
        // CTest starts tests hidden; the second ShowWindow takes effect.
        ShowWindow(m_parent, SW_SHOWNORMAL);
        ShowWindow(m_parent, SW_SHOWNORMAL);
        SetForegroundWindow(m_parent);
        SetFocus(m_parent);

        te::FileView::Callbacks callbacks;
        callbacks.rename = [this](const te::FileItem& item, const std::wstring& newName) {
            m_renamed.emplace_back(item.info.name, newName);
            m_renamedKey = item.key;
        };
        m_view.SetCallbacks(std::move(callbacks));
        m_view.AttachWindow(m_parent, GetDpiForWindow(m_parent));
        m_view.SetBounds(D2D1::RectF(0.0f, 0.0f, 600.0f, 300.0f));
        m_view.BeginLocation(1);
        std::vector<te::FileItem> items;
        items.push_back(Item(L"docs", true));
        items.push_back(Item(L"report.final.txt"));
        items.push_back(Item(L".gitignore"));
        items.push_back(Item(L"locked.txt", false, false));
        m_view.AppendItems(1, std::move(items));
    }

    void TearDown() override
    {
        if (m_parent)
        {
            DestroyWindow(m_parent);
        }
    }

    // Focuses the row with this name (Home, then Down).
    void FocusRow(const std::wstring& name)
    {
        m_view.OnKeyDown(VK_HOME, false, false);
        for (std::size_t i = 0; i < m_view.Items().size() && m_view.Items()[i].info.name != name; ++i)
        {
            m_view.OnKeyDown(VK_DOWN, false, false);
        }
        ASSERT_EQ(m_view.Items()[*m_view.SelectionState().FocusIndex()].info.name, name);
    }

    [[nodiscard]] HWND Edit()
    {
        return m_view.RenameField().Edit();
    }

    void Type(const std::wstring& text)
    {
        SetWindowTextW(Edit(), text.c_str());
    }

    void Key(UINT vk)
    {
        SendMessageW(Edit(), WM_KEYDOWN, vk, 0);
    }

    [[nodiscard]] std::pair<DWORD, DWORD> Selection()
    {
        DWORD start = 0;
        DWORD end = 0;
        SendMessageW(Edit(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        return {start, end};
    }

    [[nodiscard]] std::wstring NameOfKey(std::size_t key) const
    {
        for (const auto& item : m_view.Items())
        {
            if (item.key == key)
            {
                return item.info.name;
            }
        }
        return {};
    }

    HWND m_parent = nullptr;
    te::FileView m_view;
    std::vector<std::pair<std::wstring, std::wstring>> m_renamed;
    std::size_t m_renamedKey = 0;
};

TEST_F(FileViewRenameTest, F2EditsTheFileNameWithoutItsExtensionSelected)
{
    FocusRow(L"report.final.txt");
    ASSERT_TRUE(m_view.OnKeyDown(VK_F2, false, false));
    EXPECT_TRUE(m_view.IsRenaming());
    ASSERT_NE(Edit(), nullptr);
    EXPECT_TRUE(IsWindowVisible(Edit()));
    EXPECT_EQ(GetFocus(), Edit());
    EXPECT_EQ(m_view.RenameField().Text(), L"report.final.txt");
    EXPECT_EQ(Selection(), (std::pair<DWORD, DWORD>{0, 12})) << "up to the last dot";

    // Layered and colour-keyed like the address edit (R-02).
    EXPECT_NE(GetWindowLongPtrW(Edit(), GWL_EXSTYLE) & WS_EX_LAYERED, 0);
    COLORREF key = 0;
    DWORD flags = 0;
    ASSERT_TRUE(GetLayeredWindowAttributes(Edit(), &key, nullptr, &flags));
    EXPECT_NE(flags & LWA_COLORKEY, 0u);
    const HDC dc = GetDC(Edit());
    LRESULT brush = 0;
    EXPECT_TRUE(m_view.RenameField().HandleCtlColor(Edit(), dc, &brush));
    EXPECT_NE(brush, 0);
    EXPECT_EQ(GetBkColor(dc), key) << "the background is the transparent key";
    ReleaseDC(Edit(), dc);

    // The edit sits over the name cell of the focused row.
    RECT rect{};
    GetWindowRect(Edit(), &rect);
    MapWindowPoints(nullptr, m_parent, reinterpret_cast<POINT*>(&rect), 2);
    const float scale = static_cast<float>(GetDpiForWindow(m_parent)) / 96.0f;
    const D2D1_RECT_F cell = m_view.NameCellRect(*m_view.SelectionState().FocusIndex());
    EXPECT_NEAR(rect.top, cell.top * scale, 1.5);
    EXPECT_NEAR(rect.left, cell.left * scale, 1.5);
}

TEST_F(FileViewRenameTest, FoldersAndDotFilesSelectTheWholeName)
{
    FocusRow(L"docs");
    ASSERT_TRUE(m_view.BeginRename());
    EXPECT_EQ(Selection(), (std::pair<DWORD, DWORD>{0, 4}));
    Key(VK_ESCAPE);

    FocusRow(L".gitignore");
    ASSERT_TRUE(m_view.BeginRename());
    EXPECT_EQ(Selection(), (std::pair<DWORD, DWORD>{0, 10})) << "a leading dot is not an extension";
}

TEST_F(FileViewRenameTest, ItemsThatCannotBeRenamedDoNotStartEditing)
{
    FocusRow(L"locked.txt");
    EXPECT_FALSE(m_view.OnKeyDown(VK_F2, false, false));
    EXPECT_FALSE(m_view.IsRenaming());
}

TEST_F(FileViewRenameTest, AnInvalidNameIsRejectedAndEditingContinues)
{
    FocusRow(L"report.final.txt");
    ASSERT_TRUE(m_view.BeginRename());
    Type(L"bad:name.txt");
    Key(VK_RETURN);
    EXPECT_TRUE(m_view.IsRenaming()) << "stays in edit mode (V-4c)";
    EXPECT_TRUE(m_view.RenameField().LastRejected()) << "balloon tip shown";
    EXPECT_TRUE(m_renamed.empty()) << "nothing submitted";
    EXPECT_EQ(m_view.Items()[*m_view.SelectionState().FocusIndex()].info.name, L"report.final.txt");

    // Fixed, it goes through.
    Type(L"good.txt");
    Key(VK_RETURN);
    EXPECT_FALSE(m_view.IsRenaming());
    ASSERT_EQ(m_renamed.size(), 1u);
}

TEST_F(FileViewRenameTest, AValidNameIsSubmittedButShownOnlyAfterSuccess)
{
    FocusRow(L"report.final.txt");
    ASSERT_TRUE(m_view.BeginRename());
    Type(L"summary.txt");
    Key(VK_RETURN);

    EXPECT_FALSE(m_view.IsRenaming());
    EXPECT_FALSE(IsWindowVisible(Edit()));
    EXPECT_EQ(GetFocus(), m_parent) << "the keyboard returns to the list's window";
    ASSERT_EQ(m_renamed.size(), 1u);
    EXPECT_EQ(m_renamed[0].first, L"report.final.txt");
    EXPECT_EQ(m_renamed[0].second, L"summary.txt");
    EXPECT_EQ(NameOfKey(m_renamedKey), L"report.final.txt") << "old name until the rename succeeds (US4-3)";

    m_view.ApplyRename(m_renamedKey, L"summary.txt"); // the owner, on a successful FileOpItem
    EXPECT_EQ(NameOfKey(m_renamedKey), L"summary.txt");
}

TEST_F(FileViewRenameTest, EscapeAndAnUnchangedNameSubmitNothing)
{
    FocusRow(L"report.final.txt");
    ASSERT_TRUE(m_view.BeginRename());
    Type(L"other.txt");
    Key(VK_ESCAPE);
    EXPECT_FALSE(m_view.IsRenaming());

    ASSERT_TRUE(m_view.BeginRename());
    EXPECT_EQ(m_view.RenameField().Text(), L"report.final.txt") << "Escape discarded the typing";
    Key(VK_RETURN);
    EXPECT_FALSE(m_view.IsRenaming());
    EXPECT_TRUE(m_renamed.empty());
}

TEST_F(FileViewRenameTest, FocusLossCommitsAValidNameAndDropsAnInvalidOne)
{
    FocusRow(L"report.final.txt");
    ASSERT_TRUE(m_view.BeginRename());
    Type(L"bad|name.txt");
    SetFocus(m_parent); // a click elsewhere
    EXPECT_FALSE(m_view.IsRenaming());
    EXPECT_TRUE(m_renamed.empty()) << "invalid: the old name is kept";

    ASSERT_TRUE(m_view.BeginRename());
    Type(L"clicked-away.txt");
    SetFocus(m_parent);
    EXPECT_FALSE(m_view.IsRenaming());
    ASSERT_EQ(m_renamed.size(), 1u);
    EXPECT_EQ(m_renamed[0].second, L"clicked-away.txt");
}

TEST_F(FileViewRenameTest, NavigatingOrScrollingTheRowAwayCancels)
{
    FocusRow(L"report.final.txt");
    ASSERT_TRUE(m_view.BeginRename());
    m_view.BeginLocation(2);
    EXPECT_FALSE(m_view.IsRenaming());
    EXPECT_FALSE(IsWindowVisible(Edit()));

    // Many rows; rename the first, then scroll it out of view.
    std::vector<te::FileItem> items;
    for (int i = 0; i < 200; ++i)
    {
        items.push_back(Item((L"file" + std::to_wstring(1000 + i) + L".txt").c_str()));
    }
    m_view.AppendItems(2, std::move(items));
    m_view.OnKeyDown(VK_HOME, false, false);
    ASSERT_TRUE(m_view.BeginRename(5));
    RECT before{};
    GetWindowRect(Edit(), &before);
    m_view.OnWheel(-WHEEL_DELTA); // three rows: row 5 stays in view, and the edit follows it
    EXPECT_TRUE(m_view.IsRenaming());
    RECT after{};
    GetWindowRect(Edit(), &after);
    const float scale = static_cast<float>(GetDpiForWindow(m_parent)) / 96.0f;
    EXPECT_NEAR(before.top - after.top, 3 * te::FileView::kRowHeightDip * scale, 2.0);
    m_view.OnWheel(-WHEEL_DELTA * 20);
    EXPECT_FALSE(m_view.IsRenaming()) << "the row left the view";
    EXPECT_TRUE(m_renamed.empty());
}

} // namespace
