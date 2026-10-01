// IconProvider (T059, T087; research R-06, R-07): Shell icons arrive as WM_TE_ICON_READY with
// the right generation and item key and a 32-bpp bitmap at least the requested size;
// CancelOlderThan drops requests still queued for an earlier generation; the newest request
// is served first; an icon already sent (same system image-list index and size) comes back
// shared, without a bitmap, unless extraction is forced.

#include <te/app/MainWindow.h>
#include <te/core/Messages.h>
#include <te/shell/IconProvider.h>

#include <shlobj.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace
{

constexpr DWORD kTimeoutMs = 15000;

class IconProviderTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize =
            SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
        m_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);

        // C:\Windows and one item in it, as the file list would hold them.
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> folder;
        ASSERT_HRESULT_SUCCEEDED(
            SHGetKnownFolderIDList(FOLDERID_Windows, 0, nullptr, wil::out_param(folder)));
        ASSERT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(folder.get(), &m_folder));
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> file;
        wchar_t notepad[MAX_PATH]{};
        ExpandEnvironmentStringsW(L"%SystemRoot%\\notepad.exe", notepad, MAX_PATH);
        ASSERT_HRESULT_SUCCEEDED(SHParseDisplayName(notepad, nullptr, wil::out_param(file), 0, nullptr));
        m_item.name = L"notepad.exe";
        m_item.childPidl.reset(reinterpret_cast<ITEMID_CHILD*>(ILClone(ILFindLastID(file.get()))));
    }

    void TearDown() override
    {
        m_provider.Shutdown();
        if (m_window)
        {
            te::MainWindow::DrainPendingMessages(m_window);
            DestroyWindow(m_window);
        }
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    // Collects IconReady payloads until `count` arrived or the timeout expires.
    std::vector<std::unique_ptr<te::IconReady>> Collect(std::size_t count, DWORD timeoutMs = kTimeoutMs)
    {
        std::vector<std::unique_ptr<te::IconReady>> received;
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        MSG msg{};
        while (received.size() < count && GetTickCount64() < deadline)
        {
            if (PeekMessageW(&msg, m_window, te::WM_TE_ICON_READY, te::WM_TE_ICON_READY, PM_REMOVE))
            {
                received.push_back(te::TakeOwned<te::IconReady>(msg.lParam));
            }
            else
            {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_POSTMESSAGE);
            }
        }
        return received;
    }

    bool m_uninitialize = false;
    HWND m_window = nullptr;
    te::ShellLocation m_folder;
    te::ShellItemInfo m_item;
    te::IconProvider m_provider;
};

TEST_F(IconProviderTest, DeliversA32BppIconWithGenerationAndKey)
{
    m_provider.Request(m_window, 5, 42, m_folder, m_item, 24);
    auto received = Collect(1);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0]->gen, 5u);
    EXPECT_EQ(received[0]->itemKey, 42u);
    ASSERT_TRUE(received[0]->bmp);

    BITMAP info{};
    ASSERT_NE(GetObjectW(received[0]->bmp.get(), sizeof(info), &info), 0);
    EXPECT_GE(info.bmWidth, 24) << "SIIGBF_BIGGERSIZEOK: the requested size or larger";
    EXPECT_GE(info.bmHeight, 24);
    EXPECT_EQ(info.bmBitsPixel, 32) << "with alpha, for Direct2D";
}

TEST_F(IconProviderTest, InvalidRequestsAreIgnored)
{
    m_provider.Request(m_window, 1, 1, te::ShellLocation{}, m_item, 16);
    te::ShellItemInfo noPidl;
    m_provider.Request(m_window, 1, 2, m_folder, noPidl, 16);
    EXPECT_TRUE(Collect(1, 500).empty());
}

