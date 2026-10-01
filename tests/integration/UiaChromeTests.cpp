// UI Automation for the window chrome (T078; research R-09; UI contract §5), through the
// real UIA client against the main window over a temp folder: the order of the root's
// children, the ToolBar "Navigation" (Back / Forward / Up / Refresh with IsEnabled and
// Invoke; the Edit "Address" with Value, and the native EDIT while editing), the Tree
// "Navigation pane" (TreeItems, selection, invoke; a List until T090), the StatusBar (name
// and live setting),
// hit testing and disconnection.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): UI Automation
// keeps process-wide state keyed by window handles (see UiaFileListTests.cpp).

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>

#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>
#include <shlobj.h>

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

std::wstring NameOf(IUIAutomationElement* element)
{
    wil::unique_bstr name;
    if (!element || FAILED(element->get_CurrentName(&name)) || !name)
    {
        return L"<none>";
    }
    return name.get();
}

CONTROLTYPEID TypeOf(IUIAutomationElement* element)
{
    CONTROLTYPEID type = 0;
    if (element)
    {
        element->get_CurrentControlType(&type);
    }
    return type;
}

wil::com_ptr<IUIAutomationElement> Find(IUIAutomation* uia, IUIAutomationElement* under, CONTROLTYPEID type,
                                        const wchar_t* name = nullptr)
{
    wil::unique_variant typeValue;
    typeValue.vt = VT_I4;
    typeValue.lVal = type;
    wil::com_ptr<IUIAutomationCondition> condition;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, typeValue, &condition);
    if (name)
    {
        wil::unique_variant nameValue;
        nameValue.vt = VT_BSTR;
        nameValue.bstrVal = SysAllocString(name);
        wil::com_ptr<IUIAutomationCondition> byName;
        uia->CreatePropertyCondition(UIA_NamePropertyId, nameValue, &byName);
        wil::com_ptr<IUIAutomationCondition> both;
        uia->CreateAndCondition(condition.get(), byName.get(), &both);
        condition = both;
    }
    wil::com_ptr<IUIAutomationElement> found;
    under->FindFirst(TreeScope_Descendants, condition.get(), &found);
    return found;
}

class UiaChromeTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-uiachrome-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root / L"inner");
        for (const wchar_t* file : {L"a.txt", L"b.txt"})
        {
            std::ofstream(m_root / file) << "x";
        }
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"UIA chrome test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = m_root.wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNOACTIVATE));
        WaitIdle();
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

    void WaitIdle()
    {
        Pump(15000, [this] { return !m_window->NavigationPending(); });
        Pump(200);
    }

    [[nodiscard]] std::wstring Current() const
    {
        return m_window->CurrentLocation().ParsingPath().value_or(m_window->CurrentLocation().DisplayName());
    }

    static bool SamePath(const std::wstring& a, const fs::path& b)
    {
        return _wcsicmp(a.c_str(), b.wstring().c_str()) == 0;
    }

    bool m_uninitialize = false;
    fs::path m_root;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(UiaChromeTest, TheRootsChildrenFollowUiSection5)
{
    std::vector<std::pair<CONTROLTYPEID, std::wstring>> children;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> child;
        walker->GetFirstChildElement(window.get(), &child);
        while (child)
        {
            const CONTROLTYPEID type = TypeOf(child.get());
            if (type != UIA_TitleBarControlTypeId)
            {
                children.emplace_back(type, type == UIA_StatusBarControlTypeId ? L"" : NameOf(child.get()));
            }
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(child.get(), &next);
            child = std::move(next);
        }
    });
    const std::vector<std::pair<CONTROLTYPEID, std::wstring>> expected{
        {UIA_ButtonControlTypeId, L"Appearance and color"},
        {UIA_ToolBarControlTypeId, L"Navigation"},
        {UIA_TreeControlTypeId, L"Navigation pane"},
        {UIA_DataGridControlTypeId, L"Items"},
        {UIA_StatusBarControlTypeId, L""}};
    EXPECT_EQ(children, expected);
}

