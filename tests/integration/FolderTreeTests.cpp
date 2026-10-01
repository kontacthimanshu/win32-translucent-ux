// The navigation pane's folder tree (T090; FR-021): FolderTreeLoader lists folders only
// (no files, no zip folders), sorted naturally, with icons, on its own worker; the pane
// inserts a node's subfolders below it, collapses and re-expands without listing again,
// turns an empty folder into a leaf, answers tree navigation (parent, children, siblings),
// takes Left / Right like Explorer's tree, toggles on the chevron without navigating,
// reopens remembered folders after a refresh, and is a UI Automation Tree whose TreeItems
// nest and expand through ExpandCollapse.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): the UI Automation
// test shares UI Automation's process-wide state (see UiaFileListTests.cpp).

#include <te/app/MainWindow.h>
#include <te/core/Messages.h>
#include <te/shell/FolderTreeLoader.h>
#include <te/ui/NavigationPane.h>

#include "ScreenCapture.h"
#include <UIAutomationClient.h>
#include <shlobj.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;

te::ShellLocation At(const fs::path& path)
{
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    EXPECT_HRESULT_SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, wil::out_param(pidl), 0, nullptr));
    te::ShellLocation location;
    EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
    return location;
}

class FolderTreeTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize =
            SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-tree-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        // root: folder10, folder2 (with a1, a2), empty, a file and a zip file; other: one folder.
        fs::create_directories(m_root / L"root" / L"folder2" / L"a1");
        fs::create_directories(m_root / L"root" / L"folder2" / L"a2");
        fs::create_directories(m_root / L"root" / L"folder10");
        fs::create_directories(m_root / L"root" / L"empty");
        std::ofstream(m_root / L"root" / L"file.txt") << "x";
        std::ofstream(m_root / L"root" / L"archive.zip") << "";
        fs::create_directories(m_root / L"other" / L"inside");

        m_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
        m_pane.Attach(m_window, 96);
        m_pane.SetBounds(D2D1::RectF(0.0f, 0.0f, 260.0f, 600.0f));
        m_pane.SetNavigateCallback([this](const te::ShellLocation& l) { m_navigated.push_back(l); });
        m_pane.SetTreeChangedCallback([this] { ++m_treeChanges; });
        m_pane.SetPlacesForTesting({At(m_root / L"root"), At(m_root / L"other")});
    }

    void TearDown() override
    {
        m_pane.ShutdownLoader();
        if (m_window)
        {
            te::MainWindow::DrainPendingMessages(m_window);
            DestroyWindow(m_window);
        }
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    // Delivers loader results to the pane, as MainWindow does, until nothing is loading.
    void Settle(DWORD timeoutMs = 10000)
    {
        const ULONGLONG until = GetTickCount64() + timeoutMs;
        MSG msg{};
        while (GetTickCount64() < until)
        {
            while (PeekMessageW(&msg, m_window, te::WM_TE_TREE_CHILDREN, te::WM_TE_TREE_CHILDREN, PM_REMOVE))
            {
                std::unique_ptr<te::FolderChildren> children = te::TakeOwned<te::FolderChildren>(msg.lParam);
                m_pane.OnChildren(*children);
            }
            bool loading = false;
            for (const auto& entry : m_pane.Entries())
            {
                loading = loading || entry.loading;
            }
            if (!loading)
            {
                return;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_POSTMESSAGE);
        }
        ADD_FAILURE() << "the tree was still loading";
    }

    std::vector<std::wstring> Rows() const
    {
        std::vector<std::wstring> rows;
        for (const auto& entry : m_pane.Entries())
        {
            rows.push_back(std::wstring(static_cast<std::size_t>(entry.depth) * 2, L' ') +
                           entry.location.DisplayName());
        }
        return rows;
    }

    bool m_uninitialize = false;
    fs::path m_root;
    HWND m_window = nullptr;
    te::NavigationPane m_pane;
    std::vector<te::ShellLocation> m_navigated;
    int m_treeChanges = 0;
};

