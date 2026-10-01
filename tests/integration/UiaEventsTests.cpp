// UI Automation events (T079; research R-09; UI contract §5 "Events"), through real UIA
// event handlers on the client thread against the main window over a temp folder:
// focus moves, selection events (selected / added / removed / invalidated), the
// throttled StructureChanged on a new listing, and LiveRegionChanged on status messages.
// An in-process client can receive an event twice (T075), so counts are "at least".
//
// Run each test in its own process, as ctest does (gtest_discover_tests): UI Automation
// keeps process-wide state keyed by window handles (see UiaFileListTests.cpp).

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>

#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

// Work the client thread hands to the UI thread (which owns the window).
struct UiQueue
{
    std::mutex mutex;
    std::function<void()> action;
    std::atomic<bool> done{false};

    // On the client thread: runs `work` on the UI thread and waits for it.
    void Run(std::function<void()> work)
    {
        {
            std::scoped_lock lock(mutex);
            done = false;
            action = std::move(work);
        }
        while (!done)
        {
            Sleep(5);
        }
    }
    // On the UI thread, from the pump loop.
    void Drain()
    {
        std::function<void()> work;
        {
            std::scoped_lock lock(mutex);
            work = std::move(action);
            action = nullptr;
        }
        if (work)
        {
            work();
            done = true;
        }
    }
};

void Pump(DWORD ms, const std::function<bool()>& stop = {}, UiQueue* queue = nullptr)
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
        if (queue)
        {
            queue->Drain();
        }
        if (stop && stop())
        {
            return;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}

void WithClient(const std::function<void(IUIAutomation*, UiQueue&)>& client)
{
    UiQueue queue;
    std::atomic<bool> finished{false};
    std::thread thread([&] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            {
                wil::com_ptr<IUIAutomation> uia;
                if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&uia))))
                {
                    client(uia.get(), queue);
                    uia->RemoveAllEventHandlers();
                }
            }
            CoUninitialize();
        }
        finished = true;
    });
    Pump(60000, [&] { return finished.load(); }, &queue);
    thread.join();
}

bool WaitFor(const std::function<bool()>& condition, DWORD ms = 5000)
{
    const ULONGLONG until = GetTickCount64() + ms;
    while (!condition() && GetTickCount64() < until)
    {
        Sleep(20);
    }
    return condition();
}

std::wstring NameOf(IUIAutomationElement* element)
{
    wil::unique_bstr name;
    if (!element || FAILED(element->get_CurrentName(&name)) || !name)
    {
        return L"";
    }
    return name.get();
}

// Records automation events by ID with the sender's name.
class EventRecorder final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUIAutomationEventHandler,
                          IUIAutomationFocusChangedEventHandler, IUIAutomationStructureChangedEventHandler>
{
  public:
    IFACEMETHODIMP HandleAutomationEvent(IUIAutomationElement* sender, EVENTID eventId) override
    {
        Add(eventId, NameOf(sender));
        return S_OK;
    }
    IFACEMETHODIMP HandleFocusChangedEvent(IUIAutomationElement* sender) override
    {
        Add(UIA_AutomationFocusChangedEventId, NameOf(sender));
        return S_OK;
    }
    IFACEMETHODIMP HandleStructureChangedEvent(IUIAutomationElement* sender, StructureChangeType type,
                                               SAFEARRAY*) override
    {
        if (type == StructureChangeType_ChildrenInvalidated)
        {
            Add(UIA_StructureChangedEventId, NameOf(sender));
        }
        return S_OK;
    }

    [[nodiscard]] std::vector<std::wstring> Names(EVENTID eventId)
    {
        std::scoped_lock lock(m_mutex);
        std::vector<std::wstring> names;
        for (const auto& [id, name] : m_events)
        {
            if (id == eventId)
            {
                names.push_back(name);
            }
        }
        return names;
    }
    [[nodiscard]] std::size_t Count(EVENTID eventId)
    {
        return Names(eventId).size();
    }
    void Clear()
    {
        std::scoped_lock lock(m_mutex);
        m_events.clear();
    }

  private:
    void Add(EVENTID eventId, std::wstring name)
    {
        std::scoped_lock lock(m_mutex);
        m_events.emplace_back(eventId, std::move(name));
    }

    std::mutex m_mutex;
    std::vector<std::pair<EVENTID, std::wstring>> m_events;
};

