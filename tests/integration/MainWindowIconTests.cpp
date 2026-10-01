// File icons in the real window (T087; research R-06, R-07): over `%TEMP%\te-test\10k`,
// 10,000 .txt files that share one Shell icon, the window converts that icon once and every
// row draws the same Direct2D bitmap, also after scrolling far down.
//
// Skips when the test data is missing: run tools/New-TestData.ps1 first.

#include <te/app/MainWindow.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <set>

namespace
{

namespace fs = std::filesystem;

fs::path TenK()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return fs::path(temp) / L"te-test" / L"10k";
}

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

class MainWindowIconTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!fs::exists(TenK()))
        {
            GTEST_SKIP() << "Test data missing: run tools\\New-TestData.ps1";
        }
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"icon test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = TenK().wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
        Pump(20000,
             [this] { return !m_window->NavigationPending() && m_window->Files().Items().size() == 10000; });
    }

    void TearDown() override
    {
        if (m_window)
        {
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    // Paints until every row on screen has its icon; returns the distinct bitmaps they draw.
    std::set<ID2D1Bitmap1*> VisibleIcons()
    {
        const auto& files = m_window->Files();
        const auto visible = [&] {
            std::vector<std::size_t> rows;
            for (std::size_t i = 0; i < files.Items().size(); ++i)
            {
                if (files.IsRowVisible(i))
                {
                    rows.push_back(i);
                }
            }
            return rows;
        };
        Pump(15000, [&] {
            RedrawWindow(m_window->Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            for (const std::size_t i : visible())
            {
                if (!files.Items()[i].icon.bitmap)
                {
                    return false;
                }
            }
            return true;
        });
        std::set<ID2D1Bitmap1*> bitmaps;
        for (const std::size_t i : visible())
        {
            bitmaps.insert(files.Items()[i].icon.bitmap.get());
        }
        return bitmaps;
    }

    bool m_uninitialize = false;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(MainWindowIconTest, TenThousandTextFilesShareOneConvertedIcon)
{
    ASSERT_EQ(m_window->Files().Items().size(), 10000u);
    const std::set<ID2D1Bitmap1*> top = VisibleIcons();
    ASSERT_EQ(top.size(), 1u) << "every visible .txt row draws the same bitmap";
    EXPECT_NE(*top.begin(), nullptr);

    // Far down the list: new rows, the same bitmap, no new conversion.
    m_window->Files().SetScrollOffset(5000 * m_window->Files().RowHeight());
    const std::set<ID2D1Bitmap1*> middle = VisibleIcons();
    ASSERT_EQ(middle.size(), 1u);
    EXPECT_EQ(*middle.begin(), *top.begin());

    EXPECT_EQ(m_window->FileIcons().Size(), 1u) << "one cached bitmap for the .txt icon";
    EXPECT_EQ(m_window->FileIcons().Conversions(), 1u) << "converted once for all rows";
}

} // namespace
