// An automated accessibility audit (T083; quickstart V-5c, SC-004): walks the UI
// Automation tree of the main window over a real folder, and of the appearance popup,
// through the real UIA client, and applies checks modelled on the automated rules that
// Accessibility Insights FastPass runs (the Axe.Windows rules): names, control types,
// bounding rectangles, required patterns, parent/child types, keyboard focusability,
// ambiguous focusable siblings and the focused element. It does not replace FastPass
// (tests/manual/accessibility.md C1-C2), which remains the release check; it keeps the
// same rules from regressing between manual runs.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): UI Automation
// keeps process-wide state keyed by window handles (see UiaFileListTests.cpp).

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>
#include <te/appearance/ColorPicker.h>

#include <UIAutomationClient.h>
#include <commctrl.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

// ---------------------------------------------------------------------------
// The audit
// ---------------------------------------------------------------------------

struct Element
{
    wil::com_ptr<IUIAutomationElement> element;
    std::wstring name;
    std::wstring localizedType;
    CONTROLTYPEID type = 0;
    RECT bounds{};
    bool offscreen = false;
    bool focusable = false;
    bool enabled = true;
    std::wstring path; // "Window 'x' > List 'y' > ..."
};

std::wstring Bstr(const wil::unique_bstr& text)
{
    return text ? std::wstring(text.get()) : std::wstring();
}

Element Read(IUIAutomationElement* element, const std::wstring& parentPath)
{
    Element e;
    e.element = element;
    wil::unique_bstr text;
    element->get_CurrentName(&text);
    e.name = Bstr(text);
    element->get_CurrentLocalizedControlType(&text);
    e.localizedType = Bstr(text);
    element->get_CurrentControlType(&e.type);
    element->get_CurrentBoundingRectangle(&e.bounds);
    BOOL flag = FALSE;
    element->get_CurrentIsOffscreen(&flag);
    e.offscreen = flag != FALSE;
    element->get_CurrentIsKeyboardFocusable(&flag);
    e.focusable = flag != FALSE;
    element->get_CurrentIsEnabled(&flag);
    e.enabled = flag != FALSE;
    e.path = (parentPath.empty() ? L"" : parentPath + L" > ") + e.localizedType + L" '" + e.name + L"'";
    return e;
}

bool HasPattern(IUIAutomationElement* element, PATTERNID pattern)
{
    wil::com_ptr<IUnknown> provider;
    return SUCCEEDED(element->GetCurrentPattern(pattern, &provider)) && provider;
}

bool IsBlank(const std::wstring& text)
{
    for (const wchar_t c : text)
    {
        if (!std::iswspace(c))
        {
            return false;
        }
    }
    return true;
}

