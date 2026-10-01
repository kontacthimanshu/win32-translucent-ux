// UiaRoot (T074; research R-09; UI contract §5). A test window hosts the root with fake
// child fragments, and the real UI Automation client (IUIAutomation, on a helper MTA
// thread while this thread pumps) checks the tree: children in order, parent and sibling
// navigation, hit testing with delegation to a child, focus delegation, and
// disconnection. The last tests check the main window's WM_GETOBJECT and WM_DESTROY.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): UI Automation
// keeps process-wide state keyed by window handles, and when many windows are created and
// destroyed in one process a reused handle can carry an earlier test's state into a later
// one (seen with --gtest_shuffle in T077). The app has one main window per process.

#include <te/a11y/UiaRoot.h>
#include <te/app/MainWindow.h>

#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

constexpr HRESULT kNotAvailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);

// A child fragment with a name and a screen rectangle. A fragment with a `deep` child
// also answers the app extension, so hit tests inside it resolve to that child.
class FakeFragment final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                                               IRawElementProviderFragment, te::IUiaFragmentExtension>
{
  public:
    FakeFragment(std::wstring name, int id, RECT screen, te::UiaRoot* root,
                 IRawElementProviderFragment* parent)
        : m_name(std::move(name)), m_id(id), m_rect(screen), m_root(root), m_parent(parent)
    {
    }

    ComPtr<FakeFragment> deep; // an optional child

    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override
    {
        *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** pattern) override
    {
        *pattern = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* result) override
    {
        result->vt = VT_EMPTY;
        if (id == UIA_NamePropertyId)
        {
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(m_name.c_str());
        }
        else if (id == UIA_ControlTypePropertyId)
        {
            result->vt = VT_I4;
            result->lVal = UIA_ButtonControlTypeId;
        }
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override
    {
        *host = nullptr;
        return S_OK;
    }

    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        switch (direction)
        {
        case NavigateDirection_Parent:
            *result = m_parent;
            break;
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling:
            if (m_parent == m_root)
            {
                return m_root->Sibling(this, direction, result);
            }
            return S_OK;
        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild:
            *result = deep.Get();
            break;
        }
        if (*result)
        {
            (*result)->AddRef();
        }
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override
    {
        const int ids[] = {UiaAppendRuntimeId, m_id};
        *runtimeId = SafeArrayCreateVector(VT_I4, 0, 2);
        for (LONG i = 0; i < 2; ++i)
        {
            SafeArrayPutElement(*runtimeId, &i, const_cast<int*>(&ids[i]));
        }
        return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override
    {
        *rect = UiaRect{static_cast<double>(m_rect.left), static_cast<double>(m_rect.top),
                        static_cast<double>(m_rect.right - m_rect.left),
                        static_cast<double>(m_rect.bottom - m_rect.top)};
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override
    {
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override
    {
        return S_OK;
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override
    {
        *root = m_root;
        m_root->AddRef();
        return S_OK;
    }

    // te::IUiaFragmentExtension
    IFACEMETHODIMP HitTest(double x, double y, IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        if (deep && x >= deep->m_rect.left && x < deep->m_rect.right && y >= deep->m_rect.top &&
            y < deep->m_rect.bottom)
        {
            *result = deep.Get();
            deep->AddRef();
        }
        return S_OK;
    }
    IFACEMETHODIMP FocusedDescendant(IRawElementProviderFragment** result) override
    {
        *result = deep ? static_cast<IRawElementProviderFragment*>(deep.Get()) : this;
        (*result)->AddRef();
        return S_OK;
    }

  private:
    std::wstring m_name;
    int m_id;
    RECT m_rect;
    te::UiaRoot* m_root;
    IRawElementProviderFragment* m_parent;
};

te::UiaRoot* g_hostRoot = nullptr; // the root the test window hands out

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_GETOBJECT && static_cast<long>(lParam) == static_cast<long>(UiaRootObjectId) && g_hostRoot)
    {
        return UiaReturnRawElementProvider(hwnd, wParam, lParam, g_hostRoot);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Runs `client` on an MTA thread with an IUIAutomation, pumping this thread meanwhile:
// the providers are called on this (their) thread.
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
                else
                {
                    ADD_FAILURE() << "no UI Automation client";
                }
            }
            CoUninitialize();
        }
        done = true;
    });
    const ULONGLONG until = GetTickCount64() + 30000;
    MSG msg{};
    while (!done && GetTickCount64() < until)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
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

class UiaRootTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = HostProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"TeUiaRootTestHost";
        RegisterClassExW(&wc);
        m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"UIA host", WS_OVERLAPPEDWINDOW, 100, 100, 600, 400,
                                 nullptr, nullptr, wc.hInstance, nullptr);
        ASSERT_NE(m_hwnd, nullptr);
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

        ASSERT_HRESULT_SUCCEEDED(Microsoft::WRL::MakeAndInitialize<te::UiaRoot>(
            &m_root, m_hwnd, [this] { return m_children; }, [this] { return m_focused; }));
        g_hostRoot = m_root.Get();

        RECT client{};
        GetClientRect(m_hwnd, &client);
        MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
        const auto band = [&](int index) {
            return RECT{client.left, client.top + index * 60, client.right, client.top + index * 60 + 50};
        };
        m_first = Make<FakeFragment>(L"First", 1, band(0), m_root.Get(), m_root.Get());
        m_second = Make<FakeFragment>(L"Second", 2, band(1), m_root.Get(), m_root.Get());
        m_third = Make<FakeFragment>(L"Third", 3, band(2), m_root.Get(), m_root.Get());
        RECT inner = band(1);
        inner.right = inner.left + 100;
        m_second->deep = Make<FakeFragment>(L"Deep", 4, inner, m_root.Get(), m_second.Get());
        // A component without a provider yet (null) is skipped.
        m_children = {m_first.Get(), nullptr, m_second.Get(), m_third.Get()};
    }

    void TearDown() override
    {
        g_hostRoot = nullptr;
        if (m_root)
        {
            UiaReturnRawElementProvider(m_hwnd, 0, 0, nullptr);
            m_root->Disconnect();
        }
        m_children.clear();
        m_root.Reset();
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
        }
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    bool m_uninitialize = false;
    HWND m_hwnd = nullptr;
    ComPtr<te::UiaRoot> m_root;
    ComPtr<FakeFragment> m_first;
    ComPtr<FakeFragment> m_second;
    ComPtr<FakeFragment> m_third;
    std::vector<IRawElementProviderFragment*> m_children;
    IRawElementProviderFragment* m_focused = nullptr;
};

