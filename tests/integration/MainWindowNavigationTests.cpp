// MainWindow navigation against the real Shell and the %TEMP%\te-test tree (T066; the
// automated part of quickstart V-3a, V-3b, V-3c, V-3e, V-3g and V-3h). The window is
// driven the way the user drives it, through window messages: WM_COMMAND for the
// accelerators, WM_KEYDOWN for the list, header clicks, and the address field's edit
// control. No mouse or keyboard input is injected, and no file is opened (that would
// start an application; V-3d is manual).
//
// Skips when the test data is missing: run tools/New-TestData.ps1 first.

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>

#include <shlobj.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

fs::path TestRoot()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return fs::path(temp) / L"te-test";
}

// Pumps messages for up to `ms`, calling `check` after every dispatched message.
// Returns early once `done` returns true.
void Pump(DWORD ms, const std::function<bool()>& done = {}, const std::function<void()>& check = {})
{
    const ULONGLONG until = GetTickCount64() + ms;
    MSG msg{};
    while (GetTickCount64() < until)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (check)
            {
                check();
            }
        }
        if (done && done())
        {
            return;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

std::wstring StripLongPrefix(std::wstring path)
{
    if (path.starts_with(LR"(\\?\)"))
    {
        path.erase(0, 4);
    }
    return path;
}

class MainWindowNavigationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!fs::exists(TestRoot() / L"nested"))
        {
            GTEST_SKIP() << "Test data missing: run tools/New-TestData.ps1 to create " << TestRoot().string();
        }
    }

    void TearDown() override
    {
        if (m_window)
        {
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
    }

    void Open(const fs::path& folder)
    {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"navigation test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = folder.wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNOACTIVATE));
        WaitIdle();
    }

    // Waits until no navigation is pending and the enumeration has finished.
    void WaitIdle(DWORD ms = 15000)
    {
        Pump(ms, [this] { return !m_window->NavigationPending(); });
        Pump(300); // the remaining batches and the done message
    }

    [[nodiscard]] HWND Hwnd() const
    {
        return m_window->Hwnd();
    }

    [[nodiscard]] std::wstring Current() const
    {
        return StripLongPrefix(m_window->CurrentLocation().ParsingPath().value_or(L""));
    }

    // Past MAX_PATH the Shell reports parsing paths in 8.3 form ("SEGMEN~1"), so both sides
    // are expanded to long form first.
    static std::wstring LongForm(const std::wstring& path)
    {
        const std::wstring prefixed = LR"(\\?\)" + StripLongPrefix(path);
        std::wstring buffer(32768, L'\0');
        const DWORD length =
            GetLongPathNameW(prefixed.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return StripLongPrefix(path);
        }
        buffer.resize(length);
        return StripLongPrefix(buffer);
    }

    static bool SamePath(const std::wstring& a, const fs::path& b)
    {
        return _wcsicmp(LongForm(a).c_str(), LongForm(b.wstring()).c_str()) == 0;
    }

    // File-system names of the listed items, in list order (display names may hide
    // extensions, depending on the user's Explorer setting).
    [[nodiscard]] std::vector<std::wstring> ListedNames() const
    {
        std::vector<std::wstring> names;
        for (const te::FileItem& item : m_window->Files().Items())
        {
            wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> full(reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(
                ILCombine(m_window->CurrentLocation().IdList(), item.info.childPidl.get())));
            wil::unique_cotaskmem_string path;
            if (full && SUCCEEDED(SHGetNameFromIDList(full.get(), SIGDN_FILESYSPATH, &path)))
            {
                names.push_back(fs::path(path.get()).filename().wstring());
            }
            else
            {
                names.push_back(item.info.name);
            }
        }
        return names;
    }

    static std::set<std::wstring> DiskNames(const fs::path& folder)
    {
        std::set<std::wstring> names;
        for (const auto& entry : fs::directory_iterator(folder))
        {
            names.insert(entry.path().filename().wstring());
        }
        return names;
    }

    void Command(UINT id) const
    {
        SendMessageW(Hwnd(), WM_COMMAND, id, 0);
    }

    void Key(UINT vk) const
    {
        SendMessageW(Hwnd(), WM_KEYDOWN, vk, 0);
    }

    // Display names, in list order. Past MAX_PATH the Shell reports an item's file-system
    // path in 8.3 form, so the long-path walk matches display names instead.
    [[nodiscard]] std::vector<std::wstring> DisplayNames() const
    {
        std::vector<std::wstring> names;
        for (const te::FileItem& item : m_window->Files().Items())
        {
            names.push_back(item.info.name);
        }
        return names;
    }

    // Opens the listed item with this file-system (or display) name: Down to it, then Enter.
    void OpenListed(const std::wstring& name, bool byDisplayName = false)
    {
        const auto names = byDisplayName ? DisplayNames() : ListedNames();
        const auto it = std::find(names.begin(), names.end(), name);
        ASSERT_NE(it, names.end()) << "not listed";
        Key(VK_HOME);
        for (auto i = names.begin(); i != it; ++i)
        {
            Key(VK_DOWN);
        }
        Key(VK_RETURN);
        WaitIdle();
    }

    // Ctrl+L, type, Enter: the address-bar path of UI contract §6.
    void TypeAddress(const std::wstring& text)
    {
        Command(IDM_FOCUS_ADDRESS);
        const HWND edit = m_window->Address().Edit();
        ASSERT_NE(edit, nullptr);
        SetWindowTextW(edit, text.c_str());
        SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
        WaitIdle();
    }

    // Clicks the header of a column (DIP coordinates converted to pixels).
    void ClickHeader(te::FileView::Column column)
    {
        const D2D1_RECT_F list = m_window->Layout().fileList;
        float left = list.left;
        for (int i = 0; i < static_cast<int>(column); ++i)
        {
            left += m_window->Files().ColumnWidth(static_cast<te::FileView::Column>(i));
        }
        const float x = left + m_window->Files().ColumnWidth(column) / 2;
        const float y = list.top + te::FileView::kHeaderHeightDip / 2;
        const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
        const LPARAM point = MAKELPARAM(static_cast<int>(x * scale), static_cast<int>(y * scale));
        SendMessageW(Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(Hwnd(), WM_LBUTTONUP, 0, point);
    }

    std::unique_ptr<te::MainWindow> m_window;
};

// V-3a: typing a path lists the folder with its columns.
TEST_F(MainWindowNavigationTest, AddressBarListsTheFolderWithItsColumns)
{
    Open(TestRoot());
    TypeAddress((TestRoot() / L"nested").wstring());
    EXPECT_TRUE(SamePath(Current(), TestRoot() / L"nested"));

    const auto names = ListedNames();
    EXPECT_EQ(std::set<std::wstring>(names.begin(), names.end()), DiskNames(TestRoot() / L"nested"));
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), L"a") << "folders first";
    for (const te::FileItem& item : m_window->Files().Items())
    {
        EXPECT_FALSE(item.info.typeText.empty()) << "Type column";
        EXPECT_TRUE(item.info.modified.has_value()) << "Date modified column";
        EXPECT_EQ(item.info.size.has_value(), !item.info.isFolder) << "Size column: files only";
        if (item.info.canRename)
        {
            EXPECT_FALSE(item.info.editName.empty()) << "inline rename starts from it (T071)";
        }
    }
    // The rename name is the real file name, with the extension even when Explorer hides
    // extensions (IFileOperation::RenameItem takes the raw name).
    const auto& items = m_window->Files().Items();
    EXPECT_TRUE(std::any_of(items.begin(), items.end(), [](const te::FileItem& item) {
        return item.info.editName == L"readme-nested.txt";
    }));
    std::wstring title(256, L'\0');
    title.resize(GetWindowTextW(Hwnd(), title.data(), 256));
    EXPECT_EQ(title, L"Inference Explorer - The PC") << "a fixed title, whatever the folder";
}

