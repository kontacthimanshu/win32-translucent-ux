// ShellNavigator (T057; research R-08, R-11, UI contract §6) against the real Shell:
// parsing with trimming, quotes and environment variables; parse failures; the initial
// location fallbacks; and Open for folders and for a file with no associated app. The
// "Open with..." prompt is injected, so no modal UI appears.

#include <te/shell/ShellNavigator.h>

#include <shlobj.h>
#include <shlwapi.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

namespace fs = std::filesystem;

class ShellNavigatorTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize =
            SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    }
    void TearDown() override
    {
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    static std::wstring Path(const te::ShellLocation& location)
    {
        return location.ParsingPath().value_or(L"");
    }

    static te::ShellLocation KnownFolder(REFKNOWNFOLDERID id)
    {
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        EXPECT_HRESULT_SUCCEEDED(SHGetKnownFolderIDList(id, 0, nullptr, wil::out_param(pidl)));
        te::ShellLocation location;
        EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
        return location;
    }

    bool m_uninitialize = false;
    te::ShellNavigator m_navigator;
};

TEST_F(ShellNavigatorTest, ParsesPlainPaths)
{
    te::ShellLocation location;
    ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse(L"C:\\Windows", &location));
    EXPECT_EQ(_wcsicmp(Path(location).c_str(), L"C:\\Windows"), 0);
}

TEST_F(ShellNavigatorTest, TrimsSpacesAndQuotesAndExpandsVariables)
{
    te::ShellLocation location;
    ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse(L"   \"%SystemRoot%\\System32\"  ", &location));
    wchar_t expected[MAX_PATH]{};
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32", expected, MAX_PATH);
    EXPECT_EQ(_wcsicmp(Path(location).c_str(), expected), 0);
}

TEST_F(ShellNavigatorTest, ParsesVirtualFolders)
{
    te::ShellLocation location;
    ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse(L"shell:MyComputerFolder", &location));
    EXPECT_TRUE(location == KnownFolder(FOLDERID_ComputerFolder));
}

TEST_F(ShellNavigatorTest, ParseFailureLeavesTheLocationUnchanged)
{
    te::ShellLocation location;
    ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse(L"C:\\Windows", &location));
    const HRESULT hr = m_navigator.Parse(L"C:\\definitely\\not\\here\\te-4f1c", &location);
    EXPECT_TRUE(FAILED(hr));
    EXPECT_EQ(_wcsicmp(Path(location).c_str(), L"C:\\Windows"), 0) << "unchanged on failure (spec US3-6)";
    EXPECT_EQ(m_navigator.Parse(L"   ", &location), E_INVALIDARG);
    EXPECT_EQ(m_navigator.Parse(L"C:\\", nullptr), E_POINTER);
}

TEST_F(ShellNavigatorTest, ParentMatchesTheLocationParent)
{
    te::ShellLocation location;
    ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse(L"C:\\Windows\\System32", &location));
    const auto parent = m_navigator.Parent(location);
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(_wcsicmp(Path(*parent).c_str(), L"C:\\Windows"), 0);
}

TEST_F(ShellNavigatorTest, InitialLocationPrefersAValidCommandLineFolder)
{
    const te::ShellLocation location = m_navigator.InitialLocation(L"C:\\Windows");
    EXPECT_EQ(_wcsicmp(Path(location).c_str(), L"C:\\Windows"), 0);
}

TEST_F(ShellNavigatorTest, InitialLocationFallsBackToThisPc)
{
    const te::ShellLocation thisPc = KnownFolder(FOLDERID_ComputerFolder);
    EXPECT_TRUE(m_navigator.InitialLocation(std::nullopt) == thisPc);
    EXPECT_TRUE(m_navigator.InitialLocation(L"Z:\\no\\such\\folder\\te-4f1c") == thisPc);
    wchar_t notepad[MAX_PATH]{};
    ExpandEnvironmentStringsW(L"%SystemRoot%\\notepad.exe", notepad, MAX_PATH);
    EXPECT_TRUE(m_navigator.InitialLocation(notepad) == thisPc) << "a file is not a folder to open";
}

TEST_F(ShellNavigatorTest, OpeningAFolderAsksTheCallerToNavigate)
{
    te::ShellItemInfo folderItem;
    folderItem.isFolder = true;
    folderItem.name = L"System32";
    EXPECT_EQ(m_navigator.Open(nullptr, KnownFolder(FOLDERID_Windows), folderItem), S_FALSE);
}