TEST_F(FolderTreeTest, TheLoaderListsFoldersOnlyInNaturalOrderWithIcons)
{
    te::FolderTreeLoader loader;
    loader.Request(m_window, 42, At(m_root / L"root"), 16);
    MSG msg{};
    const ULONGLONG until = GetTickCount64() + 10000;
    std::unique_ptr<te::FolderChildren> result;
    while (!result && GetTickCount64() < until)
    {
        if (PeekMessageW(&msg, m_window, te::WM_TE_TREE_CHILDREN, te::WM_TE_TREE_CHILDREN, PM_REMOVE))
        {
            result = te::TakeOwned<te::FolderChildren>(msg.lParam);
        }
        else
        {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_POSTMESSAGE);
        }
    }
    ASSERT_TRUE(result);
    EXPECT_EQ(result->nodeId, 42u);
    EXPECT_HRESULT_SUCCEEDED(result->hr);
    std::vector<std::wstring> names;
    for (const auto& child : result->children)
    {
        names.push_back(child.location.DisplayName());
        EXPECT_TRUE(child.icon) << "an icon for " << child.location.ParsingPath().value_or(L"?").size();
    }
    EXPECT_EQ(names, (std::vector<std::wstring>{L"empty", L"folder2", L"folder10"}))
        << "folders only (no file, no zip), natural order";
    ASSERT_EQ(result->children.size(), 3u);
    EXPECT_TRUE(result->children[1].hasSubfolders) << "folder2 has a1 and a2";
}

TEST_F(FolderTreeTest, ExpandInsertsSubfoldersAndCollapseKeepsThem)
{
    ASSERT_EQ(Rows(), (std::vector<std::wstring>{L"root", L"other"}));
    m_pane.Expand(0);
    EXPECT_TRUE(m_pane.Entries()[0].loading);
    Settle();
    EXPECT_EQ(Rows(),
              (std::vector<std::wstring>{L"root", L"  empty", L"  folder2", L"  folder10", L"other"}));
    EXPECT_GT(m_treeChanges, 0) << "UI Automation hears of the new rows";

    m_pane.Expand(2); // folder2
    Settle();
    EXPECT_EQ(Rows(), (std::vector<std::wstring>{L"root", L"  empty", L"  folder2", L"    a1", L"    a2",
                                                 L"  folder10", L"other"}));

    // Tree navigation over the rows.
    EXPECT_EQ(m_pane.ParentOf(3), 2u);
    EXPECT_EQ(m_pane.ParentOf(2), 0u);
    EXPECT_FALSE(m_pane.ParentOf(0).has_value());
    EXPECT_EQ(m_pane.FirstChildOf(0), 1u);
    EXPECT_EQ(m_pane.LastChildOf(0), 5u) << "folder10, after folder2's subtree";
    EXPECT_EQ(m_pane.NextSiblingOf(2), 5u);
    EXPECT_EQ(m_pane.PreviousSiblingOf(5), 2u);
    EXPECT_EQ(m_pane.NextSiblingOf(0), 6u) << "other";
    EXPECT_FALSE(m_pane.NextSiblingOf(4).has_value()) << "a2 is folder2's last child";

    // Collapse the root: its rows are kept, with folder2 still open, and come back at once.
    const std::uint64_t folder2 = m_pane.Entries()[2].id;
    m_pane.Collapse(0);
    EXPECT_EQ(Rows(), (std::vector<std::wstring>{L"root", L"other"}));
    m_pane.Expand(0);
    EXPECT_FALSE(m_pane.Entries()[0].loading) << "no second listing";
    EXPECT_EQ(Rows(), (std::vector<std::wstring>{L"root", L"  empty", L"  folder2", L"    a1", L"    a2",
                                                 L"  folder10", L"other"}));
    EXPECT_EQ(m_pane.IndexOfNode(folder2), 2u) << "the same node";
}