// V-3b: into a, b, c; Back twice; Forward; Up; Back.
TEST_F(MainWindowNavigationTest, BackForwardAndUpFollowTheHistory)
{
    const fs::path nested = TestRoot() / L"nested";
    Open(nested);
    OpenListed(L"a");
    OpenListed(L"b");
    OpenListed(L"c");
    ASSERT_TRUE(SamePath(Current(), nested / L"a" / L"b" / L"c"));

    Command(IDM_BACK);
    WaitIdle();
    Command(IDM_BACK);
    WaitIdle();
    EXPECT_TRUE(SamePath(Current(), nested / L"a"));
    Command(IDM_FORWARD);
    WaitIdle();
    EXPECT_TRUE(SamePath(Current(), nested / L"a" / L"b"));

    Command(IDM_FORWARD);
    WaitIdle();
    ASSERT_TRUE(SamePath(Current(), nested / L"a" / L"b" / L"c"));
    Command(IDM_UP);
    WaitIdle();
    EXPECT_TRUE(SamePath(Current(), nested / L"a" / L"b"));
    Command(IDM_BACK);
    WaitIdle();
    EXPECT_TRUE(SamePath(Current(), nested / L"a" / L"b" / L"c"));

    Key(VK_BACK); // Backspace is Back too (UI §4)
    WaitIdle();
    EXPECT_TRUE(SamePath(Current(), nested / L"a" / L"b"));
}