TEST_F(IconProviderTest, CancelOlderThanDropsQueuedRequests)
{
    constexpr std::size_t kOld = 300;
    for (std::size_t i = 0; i < kOld; ++i)
    {
        m_provider.Request(m_window, 1, i, m_folder, m_item, 32);
    }
    m_provider.CancelOlderThan(2);
    m_provider.Request(m_window, 2, 9999, m_folder, m_item, 32);

    // Everything still arriving: at most the few generation-1 requests already running,
    // then generation 2's.
    std::vector<std::unique_ptr<te::IconReady>> received;
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    bool newArrived = false;
    while (!newArrived && GetTickCount64() < deadline)
    {
        for (auto& r : Collect(1, 200))
        {
            newArrived = newArrived || r->gen == 2;
            received.push_back(std::move(r));
        }
    }
    ASSERT_TRUE(newArrived);
    std::size_t old = 0;
    for (const auto& r : received)
    {
        old += r->gen == 1 ? 1 : 0;
    }
    EXPECT_LT(old, kOld / 10) << "queued generation-1 requests were dropped";
}

TEST_F(IconProviderTest, CancelOlderThanNeverLowersTheFloor)
{
    m_provider.CancelOlderThan(10);
    m_provider.CancelOlderThan(3); // late and older: ignored
    m_provider.Request(m_window, 5, 1, m_folder, m_item, 16);
    EXPECT_TRUE(Collect(1, 1000).empty()) << "generation 5 is below the floor of 10";
    m_provider.Request(m_window, 10, 2, m_folder, m_item, 16);
    EXPECT_EQ(Collect(1).size(), 1u);
}

TEST_F(IconProviderTest, ShutdownStopsFurtherRequests)
{
    m_provider.Shutdown();
    m_provider.Request(m_window, 1, 1, m_folder, m_item, 16);
    EXPECT_TRUE(Collect(1, 500).empty());
}

TEST_F(IconProviderTest, AnIconAlreadySentComesBackSharedUnlessForced)
{
    m_provider.Request(m_window, 1, 1, m_folder, m_item, 16);
    auto first = Collect(1);
    ASSERT_EQ(first.size(), 1u);
    ASSERT_TRUE(first[0]->bmp);
    EXPECT_GE(first[0]->imageIndex, 0) << "SHGetFileInfoW(SHGFI_SYSICONINDEX)";
    EXPECT_EQ(first[0]->sizePx, 16);
    EXPECT_FALSE(first[0]->shared);

    m_provider.Request(m_window, 1, 2, m_folder, m_item, 16); // the same icon again
    auto second = Collect(1);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_TRUE(second[0]->shared);
    EXPECT_FALSE(second[0]->bmp) << "no second extraction";
    EXPECT_EQ(second[0]->imageIndex, first[0]->imageIndex);

    m_provider.Request(m_window, 1, 3, m_folder, m_item, 32); // another size
    auto bigger = Collect(1);
    ASSERT_EQ(bigger.size(), 1u);
    EXPECT_TRUE(bigger[0]->bmp);

    m_provider.Request(m_window, 1, 4, m_folder, m_item, 16, true); // forced
    auto forced = Collect(1);
    ASSERT_EQ(forced.size(), 1u);
    EXPECT_TRUE(forced[0]->bmp);
    EXPECT_FALSE(forced[0]->shared);
}

TEST_F(IconProviderTest, TheNewestRequestIsServedFirst)
{
    // Queue many requests at once: the worker picks the newest waiting one each time, so
    // the last one requested arrives near the start, not at the end.
    constexpr std::size_t kCount = 60;
    for (std::size_t key = 1; key <= kCount; ++key)
    {
        m_provider.Request(m_window, 1, key, m_folder, m_item, 16);
    }
    std::vector<std::size_t> order;
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    while (order.size() < kCount && GetTickCount64() < deadline)
    {
        for (auto& r : Collect(1, 200))
        {
            order.push_back(r->itemKey);
        }
    }
    ASSERT_EQ(order.size(), kCount);
    const auto position =
        static_cast<std::size_t>(std::find(order.begin(), order.end(), kCount) - order.begin());
    EXPECT_LE(position, 2u) << "the newest request is served before the older ones";
    // Once the first request (already running) is out, the rest arrive newest first.
    std::size_t descending = 0;
    for (std::size_t i = 2; i < order.size(); ++i)
    {
        descending += order[i] < order[i - 1] ? 1 : 0;
    }
    EXPECT_GE(descending, order.size() - 4) << "LIFO order";
}

} // namespace