TEST_F(UiaRootTest, ClientsSeeTheChildrenInOrderUnderTheWindow)
{
    std::vector<std::wstring> names;
    std::vector<CONTROLTYPEID> types;
    std::wstring windowName;
    std::wstring parentOfSecond;
    std::wstring nextOfFirst;
    std::wstring previousOfThird;
    std::wstring nextOfThird;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_hwnd, &window));
        windowName = NameOf(window.get());

        wil::com_ptr<IUIAutomationTreeWalker> walker;
        ASSERT_HRESULT_SUCCEEDED(uia->get_RawViewWalker(&walker));
        wil::com_ptr<IUIAutomationElement> child;
        walker->GetFirstChildElement(window.get(), &child);
        wil::com_ptr<IUIAutomationElement> first;
        while (child)
        {
            names.push_back(NameOf(child.get()));
            CONTROLTYPEID type = 0;
            child->get_CurrentControlType(&type);
            types.push_back(type);
            if (names.back() == L"First")
            {
                first = child;
            }
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(child.get(), &next);
            child = std::move(next);
        }
        ASSERT_TRUE(first);

        wil::com_ptr<IUIAutomationElement> second;
        walker->GetNextSiblingElement(first.get(), &second);
        nextOfFirst = NameOf(second.get());
        wil::com_ptr<IUIAutomationElement> parent;
        walker->GetParentElement(second.get(), &parent);
        parentOfSecond = NameOf(parent.get());
        wil::com_ptr<IUIAutomationElement> last;
        walker->GetLastChildElement(window.get(), &last);
        wil::com_ptr<IUIAutomationElement> previous;
        walker->GetPreviousSiblingElement(last.get(), &previous);
        previousOfThird = NameOf(previous.get());
        wil::com_ptr<IUIAutomationElement> beyond;
        walker->GetNextSiblingElement(last.get(), &beyond);
        nextOfThird = beyond ? NameOf(beyond.get()) : L"<none>";
    });
    EXPECT_EQ(windowName, L"UIA host") << "the host window supplies the root's name";
    // The system's own title bar of the frame comes first (UI section 5: "TitleBar
    // (native)"), then the root's children in order, with the null one skipped.
    ASSERT_EQ(names.size(), 4u);
    EXPECT_EQ(types[0], UIA_TitleBarControlTypeId);
    EXPECT_EQ(std::vector<std::wstring>(names.begin() + 1, names.end()),
              (std::vector<std::wstring>{L"First", L"Second", L"Third"}));
    EXPECT_EQ(nextOfFirst, L"Second");
    EXPECT_EQ(parentOfSecond, L"UIA host");
    EXPECT_EQ(previousOfThird, L"Second");
    EXPECT_EQ(nextOfThird, L"<none>");
}

