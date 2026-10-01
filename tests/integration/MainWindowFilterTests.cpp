// The filter box on the real window (T092; FR-021): Ctrl+F gives it the keyboard; typing
// filters the list as it changes (EN_CHANGE); Enter keeps the filter and gives the keyboard
// back to the list; Escape clears it; a new folder clears it; the edit is named "Filter"
// for UI Automation. Text is put into the edit with WM_SETTEXT / keys as window messages;
// no input is injected.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): the UI
// Automation check shares UI Automation's process-wide state (see UiaFileListTests.cpp).

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>

#include <UIAutomationClient.h>

#include "ScreenCapture.h"

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

class MainWindowFilterTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-filter-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root / L"sub");
        for (const wchar_t* file : {L"alpha.txt", L"alphabet.log", L"beta.txt", L"gamma.md"})
        {
            std::ofstream(m_root / file) << "x";
        }
        std::ofstream(m_root / L"sub" / L"inner.txt") << "x";
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"filter test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = m_root.wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
        Pump(15000,
             [this] { return !m_window->NavigationPending() && m_window->Files().Items().size() == 5; });
        Pump(200);
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
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    // What the user types: the edit's text changes and EN_CHANGE reaches the window.
    void Type(const wchar_t* text)
    {
        SetWindowTextW(m_window->Filter().Edit(), text);
        Pump(20);
    }
    std::vector<std::wstring> Listed() const
    {
        std::vector<std::wstring> names;
        for (const te::FileItem& item : m_window->Files().Items())
        {
            names.push_back(item.info.editName.empty() ? item.info.name : item.info.editName);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    bool m_uninitialize = false;
    fs::path m_root;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(MainWindowFilterTest, CtrlFFocusesTheBoxAndTypingFilters)
{
    ASSERT_EQ(m_window->Files().Items().size(), 5u);
    SendMessageW(m_window->Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_FOCUS_FILTER, 1), 0); // Ctrl+F
    ASSERT_TRUE(m_window->Filter().IsEditing());
    EXPECT_EQ(GetFocus(), m_window->Filter().Edit());
    EXPECT_FALSE(m_window->Files().Focused());

    Type(L"ALPHA");
    EXPECT_EQ(Listed(), (std::vector<std::wstring>{L"alpha.txt", L"alphabet.log"})) << "case-insensitive";
    Type(L"alphab");
    EXPECT_EQ(Listed(), (std::vector<std::wstring>{L"alphabet.log"})) << "narrowed as it is typed";
    Type(L".txt");
    EXPECT_EQ(Listed(), (std::vector<std::wstring>{L"alpha.txt", L"beta.txt"}));
}

TEST_F(MainWindowFilterTest, EnterKeepsTheFilterAndEscapeClearsIt)
{
    SendMessageW(m_window->Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_FOCUS_FILTER, 1), 0);
    Type(L"beta");
    SendMessageW(m_window->Filter().Edit(), WM_KEYDOWN, VK_RETURN, 0);
    EXPECT_FALSE(m_window->Filter().IsEditing());
    EXPECT_EQ(m_window->Filter().Text(), L"beta") << "the box shows the filter";
    EXPECT_EQ(Listed(), (std::vector<std::wstring>{L"beta.txt"}));
    EXPECT_TRUE(m_window->Files().Focused()) << "the list has the keyboard again";

    SendMessageW(m_window->Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_FOCUS_FILTER, 1), 0);
    SendMessageW(m_window->Filter().Edit(), WM_KEYDOWN, VK_ESCAPE, 0);
    EXPECT_FALSE(m_window->Filter().IsEditing());
    EXPECT_TRUE(m_window->Filter().Text().empty());
    EXPECT_EQ(m_window->Files().Items().size(), 5u) << "everything is back";
    EXPECT_TRUE(m_window->Files().Focused());
}

TEST_F(MainWindowFilterTest, ANewFolderClearsTheFilter)
{
    SendMessageW(m_window->Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_FOCUS_FILTER, 1), 0);
    Type(L"sub");
    SendMessageW(m_window->Filter().Edit(), WM_KEYDOWN, VK_RETURN, 0);
    ASSERT_EQ(Listed(), (std::vector<std::wstring>{L"sub"}));
    SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_RETURN, 0); // open "sub"
    Pump(15000, [this] { return !m_window->NavigationPending() && m_window->Files().Items().size() == 1; });
    EXPECT_TRUE(m_window->Filter().Text().empty()) << "the box is empty in the new folder";
    EXPECT_TRUE(m_window->Files().Filter().empty());
    EXPECT_EQ(Listed(), (std::vector<std::wstring>{L"inner.txt"}));
}

// Manual visual check (not run by CTest): the system folder filtered by "shell", saved as
// te-filter.bmp in %TE_CAPTURE_DIR% (or %TEMP%). The system folder keeps personal names out
// of the picture. Run with --gtest_also_run_disabled_tests.
TEST(MainWindowFilterCapture, DISABLED_CaptureTheFilteredList)
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
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window.Hwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT& work = monitor.rcWork;
        SetWindowPos(window.Hwnd(), HWND_TOPMOST, work.left + 20, work.top + 20, (work.right - work.left) / 2,
                     (work.bottom - work.top) / 2, SWP_NOACTIVATE);
        Pump(15000, [&] { return !window.NavigationPending() && window.Files().Items().size() > 100; });
        Pump(1000);
        SendMessageW(window.Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_FOCUS_FILTER, 1), 0);
        SetWindowTextW(window.Filter().Edit(), L"shell");
        SendMessageW(window.Filter().Edit(), WM_KEYDOWN, VK_RETURN, 0);
        Pump(1500);
        RedrawWindow(window.Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        Pump(400);
        te::test::SaveScreen(window.Hwnd(), L"te-filter.bmp");
        SetWindowPos(window.Hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        DestroyWindow(window.Hwnd());
    }
    if (ole)
    {
        OleUninitialize();
    }
}

TEST_F(MainWindowFilterTest, TheEditIsNamedFilterForUiAutomation)
{
    SendMessageW(m_window->Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_FOCUS_FILTER, 1), 0);
    const HWND edit = m_window->Filter().Edit();
    ASSERT_NE(edit, nullptr);
    std::wstring name;
    CONTROLTYPEID type = 0;
    std::atomic<bool> done{false};
    std::thread client([&] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            {
                wil::com_ptr<IUIAutomation> uia;
                if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&uia))))
                {
                    wil::com_ptr<IUIAutomationElement> element;
                    if (SUCCEEDED(uia->ElementFromHandle(edit, &element)) && element)
                    {
                        wil::unique_bstr text;
                        element->get_CurrentName(&text);
                        name = text ? text.get() : L"";
                        element->get_CurrentControlType(&type);
                    }
                }
            }
            CoUninitialize();
        }
        done = true;
    });
    Pump(30000, [&] { return done.load(); });
    client.join();
    EXPECT_EQ(type, UIA_EditControlTypeId);
    EXPECT_EQ(name, L"Filter") << "Dynamic Annotation (IDS_FILTER_PLACEHOLDER)";
}

} // namespace
