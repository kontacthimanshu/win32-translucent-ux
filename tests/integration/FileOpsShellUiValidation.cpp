// Quickstart V-4 scenarios that raise the Shell's own dialogs (T073). DISABLED_ by
// default so a normal ctest run never shows UI; run them explicitly for validation:
//
//   te_integration_tests --gtest_also_run_disabled_tests --gtest_filter=FileOpsShellUi.*
//
// The dialogs are operated through UI Automation (Invoke) from a helper thread: no mouse
// or keyboard input is injected. The operations run through the production
// FileOperationService with its production flags, on fresh temp trees. The Recycle Bin
// check adds one uniquely named temp file to the Recycle Bin and removes exactly that
// item afterwards. "Continue" in the access-denied dialog (an elevation request) is never
// chosen.

#include <te/app/MainWindow.h>
#include <te/core/Messages.h>
#include <te/shell/ContextMenu.h>
#include <te/shell/FileOperationService.h>
#include <te/shell/ShellNavigator.h>

#include <UIAutomation.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlobj.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;

// UTF-8 for RecordProperty.
std::string Utf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr,
                        nullptr);
    return result;
}

std::string ReadAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// On a helper MTA thread: waits for a button whose name is one of `names` in any top-level
// window of this process, records the window's title, and invokes it. Tries the names in
// order in each window.
class DialogResponder
{
  public:
    DialogResponder(std::vector<std::wstring> names, DWORD timeoutMs)
        : m_thread([this, names = std::move(names), timeoutMs](std::stop_token stop) {
              Run(names, timeoutMs, stop);
          })
    {
    }
    ~DialogResponder()
    {
        m_thread.request_stop();
    }

    void Wait()
    {
        m_thread.join();
    }
    [[nodiscard]] bool Invoked() const
    {
        return m_invoked;
    }
    [[nodiscard]] const std::wstring& WindowTitle() const
    {
        return m_title;
    }
    [[nodiscard]] const std::wstring& Button() const
    {
        return m_button;
    }

  private:
    void Run(const std::vector<std::wstring>& names, DWORD timeoutMs, const std::stop_token& stop)
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            return;
        }
        {
            wil::com_ptr<IUIAutomation> uia;
            if (SUCCEEDED(
                    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))))
            {
                const ULONGLONG until = GetTickCount64() + timeoutMs;
                while (!stop.stop_requested() && GetTickCount64() < until && !TryOnce(uia.get(), names))
                {
                    Sleep(200);
                }
            }
        }
        CoUninitialize();
    }

    bool TryOnce(IUIAutomation* uia, const std::vector<std::wstring>& names)
    {
        wil::com_ptr<IUIAutomationElement> root;
        wil::com_ptr<IUIAutomationCondition> ownProcess;
        wil::com_ptr<IUIAutomationElementArray> windows;
        VARIANT pid{};
        pid.vt = VT_I4;
        pid.lVal = static_cast<LONG>(GetCurrentProcessId());
        if (FAILED(uia->GetRootElement(&root)) ||
            FAILED(uia->CreatePropertyCondition(UIA_ProcessIdPropertyId, pid, &ownProcess)) ||
            FAILED(root->FindAll(TreeScope_Children, ownProcess.get(), &windows)) || !windows)
        {
            return false;
        }
        int count = 0;
        windows->get_Length(&count);
        for (int w = 0; w < count; ++w)
        {
            wil::com_ptr<IUIAutomationElement> window;
            if (FAILED(windows->GetElement(w, &window)))
            {
                continue;
            }
            for (const std::wstring& name : names)
            {
                wil::unique_variant value;
                value.vt = VT_BSTR;
                value.bstrVal = SysAllocString(name.c_str());
                wil::com_ptr<IUIAutomationCondition> byName;
                wil::com_ptr<IUIAutomationElement> button;
                if (FAILED(uia->CreatePropertyCondition(UIA_NamePropertyId, value, &byName)) ||
                    FAILED(window->FindFirst(TreeScope_Descendants, byName.get(), &button)) || !button)
                {
                    continue;
                }
                wil::com_ptr<IUIAutomationInvokePattern> invoke;
                if (FAILED(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) ||
                    !invoke)
                {
                    continue;
                }
                wil::unique_bstr title;
                if (SUCCEEDED(window->get_CurrentName(&title)) && title)
                {
                    m_title = title.get();
                }
                m_button = name;
                m_invoked = SUCCEEDED(invoke->Invoke());
                return m_invoked;
            }
        }
        return false;
    }

    std::atomic<bool> m_invoked{false};
    std::wstring m_title;
    std::wstring m_button;
    std::jthread m_thread;
};