wil::com_ptr<IUIAutomationElement> FindByType(IUIAutomation* uia, IUIAutomationElement* under,
                                              CONTROLTYPEID type)
{
    wil::unique_variant value;
    value.vt = VT_I4;
    value.lVal = type;
    wil::com_ptr<IUIAutomationCondition> condition;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition);
    wil::com_ptr<IUIAutomationElement> found;
    under->FindFirst(TreeScope_Descendants, condition.get(), &found);
    return found;
}

class UiaEventsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-uiaevents-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root);
        for (int i = 0; i < 30; ++i)
        {
            std::ofstream(m_root / (L"item" + std::to_wstring(10 + i) + L".txt")) << "x";
        }
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"UIA events test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = m_root.wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
        Pump(15000,
             [this] { return !m_window->NavigationPending() && m_window->Files().Items().size() == 30; });
        Pump(300);
        SetFocus(m_window->Hwnd()); // focus events are raised while the window has the focus
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

    // Registers the recorder for everything the window raises.
    void Listen(IUIAutomation* uia, EventRecorder* recorder, IUIAutomationElement** gridOut,
                IUIAutomationElement** statusOut)
    {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        wil::com_ptr<IUIAutomationElement> grid = FindByType(uia, window.get(), UIA_DataGridControlTypeId);
        wil::com_ptr<IUIAutomationElement> status = FindByType(uia, window.get(), UIA_StatusBarControlTypeId);
        ASSERT_TRUE(grid && status);
        ASSERT_HRESULT_SUCCEEDED(uia->AddFocusChangedEventHandler(nullptr, recorder));
        for (const EVENTID id :
             {UIA_SelectionItem_ElementSelectedEventId, UIA_SelectionItem_ElementAddedToSelectionEventId,
              UIA_SelectionItem_ElementRemovedFromSelectionEventId, UIA_Selection_InvalidatedEventId})
        {
            ASSERT_HRESULT_SUCCEEDED(
                uia->AddAutomationEventHandler(id, grid.get(), TreeScope_Subtree, nullptr, recorder));
        }
        ASSERT_HRESULT_SUCCEEDED(
            uia->AddStructureChangedEventHandler(grid.get(), TreeScope_Element, nullptr, recorder));
        ASSERT_HRESULT_SUCCEEDED(uia->AddAutomationEventHandler(UIA_LiveRegionChangedEventId, status.get(),
                                                                TreeScope_Element, nullptr, recorder));
        *gridOut = grid.detach();
        *statusOut = status.detach();
    }

    bool m_uninitialize = false;
    fs::path m_root;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(UiaEventsTest, ArrowKeysRaiseFocusAndElementSelected)
{
    auto recorder = Make<EventRecorder>();
    std::wstring expected;
    WithClient([&](IUIAutomation* uia, UiQueue& ui) {
        wil::com_ptr<IUIAutomationElement> grid;
        wil::com_ptr<IUIAutomationElement> status;
        Listen(uia, recorder.Get(), &grid, &status);
        ui.Run([&] {
            SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_HOME, 0);
            SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
            SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
            expected = m_window->Files().Items()[2].info.name;
        });
        WaitFor([&] {
            const auto focus = recorder->Names(UIA_AutomationFocusChangedEventId);
            const auto selected = recorder->Names(UIA_SelectionItem_ElementSelectedEventId);
            return !focus.empty() && focus.back() == expected && !selected.empty() &&
                   selected.back() == expected;
        });
    });
    const auto focus = recorder->Names(UIA_AutomationFocusChangedEventId);
    const auto selected = recorder->Names(UIA_SelectionItem_ElementSelectedEventId);
    ASSERT_FALSE(focus.empty());
    EXPECT_EQ(focus.back(), expected) << "focus follows the focused row";
    ASSERT_FALSE(selected.empty());
    EXPECT_EQ(selected.back(), expected) << "a single selection raises ElementSelected";
}

