// Shutdown order (T088; contracts/component-interfaces.md, "Cross-thread message
// contract"): WM_CLOSE cancels background work at once; WM_DESTROY then runs cancel ->
// file-operation service -> enumerator and icon workers -> drain -> UI Automation ->
// graphics devices. Checked while `10k` is being listed, with a UI Automation client
// connected and a fake file-operation service whose Shutdown finishes one last operation
// (it posts a result that only the drain frees).
//
// Run each test in its own process, as ctest does (gtest_discover_tests): UI Automation
// keeps process-wide state keyed by window handles (see UiaFileListTests.cpp).
//
// Skips when the test data is missing: run tools/New-TestData.ps1 first.

#include <te/app/MainWindow.h>
#include <te/core/Messages.h>

#include <UIAutomationClient.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
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
            if (done && done())
            {
                return;
            }
        }
        if (done && done())
        {
            return;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

std::ptrdiff_t LivePayloads()
{
    return te::EnumBatch::Live() + te::EnumDone::Live() + te::IconReady::Live() + te::FileOpItem::Live() +
           te::FileOpDone::Live();
}

// Records its Shutdown in the trace, and "finishes" an operation there: the result it posts
// can only be freed by the drain that follows.
class TracingOperations final : public te::IFileOperationService
{
  public:
    explicit TracingOperations(std::vector<std::wstring>& trace) : m_trace(trace) {}

    void Submit(HWND notify, te::FileOpRequest) override
    {
        m_notify = notify;
    }
    void Shutdown() override
    {
        m_trace.emplace_back(L"fileops:service");
        if (m_notify)
        {
            auto done = std::make_unique<te::FileOpDone>();
            done->opId = 1;
            posted = te::PostOwned(m_notify, te::WM_TE_FILEOP_DONE, done);
        }
    }

    HWND m_notify = nullptr;
    bool posted = false;

  private:
    std::vector<std::wstring>& m_trace;
};

class MainWindowShutdownTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!fs::exists(TenK()))
        {
            GTEST_SKIP() << "Test data missing: run tools\\New-TestData.ps1";
        }
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        m_payloads = LivePayloads();
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"shutdown test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = TenK().wstring();
        options.fileOperations = &m_operations;
        options.shutdownTrace = [this](const wchar_t* step) { m_trace.emplace_back(step); };
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
    }

    void TearDown() override
    {
        if (m_window)
        {
            if (IsWindow(m_window->Hwnd()))
            {
                DestroyWindow(m_window->Hwnd());
            }
            m_window.reset();
        }
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    // A UI Automation client asks for the window's root, so the providers exist.
    void ConnectAutomation()
    {
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
                        uia->ElementFromHandle(m_window->Hwnd(), &element);
                    }
                }
                CoUninitialize();
            }
            done = true;
        });
        Pump(30000, [&] { return done.load(); });
        client.join();
    }

    bool m_uninitialize = false;
    std::ptrdiff_t m_payloads = 0;
    std::vector<std::wstring> m_trace;
    TracingOperations m_operations{m_trace};
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(MainWindowShutdownTest, CloseWhileListingRunsTheStepsInTheContractOrder)
{
    // The listing is under way (first batch on screen), icons are being requested, and a
    // UI Automation client is connected.
    Pump(15000, [this] { return !m_window->Files().Items().empty(); });
    ASSERT_FALSE(m_window->Files().Items().empty());
    RedrawWindow(m_window->Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    ConnectAutomation();
    ASSERT_NE(m_window->Automation(), nullptr);
    m_window->SubmitFileOperation(te::FileOpRequest{}); // the fake service knows the window now
    ASSERT_TRUE(m_window->Renderer().HasDevice());

    const HWND hwnd = m_window->Hwnd();
    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    Pump(5000, [&] { return !IsWindow(hwnd); });
    ASSERT_FALSE(IsWindow(hwnd)) << "WM_CLOSE destroyed the window";

    const std::vector<std::wstring> expected{L"cancel", L"fileops", L"fileops:service", L"workers",
                                             L"drain",  L"uia",     L"graphics"};
    EXPECT_EQ(m_trace, expected) << "cancel runs once, on WM_CLOSE, not again on WM_DESTROY";
    EXPECT_TRUE(m_operations.posted) << "the service's last result was posted";
    EXPECT_EQ(LivePayloads(), m_payloads) << "and freed by the drain, with every other payload";
    EXPECT_EQ(m_window->Automation(), nullptr) << "UI Automation released";
    EXPECT_FALSE(m_window->Renderer().HasDevice()) << "Direct3D, Direct2D and DirectComposition released";
    EXPECT_EQ(m_window->FileIcons().Size(), 0u);
    for (const te::FileItem& item : m_window->Files().Items())
    {
        EXPECT_FALSE(item.icon.bitmap) << "no bitmap keeps the device alive";
    }
}

TEST_F(MainWindowShutdownTest, DestroyWithoutCloseRunsTheSameSteps)
{
    // DestroyWindow alone (the owner closing it, or process exit paths): the same order,
    // cancelling in WM_DESTROY.
    Pump(15000, [this] { return !m_window->Files().Items().empty(); });
    DestroyWindow(m_window->Hwnd());
    const std::vector<std::wstring> expected{L"cancel", L"fileops", L"fileops:service", L"workers",
                                             L"drain",  L"uia",     L"graphics"};
    EXPECT_EQ(m_trace, expected);
    EXPECT_EQ(LivePayloads(), m_payloads);
}

} // namespace
