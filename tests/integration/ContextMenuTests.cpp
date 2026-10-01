// ContextMenu and ShellNavigator::ShowContextMenu (T069; research R-08; UI contract §4)
// against the real Shell on a fresh temp folder. No menu is shown and no verb is invoked:
// the tracker is replaced so a test picks a command directly, which keeps Shell UI
// (Properties, delete confirmations) off the tester's desktop. Invoking verbs is covered
// manually in V-4.

#include <te/shell/ContextMenu.h>
#include <te/shell/ShellNavigator.h>

#include <shlobj.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

// The command IDs of every item in `menu`, including submenus.
void CollectCommands(HMENU menu, std::vector<UINT>& ids)
{
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i)
    {
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE;
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info) || (info.fType & MFT_SEPARATOR) != 0)
        {
            continue;
        }
        if (info.hSubMenu)
        {
            CollectCommands(info.hSubMenu, ids);
        }
        else if (info.wID >= te::ContextMenu::kFirstCommand && info.wID <= te::ContextMenu::kLastCommand)
        {
            ids.push_back(info.wID);
        }
    }
}

class ContextMenuTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize =
            SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
        // A real (hidden) top-level owner, as the main window is.
        m_owner = CreateWindowExW(0, L"STATIC", L"context menu test", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                                  nullptr, nullptr, nullptr, nullptr);
        ASSERT_NE(m_owner, nullptr);

        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-menu-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root);
        std::ofstream(m_root / L"a.txt") << "context menu";

        ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse(m_root.wstring(), &m_folder));
        te::ShellLocation file;
        ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse((m_root / L"a.txt").wstring(), &file));
        m_item.name = L"a.txt";
        m_item.childPidl.reset(static_cast<ITEMID_CHILD*>(ILClone(ILFindLastID(file.IdList()))));
    }

    void TearDown() override
    {
        if (m_owner)
        {
            DestroyWindow(m_owner);
        }
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    // The verbs of every command a populated menu offers.
    static std::set<std::wstring> Verbs(te::ContextMenu& menu, HMENU hmenu)
    {
        std::vector<UINT> ids;
        CollectCommands(hmenu, ids);
        std::set<std::wstring> verbs;
        for (const UINT id : ids)
        {
            if (auto verb = menu.VerbOf(id))
            {
                for (wchar_t& c : *verb)
                {
                    c = static_cast<wchar_t>(towlower(c));
                }
                verbs.insert(*verb);
            }
        }
        return verbs;
    }

    // The command ID with this verb, or 0.
    static UINT CommandFor(te::ContextMenu& menu, HMENU hmenu, std::wstring_view verb)
    {
        std::vector<UINT> ids;
        CollectCommands(hmenu, ids);
        for (const UINT id : ids)
        {
            if (const auto found = menu.VerbOf(id);
                found && _wcsicmp(found->c_str(), std::wstring(verb).c_str()) == 0)
            {
                return id;
            }
        }
        return 0;
    }

    [[nodiscard]] std::vector<const te::ShellItemInfo*> Items() const
    {
        return {&m_item};
    }

    bool m_uninitialize = false;
    HWND m_owner = nullptr;
    fs::path m_root;
    te::ShellNavigator m_navigator;
    te::ShellLocation m_folder;
    te::ShellItemInfo m_item;
};

TEST_F(ContextMenuTest, ItemMenuOffersTheShellsVerbs)
{
    te::ContextMenu menu;
    ASSERT_HRESULT_SUCCEEDED(menu.Load(m_owner, m_folder, Items()));
    ASSERT_TRUE(menu.IsLoaded());
    const wil::unique_hmenu hmenu(CreatePopupMenu());
    ASSERT_HRESULT_SUCCEEDED(menu.Populate(hmenu.get(), false));
    EXPECT_GT(GetMenuItemCount(hmenu.get()), 0);

    const auto verbs = Verbs(menu, hmenu.get());
    for (const wchar_t* expected : {L"open", L"cut", L"copy", L"delete", L"rename", L"properties"})
    {
        EXPECT_TRUE(verbs.contains(expected)) << ::testing::Message() << L"missing verb " << expected;
    }
}

TEST_F(ContextMenuTest, BackgroundMenuComesFromTheFolder)
{
    te::ContextMenu menu;
    ASSERT_HRESULT_SUCCEEDED(menu.Load(m_owner, m_folder, {}));
    const wil::unique_hmenu hmenu(CreatePopupMenu());
    ASSERT_HRESULT_SUCCEEDED(menu.Populate(hmenu.get(), false));
    EXPECT_GT(GetMenuItemCount(hmenu.get()), 0);
    const auto verbs = Verbs(menu, hmenu.get());
    EXPECT_TRUE(verbs.contains(L"properties") || verbs.contains(L"paste")) << "a folder background menu";
    EXPECT_FALSE(verbs.contains(L"delete")) << "the background is not an item";
}

