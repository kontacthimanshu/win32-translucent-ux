// Responsiveness while a large folder loads (T089; quickstart V-3f, FR-018, SC-009). The
// window opens `%TEMP%\te-test\10k`; while the listing is still arriving the test scrolls
// with the wheel, resizes the window, moves with the keyboard and opens the appearance
// popup, all through window messages, and times every message the UI thread handles.
//
// No latency target is asserted (the spec sets none). The test checks that every action
// took effect while the listing was still loading and that the listing completed, and it
// prints the longest message handler and how many took longer than the Debug watchdog's
// 50 ms (in Debug builds the watchdog's own lines are captured too, as DebugView would
// show them). tests/manual/performance.md records the numbers.
//
// Skips when the test data is missing: run tools/New-TestData.ps1 first.

#include <te/app/MainWindow.h>
#include <te/core/Watchdog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

fs::path TenK()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return fs::path(temp) / L"te-test" / L"10k";
}

std::vector<std::wstring>& WatchdogLines()
{
    static std::vector<std::wstring> lines;
    return lines;
}

struct Timing
{
    std::size_t messages = 0;
    double longestMs = 0.0;
    UINT longestMsg = 0;
    std::size_t over50Ms = 0;
};

class MainWindowResponsivenessTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!fs::exists(TenK()))
        {
            GTEST_SKIP() << "Test data missing: run tools\\New-TestData.ps1";
        }
#ifdef _DEBUG
        te::DispatchWatchdog::SetSinkForTesting(
            [](const wchar_t* line) { WatchdogLines().emplace_back(line); });
#endif
        QueryPerformanceFrequency(&m_frequency);
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"responsiveness test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = TenK().wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
    }

    void TearDown() override
    {
        if (m_window)
        {
            if (m_window->Picker() && m_window->Picker()->IsOpen())
            {
                m_window->TogglePicker();
            }
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
        if (m_uninitialize)
        {
            OleUninitialize();
        }
#ifdef _DEBUG
        te::DispatchWatchdog::SetSinkForTesting(nullptr);
#endif
    }

    // One message at a time, each timed (and wrapped in the watchdog, as the app's message
    // loop does), until `done` or the timeout.
    void Pump(DWORD ms, const std::function<bool()>& done = {})
    {
        const ULONGLONG until = GetTickCount64() + ms;
        MSG msg{};
        while (GetTickCount64() < until)
        {
            if (done && done())
            {
                return;
            }
            if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
                continue;
            }
            LARGE_INTEGER start{};
            LARGE_INTEGER end{};
            QueryPerformanceCounter(&start);
            {
                const te::DispatchWatchdog watchdog(msg.message);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            QueryPerformanceCounter(&end);
            Record(msg.message, end.QuadPart - start.QuadPart);
        }
    }

    // Times a message sent directly (not queued), as the action it stands for.
    void Send(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LARGE_INTEGER start{};
        LARGE_INTEGER end{};
        QueryPerformanceCounter(&start);
        SendMessageW(m_window->Hwnd(), msg, wParam, lParam);
        QueryPerformanceCounter(&end);
        Record(msg, end.QuadPart - start.QuadPart);
    }

    void Record(UINT msg, LONGLONG ticks)
    {
        const double elapsedMs =
            1000.0 * static_cast<double>(ticks) / static_cast<double>(m_frequency.QuadPart);
        ++m_timing.messages;
        if (elapsedMs > m_timing.longestMs)
        {
            m_timing.longestMs = elapsedMs;
            m_timing.longestMsg = msg;
        }
        m_timing.over50Ms += elapsedMs > static_cast<double>(te::kWatchdogThresholdMs) ? 1 : 0;
    }

    [[nodiscard]] std::size_t Loaded() const
    {
        return m_window->Files().Items().size();
    }

    LARGE_INTEGER m_frequency{};
    Timing m_timing;
    bool m_uninitialize = false;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(MainWindowResponsivenessTest, TheWindowRespondsWhileTenThousandItemsLoad)
{
    const ULONGLONG started = GetTickCount64();
    Pump(15000, [this] { return Loaded() > 0; }); // the first batch is on screen
    ASSERT_GT(Loaded(), 0u);
    std::vector<std::size_t> loadedAt; // items listed when each action ran

    // Wheel over the file list: three notches down.
    const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
    const D2D1_RECT_F list = m_window->Layout().fileList;
    POINT inList{static_cast<LONG>((list.left + 100) * scale), static_cast<LONG>((list.top + 80) * scale)};
    ClientToScreen(m_window->Hwnd(), &inList);
    loadedAt.push_back(Loaded());
    for (int i = 0; i < 3; ++i)
    {
        Send(WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(inList.x, inList.y));
    }
    const float scrolled = m_window->Files().ScrollOffset();
    Pump(50);

    // Resize twice.
    RECT window{};
    GetWindowRect(m_window->Hwnd(), &window);
    const D2D1_RECT_F listBefore = m_window->Layout().fileList;
    loadedAt.push_back(Loaded());
    SetWindowPos(m_window->Hwnd(), nullptr, 0, 0, (window.right - window.left) - 200,
                 (window.bottom - window.top) - 100, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(50);
    const D2D1_RECT_F listResized = m_window->Layout().fileList;
    SetWindowPos(m_window->Hwnd(), nullptr, 0, 0, window.right - window.left, window.bottom - window.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(50);

    // Keyboard: End, then Home.
    loadedAt.push_back(Loaded());
    Send(WM_KEYDOWN, VK_END, 0);
    const auto focusAtEnd = m_window->Files().SelectionState().FocusIndex();
    Send(WM_KEYDOWN, VK_HOME, 0);
    Pump(50);

    // The appearance popup (it may close itself at once when the test process is not in
    // the foreground; see UiaTitleBarButtonTests.cpp): the call must return promptly.
    loadedAt.push_back(Loaded());
    m_window->TogglePicker();
    const bool popupOpened = m_window->Picker() && m_window->Picker()->IsOpen();
    if (popupOpened)
    {
        m_window->TogglePicker();
    }

    // The rest of the listing.
    Pump(30000, [this] { return Loaded() == 10000 && !m_window->NavigationPending(); });
    Pump(200);
    const ULONGLONG total = GetTickCount64() - started;

    EXPECT_GT(scrolled, 0.0f) << "the wheel scrolled the list";
    EXPECT_LT(listResized.right - listResized.left, listBefore.right - listBefore.left)
        << "the layout followed";
    ASSERT_TRUE(focusAtEnd.has_value());
    EXPECT_EQ(Loaded(), 10000u) << "the listing completed";
    const std::size_t duringLoad = static_cast<std::size_t>(
        std::count_if(loadedAt.begin(), loadedAt.end(), [](std::size_t n) { return n < 10000; }));

    std::printf("V-3f: %zu of %zu actions ran while the listing was incomplete (items then: ", duringLoad,
                loadedAt.size());
    for (const std::size_t n : loadedAt)
    {
        std::printf("%zu ", n);
    }
    std::printf("); listing done in %llu ms; popup opened: %s\n", total,
                popupOpened ? "yes" : "no (closed itself)");
    std::printf("V-3f: %zu messages handled; longest %.1f ms (msg 0x%04X); %zu over %lld ms\n",
                m_timing.messages, m_timing.longestMs, m_timing.longestMsg, m_timing.over50Ms,
                te::kWatchdogThresholdMs);
    for (const std::wstring& line : WatchdogLines())
    {
        std::printf("watchdog: %ls", line.c_str());
    }
    EXPECT_GE(duringLoad, 1u) << "at least the first action ran while items were still arriving";
}

} // namespace
