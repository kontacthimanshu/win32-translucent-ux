// The address bar's breadcrumbs (T091; FR-021): the segments are the location's ancestors
// from This PC down (the Desktop root left out); clicking a segment navigates to it; the
// chevron after a segment lists its subfolders in a menu (listed off the UI thread), and
// choosing one navigates; in a narrow field the leading segments move behind an overflow
// button whose menu lists them; clicking empty space still starts editing; and the real
// window navigates from a segment. Menus are answered through the menu tracker hook, so no
// modal menu opens.

#include <te/app/MainWindow.h>
#include <te/core/Messages.h>
#include <te/render/TextFormats.h>
#include <te/ui/AddressBar.h>

#include <shlobj.h>

#include "ScreenCapture.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
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

std::wstring ThisPcName()
{
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, wil::out_param(pidl));
    te::ShellLocation location;
    te::ShellLocation::FromIdList(pidl.get(), &location);
    return location.DisplayName();
}

D2D1_POINT_2F Center(const D2D1_RECT_F& r)
{
    return D2D1::Point2F((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f);
}

// The texts of a popup menu's items.
std::vector<std::wstring> Items(HMENU menu)
{
    std::vector<std::wstring> items;
    for (int i = 0; i < GetMenuItemCount(menu); ++i)
    {
        wchar_t text[512]{};
        GetMenuStringW(menu, static_cast<UINT>(i), text, 512, MF_BYPOSITION);
        items.emplace_back(text);
    }
    return items;
}

class BreadcrumbTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize =
            SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-crumbs-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root / L"one" / L"two" / L"three");
        fs::create_directories(m_root / L"one" / L"sibling");

        m_parent = CreateWindowExW(0, L"STATIC", L"parent", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 100, 100,
                                   1200, 300, nullptr, nullptr, nullptr, nullptr);
        ASSERT_NE(m_parent, nullptr);
        ASSERT_HRESULT_SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                                     reinterpret_cast<IUnknown**>(m_dwrite.put())));
        m_formats = std::make_unique<te::TextFormats>(m_dwrite.get());
        ASSERT_HRESULT_SUCCEEDED(m_formats->Rebuild(1.0f));
        m_bar.Attach(m_parent, 96);
        m_bar.SetTextFormats(m_formats.get());
        m_bar.SetBounds(D2D1::RectF(0.0f, 0.0f, 1100.0f, 40.0f));
        m_bar.SetLocationCallback([this](const te::ShellLocation& l) { m_navigated.push_back(l); });
        m_bar.SetMenuTracker([this](HMENU menu, HWND, POINT) {
            m_menus.push_back(Items(menu));
            return m_choose;
        });
        m_bar.SetLocation(At(m_root / L"one" / L"two"));
    }

    void TearDown() override
    {
        m_bar.ShutdownLoader();
        if (m_parent)
        {
            te::MainWindow::DrainPendingMessages(m_parent);
            DestroyWindow(m_parent);
        }
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] std::vector<std::wstring> Names() const
    {
        std::vector<std::wstring> names;
        for (const auto& crumb : m_bar.Crumbs())
        {
            names.push_back(crumb.name);
        }
        return names;
    }

    // Delivers a chevron's listing, as MainWindow does.
    void DeliverListing()
    {
        const ULONGLONG until = GetTickCount64() + 10000;
        MSG msg{};
        while (GetTickCount64() < until)
        {
            if (PeekMessageW(&msg, m_parent, te::WM_TE_TREE_CHILDREN, te::WM_TE_TREE_CHILDREN, PM_REMOVE))
            {
                std::unique_ptr<te::FolderChildren> result = te::TakeOwned<te::FolderChildren>(msg.lParam);
                ASSERT_TRUE(te::AddressBar::OwnsNode(result->nodeId)) << "tagged for the address bar";
                m_bar.OnChildren(*result);
                return;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_POSTMESSAGE);
        }
        ADD_FAILURE() << "no listing arrived";
    }

    bool m_uninitialize = false;
    fs::path m_root;
    HWND m_parent = nullptr;
    wil::com_ptr<IDWriteFactory3> m_dwrite;
    std::unique_ptr<te::TextFormats> m_formats;
    te::AddressBar m_bar;
    std::vector<te::ShellLocation> m_navigated;
    std::vector<std::vector<std::wstring>> m_menus;
    UINT m_choose = 0; // the menu item the tracker picks (0: dismissed)
};