TEST_F(UiaEventsTest, AddingAndRemovingRaiseTheirEventsAndManyChangesInvalidate)
{
    auto recorder = Make<EventRecorder>();
    std::wstring third;
    std::wstring fifth;
    WithClient([&](IUIAutomation* uia, UiQueue& ui) {
        wil::com_ptr<IUIAutomationElement> grid;
        wil::com_ptr<IUIAutomationElement> status;
        Listen(uia, recorder.Get(), &grid, &status);
        ui.Run([&] {
            m_window->Files().SelectOnly(2);
            third = m_window->Files().Items()[2].info.name;
            fifth = m_window->Files().Items()[4].info.name;
        });
        WaitFor([&] { return recorder->Count(UIA_SelectionItem_ElementSelectedEventId) > 0; });
        ui.Run([&] { m_window->Files().SetSelected(4, true); });
        WaitFor([&] { return recorder->Count(UIA_SelectionItem_ElementAddedToSelectionEventId) > 0; });
        ui.Run([&] { m_window->Files().SetSelected(2, false); });
        WaitFor([&] { return recorder->Count(UIA_SelectionItem_ElementRemovedFromSelectionEventId) > 0; });
        // Select all 30: more than 20 changes, one Invalidated instead of 29 events.
        ui.Run([&] { m_window->Files().OnKeyDown('A', true, false); });
        WaitFor([&] { return recorder->Count(UIA_Selection_InvalidatedEventId) > 0; });
    });
    const auto selected = recorder->Names(UIA_SelectionItem_ElementSelectedEventId);
    ASSERT_FALSE(selected.empty()) << "the first change after the client connected is announced too";
    EXPECT_EQ(selected.front(), third);
    const auto added = recorder->Names(UIA_SelectionItem_ElementAddedToSelectionEventId);
    const auto removed = recorder->Names(UIA_SelectionItem_ElementRemovedFromSelectionEventId);
    ASSERT_FALSE(added.empty());
    EXPECT_EQ(added.front(), fifth);
    ASSERT_FALSE(removed.empty());
    EXPECT_EQ(removed.front(), third);
    EXPECT_GE(recorder->Count(UIA_Selection_InvalidatedEventId), 1u);
    EXPECT_LE(added.size(), 2u) << "select-all raised no per-row AddedToSelection flood";
}

TEST_F(UiaEventsTest, ANewListingRaisesAThrottledStructureChange)
{
    auto recorder = Make<EventRecorder>();
    std::size_t afterRefresh = 0;
    WithClient([&](IUIAutomation* uia, UiQueue& ui) {
        wil::com_ptr<IUIAutomationElement> grid;
        wil::com_ptr<IUIAutomationElement> status;
        Listen(uia, recorder.Get(), &grid, &status);
        ui.Run([&] { SendMessageW(m_window->Hwnd(), WM_COMMAND, IDM_REFRESH, 0); });
        WaitFor([&] { return recorder->Count(UIA_StructureChangedEventId) > 0; });
        Sleep(600); // the trailing event, if any
        afterRefresh = recorder->Count(UIA_StructureChangedEventId);
    });
    EXPECT_GE(afterRefresh, 1u) << "the grid's children were invalidated";
    // One listing is a few changes (cleared, batches); throttling keeps it to at most two
    // events (the first and one trailing), each possibly delivered twice in-process.
    EXPECT_LE(afterRefresh, 4u);
    const auto names = recorder->Names(UIA_StructureChangedEventId);
    EXPECT_EQ(names.front(), L"Items");
}

TEST_F(UiaEventsTest, StatusMessagesAreAnnouncedAsALiveRegion)
{
    auto recorder = Make<EventRecorder>();
    std::wstring announced;
    WithClient([&](IUIAutomation* uia, UiQueue& ui) {
        wil::com_ptr<IUIAutomationElement> grid;
        wil::com_ptr<IUIAutomationElement> status;
        Listen(uia, recorder.Get(), &grid, &status);
        ui.Run([&] { m_window->Status().SetTransientMessage(L"3 items copied", 5000); });
        WaitFor([&] { return recorder->Count(UIA_LiveRegionChangedEventId) > 0; });
        announced = NameOf(status.get()); // what a screen reader then reads
    });
    EXPECT_GE(recorder->Count(UIA_LiveRegionChangedEventId), 1u);
    EXPECT_EQ(announced, L"3 items copied");
}

} // namespace