std::wstring Lower(std::wstring text)
{
    for (wchar_t& c : text)
    {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

// Control types whose elements must be named (Axe.Windows NameNotNull / NameNotEmpty).
bool NeedsName(CONTROLTYPEID type)
{
    switch (type)
    {
    case UIA_ButtonControlTypeId:
    case UIA_CheckBoxControlTypeId:
    case UIA_ComboBoxControlTypeId:
    case UIA_DataGridControlTypeId:
    case UIA_DataItemControlTypeId:
    case UIA_EditControlTypeId:
    case UIA_HeaderItemControlTypeId:
    case UIA_HyperlinkControlTypeId:
    case UIA_ListControlTypeId:
    case UIA_ListItemControlTypeId:
    case UIA_MenuItemControlTypeId:
    case UIA_RadioButtonControlTypeId:
    case UIA_SliderControlTypeId:
    case UIA_SplitButtonControlTypeId:
    case UIA_TabItemControlTypeId:
    case UIA_ToolBarControlTypeId:
    case UIA_TreeControlTypeId:
    case UIA_TreeItemControlTypeId:
    case UIA_WindowControlTypeId:
        return true;
    default:
        return false;
    }
}

// Control types that must be reachable with the keyboard while enabled (Axe.Windows
// IsKeyboardFocusableShouldBeTrue).
bool NeedsKeyboard(CONTROLTYPEID type)
{
    switch (type)
    {
    case UIA_CheckBoxControlTypeId:
    case UIA_ComboBoxControlTypeId:
    case UIA_EditControlTypeId:
    case UIA_HyperlinkControlTypeId:
    case UIA_RadioButtonControlTypeId:
    case UIA_SliderControlTypeId:
    case UIA_SplitButtonControlTypeId:
    case UIA_TabItemControlTypeId:
        return true;
    default:
        return false;
    }
}

class Auditor
{
  public:
    explicit Auditor(IUIAutomation* uia) : m_uia(uia)
    {
        uia->get_ControlViewWalker(&m_walker);
    }

    void Run(IUIAutomationElement* root)
    {
        Visit(Read(root, L""), nullptr, 0);
        CheckFocus();
    }

    [[nodiscard]] const std::vector<std::wstring>& Violations() const noexcept
    {
        return m_violations;
    }
    [[nodiscard]] std::size_t Count() const noexcept
    {
        return m_count;
    }
    [[nodiscard]] std::size_t CountOf(CONTROLTYPEID type) const
    {
        const auto it = m_byType.find(type);
        return it == m_byType.end() ? 0 : it->second;
    }

  private:
    void Fail(const wchar_t* rule, const Element& e, const std::wstring& detail = {})
    {
        m_violations.push_back(std::wstring(rule) + L": " + e.path +
                               (detail.empty() ? L"" : L" - " + detail));
    }

    void Visit(const Element& e, const Element* parent, int depth)
    {
        ++m_count;
        ++m_byType[e.type];
        CheckElement(e, parent);

        std::vector<Element> children;
        if (depth < 12 && m_count < 3000)
        {
            wil::com_ptr<IUIAutomationElement> child;
            m_walker->GetFirstChildElement(e.element.get(), &child);
            while (child)
            {
                children.push_back(Read(child.get(), e.path));
                wil::com_ptr<IUIAutomationElement> next;
                m_walker->GetNextSiblingElement(child.get(), &next);
                child = std::move(next);
            }
        }
        CheckSiblings(children);
        // A tree item with children can be expanded and collapsed (TreeItemWithChildren...).
        if (e.type == UIA_TreeItemControlTypeId && !children.empty() &&
            !HasPattern(e.element.get(), UIA_ExpandCollapsePatternId))
        {
            Fail(L"TreeItemWithChildrenSupportsExpandCollapse", e);
        }
        for (const Element& child : children)
        {
            Visit(child, &e, depth + 1);
        }
    }

    void CheckElement(const Element& e, const Element* parent)
    {
        // Control type and its localized name (ControlTypeValid, LocalizedControlTypeNotEmpty).
        if (e.type < UIA_ButtonControlTypeId || e.type > UIA_AppBarControlTypeId)
        {
            Fail(L"ControlTypeValid", e, std::to_wstring(e.type));
        }
        if (IsBlank(e.localizedType))
        {
            Fail(L"LocalizedControlTypeNotEmpty", e);
        }

        // Names (NameNotEmpty, NameNotWhiteSpace, NameReasonableLength, NameExcludesControlType,
        // NameExcludesLocalizedControlType).
        // The title bar is Windows' own element for the non-client area: it reports an
        // empty name for every Win32 window (a plain WS_OVERLAPPEDWINDOW window included), so
        // it is not checked here; FastPass (manual C1) is the reference for it.
        if (e.type != UIA_TitleBarControlTypeId &&
            (NeedsName(e.type) || (e.focusable && e.type != UIA_PaneControlTypeId)))
        {
            if (e.name.empty())
            {
                Fail(L"NameNotEmpty", e);
            }
            else if (IsBlank(e.name))
            {
                Fail(L"NameNotWhiteSpace", e);
            }
        }
        if (e.name.size() > 512)
        {
            Fail(L"NameReasonableLength", e, std::to_wstring(e.name.size()) + L" characters");
        }
        if (!e.name.empty() && !e.localizedType.empty() && e.type != UIA_TitleBarControlTypeId &&
            Lower(e.name) == Lower(e.localizedType))
        {
            Fail(L"NameExcludesLocalizedControlType", e);
        }

        // Bounding rectangle (BoundingRectangleNotNull / NotAllZeros / ContainedInParent) for
        // everything on screen.
        const bool empty = e.bounds.right <= e.bounds.left || e.bounds.bottom <= e.bounds.top;
        if (!e.offscreen && empty && e.type != UIA_TitleBarControlTypeId)
        {
            Fail(L"BoundingRectangleNotNull", e);
        }
        if (!e.offscreen && !empty && parent && !parent->offscreen)
        {
            const RECT& p = parent->bounds;
            constexpr LONG kTolerance = 1;
            if (e.bounds.left < p.left - kTolerance || e.bounds.top < p.top - kTolerance ||
                e.bounds.right > p.right + kTolerance || e.bounds.bottom > p.bottom + kTolerance)
            {
                Fail(L"BoundingRectangleContainedInParent", e);
            }
        }

        // Patterns each control type needs (ButtonShouldHavePatterns, EditSupportsValue, ...).
        IUIAutomationElement* el = e.element.get();
        switch (e.type)
        {
        case UIA_ButtonControlTypeId:
            if (!HasPattern(el, UIA_InvokePatternId) && !HasPattern(el, UIA_TogglePatternId) &&
                !HasPattern(el, UIA_ExpandCollapsePatternId))
            {
                Fail(L"ButtonShouldHavePatterns", e);
            }
            break;
        case UIA_EditControlTypeId:
            if (!HasPattern(el, UIA_ValuePatternId) && !HasPattern(el, UIA_TextPatternId))
            {
                Fail(L"EditSupportsValueOrText", e);
            }
            break;
        case UIA_ListItemControlTypeId:
        case UIA_DataItemControlTypeId:
            if (parent && parent->type != UIA_ListControlTypeId &&
                parent->type != UIA_DataGridControlTypeId && parent->type != UIA_GroupControlTypeId)
            {
                Fail(L"ItemParentIsListOrDataGrid", e, L"parent is " + parent->localizedType);
            }
            if (!HasPattern(el, UIA_SelectionItemPatternId))
            {
                Fail(L"SelectionItemPatternSupported", e);
            }
            break;
        case UIA_TreeItemControlTypeId:
            // TreeItemParentIsTreeOrTreeItem (T090, the folder tree).
            if (parent && parent->type != UIA_TreeControlTypeId && parent->type != UIA_TreeItemControlTypeId)
            {
                Fail(L"TreeItemParentIsTreeOrTreeItem", e, L"parent is " + parent->localizedType);
            }
            break;
        case UIA_HeaderItemControlTypeId:
            if (parent && parent->type != UIA_HeaderControlTypeId)
            {
                Fail(L"HeaderItemParentIsHeader", e, L"parent is " + parent->localizedType);
            }
            break;
        case UIA_DataGridControlTypeId:
            if (!HasPattern(el, UIA_GridPatternId) || !HasPattern(el, UIA_TablePatternId))
            {
                Fail(L"DataGridSupportsGridAndTable", e);
            }
            break;
        case UIA_ListControlTypeId:
            if (!HasPattern(el, UIA_SelectionPatternId))
            {
                Fail(L"ListSupportsSelection", e);
            }
            break;
        case UIA_SliderControlTypeId:
            if (!HasPattern(el, UIA_RangeValuePatternId) && !HasPattern(el, UIA_SelectionPatternId))
            {
                Fail(L"SliderSupportsRangeValue", e);
            }
            break;
        default:
            break;
        }

        // Keyboard access.
        if (NeedsKeyboard(e.type) && e.enabled && !e.offscreen && !e.focusable)
        {
            Fail(L"IsKeyboardFocusableShouldBeTrue", e);
        }
    }

    // Two focusable siblings with the same name and control type cannot be told apart by a
    // screen reader user (SiblingUniqueAndFocusable).
    void CheckSiblings(const std::vector<Element>& siblings)
    {
        std::map<std::pair<CONTROLTYPEID, std::wstring>, int> seen;
        for (const Element& e : siblings)
        {
            if (e.focusable && !e.name.empty() && ++seen[{e.type, e.name}] == 2)
            {
                Fail(L"SiblingUniqueAndFocusable", e);
            }
        }
    }

    // The focused element says it has the focus and can take it.
    void CheckFocus()
    {
        wil::com_ptr<IUIAutomationElement> focused;
        if (FAILED(m_uia->GetFocusedElement(&focused)) || !focused)
        {
            return;
        }
        const Element e = Read(focused.get(), L"(focused)");
        BOOL has = FALSE;
        focused->get_CurrentHasKeyboardFocus(&has);
        if (!has)
        {
            Fail(L"FocusedElementHasKeyboardFocus", e);
        }
        if (!e.focusable)
        {
            Fail(L"FocusedElementIsKeyboardFocusable", e);
        }
    }

    IUIAutomation* m_uia;
    wil::com_ptr<IUIAutomationTreeWalker> m_walker;
    std::vector<std::wstring> m_violations;
    std::size_t m_count = 0;
    std::map<CONTROLTYPEID, std::size_t> m_byType;
};

std::string Narrow(const std::wstring& text)
{
    std::string out;
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    out.resize(static_cast<std::size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

void Report(const Auditor& audit)
{
    for (const std::wstring& violation : audit.Violations())
    {
        ADD_FAILURE() << Narrow(violation);
    }
    std::printf("audited %zu elements, %zu violations\n", audit.Count(), audit.Violations().size());
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------

class UiaAuditTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-uiaaudit-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root / L"folder");
        for (const wchar_t* file : {L"alpha.txt", L"beta.txt", L"gamma.txt"})
        {
            std::ofstream(m_root / file) << "x";
        }
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"UIA audit test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = m_root.wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
        Pump(15000, [this] { return !m_window->NavigationPending(); });
        Pump(300);
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

    bool m_uninitialize = false;
    fs::path m_root;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(UiaAuditTest, TheMainWindowPassesTheAudit)
{
    SetFocus(m_window->Hwnd());
    SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_HOME, 0); // a focused, selected row
    std::optional<Auditor> audit;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        audit.emplace(uia);
        audit->Run(window.get());
    });
    ASSERT_TRUE(audit);
    Report(*audit);
    // Everything the window shows was reached: the picker, the toolbar's four buttons and
    // the address, the pane's places, the grid's header items and its four rows.
    EXPECT_GE(audit->CountOf(UIA_ButtonControlTypeId), 5u);
    EXPECT_GE(audit->CountOf(UIA_EditControlTypeId), 1u);
    EXPECT_GE(audit->CountOf(UIA_TreeItemControlTypeId), 3u) << "the navigation pane's places";
    EXPECT_EQ(audit->CountOf(UIA_HeaderItemControlTypeId), 4u);
    EXPECT_EQ(audit->CountOf(UIA_DataItemControlTypeId), 4u);
    EXPECT_EQ(audit->CountOf(UIA_StatusBarControlTypeId), 1u);
}

TEST_F(UiaAuditTest, TheAddressWhileEditingPassesTheAudit)
{
    // Ctrl+L: the native EDIT is the Address element while editing (T078).
    SendMessageW(m_window->Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_FOCUS_ADDRESS, 1), 0);
    ASSERT_TRUE(m_window->Address().IsEditing());
    std::optional<Auditor> audit;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        audit.emplace(uia);
        audit->Run(window.get());
    });
    ASSERT_TRUE(audit);
    Report(*audit);
    // One Address edit, the native one, with nothing below it (T083 finding: an override
    // provider used to add a typeless "Address" element under it, level after level).
    EXPECT_EQ(audit->CountOf(UIA_EditControlTypeId), 1u);
    EXPECT_EQ(audit->CountOf(0), 0u);
}