TEST_F(BreadcrumbTest, SegmentsRunFromThisPcToTheLocation)
{
    const std::vector<std::wstring> names = Names();
    ASSERT_GE(names.size(), 5u);
    // The first segment is a child of the namespace root (the Desktop, left out): This PC
    // for most paths, the user's own folder for paths inside the profile (such as %TEMP%),
    // as the Shell's hierarchy has them.
    const std::optional<te::ShellLocation> root = m_bar.Crumbs().front().location.Parent();
    ASSERT_TRUE(root.has_value());
    EXPECT_FALSE(root->Parent().has_value()) << "the Desktop root is left out";
    EXPECT_EQ(names[names.size() - 3], m_root.filename().wstring());
    EXPECT_EQ(names[names.size() - 2], L"one");
    EXPECT_EQ(names.back(), L"two");
    for (const auto& crumb : m_bar.Crumbs())
    {
        EXPECT_TRUE(crumb.visible) << "a wide field shows every segment";
        EXPECT_GT(crumb.nameRect.right, crumb.nameRect.left);
        EXPECT_FLOAT_EQ(crumb.chevronRect.left, crumb.nameRect.right) << "a chevron after each name";
    }
    EXPECT_FALSE(m_bar.OverflowRect().has_value());
    EXPECT_FALSE(m_bar.DisplayText().empty()) << "the path stays the element's value";
}

TEST_F(BreadcrumbTest, ASystemFolderStartsAtThisPc)
{
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    m_bar.SetLocation(At(system));
    const std::vector<std::wstring> names = Names();
    ASSERT_GE(names.size(), 3u);
    EXPECT_EQ(names.front(), ThisPcName()) << "This PC > Local Disk (C:) > Windows > System32";
    // The Shell shows "System32"; GetSystemDirectoryW may spell it "system32".
    EXPECT_EQ(_wcsicmp(names.back().c_str(), fs::path(system).filename().c_str()), 0);
}

TEST_F(BreadcrumbTest, ClickingASegmentNavigatesToIt)
{
    const auto& crumbs = m_bar.Crumbs();
    ASSERT_TRUE(m_bar.OnPointerDown(Center(crumbs[crumbs.size() - 2].nameRect))); // "one"
    ASSERT_EQ(m_navigated.size(), 1u);
    EXPECT_EQ(m_navigated[0], At(m_root / L"one"));
    EXPECT_FALSE(m_bar.IsEditing()) << "a segment does not start editing";
}

TEST_F(BreadcrumbTest, TheChevronListsSubfoldersAndNavigatesToTheChosenOne)
{
    const auto& crumbs = m_bar.Crumbs();
    m_choose = 2;                                                                    // the second subfolder
    ASSERT_TRUE(m_bar.OnPointerDown(Center(crumbs[crumbs.size() - 2].chevronRect))); // "one" >
    EXPECT_TRUE(m_menus.empty()) << "the menu waits for the listing, off the UI thread";
    DeliverListing();
    ASSERT_EQ(m_menus.size(), 1u);
    EXPECT_EQ(m_menus[0], (std::vector<std::wstring>{L"sibling", L"two"})) << "folders only, sorted";
    ASSERT_EQ(m_navigated.size(), 1u);
    EXPECT_EQ(m_navigated[0], At(m_root / L"one" / L"two"));
}

TEST_F(BreadcrumbTest, ADismissedMenuNavigatesNowhereAndAStaleListingIsIgnored)
{
    const auto& crumbs = m_bar.Crumbs();
    m_choose = 0;
    m_bar.OnPointerDown(Center(crumbs.back().chevronRect)); // "two" >
    DeliverListing();
    ASSERT_EQ(m_menus.size(), 1u);
    EXPECT_EQ(m_menus[0], (std::vector<std::wstring>{L"three"}));
    EXPECT_TRUE(m_navigated.empty());

    // A chevron clicked, then the location changes before the listing arrives.
    m_bar.OnPointerDown(Center(m_bar.Crumbs().back().chevronRect));
    m_bar.SetLocation(At(m_root));
    DeliverListing();
    EXPECT_EQ(m_menus.size(), 1u) << "no menu for the old location";
}

TEST_F(BreadcrumbTest, ANarrowFieldHidesTheLeadingSegmentsBehindTheOverflowButton)
{
    m_bar.SetBounds(D2D1::RectF(0.0f, 0.0f, 260.0f, 40.0f));
    ASSERT_TRUE(m_bar.OverflowRect().has_value());
    const auto& crumbs = m_bar.Crumbs();
    EXPECT_FALSE(crumbs.front().visible);
    EXPECT_TRUE(crumbs.back().visible) << "the location itself always shows";
    std::vector<std::wstring> hidden;
    for (auto it = crumbs.rbegin(); it != crumbs.rend(); ++it)
    {
        if (!it->visible)
        {
            hidden.push_back(it->name);
        }
    }
    m_choose = static_cast<UINT>(hidden.size()); // the last item: the first segment
    ASSERT_TRUE(m_bar.OnPointerDown(Center(*m_bar.OverflowRect())));
    ASSERT_EQ(m_menus.size(), 1u);
    EXPECT_EQ(m_menus[0], hidden) << "the hidden segments, nearest first";
    ASSERT_EQ(m_navigated.size(), 1u);
    EXPECT_EQ(m_navigated[0], crumbs.front().location) << "the last item: the first segment";

    // Wide again: everything shows.
    m_bar.SetBounds(D2D1::RectF(0.0f, 0.0f, 1100.0f, 40.0f));
    EXPECT_FALSE(m_bar.OverflowRect().has_value());
}

