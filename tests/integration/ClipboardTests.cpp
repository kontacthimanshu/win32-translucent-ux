// Clipboard (T070; research R-08) on a fresh temp tree. The tests build and read the
// Shell data objects directly and never touch the system clipboard, so the tester's
// clipboard and clipboard history stay as they were. OleSetClipboard / OleGetClipboard
// themselves are covered manually (V-4a).
//
// The last test pastes a cut through FileOperationService, which also exercises the
// data-object path of T068 (marshalled to the worker, passed to MoveItems).

#include <te/app/MainWindow.h>
#include <te/core/Messages.h>
#include <te/shell/Clipboard.h>
#include <te/shell/FileOperationService.h>
#include <te/shell/ShellNavigator.h>

#include <shellapi.h>
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

class ClipboardTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-clip-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root / L"src");
        fs::create_directories(m_root / L"dst");
        std::ofstream(m_root / L"src" / L"one.txt") << "1";
        std::ofstream(m_root / L"src" / L"two.txt") << "22";

        ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse((m_root / L"src").wstring(), &m_source));
        ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse((m_root / L"dst").wstring(), &m_destination));
        for (const wchar_t* file : {L"one.txt", L"two.txt"})
        {
            te::ShellLocation location;
            ASSERT_HRESULT_SUCCEEDED(m_navigator.Parse((m_root / L"src" / file).wstring(), &location));
            te::ShellItemInfo info;
            info.name = file;
            info.childPidl.reset(static_cast<ITEMID_CHILD*>(ILClone(ILFindLastID(location.IdList()))));
            m_items.push_back(std::move(info));
        }
    }

    void TearDown() override
    {
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    [[nodiscard]] std::vector<const te::ShellItemInfo*> Items() const
    {
        std::vector<const te::ShellItemInfo*> items;
        for (const auto& item : m_items)
        {
            items.push_back(&item);
        }
        return items;
    }

    static std::optional<DWORD> PreferredEffect(IDataObject* data)
    {
        FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)),
                         nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (FAILED(data->GetData(&format, &medium)))
        {
            return std::nullopt;
        }
        const DWORD value = *static_cast<const DWORD*>(GlobalLock(medium.hGlobal));
        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);
        return value;
    }

    // The file names in the data object's CF_HDROP, as another app would read them.
    static std::set<std::wstring> DroppedFiles(IDataObject* data)
    {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        std::set<std::wstring> files;
        if (FAILED(data->GetData(&format, &medium)))
        {
            return files;
        }
        const auto drop = static_cast<HDROP>(medium.hGlobal);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i)
        {
            std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
            path.resize(DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size())));
            files.insert(path);
        }
        ReleaseStgMedium(&medium);
        return files;
    }

    bool m_uninitialize = false;
    fs::path m_root;
    te::ShellNavigator m_navigator;
    te::ShellLocation m_source;
    te::ShellLocation m_destination;
    std::vector<te::ShellItemInfo> m_items;
};

TEST_F(ClipboardTest, CopyMakesAShellDataObjectOtherAppsCanRead)
{
    wil::com_ptr<IDataObject> data;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::CreateDataObject(m_source, Items(), false, &data));
    ASSERT_TRUE(data);

    FORMATETC idList{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_SHELLIDLIST)), nullptr,
                     DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    EXPECT_EQ(data->QueryGetData(&idList), S_OK) << "Shell ID list, for Explorer";
    const std::set<std::wstring> expected{(m_root / L"src" / L"one.txt").wstring(),
                                          (m_root / L"src" / L"two.txt").wstring()};
    EXPECT_EQ(DroppedFiles(data.get()), expected) << "CF_HDROP, for any app";
    EXPECT_EQ(PreferredEffect(data.get()).value_or(0), static_cast<DWORD>(DROPEFFECT_COPY | DROPEFFECT_LINK));
}

TEST_F(ClipboardTest, CutPrefersMove)
{
    wil::com_ptr<IDataObject> data;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::CreateDataObject(m_source, Items(), true, &data));
    EXPECT_EQ(PreferredEffect(data.get()).value_or(0), static_cast<DWORD>(DROPEFFECT_MOVE));
}

TEST_F(ClipboardTest, PasteRequestFollowsThePreferredEffect)
{
    wil::com_ptr<IDataObject> copied;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::CreateDataObject(m_source, Items(), false, &copied));
    te::FileOpRequest request;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::PasteRequestFrom(copied.get(), m_destination, &request));
    EXPECT_EQ(request.kind, te::FileOpKind::Copy);
    EXPECT_EQ(request.dataObject.get(), copied.get());
    ASSERT_TRUE(request.destination.has_value());
    EXPECT_TRUE(*request.destination == m_destination);
    EXPECT_TRUE(request.sources.empty()) << "the data object names the items";

    wil::com_ptr<IDataObject> cut;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::CreateDataObject(m_source, Items(), true, &cut));
    te::FileOpRequest move;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::PasteRequestFrom(cut.get(), m_destination, &move));
    EXPECT_EQ(move.kind, te::FileOpKind::Move);
}