// V-3c: every column in both directions keeps folders first; names are in natural order.
TEST_F(MainWindowNavigationTest, HeaderSortingKeepsFoldersFirstAndNaturalOrder)
{
    const fs::path folder = fs::temp_directory_path() / L"te-nav-sort";
    fs::remove_all(folder);
    fs::create_directories(folder / L"zz-folder");
    for (const wchar_t* name : {L"file10.txt", L"file2.txt", L"file1.txt", L"b.log"})
    {
        std::ofstream(folder / name) << std::string(std::wcslen(name) * 10, 'x');
    }
    Open(folder);
    ASSERT_EQ(m_window->Files().Items().size(), 5u);

    {
        const auto names = ListedNames();
        EXPECT_EQ(names, (std::vector<std::wstring>{L"zz-folder", L"b.log", L"file1.txt", L"file2.txt",
                                                    L"file10.txt"}));
    }
    using Column = te::FileView::Column;
    for (const Column column : {Column::Name, Column::DateModified, Column::Type, Column::Size})
    {
        for (int click = 0; click < 2; ++click)
        {
            ClickHeader(column);
            const te::SortState sort = m_window->Files().Sort();
            SCOPED_TRACE(::testing::Message() << "field " << static_cast<int>(sort.field) << " direction "
                                              << static_cast<int>(sort.direction));
            EXPECT_EQ(static_cast<int>(sort.field), static_cast<int>(column));
            EXPECT_TRUE(m_window->Files().Items().front().info.isFolder) << "folders first";
        }
    }
    // Name, descending, then ascending again: natural order both ways.
    ClickHeader(Column::Name);
    ASSERT_EQ(m_window->Files().Sort().direction, te::SortDirection::Ascending);
    ClickHeader(Column::Name);
    ASSERT_EQ(m_window->Files().Sort().direction, te::SortDirection::Descending);
    EXPECT_EQ(ListedNames(),
              (std::vector<std::wstring>{L"zz-folder", L"file10.txt", L"file2.txt", L"file1.txt", L"b.log"}));

    DestroyWindow(Hwnd());
    m_window.reset();
    fs::remove_all(folder);
}

// V-3e: an unmapped drive and a folder that cannot be listed are recoverable errors; the
// previous folder stays.
TEST_F(MainWindowNavigationTest, UnreachableFoldersKeepThePreviousFolder)
{
    const fs::path nested = TestRoot() / L"nested";
    Open(nested);
    const auto before = ListedNames();
    const std::size_t entries = m_window->History().Entries().size();

    wchar_t drive = 0;
    const DWORD used = GetLogicalDrives();
    for (wchar_t letter = L'Z'; letter >= L'M'; --letter)
    {
        if ((used & (1u << (letter - L'A'))) == 0)
        {
            drive = letter;
            break;
        }
    }
    ASSERT_NE(drive, 0) << "no unmapped drive letter between M: and Z:";
    TypeAddress(std::wstring(1, drive) + L":\\");
    EXPECT_TRUE(SamePath(Current(), nested));
    EXPECT_FALSE(m_window->Address().Error().empty()) << "inline message under the field";
    EXPECT_EQ(ListedNames(), before);

    m_window->Address().CancelEdit();
    TypeAddress((TestRoot() / L"readonly-acl").wstring());
    EXPECT_TRUE(SamePath(Current(), nested));
    EXPECT_EQ(ListedNames(), before);
    EXPECT_EQ(m_window->History().Entries().size(), entries) << "a failed navigation adds no entry";
    EXPECT_FALSE(m_window->NavigationPending());

    // Still usable afterwards.
    OpenListed(L"a");
    EXPECT_TRUE(SamePath(Current(), nested / L"a"));
}

// V-3g: rapid Back / Forward between 10k and nested. After every message the list holds
// only items of the current generation and of the folder the window shows.
TEST_F(MainWindowNavigationTest, RapidBackForwardNeverMixesFolders)
{
    if (!fs::exists(TestRoot() / L"10k"))
    {
        GTEST_SKIP() << "10k test folder missing";
    }
    const fs::path nested = TestRoot() / L"nested";
    const fs::path tenK = TestRoot() / L"10k";
    Open(tenK);
    TypeAddress(nested.wstring());
    ASSERT_TRUE(SamePath(Current(), nested));

    int violations = 0;
    std::size_t checks = 0;
    const auto check = [&] {
        ++checks;
        const te::Generation gen = m_window->Files().CurrentGeneration();
        const bool inTenK = SamePath(Current(), tenK);
        for (const te::FileItem& item : m_window->Files().Items())
        {
            const std::wstring& name = item.info.name;
            const bool tenKName = name.starts_with(L"file") && name.size() >= 9;
            const bool nestedName =
                name == L"a" || name.starts_with(L"readme-nested") || name.starts_with(L"notes-nested");
            if (item.generation != gen || (inTenK ? !tenKName : !nestedName))
            {
                ++violations;
                return;
            }
        }
    };

    // Ten alternations, each posted before the previous one could finish loading.
    for (int i = 0; i < 10; ++i)
    {
        PostMessageW(Hwnd(), WM_COMMAND, (i % 2 == 0) ? IDM_BACK : IDM_FORWARD, 0);
        Pump(30, {}, check);
    }
    Pump(15000, [this] { return !m_window->NavigationPending(); }, check);
    Pump(500, {}, check);

    EXPECT_EQ(violations, 0) << "items of another folder or generation were shown";
    EXPECT_GT(checks, 10u);
    // Ten alternations starting at nested end at nested again.
    EXPECT_TRUE(SamePath(Current(), nested));
    const auto names = ListedNames();
    EXPECT_EQ(std::set<std::wstring>(names.begin(), names.end()), DiskNames(nested));
    EXPECT_EQ(m_window->Address().DisplayText(), nested.wstring()) << "address matches the list";
}

// V-3h (browsing part): Unicode names list, sort and open; the long-path chain can be
// walked with the list and reached directly from the address bar.
TEST_F(MainWindowNavigationTest, UnicodeNamesAndLongPaths)
{
    const fs::path unicode = TestRoot() / L"unicode";
    Open(unicode);
    ASSERT_TRUE(SamePath(Current(), unicode));
    auto names = ListedNames();
    EXPECT_EQ(std::set<std::wstring>(names.begin(), names.end()), DiskNames(unicode));
    for (const te::FileItem& item : m_window->Files().Items())
    {
        EXPECT_FALSE(item.info.name.empty());
    }
    // Sorted by name in both directions.
    Key(VK_HOME);
    ClickHeader(te::FileView::Column::Name); // Name is the default: this reverses it
    const auto descending = ListedNames();
    ClickHeader(te::FileView::Column::Name);
    const auto ascending = ListedNames();
    EXPECT_TRUE(
        std::equal(ascending.begin() + 1, ascending.end(), descending.rbegin(), descending.rend() - 1))
        << "files reverse; the folder stays first";

    // Open the Unicode folder.
    std::wstring folderName;
    for (const auto& entry : fs::directory_iterator(unicode))
    {
        if (entry.is_directory())
        {
            folderName = entry.path().filename().wstring();
        }
    }
    ASSERT_FALSE(folderName.empty());
    OpenListed(folderName);
    EXPECT_TRUE(SamePath(Current(), unicode / folderName));

    // The long path: walk it level by level with the list.
    const fs::path longRoot = TestRoot() / L"longpath";
    TypeAddress(longRoot.wstring());
    ASSERT_TRUE(SamePath(Current(), longRoot));
    fs::path expected = longRoot;
    int levels = 0;
    for (;;)
    {
        names = DisplayNames();
        const auto folder = std::find_if(names.begin(), names.end(),
                                         [](const std::wstring& n) { return n.starts_with(L"segment-"); });
        if (folder == names.end())
        {
            break;
        }
        const std::wstring next = *folder;
        OpenListed(next, true);
        expected /= next;
        ++levels;
        ASSERT_TRUE(SamePath(Current(), expected))
            << "level " << levels << ": " << ::testing::PrintToString(Current()) << " title "
            << ::testing::PrintToString(m_window->CurrentLocation().DisplayName());
    }
    EXPECT_GT(expected.wstring().size(), static_cast<std::size_t>(MAX_PATH)) << "deeper than MAX_PATH";
    names = DisplayNames();
    EXPECT_TRUE(std::any_of(names.begin(), names.end(), [](const std::wstring& n) {
        return n.starts_with(L"deep-file");
    })) << "the file at the bottom of the chain is listed";

    // And directly from the address bar.
    TypeAddress(unicode.wstring());
    ASSERT_TRUE(SamePath(Current(), unicode));
    TypeAddress(expected.wstring());
    EXPECT_TRUE(SamePath(Current(), expected)) << "a path longer than MAX_PATH typed in the address bar";
    EXPECT_EQ(_wcsicmp(m_window->Address().DisplayText().c_str(), expected.wstring().c_str()), 0)
        << "the address shows long names, not the Shell's 8.3 form";
    EXPECT_TRUE(m_window->Address().Error().empty()) << m_window->Address().Error();
}

} // namespace