TEST_F(UiaRootTest, HitTestingDelegatesToTheChildUnderThePoint)
{
    RECT client{};
    GetClientRect(m_hwnd, &client);
    MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
    std::wstring atThird;
    std::wstring atDeep;
    std::wstring atSecondOutsideDeep;
    // ElementFromPoint hit-tests the real screen: keep the window above any other while it
    // does, or a window that happens to cover the point answers instead (seen once in a
    // full run, T086).
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    WithClient([&](IUIAutomation* uia) {
        const auto at = [&](int x, int y) {
            wil::com_ptr<IUIAutomationElement> element;
            uia->ElementFromPoint(POINT{x, y}, &element);
            return NameOf(element.get());
        };
        atThird = at(client.left + 300, client.top + 2 * 60 + 25);
        atDeep = at(client.left + 50, client.top + 60 + 25);
        atSecondOutsideDeep = at(client.left + 300, client.top + 60 + 25);
    });
    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EXPECT_EQ(atThird, L"Third");
    EXPECT_EQ(atDeep, L"Deep") << "the child's extension resolved a deeper element";
    EXPECT_EQ(atSecondOutsideDeep, L"Second");
}

TEST_F(UiaRootTest, FocusIsDelegatedToTheFocusedChild)
{
    wil::com_ptr<IRawElementProviderFragment> focused;
    ASSERT_HRESULT_SUCCEEDED(m_root->GetFocus(&focused));
    EXPECT_FALSE(focused) << "nothing custom has the focus: the window itself";

    m_focused = m_third.Get();
    ASSERT_HRESULT_SUCCEEDED(m_root->GetFocus(&focused));
    EXPECT_EQ(focused.get(), static_cast<IRawElementProviderFragment*>(m_third.Get()))
        << "Third's extension says it has the focus itself";

    m_focused = m_second.Get();
    ASSERT_HRESULT_SUCCEEDED(m_root->GetFocus(&focused));
    EXPECT_EQ(focused.get(), static_cast<IRawElementProviderFragment*>(m_second->deep.Get()))
        << "Second's extension points at its focused descendant";
}