TEST_F(UiaChromeTest, ToolbarButtonsMirrorTheirStateAndInvoke)
{
    std::vector<std::wstring> names;
    BOOL backEnabledAtStart = TRUE;
    BOOL upEnabled = FALSE;
    std::wstring backKeys;
    HRESULT forwardWhileDisabled = S_OK;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        const auto toolbar = Find(uia, window.get(), UIA_ToolBarControlTypeId, L"Navigation");
        ASSERT_TRUE(toolbar);
        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> child;
        walker->GetFirstChildElement(toolbar.get(), &child);
        while (child)
        {
            names.push_back(NameOf(child.get()));
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(child.get(), &next);
            child = std::move(next);
        }
        const auto back = Find(uia, toolbar.get(), UIA_ButtonControlTypeId, L"Back");
        back->get_CurrentIsEnabled(&backEnabledAtStart);
        wil::unique_bstr keys;
        back->get_CurrentAcceleratorKey(&keys);
        backKeys = keys ? keys.get() : L"";
        const auto up = Find(uia, toolbar.get(), UIA_ButtonControlTypeId, L"Up to the parent folder");
        up->get_CurrentIsEnabled(&upEnabled);
        const auto forward = Find(uia, toolbar.get(), UIA_ButtonControlTypeId, L"Forward");
        wil::com_ptr<IUIAutomationInvokePattern> forwardInvoke;
        forward->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&forwardInvoke));
        forwardWhileDisabled = forwardInvoke->Invoke();
    });
    EXPECT_EQ(names, (std::vector<std::wstring>{L"Back", L"Forward", L"Up to the parent folder", L"Refresh",
                                                L"Address"}));
    EXPECT_FALSE(backEnabledAtStart) << "nothing to go back to yet";
    EXPECT_TRUE(upEnabled);
    EXPECT_EQ(backKeys, L"Alt+Left Arrow");
    EXPECT_EQ(forwardWhileDisabled, static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED));

    // Up, then Back, through Invoke; each is posted and runs from the message loop.
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        const auto up = Find(uia, window.get(), UIA_ButtonControlTypeId, L"Up to the parent folder");
        wil::com_ptr<IUIAutomationInvokePattern> invoke;
        up->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
        ASSERT_HRESULT_SUCCEEDED(invoke->Invoke());
    });
    WaitIdle();
    EXPECT_TRUE(SamePath(Current(), m_root.parent_path())) << "Up";
    BOOL backEnabled = FALSE;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        const auto back = Find(uia, window.get(), UIA_ButtonControlTypeId, L"Back");
        back->get_CurrentIsEnabled(&backEnabled);
        wil::com_ptr<IUIAutomationInvokePattern> invoke;
        back->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
        ASSERT_HRESULT_SUCCEEDED(invoke->Invoke());
    });
    WaitIdle();
    EXPECT_TRUE(backEnabled);
    EXPECT_TRUE(SamePath(Current(), m_root)) << "Back";
}

TEST_F(UiaChromeTest, TheAddressHasAValueAndSetValueNavigates)
{
    std::wstring value;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        const auto address = Find(uia, window.get(), UIA_EditControlTypeId, L"Address");
        ASSERT_TRUE(address);
        wil::com_ptr<IUIAutomationValuePattern> valuePattern;
        ASSERT_HRESULT_SUCCEEDED(
            address->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern)));
        wil::unique_bstr text;
        valuePattern->get_CurrentValue(&text);
        value = text ? text.get() : L"";
        const std::wstring inner = (m_root / L"inner").wstring();
        wil::unique_bstr newValue(SysAllocString(inner.c_str()));
        ASSERT_HRESULT_SUCCEEDED(valuePattern->SetValue(newValue.get()));
    });
    EXPECT_TRUE(SamePath(value, m_root));
    WaitIdle();
    EXPECT_TRUE(SamePath(Current(), m_root / L"inner")) << "SetValue navigated";
}

TEST_F(UiaChromeTest, WhileEditingTheAddressIsTheNativeEdit)
{
    SendMessageW(m_window->Hwnd(), WM_COMMAND, IDM_FOCUS_ADDRESS, 0);
    ASSERT_TRUE(m_window->Address().IsEditing());
    HWND reported = nullptr;
    std::wstring name;
    int addresses = 0;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        wil::unique_variant typeValue;
        typeValue.vt = VT_I4;
        typeValue.lVal = UIA_EditControlTypeId;
        wil::com_ptr<IUIAutomationCondition> condition;
        uia->CreatePropertyCondition(UIA_ControlTypePropertyId, typeValue, &condition);
        wil::com_ptr<IUIAutomationElementArray> edits;
        window->FindAll(TreeScope_Descendants, condition.get(), &edits);
        edits->get_Length(&addresses);
        const auto address = Find(uia, window.get(), UIA_EditControlTypeId, L"Address");
        ASSERT_TRUE(address);
        name = NameOf(address.get());
        UIA_HWND handle{};
        address->get_CurrentNativeWindowHandle(&handle);
        reported = static_cast<HWND>(handle);
    });
    EXPECT_EQ(addresses, 1) << "one Address element, not ours plus the EDIT";
    EXPECT_EQ(name, L"Address");
    EXPECT_EQ(reported, m_window->Address().Edit()) << "the native EDIT is the element's host";
    m_window->Address().CancelEdit();
}

TEST_F(UiaChromeTest, TheNavigationPaneListsItsPlaces)
{
    std::vector<std::wstring> names;
    HRESULT addToSelection = S_OK;
    int selected = -1;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        const auto pane = Find(uia, window.get(), UIA_TreeControlTypeId, L"Navigation pane");
        ASSERT_TRUE(pane);
        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> item;
        walker->GetFirstChildElement(pane.get(), &item);
        while (item)
        {
            EXPECT_EQ(TypeOf(item.get()), UIA_TreeItemControlTypeId);
            names.push_back(NameOf(item.get()));
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(item.get(), &next);
            item = std::move(next);
        }
        wil::com_ptr<IUIAutomationSelectionPattern> selection;
        pane->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection));
        wil::com_ptr<IUIAutomationElementArray> current;
        selection->GetCurrentSelection(&current);
        current->get_Length(&selected);

        walker->GetFirstChildElement(pane.get(), &item);
        wil::com_ptr<IUIAutomationSelectionItemPattern> selectionItem;
        item->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectionItem));
        addToSelection = selectionItem->AddToSelection();
    });
    const auto& entries = m_window->Places().Entries();
    ASSERT_EQ(names.size(), entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        EXPECT_EQ(names[i], entries[i].location.DisplayName());
    }
    EXPECT_EQ(selected, 0) << "the temp folder is not one of the places";
    EXPECT_EQ(addToSelection, static_cast<HRESULT>(UIA_E_INVALIDOPERATION)) << "one place at a time";
}

