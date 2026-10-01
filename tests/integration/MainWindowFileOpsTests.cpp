// File operations wired into MainWindow (T072; research R-08; UI contract §4, §6; spec
// US4-3, US4-6). The window runs on a fresh temp folder.
//
// Most tests inject a fake IFileOperationService that records requests and lets the test
// post the results, so keys and menus never delete, move or copy anything and the status
// and dialog behaviour is deterministic. One test renames for real, end to end. The
// clipboard keys are not exercised here: they would replace the tester's clipboard (V-4a).

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>
#include <te/core/Messages.h>

#include <shlobj.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

void Pump(DWORD ms, const std::function<bool()>& done = {})
{
    const ULONGLONG until = GetTickCount64() + ms;
    MSG msg{};
    while (GetTickCount64() < until)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done && done())
        {
            return;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

class FakeOperations final : public te::IFileOperationService
{
  public:
    void Submit(HWND notify, te::FileOpRequest request) override
    {
        notifyHwnd = notify;
        requests.push_back(std::move(request));
    }
    void Shutdown() override
    {
        ++shutdowns;
    }

    HWND notifyHwnd = nullptr;
    std::vector<te::FileOpRequest> requests;
    int shutdowns = 0;
};

class MainWindowFileOpsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-winops-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root / L"sub");
        for (const wchar_t* file : {L"alpha.txt", L"beta.txt", L"gamma.txt"})
        {
            std::ofstream(m_root / file) << "content";
        }
    }

    void TearDown() override
    {
        if (m_window)
        {
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
    }

    void Open(bool fakeOperations = true)
    {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"file operations test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = m_root.wstring();
        options.fileOperations = fakeOperations ? &m_fake : nullptr;
        options.showOperationFailures = [this](HWND, const std::wstring& title, const std::wstring& items) {
            m_failureTitle = title;
            m_failureItems = items;
            ++m_failureDialogs;
        };
        options.contextMenuTracker = [this](HMENU menu, HWND, POINT) -> UINT {
            ++m_menusShown;
            return m_menuChoice ? m_menuChoice(menu) : 0;
        };
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNOACTIVATE));
        WaitIdle();
    }

    void WaitIdle()
    {
        Pump(15000, [this] { return !m_window->NavigationPending(); });
        Pump(300);
    }

    [[nodiscard]] HWND Hwnd() const
    {
        return m_window->Hwnd();
    }

    [[nodiscard]] std::wstring RealName(const te::FileItem& item) const
    {
        return item.info.editName.empty() ? item.info.name : item.info.editName;
    }

    // Focuses and selects the row with this real name (Home, then Down).
    void Select(const std::wstring& name)
    {
        SendMessageW(Hwnd(), WM_KEYDOWN, VK_HOME, 0);
        const auto& items = m_window->Files().Items();
        for (std::size_t i = 0; i < items.size() && RealName(items[i]) != name; ++i)
        {
            SendMessageW(Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        }
        const auto focus = m_window->Files().SelectionState().FocusIndex();
        ASSERT_TRUE(focus.has_value());
        ASSERT_EQ(RealName(items[*focus]), name);
    }

    [[nodiscard]] std::wstring StatusText() const
    {
        return m_window->Status().LeftText();
    }

    void PostItem(std::uint64_t opId, te::FileOpKind kind, HRESULT hr,
                  std::optional<std::wstring> newName = std::nullopt, te::ShellLocation item = {})
    {
        auto message = std::make_unique<te::FileOpItem>();
        message->opId = opId;
        message->kind = kind;
        message->hr = hr;
        message->newName = std::move(newName);
        message->item = std::move(item);
        ASSERT_TRUE(te::PostOwned(Hwnd(), te::WM_TE_FILEOP_ITEM, message));
    }

    void PostDone(std::uint64_t opId, te::FileOpFinalState state, std::vector<te::Status> errors = {})
    {
        auto message = std::make_unique<te::FileOpDone>();
        message->opId = opId;
        message->state = state;
        message->errors = std::move(errors);
        ASSERT_TRUE(te::PostOwned(Hwnd(), te::WM_TE_FILEOP_DONE, message));
    }

    fs::path m_root;
    FakeOperations m_fake;
    std::unique_ptr<te::MainWindow> m_window;
    int m_failureDialogs = 0;
    std::wstring m_failureTitle;
    std::wstring m_failureItems;
    int m_menusShown = 0;
    std::function<UINT(HMENU)> m_menuChoice;
};

TEST_F(MainWindowFileOpsTest, DeleteRecyclesAndShiftDeleteDeletesPermanently)
{
    Open();
    Select(L"beta.txt");
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_DELETE, 0);
    ASSERT_EQ(m_fake.requests.size(), 1u);
    EXPECT_EQ(m_fake.requests[0].kind, te::FileOpKind::Recycle);
    ASSERT_EQ(m_fake.requests[0].sources.size(), 1u);
    EXPECT_EQ(fs::path(m_fake.requests[0].sources[0].ParsingPath().value_or(L"")).filename(), L"beta.txt");
    EXPECT_EQ(m_fake.notifyHwnd, Hwnd());
    EXPECT_EQ(StatusText(), L"Deleting 1 items…") << "persistent while it runs (US4-6)";
    EXPECT_TRUE(m_window->OperationsRunning());
    EXPECT_TRUE(fs::exists(m_root / L"beta.txt")) << "the fake service touched nothing";

    // Shift+Delete: the key state comes from the thread's keyboard state.
    BYTE keys[256]{};
    ASSERT_TRUE(GetKeyboardState(keys));
    const BYTE saved = keys[VK_SHIFT];
    keys[VK_SHIFT] = 0x80;
    SetKeyboardState(keys);
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_DELETE, 0);
    keys[VK_SHIFT] = saved;
    SetKeyboardState(keys);
    ASSERT_EQ(m_fake.requests.size(), 2u);
    EXPECT_EQ(m_fake.requests[1].kind, te::FileOpKind::DeletePermanent);
}

TEST_F(MainWindowFileOpsTest, StatusShowsProgressThenTheResult)
{
    Open();
    const std::uint64_t first = m_window->SubmitFileOperation([&] {
        te::FileOpRequest request;
        request.kind = te::FileOpKind::Copy;
        for (const wchar_t* file : {L"alpha.txt", L"beta.txt", L"gamma.txt"})
        {
            te::ShellLocation location;
            wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
            EXPECT_HRESULT_SUCCEEDED(
                SHParseDisplayName((m_root / file).c_str(), nullptr, wil::out_param(pidl), 0, nullptr));
            EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
            request.sources.push_back(std::move(location));
        }
        return request;
    }());
    EXPECT_EQ(StatusText(), L"Copying 3 items…");

    // A second operation queues behind it; its message shows once the first ends.
    te::FileOpRequest rename;
    rename.kind = te::FileOpKind::Rename;
    rename.sources.push_back(m_fake.requests[0].sources[0]);
    rename.newName = L"x.txt";
    const std::uint64_t second = m_window->SubmitFileOperation(std::move(rename));

    PostItem(first, te::FileOpKind::Copy, S_OK);
    PostItem(first, te::FileOpKind::Copy, S_OK);
    PostItem(first, te::FileOpKind::Copy, S_OK);
    PostDone(first, te::FileOpFinalState::Succeeded);
    Pump(200);
    EXPECT_EQ(StatusText(), L"Renaming…") << "the next running operation";

    PostDone(second, te::FileOpFinalState::Cancelled);
    Pump(200);
    EXPECT_EQ(StatusText(), L"Operation cancelled — 0 items completed before cancellation");
    EXPECT_FALSE(m_window->OperationsRunning());
    EXPECT_EQ(m_failureDialogs, 0) << "a cancellation is not a failure";
}

TEST_F(MainWindowFileOpsTest, PartialFailureListsTheItemsAndSaysHowManyCompleted)
{
    Open();
    Select(L"alpha.txt");
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_DELETE, 0);
    ASSERT_EQ(m_fake.requests.size(), 1u);
    const std::uint64_t id = m_fake.requests[0].id;

    PostItem(id, te::FileOpKind::Recycle, E_ACCESSDENIED);
    PostDone(id, te::FileOpFinalState::Failed, {{E_ACCESSDENIED, L"alpha.txt: Access is denied."}});
    Pump(300);
    EXPECT_EQ(StatusText(), L"0 of 1 items completed");
    EXPECT_EQ(m_failureDialogs, 1);
    EXPECT_EQ(m_failureTitle, L"Some items couldn't be processed");
    EXPECT_EQ(m_failureItems, L"alpha.txt: Access is denied.");
}