class FileOpsShellUi : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        // A real, visible owner: the Shell's dialogs are modal to it, as to the main window.
        m_owner = CreateWindowExW(0, L"STATIC", L"V-4 owner", WS_OVERLAPPEDWINDOW, 200, 200, 400, 200,
                                  nullptr, nullptr, nullptr, nullptr);
        ASSERT_NE(m_owner, nullptr);
        ShowWindow(m_owner, SW_SHOWNORMAL);
        ShowWindow(m_owner, SW_SHOWNORMAL);
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-v4-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root);
    }

    void TearDown() override
    {
        m_service.reset();
        if (m_owner)
        {
            te::MainWindow::DrainPendingMessages(m_owner);
            DestroyWindow(m_owner);
        }
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    te::ShellLocation Location(const fs::path& path)
    {
        te::ShellLocation location;
        EXPECT_HRESULT_SUCCEEDED(m_navigator.Parse(path.wstring(), &location)) << path.string();
        return location;
    }

    struct Outcome
    {
        std::vector<std::unique_ptr<te::FileOpItem>> items;
        std::unique_ptr<te::FileOpDone> done;
        ULONGLONG longestGapMs = 0; // between two pump iterations of this (UI) thread
    };

    Outcome Run(te::FileOpRequest request, DWORD timeoutMs = 120000)
    {
        if (!m_service)
        {
            m_service = std::make_unique<te::FileOperationService>();
        }
        request.id = 1;
        m_service->Submit(m_owner, std::move(request));
        Outcome outcome;
        const ULONGLONG until = GetTickCount64() + timeoutMs;
        ULONGLONG last = GetTickCount64();
        MSG msg{};
        while (!outcome.done && GetTickCount64() < until)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.hwnd == m_owner && msg.message == te::WM_TE_FILEOP_ITEM)
                {
                    outcome.items.push_back(te::TakeOwned<te::FileOpItem>(msg.lParam));
                }
                else if (msg.hwnd == m_owner && msg.message == te::WM_TE_FILEOP_DONE)
                {
                    outcome.done = te::TakeOwned<te::FileOpDone>(msg.lParam);
                }
                else
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            const ULONGLONG now = GetTickCount64();
            outcome.longestGapMs = std::max(outcome.longestGapMs, now - last);
            last = now;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        EXPECT_NE(outcome.done, nullptr) << "no WM_TE_FILEOP_DONE";
        return outcome;
    }

    bool m_uninitialize = false;
    HWND m_owner = nullptr;
    fs::path m_root;
    te::ShellNavigator m_navigator;
    std::unique_ptr<te::FileOperationService> m_service;
};