TEST_F(FolderTreeTest, AnEmptyFolderBecomesALeaf)
{
    m_pane.Expand(0);
    Settle();
    ASSERT_EQ(m_pane.Entries()[1].location.DisplayName(), L"empty");
    m_pane.Expand(1);
    Settle();
    EXPECT_FALSE(m_pane.Entries()[1].expandable);
    EXPECT_FALSE(m_pane.Entries()[1].expanded);
    EXPECT_EQ(m_pane.Entries().size(), 5u) << "nothing inserted";
}

TEST_F(FolderTreeTest, LeftAndRightWorkAsInExplorersTree)
{
    m_pane.SetFocused(true);
    m_pane.OnKeyDown(VK_HOME);
    ASSERT_EQ(m_pane.FocusIndex(), 0u);
    m_pane.OnKeyDown(VK_RIGHT); // expand
    Settle();
    EXPECT_TRUE(m_pane.Entries()[0].expanded);
    EXPECT_EQ(m_pane.FocusIndex(), 0u);
    m_pane.OnKeyDown(VK_RIGHT); // to the first child
    EXPECT_EQ(m_pane.FocusIndex(), 1u);
    m_pane.OnKeyDown(VK_LEFT); // "empty" is collapsed: to the parent
    EXPECT_EQ(m_pane.FocusIndex(), 0u);
    m_pane.OnKeyDown(VK_LEFT); // collapse
    EXPECT_FALSE(m_pane.Entries()[0].expanded);
    EXPECT_EQ(m_pane.Entries().size(), 2u);
    EXPECT_TRUE(m_navigated.empty()) << "expanding and collapsing never navigates";
}