TEST_F(MainWindowFileOpsTest, SkippedItemsAreNotCountedAsDone)
{
    Open();
    Select(L"alpha.txt");
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_DELETE, 0);
    const std::uint64_t id = m_fake.requests.at(0).id;
    PostItem(id, te::FileOpKind::Recycle, COPYENGINE_S_USER_IGNORED);
    PostDone(id, te::FileOpFinalState::Succeeded);
    Pump(300);
    EXPECT_EQ(StatusText(), L"0 of 1 items completed");
}

TEST_F(MainWindowFileOpsTest, TheListRefreshesAfterAnOperationOnTheFolderAndKeepsTheSelection)
{
    Open();
    Select(L"gamma.txt");
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_DELETE, 0);
    const std::uint64_t id = m_fake.requests.at(0).id;
    const te::Generation before = m_window->Files().CurrentGeneration();

    std::ofstream(m_root / L"delta.txt") << "appeared meanwhile";
    PostItem(id, te::FileOpKind::Recycle, S_OK);
    PostDone(id, te::FileOpFinalState::Succeeded);
    Pump(200);
    WaitIdle();

    EXPECT_NE(m_window->Files().CurrentGeneration(), before) << "re-read with a new generation";
    const auto& items = m_window->Files().Items();
    EXPECT_TRUE(std::any_of(items.begin(), items.end(),
                            [&](const te::FileItem& item) { return RealName(item) == L"delta.txt"; }));
    const auto focus = m_window->Files().SelectionState().FocusIndex();
    ASSERT_TRUE(focus.has_value());
    EXPECT_EQ(RealName(items[*focus]), L"gamma.txt") << "selection and focus restored";
    EXPECT_EQ(m_window->Files().Selection().size(), 1u);
}

TEST_F(MainWindowFileOpsTest, RenameFromTheListShowsTheNewNameOnlyAfterSuccess)
{
    Open();
    Select(L"alpha.txt");
    ASSERT_TRUE(m_window->Files().BeginRename());
    const HWND edit = m_window->Files().RenameField().Edit();
    SetWindowTextW(edit, L"omega.txt");
    SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);

    ASSERT_EQ(m_fake.requests.size(), 1u);
    const te::FileOpRequest& request = m_fake.requests[0];
    EXPECT_EQ(request.kind, te::FileOpKind::Rename);
    EXPECT_EQ(request.newName.value_or(L""), L"omega.txt");
    EXPECT_EQ(StatusText(), L"Renaming…");
    const auto names = [&] {
        std::vector<std::wstring> result;
        for (const auto& item : m_window->Files().Items())
        {
            result.push_back(RealName(item));
        }
        return result;
    };
    const auto before = names();
    EXPECT_NE(std::find(before.begin(), before.end(), L"alpha.txt"), before.end())
        << "old name until the rename succeeds (US4-3)";

    // The service reports success for the item: now the row shows the new name.
    PostItem(request.id, te::FileOpKind::Rename, S_OK, L"omega.txt", request.sources[0]);
    Pump(100);
    const auto after = names();
    EXPECT_NE(std::find(after.begin(), after.end(), L"omega.txt"), after.end());
    EXPECT_EQ(std::find(after.begin(), after.end(), L"alpha.txt"), after.end());
}

TEST_F(MainWindowFileOpsTest, RealRenameEndToEnd)
{
    Open(/*fakeOperations=*/false);
    Select(L"beta.txt");
    ASSERT_TRUE(m_window->Files().BeginRename());
    const HWND edit = m_window->Files().RenameField().Edit();
    SetWindowTextW(edit, L"renamed 文件.txt");
    SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);

    Pump(15000, [this] { return !m_window->OperationsRunning(); });
    WaitIdle();
    EXPECT_TRUE(fs::exists(m_root / L"renamed 文件.txt"));
    EXPECT_FALSE(fs::exists(m_root / L"beta.txt"));
    EXPECT_EQ(StatusText(), L"Item renamed");
    const auto& items = m_window->Files().Items();
    const auto focus = m_window->Files().SelectionState().FocusIndex();
    ASSERT_TRUE(focus.has_value());
    EXPECT_EQ(RealName(items[*focus]), L"renamed 文件.txt") << "the renamed item stays selected";
}