// V-4b: a move into a folder with the same names shows the Shell's conflict dialog;
// "Skip" leaves every target unchanged and nothing is reported as done.
TEST_F(FileOpsShellUi, DISABLED_V4b_ConflictDialogSkipKeepsTargets)
{
    fs::create_directories(m_root / L"src");
    fs::create_directories(m_root / L"dst");
    for (int i = 1; i <= 5; ++i)
    {
        const std::wstring name = L"conflict" + std::to_wstring(i) + L".txt";
        std::ofstream(m_root / L"src" / name) << "SOURCE " << i;
        std::ofstream(m_root / L"dst" / name) << "DESTINATION " << i;
    }
    te::FileOpRequest request;
    request.kind = te::FileOpKind::Move;
    for (int i = 1; i <= 5; ++i)
    {
        request.sources.push_back(Location(m_root / L"src" / (L"conflict" + std::to_wstring(i) + L".txt")));
    }
    request.destination = Location(m_root / L"dst");

    DialogResponder skip({L"Skip these files", L"Skip"}, 30000);
    const Outcome outcome = Run(std::move(request));
    skip.Wait();

    EXPECT_TRUE(skip.Invoked()) << "the Shell conflict dialog appeared";
    RecordProperty("dialog", Utf8(skip.WindowTitle()));
    ASSERT_NE(outcome.done, nullptr);
    std::size_t done = 0;
    for (const auto& item : outcome.items)
    {
        done += (SUCCEEDED(item->hr) && item->hr != COPYENGINE_S_USER_IGNORED) ? 1 : 0;
    }
    std::string codes;
    for (const auto& item : outcome.items)
    {
        char code[16]{};
        sprintf_s(code, "%08lX ", static_cast<unsigned long>(item->hr));
        codes += code;
    }
    RecordProperty("item_hr", codes);
    EXPECT_EQ(done, 0u) << "skipped items are not reported as moved: " << codes;
    RecordProperty("state", static_cast<int>(outcome.done->state));
    for (int i = 1; i <= 5; ++i)
    {
        const std::wstring name = L"conflict" + std::to_wstring(i) + L".txt";
        EXPECT_EQ(ReadAll(m_root / L"dst" / name), "DESTINATION " + std::to_string(i)) << "target unchanged";
        EXPECT_EQ(ReadAll(m_root / L"src" / name), "SOURCE " + std::to_string(i)) << "source still there";
    }
}

// V-4d (second half): Shift+Delete shows the Shell's permanent-delete confirmation; "No"
// keeps the file and the operation ends Cancelled.
TEST_F(FileOpsShellUi, DISABLED_V4d_PermanentDeleteAsksAndNoKeepsTheFile)
{
    std::ofstream(m_root / L"keep-me.txt") << "keep";
    te::FileOpRequest request;
    request.kind = te::FileOpKind::DeletePermanent;
    request.sources.push_back(Location(m_root / L"keep-me.txt"));

    DialogResponder no({L"No"}, 30000);
    const Outcome outcome = Run(std::move(request));
    no.Wait();

    EXPECT_TRUE(no.Invoked()) << "the Shell asked before deleting permanently";
    RecordProperty("dialog", Utf8(no.WindowTitle()));
    ASSERT_NE(outcome.done, nullptr);
    EXPECT_EQ(outcome.done->state, te::FileOpFinalState::Cancelled);
    EXPECT_TRUE(fs::exists(m_root / L"keep-me.txt"));
}

// The Recycle Bin item deleted from `original`, if any. A recycled item's path is its $R
// storage file; its display name is the original path, possibly without the extension.
wil::com_ptr<IShellItem> FindInRecycleBin(const fs::path& original, std::wstring* seen = nullptr)
{
    wil::com_ptr<IShellItem> bin;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&bin))))
    {
        return nullptr;
    }
    wil::com_ptr<IEnumShellItems> items;
    if (FAILED(bin->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))))
    {
        return nullptr;
    }
    // In the Recycle Bin the display name is the original full path, without the extension
    // when Explorer hides it.
    const std::wstring file = original.wstring();
    const std::wstring stem = (original.parent_path() / original.stem()).wstring();
    wil::com_ptr<IShellItem> item;
    while (items->Next(1, item.put(), nullptr) == S_OK)
    {
        wil::unique_cotaskmem_string name;
        if (seen && SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)))
        {
            *seen += name.get();
            *seen += L"; ";
            name.reset();
        }
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) &&
            (_wcsicmp(name.get(), file.c_str()) == 0 || _wcsicmp(name.get(), stem.c_str()) == 0))
        {
            return item;
        }
    }
    return nullptr;
}