TEST_F(UiaChromeTest, InvokingAPlaceNavigatesAndSelectsIt)
{
    // The pane's second place (Desktop in the usual order): any place but the drives.
    const auto& entries = m_window->Places().Entries();
    ASSERT_GE(entries.size(), 2u);
    const std::optional<std::size_t> desktop = 1;
    const std::wstring name = entries[*desktop].location.DisplayName();
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        const auto item = Find(uia, window.get(), UIA_TreeItemControlTypeId, name.c_str());
        ASSERT_TRUE(item);
        wil::com_ptr<IUIAutomationInvokePattern> invoke;
        ASSERT_HRESULT_SUCCEEDED(item->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)));
        ASSERT_HRESULT_SUCCEEDED(invoke->Invoke());
    });
    WaitIdle();
    EXPECT_EQ(m_window->Places().CurrentIndex(), desktop);
    BOOL selected = FALSE;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        const auto item = Find(uia, window.get(), UIA_TreeItemControlTypeId, name.c_str());
        wil::com_ptr<IUIAutomationSelectionItemPattern> selectionItem;
        item->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectionItem));
        selectionItem->get_CurrentIsSelected(&selected);
    });
    EXPECT_TRUE(selected);
}

TEST_F(UiaChromeTest, TheStatusBarIsAPoliteLiveRegionNamedByItsText)
{
    std::wstring name;
    int live = -1;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
        const auto status = Find(uia, window.get(), UIA_StatusBarControlTypeId);
        ASSERT_TRUE(status);
        name = NameOf(status.get());
        wil::unique_variant value;
        status->GetCurrentPropertyValue(UIA_LiveSettingPropertyId, &value);
        live = value.vt == VT_I4 ? value.lVal : -1;
    });
    EXPECT_EQ(name, m_window->Status().LeftText());
    EXPECT_EQ(name, L"3 items");
    EXPECT_EQ(live, Polite);
}

TEST_F(UiaChromeTest, HitTestingFindsButtonsAndPlaces)
{
    const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
    const auto toScreen = [&](float x, float y) {
        POINT point{static_cast<LONG>(x * scale), static_cast<LONG>(y * scale)};
        ClientToScreen(m_window->Hwnd(), &point);
        return point;
    };
    const D2D1_RECT_F refresh = m_window->Buttons().ButtonRect(te::Toolbar::Button::Refresh);
    const POINT atRefresh = toScreen((refresh.left + refresh.right) / 2, (refresh.top + refresh.bottom) / 2);
    const D2D1_RECT_F firstPlace = m_window->Places().EntryRect(0);
    const POINT atPlace = toScreen(firstPlace.left + 30, (firstPlace.top + firstPlace.bottom) / 2);
    std::wstring refreshName;
    std::wstring placeName;
    // ElementFromPoint hit-tests the real screen: keep the window above any other while it
    // does, or a window that happens to cover the point answers instead (seen once in a
    // full run, T086).
    SetWindowPos(m_window->Hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> element;
        uia->ElementFromPoint(atRefresh, &element);
        refreshName = NameOf(element.get());
        element.reset();
        uia->ElementFromPoint(atPlace, &element);
        placeName = NameOf(element.get());
    });
    SetWindowPos(m_window->Hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EXPECT_EQ(refreshName, L"Refresh");
    EXPECT_EQ(placeName, m_window->Places().Entries()[0].location.DisplayName());
}

TEST_F(UiaChromeTest, ChromeProvidersAreUnavailableAfterTheWindowCloses)
{
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        uia->ElementFromHandle(m_window->Hwnd(), &window);
    });
    ASSERT_NE(m_window->Automation(), nullptr);
    wil::com_ptr<IRawElementProviderFragment> last;
    ASSERT_HRESULT_SUCCEEDED(m_window->Automation()->Navigate(NavigateDirection_LastChild, &last));
    wil::com_ptr<IRawElementProviderSimple> status = last.query<IRawElementProviderSimple>();
    wil::unique_variant name;
    ASSERT_HRESULT_SUCCEEDED(status->GetPropertyValue(UIA_NamePropertyId, &name));

    DestroyWindow(m_window->Hwnd());
    m_window.reset();
    name.reset();
    EXPECT_EQ(status->GetPropertyValue(UIA_NamePropertyId, &name),
              static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
}

} // namespace