TEST_F(ContextMenuTest, ExtendedVerbsNeverRemoveCommands)
{
    te::ContextMenu normal;
    ASSERT_HRESULT_SUCCEEDED(normal.Load(m_owner, m_folder, Items()));
    const wil::unique_hmenu normalMenu(CreatePopupMenu());
    ASSERT_HRESULT_SUCCEEDED(normal.Populate(normalMenu.get(), false));

    te::ContextMenu extended;
    ASSERT_HRESULT_SUCCEEDED(extended.Load(m_owner, m_folder, Items()));
    const wil::unique_hmenu extendedMenu(CreatePopupMenu());
    ASSERT_HRESULT_SUCCEEDED(extended.Populate(extendedMenu.get(), true));

    const auto normalVerbs = Verbs(normal, normalMenu.get());
    const auto extendedVerbs = Verbs(extended, extendedMenu.get());
    for (const auto& verb : normalVerbs)
    {
        EXPECT_TRUE(extendedVerbs.contains(verb)) << ::testing::Message() << L"lost with Shift: " << verb;
    }
}

TEST_F(ContextMenuTest, InvalidInputsAreRejected)
{
    te::ContextMenu menu;
    EXPECT_EQ(menu.Load(m_owner, te::ShellLocation{}, Items()), E_INVALIDARG);
    const te::ShellItemInfo noPidl;
    const std::vector<const te::ShellItemInfo*> bad{&noPidl};
    EXPECT_EQ(menu.Load(m_owner, m_folder, bad), E_INVALIDARG);
    EXPECT_FALSE(menu.IsLoaded());
    EXPECT_EQ(menu.Populate(nullptr, false), E_UNEXPECTED) << "not loaded";
    EXPECT_EQ(menu.Show(m_owner, POINT{}, false), E_UNEXPECTED);
    EXPECT_FALSE(menu.VerbOf(1).has_value());
}

TEST_F(ContextMenuTest, ChoosingRenameStartsInlineRenameInsteadOfTheVerb)
{
    int tracked = 0;
    m_navigator.SetMenuTracker([&](HMENU menu, HWND owner, POINT) -> UINT {
        ++tracked;
        EXPECT_EQ(owner, m_owner);
        // Pick "rename" the way the user would, from the populated menu.
        te::ContextMenu probe;
        EXPECT_HRESULT_SUCCEEDED(probe.Load(m_owner, m_folder, Items()));
        const wil::unique_hmenu copy(CreatePopupMenu());
        EXPECT_HRESULT_SUCCEEDED(probe.Populate(copy.get(), false));
        const UINT rename = CommandFor(probe, copy.get(), L"rename");
        EXPECT_NE(rename, 0u);
        // The IDs are assigned identically for the same items and flags; confirm the
        // shown menu has the same command.
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_ID;
        EXPECT_TRUE(GetMenuItemInfoW(menu, rename, FALSE, &info));
        return rename;
    });
    const auto items = Items();
    EXPECT_EQ(m_navigator.ShowContextMenu(m_owner, POINT{10, 10}, m_folder, items, false),
              te::kContextMenuRename);
    EXPECT_EQ(tracked, 1);
    EXPECT_TRUE(fs::exists(m_root / L"a.txt")) << "the rename verb itself did not run";
}

TEST_F(ContextMenuTest, DismissingTheMenuRunsNothing)
{
    bool forwarded = false;
    m_navigator.SetMenuTracker([&](HMENU menu, HWND, POINT) -> UINT {
        // While the menu is open, menu messages reach the Shell's handler. It fills its
        // own submenus ("Send to", "Open with") when they open, and declines the rest.
        const int count = GetMenuItemCount(menu);
        for (int i = 0; i < count && !forwarded; ++i)
        {
            if (const HMENU submenu = GetSubMenu(menu, i))
            {
                LRESULT result = 0;
                forwarded = m_navigator.HandleMenuMessage(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(submenu),
                                                          MAKELPARAM(i, FALSE), &result);
            }
        }
        return 0; // Escape / click outside
    });
    const auto items = Items();
    EXPECT_EQ(m_navigator.ShowContextMenu(m_owner, POINT{10, 10}, m_folder, items, false), S_FALSE);
    EXPECT_TRUE(forwarded) << "a submenu's WM_INITMENUPOPUP handled by IContextMenu3/2 while open";
    LRESULT result = 0;
    EXPECT_FALSE(m_navigator.HandleMenuMessage(WM_INITMENUPOPUP, 0, 0, &result)) << "no menu open any more";
    EXPECT_TRUE(fs::exists(m_root / L"a.txt"));
}

} // namespace