TEST_F(ShellNavigatorTest, NoAssociationOffersOpenWith)
{
    // A fresh extension every run: once Windows has shown its own "How do you want to
    // open" window for a type, it records an association, so a fixed name would only
    // work once.
    GUID guid{};
    ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
    wchar_t extension[32]{};
    swprintf_s(extension, L".te%08lx", guid.Data1);
    const std::wstring fileName = std::wstring(L"sample") + extension;

    // Never risk system UI in a test: if the type is somehow associated, skip.
    wchar_t command[MAX_PATH * 2]{};
    DWORD length = static_cast<DWORD>(std::size(command));
    if (AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_COMMAND, extension, nullptr, command,
                          &length) != HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION))
    {
        GTEST_SKIP() << "the test extension unexpectedly has an association";
    }

    const fs::path dir = fs::temp_directory_path() / L"te-navigator";
    fs::create_directories(dir);
    const fs::path file = dir / fileName;
    {
        std::ofstream(file) << "no app";
    }

    int asked = 0;
    std::wstring askedName;
    te::ShellNavigator navigator({}, [&](HWND, const std::wstring& name) {
        ++asked;
        askedName = name;
        return false; // the user cancels
    });

    te::ShellLocation folder;
    ASSERT_HRESULT_SUCCEEDED(navigator.Parse(dir.wstring(), &folder));
    te::ShellLocation fileLocation;
    ASSERT_HRESULT_SUCCEEDED(navigator.Parse(file.wstring(), &fileLocation));
    te::ShellItemInfo item;
    item.name = fileName;
    item.childPidl.reset(static_cast<ITEMID_CHILD*>(ILClone(ILFindLastID(fileLocation.IdList()))));

    const HRESULT hr = navigator.Open(nullptr, folder, item);
    EXPECT_EQ(hr, HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION));
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(askedName, fileName);

    fs::remove_all(dir);
}

TEST_F(ShellNavigatorTest, MenuMessagesAreNotHandledWithoutAnOpenMenu)
{
    // With a menu open they are forwarded (ContextMenuTests); otherwise the window
    // handles them itself.
    LRESULT result = 0;
    EXPECT_FALSE(m_navigator.HandleMenuMessage(WM_INITMENUPOPUP, 0, 0, &result));
    EXPECT_FALSE(m_navigator.HandleMenuMessage(WM_DRAWITEM, 0, 0, &result));
}

} // namespace

namespace
{

TEST_F(ShellNavigatorTest, ResolvesDotDotInFileSystemPaths)
{
    te::ShellLocation location;
    ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse(LR"(C:\Windows\System32\..\.\System32\..)", &location));
    EXPECT_EQ(_wcsicmp(Path(location).c_str(), LR"(C:\Windows)"), 0);
}

TEST_F(ShellNavigatorTest, ParsesPathsLongerThanMaxPath)
{
    // Canonicalizing a path past MAX_PATH adds \\?\, which SHParseDisplayName rejects
    // (found in T066, V-3h).
    const fs::path deep = fs::temp_directory_path() / L"te-navigator-long" / std::wstring(120, L'd') /
                          std::wstring(120, L'e') / std::wstring(60, L'f');
    ASSERT_GT(deep.wstring().size(), static_cast<std::size_t>(MAX_PATH));
    ASSERT_TRUE(
        CreateDirectoryW((LR"(\\?\)" + (fs::temp_directory_path() / L"te-navigator-long").wstring()).c_str(),
                         nullptr) ||
        GetLastError() == ERROR_ALREADY_EXISTS);
    std::wstring level = LR"(\\?\)" + (fs::temp_directory_path() / L"te-navigator-long").wstring();
    for (const std::wstring& part :
         {std::wstring(120, L'd'), std::wstring(120, L'e'), std::wstring(60, L'f')})
    {
        level += L"\\" + part;
        ASSERT_TRUE(CreateDirectoryW(level.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
    }

    te::ShellLocation location;
    EXPECT_HRESULT_SUCCEEDED(m_navigator.Parse(deep.wstring(), &location));
    EXPECT_TRUE(location.IsValid());
    te::ShellLocation prefixed;
    EXPECT_HRESULT_SUCCEEDED(m_navigator.Parse(LR"(\\?\)" + deep.wstring(), &prefixed))
        << "a typed \\\\?\\ prefix";
    EXPECT_TRUE(location == prefixed);

    fs::remove_all(LR"(\\?\)" + (fs::temp_directory_path() / L"te-navigator-long").wstring());
}

} // namespace