// V-4d (first half): Delete sends the item to the Recycle Bin, confirmed by its contents.
TEST_F(FileOpsShellUi, DISABLED_V4d_DeleteGoesToTheRecycleBin)
{
    GUID guid{};
    ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
    wchar_t name[64]{};
    swprintf_s(name, L"te-v4d-recycled-%08lx.txt", guid.Data1);
    const fs::path file = m_root / name;
    std::ofstream(file) << "recycle me";

    te::FileOpRequest request;
    request.kind = te::FileOpKind::Recycle;
    request.sources.push_back(Location(file));
    // The Shell asks first only if "Display delete confirmation dialog" is on. The UIA
    // responder is started only then: after an earlier test's dialog it can stall for
    // minutes in UIA (seen in T073), although the recycle itself takes milliseconds.
    SHELLSTATE state{};
    SHGetSetSettings(&state, SSF_NOCONFIRMRECYCLE, FALSE);
    const bool asks = !state.fNoConfirmRecycle;
    std::optional<DialogResponder> confirm;
    if (asks)
    {
        confirm.emplace(std::vector<std::wstring>{L"Yes"}, 10000);
    }
    const ULONGLONG t0 = GetTickCount64();
    const Outcome outcome = Run(std::move(request));
    RecordProperty("recycle_ms", static_cast<int>(GetTickCount64() - t0));
    if (confirm)
    {
        confirm->Wait();
    }

    ASSERT_NE(outcome.done, nullptr);
    EXPECT_EQ(outcome.done->state, te::FileOpFinalState::Succeeded);
    EXPECT_FALSE(fs::exists(file));
    std::wstring seen;
    const ULONGLONG t1 = GetTickCount64();
    wil::com_ptr<IShellItem> recycled = FindInRecycleBin(file, &seen);
    RecordProperty("find_ms", static_cast<int>(GetTickCount64() - t1));
    EXPECT_TRUE(recycled) << "found in the Recycle Bin; listed: " << Utf8(seen);
    RecordProperty("confirmation_setting", asks ? 1 : 0);
    RecordProperty("confirmation_shown", confirm && confirm->Invoked() ? 1 : 0);

    // Clean up exactly this item from the Recycle Bin (test code only; the product never
    // sets FOF_NOCONFIRMATION).
    if (recycled)
    {
        wil::com_ptr<IFileOperation> cleanup;
        ASSERT_HRESULT_SUCCEEDED(
            CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&cleanup)));
        cleanup->SetOperationFlags(FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT);
        ASSERT_HRESULT_SUCCEEDED(cleanup->DeleteItem(recycled.get(), nullptr));
        const ULONGLONG t2 = GetTickCount64();
        EXPECT_HRESULT_SUCCEEDED(cleanup->PerformOperations());
        RecordProperty("cleanup_ms", static_cast<int>(GetTickCount64() - t2));
        EXPECT_FALSE(FindInRecycleBin(file)) << "test item removed from the Recycle Bin";
    }
}

// V-4e: copying into a folder that denies writing reports the failure; the Shell's dialog
// is answered with Skip (or Cancel), never with the elevation "Continue".
TEST_F(FileOpsShellUi, DISABLED_V4e_AccessDeniedIsReportedAndLeavesNothing)
{
    const fs::path locked = fs::temp_directory_path() / L"te-test" / L"readonly-acl";
    if (!fs::exists(locked))
    {
        GTEST_SKIP() << "run tools/New-TestData.ps1";
    }
    std::ofstream(m_root / L"copy-me.txt") << "copy";
    te::FileOpRequest request;
    request.kind = te::FileOpKind::Copy;
    request.sources.push_back(Location(m_root / L"copy-me.txt"));
    request.destination = Location(locked);

    DialogResponder answer({L"Skip", L"Cancel"}, 30000);
    const Outcome outcome = Run(std::move(request));
    answer.Wait();

    ASSERT_NE(outcome.done, nullptr);
    RecordProperty("dialog", Utf8(answer.WindowTitle()));
    RecordProperty("button", Utf8(answer.Button()));
    RecordProperty("state", static_cast<int>(outcome.done->state));
    RecordProperty("first_error",
                   outcome.done->errors.empty()
                       ? std::string("none")
                       : std::to_string(static_cast<unsigned long>(outcome.done->errors[0].hr)));
    EXPECT_NE(outcome.done->state, te::FileOpFinalState::Succeeded);
    // No partial file: listing the folder is denied to the user, so check with the name.
    EXPECT_EQ(GetFileAttributesW((locked / L"copy-me.txt").c_str()), INVALID_FILE_ATTRIBUTES);
}

