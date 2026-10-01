// The picker button's UI Automation provider (T075; research R-09; UI contract §5),
// through the real UIA client against the main window: a Button "Appearance and color"
// first among the root's children, its bounds equal to the picker's screen rectangle,
// Invoke and Expand/Collapse opening and closing the popup, the ExpandCollapseState
// property-changed event, and disconnection when the window closes.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): UI Automation
// keeps process-wide state keyed by window handles, and when many windows are created and
// destroyed in one process a reused handle can carry an earlier test's state into a later
// one (seen with --gtest_shuffle in T077). The app has one main window per process.

#include <te/app/MainWindow.h>
#include <te/appearance/AppearanceIds.h>

#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

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

// Runs `client` on an MTA thread with an IUIAutomation while this (the provider's) thread
// pumps.
void WithClient(const std::function<void(IUIAutomation*)>& client)
{
    std::atomic<bool> done{false};
    std::thread thread([&] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            {
                wil::com_ptr<IUIAutomation> uia;
                if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&uia))))
                {
                    client(uia.get());
                }
            }
            CoUninitialize();
        }
        done = true;
    });
    Pump(60000, [&] { return done.load(); });
    thread.join();
}

class ExpandCollapseHandler final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUIAutomationPropertyChangedEventHandler>
{
  public:
    IFACEMETHODIMP HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT newValue) override
    {
        if (id == UIA_ExpandCollapseExpandCollapseStatePropertyId && newValue.vt == VT_I4)
        {
            std::scoped_lock lock(m_mutex);
            m_states.push_back(static_cast<ExpandCollapseState>(newValue.lVal));
        }
        return S_OK;
    }
    [[nodiscard]] std::vector<ExpandCollapseState> States()
    {
        std::scoped_lock lock(m_mutex);
        return m_states;
    }

  private:
    std::mutex m_mutex;
    std::vector<ExpandCollapseState> m_states;
};

// Creates the window's UIA root the way UIA does, through a client request. (Sending
// WM_GETOBJECT directly from the test made UIA stop asking later windows of the same
// process for their providers.)
void RequestRoot(HWND hwnd)
{
    WithClient([hwnd](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> element;
        uia->ElementFromHandle(hwnd, &element);
    });
}

wil::com_ptr<IUIAutomationElement> FindByName(IUIAutomation* uia, IUIAutomationElement* under,
                                              const wchar_t* name)
{
    wil::unique_variant value;
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(name);
    wil::com_ptr<IUIAutomationCondition> condition;
    wil::com_ptr<IUIAutomationElement> found;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_NamePropertyId, value, &condition)))
    {
        under->FindFirst(TreeScope_Descendants, condition.get(), &found);
    }
    return found;
}

// Waits on the client thread (the provider thread pumps meanwhile).
bool WaitFor(const std::function<bool()>& condition, DWORD ms = 5000)
{
    const ULONGLONG until = GetTickCount64() + ms;
    while (!condition() && GetTickCount64() < until)
    {
        Sleep(20);
    }
    return condition();
}

class UiaPickerButtonTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"UIA picker test";
        options.quitOnDestroy = false;
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
        Pump(200);
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

    [[nodiscard]] bool PickerOpen() const
    {
        return m_window->Picker() && m_window->Picker()->IsOpen();
    }

    bool m_uninitialize = false;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(UiaPickerButtonTest, IsAButtonNamedAppearanceAndColorOverThePicker)
{
    std::wstring name;
    std::wstring automationId;
    std::wstring accelerator;
    CONTROLTYPEID type = 0;
    RECT bounds{};
    bool invoke = false;
    bool expandCollapse = false;
    ExpandCollapseState state = ExpandCollapseState_LeafNode;
    std::wstring firstCustomChild;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        const auto button = FindByName(uia, window.get(), L"Appearance and color");
        ASSERT_TRUE(button);
        wil::unique_bstr text;
        button->get_CurrentName(&text);
        name = text.get();
        button->get_CurrentAutomationId(&text);
        automationId = text.get();
        button->get_CurrentAcceleratorKey(&text);
        accelerator = text.get();
        button->get_CurrentControlType(&type);
        button->get_CurrentBoundingRectangle(&bounds);
        wil::com_ptr<IUIAutomationInvokePattern> invokePattern;
        invoke = SUCCEEDED(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invokePattern))) &&
                 invokePattern;
        wil::com_ptr<IUIAutomationExpandCollapsePattern> expand;
        expandCollapse =
            SUCCEEDED(button->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) &&
            expand;
        if (expand)
        {
            expand->get_CurrentExpandCollapseState(&state);
        }

        // The first custom child of the window, after the native title bar.
        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_ControlViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> child;
        walker->GetFirstChildElement(window.get(), &child);
        while (child)
        {
            CONTROLTYPEID childType = 0;
            child->get_CurrentControlType(&childType);
            if (childType != UIA_TitleBarControlTypeId)
            {
                child->get_CurrentName(&text);
                firstCustomChild = text ? text.get() : L"";
                break;
            }
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(child.get(), &next);
            child = std::move(next);
        }
    });
    EXPECT_EQ(name, L"Appearance and color");
    EXPECT_EQ(automationId, L"AppearanceButton");
    EXPECT_EQ(accelerator, L"Alt+Shift+C");
    EXPECT_EQ(type, UIA_ButtonControlTypeId);
    EXPECT_TRUE(invoke);
    EXPECT_TRUE(expandCollapse);
    EXPECT_EQ(state, ExpandCollapseState_Collapsed);
    EXPECT_EQ(firstCustomChild, L"Appearance and color") << "UI §5 order";

    // The picker's screen rectangle: inside the window, 40 DIP wide, left of the caption
    // buttons.
    RECT window{};
    GetWindowRect(m_window->Hwnd(), &window);
    EXPECT_GE(bounds.left, window.left);
    EXPECT_LE(bounds.right, window.right);
    EXPECT_GE(bounds.top, window.top);
    EXPECT_GT(bounds.bottom, bounds.top);
    POINT captionLeft{static_cast<LONG>(m_window->Layout().captionButtons.left * m_window->Dpi() / 96.0f), 0};
    ClientToScreen(m_window->Hwnd(), &captionLeft);
    EXPECT_LE(bounds.right, captionLeft.x);
    const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
    EXPECT_NEAR(bounds.right - bounds.left, 40.0f * scale, 2.0f) << "the 40-DIP picker (UI §1)";
}

// Note: when the test process is not the foreground app, the popup cannot keep the
// activation and its "close when deactivated" rule (R-10) may dismiss it within a moment.
// So each check reads the state right after the action, and compares the reported state
// with the popup's real state rather than assuming it stayed open.
TEST_F(UiaPickerButtonTest, InvokeAndExpandCollapseOpenAndCloseThePopup)
{
    ExpandCollapseState afterExpand = ExpandCollapseState_LeafNode;
    bool openAfterExpand = false;
    ExpandCollapseState afterCollapse = ExpandCollapseState_LeafNode;
    ExpandCollapseState afterInvoke = ExpandCollapseState_LeafNode;
    bool openAfterInvoke = false;
    int mismatches = 0;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        const auto button = FindByName(uia, window.get(), L"Appearance and color");
        ASSERT_TRUE(button);
        wil::com_ptr<IUIAutomationExpandCollapsePattern> expand;
        ASSERT_HRESULT_SUCCEEDED(
            button->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand)));
        wil::com_ptr<IUIAutomationInvokePattern> invoke;
        ASSERT_HRESULT_SUCCEEDED(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)));
        const auto state = [&] {
            ExpandCollapseState current = ExpandCollapseState_LeafNode;
            expand->get_CurrentExpandCollapseState(&current);
            return current;
        };

        ASSERT_HRESULT_SUCCEEDED(expand->Expand());
        openAfterExpand = WaitFor([&] { return PickerOpen(); });
        afterExpand = state();

        ASSERT_HRESULT_SUCCEEDED(expand->Collapse());
        WaitFor([&] { return !PickerOpen(); });
        afterCollapse = state();
        ASSERT_HRESULT_SUCCEEDED(expand->Collapse()); // already closed: stays closed

        ASSERT_HRESULT_SUCCEEDED(invoke->Invoke());
        openAfterInvoke = WaitFor([&] { return PickerOpen(); });
        afterInvoke = state();

        // Whatever happens meanwhile, the reported state follows the popup.
        for (int i = 0; i < 20; ++i)
        {
            const bool open = PickerOpen();
            const ExpandCollapseState reported = state();
            if (reported != (open ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed) &&
                PickerOpen() == open)
            {
                ++mismatches;
            }
            Sleep(25);
        }
        ASSERT_HRESULT_SUCCEEDED(expand->Collapse());
        WaitFor([&] { return !PickerOpen(); });
    });
    EXPECT_TRUE(openAfterExpand) << "Expand opened the popup";
    EXPECT_NE(afterExpand, ExpandCollapseState_LeafNode) << "a real state was reported";
    EXPECT_EQ(afterCollapse, ExpandCollapseState_Collapsed);
    EXPECT_TRUE(openAfterInvoke) << "Invoke opened the popup";
    EXPECT_EQ(mismatches, 0);
    EXPECT_FALSE(PickerOpen());
}