TEST_F(UiaAuditTest, AnInlineRenamePassesTheAudit)
{
    // F2 on a row: the native rename EDIT is on screen (T071).
    SetFocus(m_window->Hwnd());
    SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(m_window->Hwnd(), WM_KEYDOWN, VK_F2, 0);
    ASSERT_TRUE(m_window->Files().IsRenaming());
    std::optional<Auditor> audit;
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(m_window->Hwnd(), &window));
        audit.emplace(uia);
        audit->Run(window.get());
    });
    ASSERT_TRUE(audit);
    Report(*audit);
    EXPECT_EQ(audit->CountOf(UIA_EditControlTypeId), 2u) << "the address (display mode) and the rename edit";
}

// ---------------------------------------------------------------------------
// The appearance popup
// ---------------------------------------------------------------------------

TEST(UiaAuditPopup, TheAppearancePopupPassesTheAudit)
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    const HWND owner = CreateWindowExW(0, L"STATIC", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100,
                                       800, 600, nullptr, nullptr, nullptr, nullptr);
    ASSERT_NE(owner, nullptr);
    {
        te::ColorPicker picker(GetModuleHandleW(nullptr), {});
        te::RenderingCapabilities caps;
        caps.systemBackdropSupported = true;
        caps.accent = {0x00, 0x78, 0xD4};
        picker.SetCapabilities(caps);
        te::AppearanceSettings settings;
        te::EffectiveAppearance effective;
        effective.requested = settings.backdropMode;
        effective.applied = settings.backdropMode;
        effective.opacityControlEnabled = true;
        picker.Show(owner, RECT{600, 130, 640, 160}, settings, effective);
        const HWND dialog = picker.Dialog();
        ASSERT_NE(dialog, nullptr);

        std::optional<Auditor> audit;
        WithClient([&](IUIAutomation* uia) {
            wil::com_ptr<IUIAutomationElement> root;
            ASSERT_HRESULT_SUCCEEDED(uia->ElementFromHandle(dialog, &root));
            audit.emplace(uia);
            audit->Run(root.get());
        });
        ASSERT_TRUE(audit);
        Report(*audit);
        EXPECT_EQ(audit->CountOf(UIA_RadioButtonControlTypeId), 3u) << "Acrylic, Mica, Solid";
        EXPECT_GE(audit->CountOf(UIA_ButtonControlTypeId), 15u) << "12 swatches, Accent, Custom, Reset";
        EXPECT_EQ(audit->CountOf(UIA_SliderControlTypeId), 2u) << "surface opacity, tint strength";
    }
    DestroyWindow(owner);
}

} // namespace