// V-4f: cancelling a large copy in the Shell's progress dialog ends Cancelled with the
// number completed, and this (UI) thread keeps pumping throughout.
TEST_F(FileOpsShellUi, DISABLED_V4f_CancelInTheProgressDialog)
{
    const fs::path tenK = fs::temp_directory_path() / L"te-test" / L"10k";
    if (!fs::exists(tenK))
    {
        GTEST_SKIP() << "run tools/New-TestData.ps1";
    }
    fs::create_directories(m_root / L"copy");
    te::FileOpRequest request;
    request.kind = te::FileOpKind::Copy;
    request.sources.push_back(Location(tenK));
    request.destination = Location(m_root / L"copy");

    // The Windows 11 progress dialog names its cancel button "Cancel tile".
    DialogResponder cancel({L"Cancel tile"}, 60000);
    const Outcome outcome = Run(std::move(request), 300000);
    cancel.Wait();

    ASSERT_NE(outcome.done, nullptr);
    RecordProperty("button", Utf8(cancel.Button()));
    RecordProperty("state", static_cast<int>(outcome.done->state));
    RecordProperty("longest_gap_ms", static_cast<int>(outcome.longestGapMs));
    std::size_t copied = 0;
    if (fs::exists(m_root / L"copy" / L"10k"))
    {
        for ([[maybe_unused]] const auto& entry : fs::directory_iterator(m_root / L"copy" / L"10k"))
        {
            ++copied;
        }
    }
    RecordProperty("files_copied_before_cancel", static_cast<int>(copied));
    EXPECT_TRUE(cancel.Invoked()) << "the progress dialog appeared and was cancelled";
    EXPECT_EQ(outcome.done->state, te::FileOpFinalState::Cancelled);
    EXPECT_LT(copied, 10000u);
    EXPECT_LT(outcome.longestGapMs, 250u) << "the UI thread was never blocked";
}

// V-4g: the context menu's Properties opens the Shell's property sheet for the file.
TEST_F(FileOpsShellUi, DISABLED_V4g_PropertiesFromTheContextMenu)
{
    std::ofstream(m_root / L"props.txt") << "properties";
    te::ShellLocation folder = Location(m_root);
    te::ShellLocation file = Location(m_root / L"props.txt");
    te::ShellItemInfo info;
    info.childPidl.reset(static_cast<ITEMID_CHILD*>(ILClone(ILFindLastID(file.IdList()))));
    const std::vector<const te::ShellItemInfo*> items{&info};

    te::ContextMenu menu;
    ASSERT_HRESULT_SUCCEEDED(menu.Load(m_owner, folder, items));
    const wil::unique_hmenu hmenu(CreatePopupMenu());
    ASSERT_HRESULT_SUCCEEDED(menu.Populate(hmenu.get(), false));
    UINT properties = 0;
    for (UINT id = te::ContextMenu::kFirstCommand; id < 0x200 && properties == 0; ++id)
    {
        if (const auto verb = menu.VerbOf(id); verb && _wcsicmp(verb->c_str(), L"properties") == 0)
        {
            properties = id;
        }
    }
    ASSERT_NE(properties, 0u);
    ASSERT_HRESULT_SUCCEEDED(menu.Invoke(m_owner, properties, POINT{300, 300}, false, false));

    // The sheet opens asynchronously on its own thread; find it, then close it.
    HWND sheet = nullptr;
    for (int i = 0; i < 100 && !sheet; ++i)
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            DispatchMessageW(&msg);
        }
        EnumWindows(
            [](HWND hwnd, LPARAM found) -> BOOL {
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                wchar_t title[256]{};
                GetWindowTextW(hwnd, title, 256);
                // "props Properties" (or "props.txt Properties" when extensions show).
                if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && wcsstr(title, L"props") &&
                    wcsstr(title, L"Properties"))
                {
                    *reinterpret_cast<HWND*>(found) = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&sheet));
        Sleep(100);
    }
    ASSERT_NE(sheet, nullptr) << "the Properties sheet appeared";
    wchar_t title[256]{};
    GetWindowTextW(sheet, title, 256);
    RecordProperty("sheet", Utf8(title));
    PostMessageW(sheet, WM_CLOSE, 0, 0);
    for (int i = 0; i < 30 && IsWindow(sheet); ++i)
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            DispatchMessageW(&msg);
        }
        Sleep(100);
    }
    EXPECT_FALSE(IsWindow(sheet)) << "closed again";
}

} // namespace