TEST_F(FolderTreeTest, TheChevronTogglesAndTheNameNavigates)
{
    const D2D1_RECT_F chevron = m_pane.ExpanderRect(0);
    m_pane.OnPointerDown(
        D2D1::Point2F((chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2));
    Settle();
    EXPECT_TRUE(m_pane.Entries()[0].expanded);
    EXPECT_TRUE(m_navigated.empty()) << "the chevron does not navigate";

    const D2D1_RECT_F row = m_pane.EntryRect(2); // folder2's name
    m_pane.OnPointerDown(D2D1::Point2F(row.right - 40.0f, (row.top + row.bottom) / 2));
    ASSERT_EQ(m_navigated.size(), 1u);
    EXPECT_EQ(m_navigated[0].DisplayName(), L"folder2");

    m_pane.OnPointerDown(
        D2D1::Point2F((chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2));
    EXPECT_FALSE(m_pane.Entries()[0].expanded);
}

TEST_F(FolderTreeTest, OpenFoldersOpenAgainAfterARefresh)
{
    m_pane.Expand(0);
    Settle();
    m_pane.Expand(2); // folder2
    Settle();
    // A drive arrives: the places are rebuilt, the open folders load and open again.
    m_pane.SetPlacesForTesting({At(m_root / L"root"), At(m_root / L"other")});
    Settle();
    Settle(); // folder2 opens once root's children are in
    EXPECT_EQ(Rows(), (std::vector<std::wstring>{L"root", L"  empty", L"  folder2", L"    a1", L"    a2",
                                                 L"  folder10", L"other"}));
}

TEST_F(FolderTreeTest, TheCurrentFolderIsHighlightedInsideTheTree)
{
    m_pane.Expand(0);
    Settle();
    m_pane.SetCurrent(At(m_root / L"root" / L"folder10"));
    EXPECT_EQ(m_pane.CurrentIndex(), 3u);
    m_pane.Collapse(0);
    EXPECT_FALSE(m_pane.CurrentIndex().has_value()) << "no longer on screen";
    m_pane.Expand(0);
    EXPECT_EQ(m_pane.CurrentIndex(), 3u);
}

// UI Automation, through the real client, on the real window.
TEST(FolderTreeUia, TheTreeNestsAndExpandsThroughExpandCollapse)
{
    const bool ole = SUCCEEDED(OleInitialize(nullptr));
    GUID guid{};
    ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
    wchar_t name[40]{};
    swprintf_s(name, L"te-treeuia-%08lx", guid.Data1);
    const fs::path root = fs::temp_directory_path() / name;
    fs::create_directories(root / L"alpha");
    fs::create_directories(root / L"beta");
    {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"tree UIA test";
        options.quitOnDestroy = false;
        te::MainWindow window(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(window.Create(SW_SHOWNOACTIVATE));
        window.Places().SetPlacesForTesting({At(root)});

        const auto pump = [](DWORD ms, const std::function<bool()>& done) {
            const ULONGLONG until = GetTickCount64() + ms;
            MSG msg{};
            while (GetTickCount64() < until && !(done && done()))
            {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            }
        };
        const auto withClient = [&](const std::function<void(IUIAutomation*)>& client) {
            std::atomic<bool> done{false};
            std::thread thread([&] {
                if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
                {
                    {
                        wil::com_ptr<IUIAutomation> uia;
                        if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                                       IID_PPV_ARGS(&uia))))
                        {
                            client(uia.get());
                        }
                    }
                    CoUninitialize();
                }
                done = true;
            });
            pump(30000, [&] { return done.load(); });
            thread.join();
        };

        std::wstring paneName;
        CONTROLTYPEID paneType = 0;
        CONTROLTYPEID itemType = 0;
        ExpandCollapseState before = ExpandCollapseState_LeafNode;
        withClient([&](IUIAutomation* uia) {
            wil::com_ptr<IUIAutomationElement> element;
            uia->ElementFromHandle(window.Hwnd(), &element);
            wil::com_ptr<IUIAutomationTreeWalker> walker;
            uia->get_ControlViewWalker(&walker);
            // The pane, then its first item.
            wil::com_ptr<IUIAutomationElement> child;
            walker->GetFirstChildElement(element.get(), &child);
            while (child)
            {
                CONTROLTYPEID type = 0;
                child->get_CurrentControlType(&type);
                if (type == UIA_TreeControlTypeId)
                {
                    break;
                }
                wil::com_ptr<IUIAutomationElement> next;
                walker->GetNextSiblingElement(child.get(), &next);
                child = std::move(next);
            }
            ASSERT_TRUE(child);
            wil::unique_bstr text;
            child->get_CurrentName(&text);
            paneName = text ? text.get() : L"";
            child->get_CurrentControlType(&paneType);
            wil::com_ptr<IUIAutomationElement> item;
            walker->GetFirstChildElement(child.get(), &item);
            ASSERT_TRUE(item);
            item->get_CurrentControlType(&itemType);
            wil::com_ptr<IUIAutomationExpandCollapsePattern> expand;
            ASSERT_HRESULT_SUCCEEDED(
                item->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand)));
            ASSERT_TRUE(expand);
            expand->get_CurrentExpandCollapseState(&before);
            ASSERT_HRESULT_SUCCEEDED(expand->Expand()); // posted to the window
        });
        EXPECT_EQ(paneName, L"Navigation pane");
        EXPECT_EQ(paneType, UIA_TreeControlTypeId);
        EXPECT_EQ(itemType, UIA_TreeItemControlTypeId);
        EXPECT_EQ(before, ExpandCollapseState_Collapsed);

        pump(10000, [&] { return window.Places().Entries().size() == 3; });
        ASSERT_EQ(window.Places().Entries().size(), 3u) << "alpha and beta under the place";

        std::vector<std::wstring> children;
        ExpandCollapseState after = ExpandCollapseState_LeafNode;
        std::wstring parentOfFirst;
        withClient([&](IUIAutomation* uia) {
            wil::com_ptr<IUIAutomationElement> element;
            uia->ElementFromHandle(window.Hwnd(), &element);
            wil::com_ptr<IUIAutomationTreeWalker> walker;
            uia->get_ControlViewWalker(&walker);
            wil::com_ptr<IUIAutomationCondition> isItem;
            wil::unique_variant type;
            type.vt = VT_I4;
            type.lVal = UIA_TreeItemControlTypeId;
            uia->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &isItem);
            wil::com_ptr<IUIAutomationElement> place;
            element->FindFirst(TreeScope_Descendants, isItem.get(), &place);
            ASSERT_TRUE(place);
            wil::com_ptr<IUIAutomationExpandCollapsePattern> expand;
            place->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand));
            ASSERT_TRUE(expand);
            expand->get_CurrentExpandCollapseState(&after);
            wil::com_ptr<IUIAutomationElement> child;
            walker->GetFirstChildElement(place.get(), &child);
            while (child)
            {
                wil::unique_bstr text;
                child->get_CurrentName(&text);
                children.push_back(text ? text.get() : L"");
                if (children.size() == 1)
                {
                    wil::com_ptr<IUIAutomationElement> parent;
                    walker->GetParentElement(child.get(), &parent);
                    wil::unique_bstr parentName;
                    if (parent)
                    {
                        parent->get_CurrentName(&parentName);
                    }
                    parentOfFirst = parentName ? parentName.get() : L"";
                }
                wil::com_ptr<IUIAutomationElement> next;
                walker->GetNextSiblingElement(child.get(), &next);
                child = std::move(next);
            }
        });
        EXPECT_EQ(after, ExpandCollapseState_Expanded);
        EXPECT_EQ(children, (std::vector<std::wstring>{L"alpha", L"beta"})) << "nested under their place";
        EXPECT_EQ(parentOfFirst, root.filename().wstring());
        DestroyWindow(window.Hwnd());
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    if (ole)
    {
        OleUninitialize();
    }
}

// Manual visual check (not run by CTest): the tree over %TEMP%\te-test, three levels
// open, saved as te-tree.bmp in %TE_CAPTURE_DIR% (or %TEMP%). Needs the test data
// (tools/New-TestData.ps1). Run with --gtest_also_run_disabled_tests.
TEST(FolderTreeUia, DISABLED_CaptureTheTree)
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const fs::path data = fs::path(temp) / L"te-test";
    ASSERT_TRUE(fs::exists(data / L"nested" / L"a")) << "run tools/New-TestData.ps1";
    const bool ole = SUCCEEDED(OleInitialize(nullptr));
    {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"Translucent Explorer";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = (data / L"nested" / L"a").wstring();
        te::MainWindow window(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(window.Create(SW_SHOWNORMAL));
        const auto pump = [](DWORD ms) {
            const ULONGLONG until = GetTickCount64() + ms;
            MSG msg{};
            while (GetTickCount64() < until)
            {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            }
        };
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window.Hwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT& work = monitor.rcWork;
        SetWindowPos(window.Hwnd(), HWND_TOPMOST, work.left + 20, work.top + 20, (work.right - work.left) / 2,
                     (work.bottom - work.top) * 6 / 10, SWP_NOACTIVATE);
        window.Places().SetPlacesForTesting({At(data), At(fs::path(temp))});
        window.Places().SetCurrent(At(data / L"nested" / L"a"));
        auto& pane = window.Places();
        const auto open = [&](const std::wstring& name) {
            for (std::size_t i = 0; i < pane.Entries().size(); ++i)
            {
                if (pane.Entries()[i].location.DisplayName() == name)
                {
                    pane.Expand(i);
                    pump(1500);
                    return;
                }
            }
        };
        open(L"te-test");
        open(L"nested");
        open(L"a");
        SendMessageW(window.Hwnd(), WM_KEYDOWN, VK_F6, 0); // focus cues
        SendMessageW(window.Hwnd(), WM_KEYDOWN, VK_F6, 0);
        SendMessageW(window.Hwnd(), WM_KEYDOWN, VK_F6, 0); // the pane
        RedrawWindow(window.Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        pump(600);
        te::test::SaveScreen(window.Hwnd(), L"te-tree.bmp");
        SetWindowPos(window.Hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        DestroyWindow(window.Hwnd());
    }
    if (ole)
    {
        OleUninitialize();
    }
}

} // namespace