TEST_F(UiaPickerButtonTest, OpeningAndClosingRaiseTheStateChange)
{
    auto handler = Microsoft::WRL::Make<ExpandCollapseHandler>();
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        const auto button = FindByName(uia, window.get(), L"Appearance and color");
        ASSERT_TRUE(button);
        PROPERTYID properties[] = {UIA_ExpandCollapseExpandCollapseStatePropertyId};
        ASSERT_HRESULT_SUCCEEDED(uia->AddPropertyChangedEventHandlerNativeArray(
            button.get(), TreeScope_Element, nullptr, handler.Get(), properties, 1));

        // Open the way the user does (Alt+Shift+C), not through UIA, then close.
        PostMessageW(m_window->Hwnd(), WM_COMMAND, IDM_OPEN_APPEARANCE, 0);
        WaitFor([&] { return !handler->States().empty(); });
        if (PickerOpen())
        {
            PostMessageW(m_window->Hwnd(), WM_COMMAND, IDM_OPEN_APPEARANCE, 0);
        }
        WaitFor([&] { return !PickerOpen(); });
        WaitFor([&] { return handler->States().size() >= 2; }, 2000);

        uia->RemovePropertyChangedEventHandler(button.get(), handler.Get());
    });
    // An in-process client receives each event twice (the provider raises it once, as a
    // probe showed during T075), so consecutive repeats are collapsed first.
    std::vector<ExpandCollapseState> states;
    for (const ExpandCollapseState state : handler->States())
    {
        if (states.empty() || states.back() != state)
        {
            states.push_back(state);
        }
    }
    EXPECT_EQ(states,
              (std::vector<ExpandCollapseState>{ExpandCollapseState_Expanded, ExpandCollapseState_Collapsed}))
        << "opening raised Expanded, closing Collapsed";
}

TEST_F(UiaPickerButtonTest, TheButtonIsUnavailableAfterTheWindowCloses)
{
    RequestRoot(m_window->Hwnd());
    ASSERT_NE(m_window->Automation(), nullptr);
    wil::com_ptr<IRawElementProviderFragment> first;
    ASSERT_HRESULT_SUCCEEDED(m_window->Automation()->Navigate(NavigateDirection_FirstChild, &first));
    ASSERT_TRUE(first);
    wil::com_ptr<IExpandCollapseProvider> expand = first.query<IExpandCollapseProvider>();

    DestroyWindow(m_window->Hwnd());
    m_window.reset();
    ExpandCollapseState state{};
    EXPECT_EQ(expand->get_ExpandCollapseState(&state), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    EXPECT_EQ(expand->Expand(), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
}

} // namespace