TEST_F(ClipboardTest, SomethingOtherThanFilesIsNotPasted)
{
    // A data object with text only, as from a text editor.
    wil::com_ptr<IDataObject> text;
    ASSERT_HRESULT_SUCCEEDED(SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_PPV_ARGS(&text)));
    const wchar_t content[] = L"not a file";
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(content));
    ASSERT_NE(memory, nullptr);
    memcpy(GlobalLock(memory), content, sizeof(content));
    GlobalUnlock(memory);
    FORMATETC format{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = memory;
    ASSERT_HRESULT_SUCCEEDED(text->SetData(&format, &medium, TRUE));

    te::FileOpRequest request;
    request.id = 77;
    EXPECT_EQ(te::Clipboard::PasteRequestFrom(text.get(), m_destination, &request), DV_E_FORMATETC);
    EXPECT_EQ(request.id, 77u) << "unchanged";
    EXPECT_FALSE(request.dataObject);
}

TEST_F(ClipboardTest, InvalidInputsAreRejected)
{
    wil::com_ptr<IDataObject> data;
    EXPECT_EQ(te::Clipboard::CreateDataObject(m_source, {}, false, &data), E_INVALIDARG);
    EXPECT_EQ(te::Clipboard::CreateDataObject(te::ShellLocation{}, Items(), false, &data), E_INVALIDARG);
    EXPECT_FALSE(data);
    te::FileOpRequest request;
    EXPECT_EQ(te::Clipboard::PasteRequestFrom(nullptr, m_destination, &request), E_INVALIDARG);
}

// Cut in one folder, paste in another: the files move, through the same service the app
// uses, with the data object marshalled to its worker thread.
TEST_F(ClipboardTest, PastingACutMovesTheFiles)
{
    wil::com_ptr<IDataObject> cut;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::CreateDataObject(m_source, Items(), true, &cut));
    te::FileOpRequest request;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::PasteRequestFrom(cut.get(), m_destination, &request));
    request.id = 5;

    const HWND window =
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    te::FileOperationService service;
    service.Submit(window, std::move(request));
    cut.reset(); // the service holds its own reference

    std::unique_ptr<te::FileOpDone> done;
    int items = 0;
    const ULONGLONG until = GetTickCount64() + 30000;
    MSG msg{};
    while (!done && GetTickCount64() < until)
    {
        // The worker calls back into the data object in this apartment, so this thread
        // must keep dispatching while it waits.
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.hwnd == window && msg.message == te::WM_TE_FILEOP_ITEM)
            {
                auto item = te::TakeOwned<te::FileOpItem>(msg.lParam);
                EXPECT_EQ(item->hr, S_OK);
                ++items;
            }
            else if (msg.hwnd == window && msg.message == te::WM_TE_FILEOP_DONE)
            {
                done = te::TakeOwned<te::FileOpDone>(msg.lParam);
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    service.Shutdown();
    te::MainWindow::DrainPendingMessages(window);
    DestroyWindow(window);

    ASSERT_NE(done, nullptr) << "no WM_TE_FILEOP_DONE";
    EXPECT_EQ(done->state, te::FileOpFinalState::Succeeded);
    EXPECT_EQ(items, 2);
    EXPECT_FALSE(fs::exists(m_root / L"src" / L"one.txt"));
    EXPECT_FALSE(fs::exists(m_root / L"src" / L"two.txt"));
    EXPECT_TRUE(fs::exists(m_root / L"dst" / L"one.txt"));
    EXPECT_TRUE(fs::exists(m_root / L"dst" / L"two.txt"));
}

// Shutting down right after a paste is submitted must not deadlock: the worker calls into
// the data object, which lives in this thread's apartment, while this thread waits for
// it. (A regression hangs here rather than failing.)
TEST_F(ClipboardTest, ShutdownDuringAPasteServesTheDataObjectAndFinishes)
{
    wil::com_ptr<IDataObject> copied;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::CreateDataObject(m_source, Items(), false, &copied));
    te::FileOpRequest request;
    ASSERT_HRESULT_SUCCEEDED(te::Clipboard::PasteRequestFrom(copied.get(), m_destination, &request));
    copied.reset();

    const HWND window =
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    {
        // The factory runs on the worker when the operation starts, just before the data
        // object is unmarshalled from this thread's apartment. Waiting for it (without
        // pumping) makes the operation certainly running when Shutdown is called; otherwise
        // Shutdown may drop it while still queued, which is also allowed.
        const wil::unique_event started(wil::EventOptions::ManualReset);
        te::FileOperationService service([&started](IFileOperation** operation) {
            started.SetEvent();
            return CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, __uuidof(IFileOperation),
                                    reinterpret_cast<void**>(operation));
        });
        service.Submit(window, std::move(request));
        ASSERT_TRUE(started.wait(10000));
        const ULONGLONG begin = GetTickCount64();
        service.Shutdown(); // no message loop here: Shutdown itself must serve COM calls
        EXPECT_LT(GetTickCount64() - begin, 30000u);
    }
    te::MainWindow::DrainPendingMessages(window);
    DestroyWindow(window);
    // The running operation was allowed to finish (Shutdown waits for it).
    EXPECT_TRUE(fs::exists(m_root / L"dst" / L"one.txt"));
    EXPECT_TRUE(fs::exists(m_root / L"src" / L"one.txt")) << "a copy, not a move";
}

} // namespace
