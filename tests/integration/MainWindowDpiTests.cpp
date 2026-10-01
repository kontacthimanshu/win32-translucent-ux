// DPI changes on the real window (T081; the automated part of quickstart V-5d). The
// messages Windows sends when the window moves to a monitor with another scale, or the
// scale changes, are sent here directly: WM_GETDPISCALEDSIZE, then WM_DPICHANGED with the
// rectangle built from its answer. The user's display settings are never changed; real
// monitor moves and 100 / 150 / 200 % runs are manual (tests/manual/dpi.md, T083).
//
// Skips when the test data is missing: run tools/New-TestData.ps1 first.

#include <te/app/MainWindow.h>
#include <te/window/CaptionHitTester.h>
#include <te/window/CustomTitleBar.h>
#include <te/window/DpiManager.h>

#include "../../resources/resource.h" // IDI_APP, embedded in the test executable
#include "ScreenCapture.h"

#include <gtest/gtest.h>
#include <wil/resource.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

fs::path TestFolder()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return fs::path(temp) / L"te-test" / L"nested";
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

int IconWidth(HICON icon)
{
    ICONINFO info{};
    if (!icon || !GetIconInfo(icon, &info))
    {
        return 0;
    }
    wil::unique_hbitmap color(info.hbmColor);
    wil::unique_hbitmap mask(info.hbmMask);
    BITMAP bitmap{};
    GetObjectW(color ? color.get() : mask.get(), sizeof(bitmap), &bitmap);
    return bitmap.bmWidth;
}

class MainWindowDpiTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!fs::exists(TestFolder()))
        {
            GTEST_SKIP() << "Test data missing: run tools/New-TestData.ps1";
        }
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"dpi test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = TestFolder().wstring();
        options.iconResourceId = IDI_APP;
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
        // 45 % of the work area: it still fits on the monitor at twice the scale (Windows
        // never makes a window larger than the screen, ptMaxTrackSize), and it stays above
        // the minimum size at every scale tried (which would otherwise grow it).
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(Hwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT& work = monitor.rcWork;
        SetWindowPos(Hwnd(), nullptr, work.left + 20, work.top + 20, (work.right - work.left) * 45 / 100,
                     (work.bottom - work.top) * 45 / 100, SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(15000, [this] { return !m_window->NavigationPending() && !m_window->Files().Items().empty(); });
        WaitForIcons();
    }

    void TearDown() override
    {
        if (m_window)
        {
            if (m_window->Picker() && m_window->Picker()->IsOpen())
            {
                m_window->TogglePicker();
            }
            DestroyWindow(Hwnd());
            m_window.reset();
        }
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    [[nodiscard]] HWND Hwnd() const
    {
        return m_window->Hwnd();
    }

    [[nodiscard]] bool IconsSettled() const
    {
        const auto& items = m_window->Files().Items();
        if (items.empty())
        {
            return false;
        }
        for (const te::FileItem& item : items)
        {
            if (item.icon.state != te::IconSlot::State::Ready &&
                item.icon.state != te::IconSlot::State::Failed)
            {
                return false;
            }
        }
        return true;
    }

    void WaitForIcons()
    {
        // Icons are requested while painting; keep painting until every row has one.
        Pump(15000, [this] {
            RedrawWindow(Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            return IconsSettled();
        });
    }

    // What Windows does on a DPI change: asks for the size, then sends WM_DPICHANGED with
    // the rectangle built from the answer (the window keeps its top-left corner here).
    RECT ChangeDpi(UINT newDpi)
    {
        RECT window{};
        GetWindowRect(Hwnd(), &window);
        SIZE size{};
        EXPECT_EQ(SendMessageW(Hwnd(), WM_GETDPISCALEDSIZE, newDpi, reinterpret_cast<LPARAM>(&size)), TRUE);
        // In a real change the window already has the new DPI when WM_DPICHANGED arrives,
        // so Windows draws its frame at that DPI. Here the window keeps its real DPI, and
        // with it the real frame: the rectangle carries that frame instead, so the client
        // area gets exactly the size asked for.
        const SIZE askedFrame = te::CustomTitleBar::FrameSizeForDpi(Hwnd(), newDpi);
        const SIZE realFrame = te::CustomTitleBar::FrameSizeForDpi(Hwnd(), GetDpiForWindow(Hwnd()));
        size.cx += realFrame.cx - askedFrame.cx;
        size.cy += realFrame.cy - askedFrame.cy;
        RECT suggested{window.left, window.top, window.left + size.cx, window.top + size.cy};
        SendMessageW(Hwnd(), WM_DPICHANGED, MAKEWPARAM(newDpi, newDpi), reinterpret_cast<LPARAM>(&suggested));
        return suggested;
    }

    [[nodiscard]] SIZE ClientSize() const
    {
        RECT client{};
        GetClientRect(Hwnd(), &client);
        return {client.right, client.bottom};
    }

    bool m_uninitialize = false;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(MainWindowDpiTest, TheFrameModelMatchesTheWindow)
{
    // FrameSizeForDpi is what WM_GETDPISCALEDSIZE adds to the scaled client area; at the
    // current DPI it must equal the real frame (WM_NCCALCSIZE keeps the side and bottom
    // borders and gives the caption to the client area).
    RECT window{};
    GetWindowRect(Hwnd(), &window);
    const SIZE client = ClientSize();
    const SIZE frame = te::CustomTitleBar::FrameSizeForDpi(Hwnd(), m_window->Dpi());
    EXPECT_EQ(window.right - window.left - client.cx, frame.cx);
    EXPECT_EQ(window.bottom - window.top - client.cy, frame.cy);
}

TEST_F(MainWindowDpiTest, GetDpiScaledSizeKeepsTheClientAreaInDips)
{
    const UINT dpi = m_window->Dpi();
    const SIZE client = ClientSize();
    for (const UINT newDpi : {dpi * 3 / 2, dpi * 2, dpi * 2 / 3})
    {
        SIZE size{};
        ASSERT_EQ(SendMessageW(Hwnd(), WM_GETDPISCALEDSIZE, newDpi, reinterpret_cast<LPARAM>(&size)), TRUE);
        const SIZE frame = te::CustomTitleBar::FrameSizeForDpi(Hwnd(), newDpi);
        EXPECT_EQ(size.cx - frame.cx, std::lround(static_cast<double>(client.cx) * newDpi / dpi)) << newDpi;
        EXPECT_EQ(size.cy - frame.cy, std::lround(static_cast<double>(client.cy) * newDpi / dpi)) << newDpi;
    }
    EXPECT_EQ(m_window->Dpi(), dpi) << "only a question: nothing changes yet";
}

TEST_F(MainWindowDpiTest, DpiChangeRescalesEverythingAndKeepsTheListing)
{
    ASSERT_TRUE(IconsSettled());
    const UINT dpi = m_window->Dpi();
    const UINT newDpi = dpi * 3 / 2;
    const float oldScale = static_cast<float>(dpi) / 96.0f;
    const float newScale = static_cast<float>(newDpi) / 96.0f;

    // Select the second row, so the change can be shown to keep the selection.
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
    auto& files = m_window->Files();
    const std::vector<std::size_t> selected = files.SelectionState().SelectedIndices();
    const std::size_t itemCount = files.Items().size();
    const te::Generation listing = files.CurrentGeneration();
    const te::Generation icons = files.IconGeneration();
    const D2D1_RECT_F fileList = m_window->Layout().fileList;
    const SIZE oldClient = ClientSize();

    const RECT suggested = ChangeDpi(newDpi);

    // The window took the suggested rectangle, and its client area kept its DIPs.
    RECT window{};
    GetWindowRect(Hwnd(), &window);
    EXPECT_EQ(window.right - window.left, suggested.right - suggested.left);
    EXPECT_EQ(window.bottom - window.top, suggested.bottom - suggested.top);
    const SIZE client = ClientSize();
    EXPECT_NEAR(client.cx / newScale, oldClient.cx / oldScale, 1.0f);
    EXPECT_NEAR(client.cy / newScale, oldClient.cy / oldScale, 1.0f);

    // DPI, render target and layout.
    EXPECT_EQ(m_window->Dpi(), newDpi);
    EXPECT_EQ(m_window->Renderer().Dpi(), newDpi);
    EXPECT_EQ(m_window->Renderer().SizePx().cx, client.cx);
    EXPECT_EQ(m_window->Renderer().SizePx().cy, client.cy);
    const D2D1_RECT_F list = m_window->Layout().fileList;
    EXPECT_NEAR(list.right - list.left, fileList.right - fileList.left, 1.0f) << "the same DIPs";
    // The caption above it is built from system metrics, which Windows rounds at each DPI
    // on its own, so the height may differ by a DIP or so.
    EXPECT_NEAR(list.bottom - list.top, fileList.bottom - fileList.top, 2.0f);

    // Caption layout recomputed at the new DPI: the 40-DIP picker.
    const RECT picker = m_window->TitleBar().Layout().picker;
    EXPECT_NEAR(picker.right - picker.left, te::CaptionHitTester::kPickerWidthDip * newScale, 1.0f);
    EXPECT_GE(m_window->TitleBar().Layout().captionHeightPx, GetSystemMetricsForDpi(SM_CYCAPTION, newDpi));

    // The title bar icon, loaded again at the new size.
    EXPECT_EQ(IconWidth(m_window->SmallIcon()), GetSystemMetricsForDpi(SM_CXSMICON, newDpi));

    // The listing stays; only the icons start over, under a new generation.
    EXPECT_EQ(files.CurrentGeneration(), listing);
    EXPECT_GT(files.IconGeneration(), icons);
    EXPECT_EQ(files.Items().size(), itemCount);
    EXPECT_EQ(files.SelectionState().SelectedIndices(), selected);
    for (const te::FileItem& item : files.Items())
    {
        EXPECT_NE(item.icon.state, te::IconSlot::State::Ready) << "every icon is requested again";
    }
    WaitForIcons();
    EXPECT_TRUE(IconsSettled()) << "the icons arrived at the new size";
    std::size_t withBitmap = 0;
    for (const te::FileItem& item : files.Items())
    {
        if (item.icon.bitmap)
        {
            ++withBitmap;
            EXPECT_GE(item.icon.bitmap->GetPixelSize().width,
                      static_cast<UINT32>(std::lround(te::FileView::kIconSizeDip * newScale)))
                << "at least the row's icon size at the new DPI";
        }
    }
    EXPECT_GT(withBitmap, 0u);
}

TEST_F(MainWindowDpiTest, BackToTheOriginalDpiRestoresTheWindowSize)
{
    RECT before{};
    GetWindowRect(Hwnd(), &before);
    const SIZE client = ClientSize();
    const UINT dpi = m_window->Dpi();
    ChangeDpi(dpi * 2);
    ChangeDpi(dpi);
    RECT after{};
    GetWindowRect(Hwnd(), &after);
    EXPECT_EQ(after.right - after.left, before.right - before.left) << "no drift from rounding";
    EXPECT_EQ(after.bottom - after.top, before.bottom - before.top);
    EXPECT_EQ(ClientSize().cx, client.cx);
    EXPECT_EQ(ClientSize().cy, client.cy);
}

TEST_F(MainWindowDpiTest, AnOpenPopupFollowsThePickerButton)
{
    // When the test process is not the foreground app, the popup may close itself at once
    // (its deactivation rule, R-10; see UiaTitleBarButtonTests.cpp): then there is nothing
    // to follow, and ColorPickerTests covers Reposition on its own.
    m_window->TogglePicker();
    ASSERT_NE(m_window->Picker(), nullptr);
    if (!m_window->Picker()->IsOpen())
    {
        GTEST_SKIP() << "the popup closed itself (test process not in the foreground)";
    }
    ChangeDpi(m_window->Dpi() * 3 / 2);
    if (!m_window->Picker()->IsOpen())
    {
        GTEST_SKIP() << "the popup closed itself (test process not in the foreground)";
    }
    const RECT anchor = m_window->TitleBar().PickerScreenRect(Hwnd());
    RECT popup{};
    GetWindowRect(m_window->Picker()->Dialog(), &popup);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(Hwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
    EXPECT_GE(popup.left, monitor.rcWork.left);
    EXPECT_GE(popup.top, monitor.rcWork.top);
    EXPECT_LE(popup.right, monitor.rcWork.right);
    EXPECT_LE(popup.bottom, monitor.rcWork.bottom);
    if (anchor.right <= monitor.rcWork.right &&
        anchor.bottom + (popup.bottom - popup.top) <= monitor.rcWork.bottom)
    {
        EXPECT_EQ(popup.right, anchor.right) << "right-aligned under the moved button";
        EXPECT_EQ(popup.top, anchor.bottom);
    }
}

// Manual visual check (not run by CTest): the window after a DPI change to 100 %, 200 %
// and back, each copied from the screen to te-dpi-<step>-<dpi>.bmp in %TE_CAPTURE_DIR% (or
// %TEMP%). Run with --gtest_also_run_disabled_tests. The caption buttons are drawn by the
// DWM at the monitor's real scale; everything else is the app's.
TEST_F(MainWindowDpiTest, DISABLED_CaptureAtEachScale)
{
    SetWindowPos(Hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_HOME, 0);
    const UINT real = m_window->Dpi();
    int step = 1;
    for (const UINT dpi : {real, 96u, 192u, real})
    {
        if (dpi != m_window->Dpi())
        {
            ChangeDpi(dpi);
        }
        WaitForIcons();
        RedrawWindow(Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        Pump(400);
        te::test::SaveScreen(Hwnd(),
                             L"te-dpi-" + std::to_wstring(step++) + L"-" + std::to_wstring(dpi) + L".bmp");
    }
    SetWindowPos(Hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace
