// DirectoryEnumerator (T052, T084; research R-07, FR-018–FR-020) against the generated test
// tree in %TEMP%\te-test (tools/New-TestData.ps1). Results are posted to a message-only
// window on this thread and collected with PeekMessageW.

#include <te/app/MainWindow.h>
#include <te/core/GenerationGuard.h>
#include <te/core/Messages.h>
#include <te/shell/DirectoryEnumerator.h>

#include <shlobj.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

constexpr DWORD kTimeoutMs = 15000;

fs::path TestRoot()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return fs::path(temp) / L"te-test";
}

// One message received from the enumerator, in arrival order.
struct Received
{
    UINT msg = 0;
    std::unique_ptr<te::EnumBatch> batch;
    std::unique_ptr<te::EnumDone> done;
};

class DirectoryEnumeratorTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!fs::exists(TestRoot() / L".te-test-data"))
        {
            GTEST_SKIP() << "Test data missing: run tools\\New-TestData.ps1 (creates " << TestRoot().string()
                         << ") and re-run.";
        }
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_uninitialize = SUCCEEDED(hr); // RPC_E_CHANGED_MODE: the thread already has COM
        m_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
        m_enumerator = std::make_unique<te::DirectoryEnumerator>();
    }

    void TearDown() override
    {
        if (m_enumerator)
        {
            m_enumerator->Shutdown();
            m_enumerator.reset();
        }
        if (m_window)
        {
            te::MainWindow::DrainPendingMessages(m_window); // frees any payloads still queued
            DestroyWindow(m_window);
        }
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    static te::ShellLocation Location(const fs::path& path)
    {
        te::ShellLocation location;
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        EXPECT_HRESULT_SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, wil::out_param(pidl), 0, nullptr))
            << path.string();
        if (pidl)
        {
            EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location)) << path.string();
        }
        return location;
    }

    // Collects WM_TE_ENUM_BATCH / WM_TE_ENUM_DONE until an EnumDone for `untilGen`
    // arrives or the timeout expires.
    std::vector<Received> Collect(te::Generation untilGen)
    {
        std::vector<Received> received;
        const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
        MSG msg{};
        while (GetTickCount64() < deadline)
        {
            if (!PeekMessageW(&msg, m_window, te::WM_TE_ENUM_BATCH, te::WM_TE_ENUM_DONE, PM_REMOVE))
            {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_POSTMESSAGE);
                continue;
            }
            Received r;
            r.msg = msg.message;
            if (msg.message == te::WM_TE_ENUM_BATCH)
            {
                r.batch = te::TakeOwned<te::EnumBatch>(msg.lParam);
            }
            else
            {
                r.done = te::TakeOwned<te::EnumDone>(msg.lParam);
            }
            const bool finished = r.done && r.done->gen == untilGen;
            received.push_back(std::move(r));
            if (finished)
            {
                return received;
            }
        }
        ADD_FAILURE() << "no EnumDone for generation " << untilGen << " within " << kTimeoutMs << " ms";
        return received;
    }

    static const te::EnumDone* DoneFor(const std::vector<Received>& received, te::Generation gen)
    {
        for (const Received& r : received)
        {
            if (r.done && r.done->gen == gen)
            {
                return r.done.get();
            }
        }
        return nullptr;
    }

    static std::vector<const te::ShellItemInfo*> ItemsFor(const std::vector<Received>& received,
                                                          te::Generation gen)
    {
        std::vector<const te::ShellItemInfo*> items;
        for (const Received& r : received)
        {
            if (r.batch && r.batch->gen == gen)
            {
                for (const te::ShellItemInfo& item : r.batch->items)
                {
                    items.push_back(&item);
                }
            }
        }
        return items;
    }

    HWND m_window = nullptr;
    bool m_uninitialize = false;
    std::unique_ptr<te::DirectoryEnumerator> m_enumerator;
};

// Display names hide known extensions when Explorer is set to; accept both forms.
bool NameIs(const std::wstring& name, const std::wstring& file)
{
    return _wcsicmp(name.c_str(), file.c_str()) == 0 ||
           _wcsicmp(name.c_str(), fs::path(file).stem().c_str()) == 0;
}

TEST_F(DirectoryEnumeratorTest, NestedFolderDeliversNamesTypesAndSizes)
{
    m_enumerator->Start(m_window, 1, Location(TestRoot() / L"nested" / L"a"));
    const std::vector<Received> received = Collect(1);

    const te::EnumDone* done = DoneFor(received, 1);
    ASSERT_NE(done, nullptr);
    EXPECT_HRESULT_SUCCEEDED(done->hr);
    EXPECT_FALSE(done->cancelled);
    EXPECT_EQ(received.back().msg, te::WM_TE_ENUM_DONE) << "EnumDone comes last";

    const auto items = ItemsFor(received, 1);
    ASSERT_EQ(items.size(), 3u) << "folder b, readme-a.txt, notes-a.txt";
    int folders = 0, files = 0;
    for (const te::ShellItemInfo* item : items)
    {
        SCOPED_TRACE(::testing::Message() << item->name);
        EXPECT_NE(item->childPidl, nullptr) << "identity within the folder";
        EXPECT_FALSE(item->typeText.empty());
        EXPECT_TRUE(item->modified.has_value());
        if (item->isFolder)
        {
            ++folders;
            EXPECT_TRUE(NameIs(item->name, L"b"));
            EXPECT_FALSE(item->size.has_value()) << "folders have no size";
        }
        else if (NameIs(item->name, L"readme-a.txt"))
        {
            ++files;
            ASSERT_TRUE(item->size.has_value());
            EXPECT_EQ(*item->size, 7u) << "\"Level a\"";
        }
        else if (NameIs(item->name, L"notes-a.txt"))
        {
            ++files;
            ASSERT_TRUE(item->size.has_value());
            EXPECT_EQ(*item->size, 17u) << "\"Notes for level a\"";
        }
    }
    EXPECT_EQ(folders, 1);
    EXPECT_EQ(files, 2);
}

TEST_F(DirectoryEnumeratorTest, AccessDeniedFolderReportsEAccessDenied)
{
    m_enumerator->Start(m_window, 1, Location(TestRoot() / L"readonly-acl"));
    const std::vector<Received> received = Collect(1);
    const te::EnumDone* done = DoneFor(received, 1);
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->hr, E_ACCESSDENIED);
    EXPECT_FALSE(done->cancelled);
    EXPECT_TRUE(ItemsFor(received, 1).empty());
}

// T084 (a): exactly 10,000 items in batches of at most 256, and the UI-side guard
// accepts every one of them.
TEST_F(DirectoryEnumeratorTest, TenThousandFilesArriveInBatchesOfAtMost256)
{
    te::GenerationGuard guard;
    const te::Generation gen = guard.Advance();
    m_enumerator->Start(m_window, gen, Location(TestRoot() / L"10k"));
    std::vector<Received> received = Collect(gen);
    const te::EnumDone* done = DoneFor(received, gen);
    ASSERT_NE(done, nullptr);
    EXPECT_HRESULT_SUCCEEDED(done->hr);
    EXPECT_FALSE(done->cancelled);
    std::size_t accepted = 0;
    std::size_t batches = 0;
    for (Received& r : received)
    {
        if (r.batch)
        {
            ++batches;
            EXPECT_LE(r.batch->items.size(), te::DirectoryEnumerator::kBatchSize);
            EXPECT_FALSE(r.batch->items.empty());
            const std::unique_ptr<te::EnumBatch> taken = guard.Accept(std::move(r.batch));
            ASSERT_NE(taken, nullptr) << "the guard rejected a current batch";
            accepted += taken->items.size();
        }
    }
    EXPECT_EQ(accepted, 10000u);
    EXPECT_GE(batches, 10000u / te::DirectoryEnumerator::kBatchSize);
    EXPECT_EQ(guard.RejectedCount(), 0u);
}

// Starting generation 2 while generation 1 is running: once the guard has accepted a
// generation-2 batch, no generation-1 batch may follow (research R-07).
TEST_F(DirectoryEnumeratorTest, NewerStartSupersedesTheRunningEnumeration)
{
    te::GenerationGuard guard;
    const te::Generation first = guard.Advance();
    m_enumerator->Start(m_window, first, Location(TestRoot() / L"10k"));
    const te::Generation second = guard.Advance();
    m_enumerator->Start(m_window, second, Location(TestRoot() / L"nested" / L"a"));

    const std::vector<Received> received = Collect(second);
    bool secondAccepted = false;
    for (const Received& r : received)
    {
        if (!r.batch)
        {
            continue;
        }
        if (r.batch->gen == first)
        {
            EXPECT_FALSE(secondAccepted) << "a generation-1 batch arrived after generation 2 was accepted";
        }
        // Copy the header only: the guard takes ownership, and the test keeps `received`.
        auto probe = std::make_unique<te::EnumBatch>();
        probe->gen = r.batch->gen;
        const bool accepted = guard.Accept(std::move(probe)) != nullptr;
        EXPECT_EQ(accepted, r.batch->gen == second);
        secondAccepted = secondAccepted || accepted;
    }
    EXPECT_TRUE(secondAccepted);
    EXPECT_EQ(ItemsFor(received, second).size(), 3u);
    EXPECT_LT(ItemsFor(received, first).size(), 10000u) << "generation 1 was cancelled";
}

TEST_F(DirectoryEnumeratorTest, CancelAllEndsWithCancelled)
{
    m_enumerator->Start(m_window, 3, Location(TestRoot() / L"10k"));
    m_enumerator->CancelAll();
    const std::vector<Received> received = Collect(3);
    const te::EnumDone* done = DoneFor(received, 3);
    ASSERT_NE(done, nullptr);
    EXPECT_TRUE(done->cancelled);
    EXPECT_LT(ItemsFor(received, 3).size(), 10000u);
}

// T084 (b): CancelAll after the first batch ends the request with EnumDone { cancelled }
// within 2 s, and no batch is posted after CancelAll returns. Batches already queued
// before it are taken off the queue first; everything after must be the EnumDone alone.
TEST_F(DirectoryEnumeratorTest, CancelAllAfterTheFirstBatchStopsTheBatches)
{
    m_enumerator->Start(m_window, 4, Location(TestRoot() / L"10k"));
    MSG msg{};
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    bool first = false;
    while (!first && GetTickCount64() < deadline)
    {
        if (PeekMessageW(&msg, m_window, te::WM_TE_ENUM_BATCH, te::WM_TE_ENUM_BATCH, PM_REMOVE))
        {
            te::TakeOwned<te::EnumBatch>(msg.lParam).reset();
            first = true;
        }
        else
        {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_POSTMESSAGE);
        }
    }
    ASSERT_TRUE(first) << "no first batch";

    const ULONGLONG cancelledAt = GetTickCount64();
    m_enumerator->CancelAll();
    // What was posted before CancelAll returned is already in the queue.
    std::size_t queuedBefore = 0;
    while (PeekMessageW(&msg, m_window, te::WM_TE_ENUM_BATCH, te::WM_TE_ENUM_BATCH, PM_REMOVE))
    {
        te::TakeOwned<te::EnumBatch>(msg.lParam).reset();
        ++queuedBefore;
    }

    const std::vector<Received> after = Collect(4);
    const ULONGLONG elapsed = GetTickCount64() - cancelledAt;
    const te::EnumDone* done = DoneFor(after, 4);
    ASSERT_NE(done, nullptr);
    EXPECT_TRUE(done->cancelled);
    EXPECT_LE(elapsed, 2000u) << "EnumDone within 2 s of CancelAll";
    EXPECT_TRUE(ItemsFor(after, 4).empty()) << "a batch was posted after CancelAll returned";
    EXPECT_EQ(after.size(), 1u) << "only the EnumDone";
    std::printf("batches queued before the cancel: %zu; EnumDone after %llu ms\n", queuedBefore, elapsed);
}

// T084 (c): Shutdown while enumerating 10k returns, and every payload the enumerator
// created is freed: those it could not post or chose not to, by the worker; those in the
// queue, by the drain the window does on WM_DESTROY (MainWindow::DrainPendingMessages).
TEST_F(DirectoryEnumeratorTest, ShutdownWhileEnumeratingFreesEveryPayload)
{
    const std::ptrdiff_t batchesBefore = te::EnumBatch::Live();
    const std::ptrdiff_t donesBefore = te::EnumDone::Live();

    m_enumerator->Start(m_window, 5, Location(TestRoot() / L"10k"));
    MSG msg{};
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    while (!PeekMessageW(&msg, m_window, te::WM_TE_ENUM_BATCH, te::WM_TE_ENUM_BATCH, PM_NOREMOVE) &&
           GetTickCount64() < deadline)
    {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_POSTMESSAGE);
    }
    ASSERT_GT(te::EnumBatch::Live(), batchesBefore) << "the enumeration is running";

    const ULONGLONG started = GetTickCount64();
    m_enumerator->Shutdown();
    const ULONGLONG took = GetTickCount64() - started;
    EXPECT_LE(took, 5000u) << "Shutdown returned";

    // Count what reached the queue before the drain frees it.
    std::size_t items = 0;
    bool doneCancelled = false;
    while (PeekMessageW(&msg, m_window, te::WM_TE_ENUM_BATCH, te::WM_TE_ENUM_DONE, PM_REMOVE))
    {
        if (msg.message == te::WM_TE_ENUM_BATCH)
        {
            items += te::TakeOwned<te::EnumBatch>(msg.lParam)->items.size();
        }
        else
        {
            doneCancelled = te::TakeOwned<te::EnumDone>(msg.lParam)->cancelled;
        }
    }
    EXPECT_TRUE(doneCancelled || items < 10000u) << "Shutdown interrupted the enumeration";
    te::MainWindow::DrainPendingMessages(m_window);

    EXPECT_EQ(te::EnumBatch::Live(), batchesBefore) << "every batch freed";
    EXPECT_EQ(te::EnumDone::Live(), donesBefore) << "every EnumDone freed";
    std::printf("Shutdown took %llu ms; %zu items had reached the queue\n", took, items);
}

// The drain frees every payload type left in the queue (T084 c, used by WM_DESTROY).
TEST_F(DirectoryEnumeratorTest, DrainFreesEveryPayloadType)
{
    const std::ptrdiff_t before = te::EnumBatch::Live() + te::EnumDone::Live() + te::IconReady::Live() +
                                  te::FileOpItem::Live() + te::FileOpDone::Live();
    auto batch = std::make_unique<te::EnumBatch>();
    auto done = std::make_unique<te::EnumDone>();
    auto icon = std::make_unique<te::IconReady>();
    auto item = std::make_unique<te::FileOpItem>();
    auto opDone = std::make_unique<te::FileOpDone>();
    ASSERT_TRUE(te::PostOwned(m_window, te::WM_TE_ENUM_BATCH, batch));
    ASSERT_TRUE(te::PostOwned(m_window, te::WM_TE_ENUM_DONE, done));
    ASSERT_TRUE(te::PostOwned(m_window, te::WM_TE_ICON_READY, icon));
    ASSERT_TRUE(te::PostOwned(m_window, te::WM_TE_FILEOP_ITEM, item));
    ASSERT_TRUE(te::PostOwned(m_window, te::WM_TE_FILEOP_DONE, opDone));
    EXPECT_EQ(te::EnumBatch::Live() + te::EnumDone::Live() + te::IconReady::Live() + te::FileOpItem::Live() +
                  te::FileOpDone::Live(),
              before + 5);
    te::MainWindow::DrainPendingMessages(m_window);
    EXPECT_EQ(te::EnumBatch::Live() + te::EnumDone::Live() + te::IconReady::Live() + te::FileOpItem::Live() +
                  te::FileOpDone::Live(),
              before);
}

} // namespace