TEST_F(BreadcrumbTest, EmptySpaceStartsEditingAndHidesTheSegments)
{
    const D2D1_RECT_F field = m_bar.FieldRect();
    ASSERT_TRUE(m_bar.OnPointerDown(D2D1::Point2F(field.right - 10.0f, (field.top + field.bottom) / 2.0f)));
    EXPECT_TRUE(m_bar.IsEditing());
    EXPECT_TRUE(m_navigated.empty());
    // While editing, a click in the field stays in the edit.
    EXPECT_TRUE(m_bar.OnPointerDown(Center(m_bar.Crumbs().front().nameRect)));
    EXPECT_TRUE(m_navigated.empty());
    m_bar.CancelEdit();
}

// Manual visual check (not run by CTest): the window over the system folder, wide and then
// narrow (overflow button), saved as te-crumbs-wide.bmp and te-crumbs-narrow.bmp in
// %TE_CAPTURE_DIR% (or %TEMP%). The system folder keeps personal names out of the pictures.
// Run with --gtest_also_run_disabled_tests.
TEST(BreadcrumbWindow, DISABLED_CaptureTheBreadcrumbs)
{
    const bool ole = SUCCEEDED(OleInitialize(nullptr));
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"Translucent Explorer";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = std::wstring(system);
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
        for (const auto& [width, file] : {std::pair{(work.right - work.left) / 2, L"te-crumbs-wide.bmp"},
                                          std::pair{560L, L"te-crumbs-narrow.bmp"}})
        {
            SetWindowPos(window.Hwnd(), HWND_TOPMOST, work.left + 20, work.top + 20, width, 260,
                         SWP_NOACTIVATE);
            pump(1500);
            // Hover the segment before the last one.
            const auto& crumbs = window.Address().Crumbs();
            if (crumbs.size() >= 2 && crumbs[crumbs.size() - 2].visible)
            {
                const float scale = static_cast<float>(window.Dpi()) / 96.0f;
                const D2D1_POINT_2F point = Center(crumbs[crumbs.size() - 2].nameRect);
                SendMessageW(
                    window.Hwnd(), WM_MOUSEMOVE, 0,
                    MAKELPARAM(static_cast<int>(point.x * scale), static_cast<int>(point.y * scale)));
            }
            RedrawWindow(window.Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            pump(400);
            te::test::SaveScreen(window.Hwnd(), file);
        }
        SetWindowPos(window.Hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        DestroyWindow(window.Hwnd());
    }
    if (ole)
    {
        OleUninitialize();
    }
}

// The real window: a click on a segment navigates there.
TEST(BreadcrumbWindow, ASegmentClickNavigatesTheWindow)
{
    const bool ole = SUCCEEDED(OleInitialize(nullptr));
    GUID guid{};
    ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
    wchar_t name[40]{};
    swprintf_s(name, L"te-crumbwin-%08lx", guid.Data1);
    const fs::path root = fs::temp_directory_path() / name;
    fs::create_directories(root / L"inner");
    {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"breadcrumb test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = (root / L"inner").wstring();
        te::MainWindow window(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(window.Create(SW_SHOWNOACTIVATE));
        const auto pump = [&](const std::function<bool()>& done) {
            const ULONGLONG until = GetTickCount64() + 15000;
            MSG msg{};
            while (GetTickCount64() < until && !done())
            {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            }
        };
        pump([&] { return !window.NavigationPending() && window.CurrentLocation() == At(root / L"inner"); });
        ASSERT_EQ(window.CurrentLocation(), At(root / L"inner"));

        const auto& crumbs = window.Address().Crumbs();
        ASSERT_GE(crumbs.size(), 2u);
        ASSERT_TRUE(crumbs[crumbs.size() - 2].visible);
        const float scale = static_cast<float>(window.Dpi()) / 96.0f;
        const D2D1_POINT_2F point = Center(crumbs[crumbs.size() - 2].nameRect); // the parent
        const LPARAM at = MAKELPARAM(static_cast<int>(point.x * scale), static_cast<int>(point.y * scale));
        SendMessageW(window.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        SendMessageW(window.Hwnd(), WM_LBUTTONUP, 0, at);
        pump([&] { return !window.NavigationPending() && window.CurrentLocation() == At(root); });
        EXPECT_EQ(window.CurrentLocation(), At(root)) << "the breadcrumb's folder";
        EXPECT_FALSE(window.Address().IsEditing());
        DestroyWindow(window.Hwnd());
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    if (ole)
    {
        OleUninitialize();
    }
}

} // namespace
