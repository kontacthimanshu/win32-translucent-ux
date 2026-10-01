// ShellLocation (T053; data-model: Location) with real Shell folders: display strings and
// attributes, parent chain up to the Desktop root, identity by ILIsEqual, copies that own
// their own PIDL, and a virtual folder (This PC).

#include <te/shell/ShellTypes.h>

#include <shlobj.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{

namespace fs = std::filesystem;

class ShellLocationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_uninitialize = SUCCEEDED(hr);
    }
    void TearDown() override
    {
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    static te::ShellLocation FromPath(const std::wstring& path)
    {
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        EXPECT_HRESULT_SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, wil::out_param(pidl), 0, nullptr));
        te::ShellLocation location;
        EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
        return location;
    }

    static std::wstring TempFolder()
    {
        return fs::temp_directory_path().lexically_normal().wstring();
    }

    bool m_uninitialize = false;
};

TEST_F(ShellLocationTest, FileSystemFolderCapturesStringsAndAttributes)
{
    const fs::path folder = fs::temp_directory_path() / L"te-shelllocation";
    fs::create_directories(folder);
    const te::ShellLocation location = FromPath(folder.wstring());
    ASSERT_TRUE(location.IsValid());
    EXPECT_EQ(location.DisplayName(), L"te-shelllocation");
    ASSERT_TRUE(location.ParsingPath().has_value());
    EXPECT_EQ(_wcsicmp(location.ParsingPath()->c_str(), folder.wstring().c_str()), 0);
    EXPECT_TRUE(location.IsFileSystem());
    fs::remove(folder);
}

TEST_F(ShellLocationTest, EmptyLocationIsInvalid)
{
    const te::ShellLocation empty;
    EXPECT_FALSE(empty.IsValid());
    EXPECT_FALSE(empty.Parent().has_value());
    wil::com_ptr<IShellItem> item;
    EXPECT_FALSE(SUCCEEDED(empty.Item(&item)));
    EXPECT_EQ(te::ShellLocation::FromIdList(nullptr, nullptr), E_POINTER);
}

TEST_F(ShellLocationTest, EqualityIsIdentityNotObject)
{
    const te::ShellLocation a = FromPath(TempFolder());
    const te::ShellLocation b = FromPath(TempFolder());
    const te::ShellLocation windows = FromPath(L"C:\\Windows");
    EXPECT_TRUE(a == b) << "two parses of the same folder are the same location";
    EXPECT_FALSE(a == windows);
    EXPECT_TRUE(te::ShellLocation{} == te::ShellLocation{});
    EXPECT_FALSE(a == te::ShellLocation{});
}

TEST_F(ShellLocationTest, CopyOwnsItsOwnIdList)
{
    te::ShellLocation original = FromPath(L"C:\\Windows");
    te::ShellLocation copy = original;
    EXPECT_TRUE(copy == original);
    EXPECT_NE(copy.IdList(), original.IdList()) << "cloned, not shared";
    EXPECT_EQ(copy.DisplayName(), original.DisplayName());

    te::ShellLocation assigned;
    assigned = original;
    EXPECT_TRUE(assigned == original);
    original = te::ShellLocation{}; // the copies stay valid
    EXPECT_TRUE(copy.IsValid());
    EXPECT_TRUE(assigned.IsValid());
}

TEST_F(ShellLocationTest, ParentChainEndsAtTheDesktopRoot)
{
    te::ShellLocation location = FromPath(L"C:\\Windows\\System32");
    const std::optional<te::ShellLocation> windows = location.Parent();
    ASSERT_TRUE(windows.has_value());
    EXPECT_TRUE(*windows == FromPath(L"C:\\Windows"));

    // Walk up: C:\Windows -> C:\ -> This PC -> Desktop, which has no parent.
    std::optional<te::ShellLocation> current = windows;
    int steps = 0;
    while (current && steps < 10)
    {
        std::optional<te::ShellLocation> next = current->Parent();
        if (!next)
        {
            break;
        }
        current = std::move(next);
        ++steps;
    }
    ASSERT_TRUE(current.has_value());
    EXPECT_TRUE(ILIsEmpty(current->IdList())) << "the root is the Desktop (empty ID list)";
    EXPECT_FALSE(current->Parent().has_value());
    EXPECT_GE(steps, 3);
}

TEST_F(ShellLocationTest, VirtualFolderHasAParsingNameButIsNotFileSystem)
{
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    ASSERT_HRESULT_SUCCEEDED(
        SHGetKnownFolderIDList(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT, nullptr, wil::out_param(pidl)));
    te::ShellLocation thisPc;
    ASSERT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &thisPc));
    EXPECT_FALSE(thisPc.IsFileSystem());
    EXPECT_FALSE(thisPc.DisplayName().empty());
    ASSERT_TRUE(thisPc.ParsingPath().has_value());
    EXPECT_TRUE(thisPc.ParsingPath()->starts_with(L"::{")) << "a CLSID parsing name";
}

TEST_F(ShellLocationTest, ItemIsCreatedLazilyAndReused)
{
    const te::ShellLocation location = FromPath(L"C:\\Windows");
    wil::com_ptr<IShellItem> first, second;
    ASSERT_HRESULT_SUCCEEDED(location.Item(&first));
    ASSERT_HRESULT_SUCCEEDED(location.Item(&second));
    EXPECT_EQ(first.get(), second.get());
    wil::unique_cotaskmem_string path;
    ASSERT_HRESULT_SUCCEEDED(first->GetDisplayName(SIGDN_FILESYSPATH, &path));
    EXPECT_EQ(_wcsicmp(path.get(), L"C:\\Windows"), 0);
}

} // namespace