TEST_F(MainWindowFileOpsTest, ContextMenuRenameStartsInlineRename)
{
    Open();
    Select(L"gamma.txt");
    // Choose "rename" from the Shell's menu, as the user would.
    m_menuChoice = [](HMENU menu) -> UINT {
        const int count = GetMenuItemCount(menu);
        for (int i = 0; i < count; ++i)
        {
            wchar_t text[128]{};
            MENUITEMINFOW info{sizeof(info)};
            info.fMask = MIIM_ID | MIIM_STRING;
            info.dwTypeData = text;
            info.cch = 128;
            if (GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info) && wcsstr(text, L"Rena") != nullptr)
            {
                return info.wID;
            }
        }
        return 0;
    };
    // Shift+F10 / the Menu key arrive as WM_CONTEXTMENU at (-1, -1).
    SendMessageW(Hwnd(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(Hwnd()), MAKELPARAM(-1, -1));
    EXPECT_EQ(m_menusShown, 1);
    EXPECT_TRUE(m_window->Files().IsRenaming()) << "Rename goes to inline rename";
    EXPECT_TRUE(m_fake.requests.empty());
    m_window->Files().CancelRename();
}

TEST_F(MainWindowFileOpsTest, RightClickSelectsTheRowAndTheMenuIsOnlyForTheList)
{
    Open();
    Select(L"alpha.txt");
    const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
    // Right-click the third row.
    const D2D1_RECT_F row = m_window->Files().RowRect(2);
    const LPARAM point =
        MAKELPARAM(static_cast<int>((row.left + 40.0f) * scale), static_cast<int>((row.top + 10.0f) * scale));
    SendMessageW(Hwnd(), WM_RBUTTONDOWN, MK_RBUTTON, point);
    const auto focus = m_window->Files().SelectionState().FocusIndex();
    ASSERT_TRUE(focus.has_value());
    EXPECT_EQ(*focus, 2u);
    EXPECT_EQ(m_window->Files().Selection().size(), 1u) << "right-click on an unselected row selects only it";

    POINT screen{static_cast<LONG>((row.left + 40.0f) * scale), static_cast<LONG>((row.top + 10.0f) * scale)};
    ClientToScreen(Hwnd(), &screen);
    SendMessageW(Hwnd(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(Hwnd()), MAKELPARAM(screen.x, screen.y));
    EXPECT_EQ(m_menusShown, 1) << "dismissed (the tracker returns 0)";

    // Over the navigation pane: not the file list's menu.
    POINT pane{static_cast<LONG>((m_window->Layout().navigationPane.left + 20.0f) * scale),
               static_cast<LONG>((m_window->Layout().navigationPane.top + 20.0f) * scale)};
    ClientToScreen(Hwnd(), &pane);
    SendMessageW(Hwnd(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(Hwnd()), MAKELPARAM(pane.x, pane.y));
    EXPECT_EQ(m_menusShown, 1);
}

TEST_F(MainWindowFileOpsTest, ClosingTheWindowShutsTheServiceDown)
{
    Open();
    DestroyWindow(Hwnd());
    EXPECT_EQ(m_fake.shutdowns, 1);
    m_window.reset();
}

// V-4a without the clipboard (T073): three files copied through the window with the real
// service; the status names the result and the destination is not the folder on screen.
TEST_F(MainWindowFileOpsTest, CopyingThreeFilesReportsThreeItemsCopied)
{
    Open(/*fakeOperations=*/false);
    te::FileOpRequest request;
    request.kind = te::FileOpKind::Copy;
    for (const wchar_t* file : {L"alpha.txt", L"beta.txt", L"gamma.txt"})
    {
        te::ShellLocation location;
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        ASSERT_HRESULT_SUCCEEDED(
            SHParseDisplayName((m_root / file).c_str(), nullptr, wil::out_param(pidl), 0, nullptr));
        ASSERT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
        request.sources.push_back(std::move(location));
    }
    te::ShellLocation destination;
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    ASSERT_HRESULT_SUCCEEDED(
        SHParseDisplayName((m_root / L"sub").c_str(), nullptr, wil::out_param(pidl), 0, nullptr));
    ASSERT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &destination));
    request.destination = std::move(destination);

    m_window->SubmitFileOperation(std::move(request));
    EXPECT_EQ(StatusText(), L"Copying 3 items…");
    Pump(30000, [this] { return !m_window->OperationsRunning(); });
    EXPECT_EQ(StatusText(), L"3 items copied");
    for (const wchar_t* file : {L"alpha.txt", L"beta.txt", L"gamma.txt"})
    {
        EXPECT_TRUE(fs::exists(m_root / L"sub" / file));
        EXPECT_TRUE(fs::exists(m_root / file)) << "a copy leaves the source";
    }
    EXPECT_EQ(m_failureDialogs, 0);
}

} // namespace