TEST_F(UiaRootTest, ARootProviderWithoutAWindowIsUnavailable)
{
    ProviderOptions options{};
    ASSERT_HRESULT_SUCCEEDED(m_root->get_ProviderOptions(&options));
    EXPECT_NE(options & ProviderOptions_ServerSideProvider, 0);
    EXPECT_NE(options & ProviderOptions_UseComThreading, 0);

    UiaReturnRawElementProvider(m_hwnd, 0, 0, nullptr);
    m_root->Disconnect();
    EXPECT_FALSE(m_root->IsConnected());
    wil::com_ptr<IRawElementProviderFragment> result;
    EXPECT_EQ(m_root->Navigate(NavigateDirection_FirstChild, &result), kNotAvailable);
    EXPECT_EQ(m_root->ElementProviderFromPoint(0, 0, &result), kNotAvailable);
    EXPECT_EQ(m_root->GetFocus(&result), kNotAvailable);
    wil::com_ptr<IRawElementProviderSimple> host;
    EXPECT_EQ(m_root->get_HostRawElementProvider(&host), kNotAvailable);
    m_root->Disconnect(); // twice is harmless
}

TEST(UiaMainWindow, AnswersWmGetObjectAndDisconnectsOnDestroy)
{
    const bool uninitialize = SUCCEEDED(OleInitialize(nullptr));
    te::MainWindow::Options options;
    options.instance = GetModuleHandleW(nullptr);
    options.title = L"UIA main window";
    options.quitOnDestroy = false;
    auto window = std::make_unique<te::MainWindow>(std::move(options));
    ASSERT_HRESULT_SUCCEEDED(window->Create(SW_SHOWNOACTIVATE));
    EXPECT_EQ(window->Automation(), nullptr) << "created on demand";

    // A real client request creates the root (WM_GETOBJECT with UiaRootObjectId).
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> element;
        uia->ElementFromHandle(window->Hwnd(), &element);
    });
    ASSERT_NE(window->Automation(), nullptr);
    EXPECT_TRUE(window->Automation()->IsConnected());

    std::wstring name;
    std::vector<std::wstring> captionButtons;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> element;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(window->Hwnd(), &element));
        name = NameOf(element.get());
        // The native caption buttons (T027 spike) are still reported with our root present.
        for (const wchar_t* button : {L"Minimize", L"Maximize", L"Close"})
        {
            wil::unique_variant value;
            value.vt = VT_BSTR;
            value.bstrVal = SysAllocString(button);
            wil::com_ptr<IUIAutomationCondition> condition;
            wil::com_ptr<IUIAutomationElement> found;
            if (SUCCEEDED(uia->CreatePropertyCondition(UIA_NamePropertyId, value, &condition)) &&
                SUCCEEDED(element->FindFirst(TreeScope_Descendants, condition.get(), &found)) && found)
            {
                captionButtons.push_back(button);
            }
        }
    });
    EXPECT_EQ(name, L"UIA main window");
    EXPECT_EQ(captionButtons, (std::vector<std::wstring>{L"Minimize", L"Maximize", L"Close"}));

    Microsoft::WRL::ComPtr<te::UiaRoot> root = window->Automation();
    DestroyWindow(window->Hwnd());
    EXPECT_EQ(window->Automation(), nullptr);
    EXPECT_FALSE(root->IsConnected()) << "disconnected on WM_DESTROY";
    wil::com_ptr<IRawElementProviderFragment> result;
    EXPECT_EQ(root->Navigate(NavigateDirection_FirstChild, &result), kNotAvailable);
    root.Reset();
    window.reset();
    if (uninitialize)
    {
        OleUninitialize();
    }
}

TEST(UiaMainWindow, OtherObjectIdsAreLeftToTheSystem)
{
    const bool uninitialize = SUCCEEDED(OleInitialize(nullptr));
    te::MainWindow::Options options;
    options.instance = GetModuleHandleW(nullptr);
    options.title = L"UIA main window";
    options.quitOnDestroy = false;
    auto window = std::make_unique<te::MainWindow>(std::move(options));
    ASSERT_HRESULT_SUCCEEDED(window->Create(SW_SHOWNOACTIVATE));
    SendMessageW(window->Hwnd(), WM_GETOBJECT, 0, static_cast<LPARAM>(OBJID_CLIENT));
    EXPECT_EQ(window->Automation(), nullptr) << "MSAA requests do not create the UIA root";
    DestroyWindow(window->Hwnd());
    window.reset();
    if (uninitialize)
    {
        OleUninitialize();
    }
}

} // namespace
