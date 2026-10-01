#include <te/a11y/UiaChrome.h>

#include <te/a11y/UiaCommon.h>

#include <UIAutomationCoreApi.h>

#include <wil/result.h>

#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace te
{

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using uia::kNotAvailable;

namespace
{

constexpr int kRuntimeIdToolbar = 7;
constexpr int kRuntimeIdToolbarButton = 8;
constexpr int kRuntimeIdAddress = 9;
constexpr int kRuntimeIdPane = 10;
constexpr int kRuntimeIdPaneItem = 11;
constexpr int kRuntimeIdStatus = 12;
constexpr HRESULT kNotEnabled = static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);

bool Contains(const D2D1_RECT_F& rect, D2D1_POINT_2F point)
{
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

template <class T> HRESULT Return(T* object, IRawElementProviderFragment** result)
{
    *result = object;
    if (object)
    {
        object->AddRef();
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// The boilerplate every chrome fragment shares; subclasses describe themselves.
// ---------------------------------------------------------------------------
template <class... Patterns>
class ChromeFragment : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                                           IRawElementProviderFragment, Patterns...>
{
  public:
    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override
    {
        *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** pattern) override
    {
        *pattern = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        *pattern = Pattern(patternId);
        if (*pattern)
        {
            (*pattern)->AddRef();
        }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override
    {
        result->vt = VT_EMPTY;
        RETURN_HR_IF(kNotAvailable, !Available());
        switch (propertyId)
        {
        case UIA_ControlTypePropertyId:
            uia::SetInt(result, ControlType());
            return S_OK;
        case UIA_NamePropertyId:
            uia::SetString(result, Name());
            return S_OK;
        case UIA_IsEnabledPropertyId:
            uia::SetBool(result, Enabled());
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            uia::SetBool(result, true);
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            uia::SetBool(result, Focusable());
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            uia::SetBool(result, HasFocus());
            return S_OK;
        default:
            ExtraProperty(propertyId, result);
            return S_OK;
        }
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override
    {
        *host = nullptr;
        return Available() ? HostProvider(host) : S_OK;
    }

    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        return NavigateTo(direction, result);
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override
    {
        *runtimeId = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        const std::vector<int> parts = RuntimeParts();
        *runtimeId = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(parts.size() + 1));
        RETURN_IF_NULL_ALLOC(*runtimeId);
        LONG index = 0;
        int append = UiaAppendRuntimeId;
        RETURN_IF_FAILED(SafeArrayPutElement(*runtimeId, &index, &append));
        for (int part : parts)
        {
            ++index;
            RETURN_IF_FAILED(SafeArrayPutElement(*runtimeId, &index, &part));
        }
        return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override
    {
        *rect = UiaRect{};
        RETURN_HR_IF(kNotAvailable, !Available());
        const D2D1_RECT_F dip = Dip();
        if (dip.right > dip.left && dip.bottom > dip.top)
        {
            *rect = m_host->toScreen(dip);
        }
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override
    {
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override
    {
        RETURN_HR_IF(kNotAvailable, !Available());
        TakeFocus();
        return S_OK;
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override
    {
        *root = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        *root = m_root.Get();
        (*root)->AddRef();
        return S_OK;
    }

  protected:
    void Init(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host)
    {
        m_root = root;
        m_host = std::move(host);
    }
    [[nodiscard]] bool Available() const noexcept
    {
        return m_root && m_root->IsConnected() && m_host;
    }
    // A top-level fragment's siblings are the root's other children.
    HRESULT RootNavigate(NavigateDirection direction, IRawElementProviderFragment** result)
    {
        switch (direction)
        {
        case NavigateDirection_Parent:
            return Return(static_cast<IRawElementProviderFragment*>(m_root.Get()), result);
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling:
            return m_root->Sibling(this, direction, result);
        default:
            return S_OK;
        }
    }

    [[nodiscard]] virtual int ControlType() const = 0;
    [[nodiscard]] virtual std::wstring Name() const = 0;
    [[nodiscard]] virtual D2D1_RECT_F Dip() const = 0;
    [[nodiscard]] virtual std::vector<int> RuntimeParts() const = 0;
    virtual HRESULT NavigateTo(NavigateDirection direction, IRawElementProviderFragment** result) = 0;
    [[nodiscard]] virtual bool Enabled() const
    {
        return true;
    }
    [[nodiscard]] virtual bool Focusable() const
    {
        return false;
    }
    [[nodiscard]] virtual bool HasFocus() const
    {
        return false;
    }
    virtual void TakeFocus() {}
    virtual IUnknown* Pattern(PATTERNID)
    {
        return nullptr;
    }
    virtual void ExtraProperty(PROPERTYID, VARIANT*) {}
    virtual HRESULT HostProvider(IRawElementProviderSimple**)
    {
        return S_OK;
    }

    ComPtr<UiaRoot> m_root;
    std::shared_ptr<const UiaChromeHost> m_host;
};

// ---------------------------------------------------------------------------
// ToolBar "Navigation": Back, Forward, Up, Refresh and the Address edit.
// ---------------------------------------------------------------------------
class UiaToolbar;
HRESULT MakeButton(UiaRoot* root, const std::shared_ptr<const UiaChromeHost>& host, UiaToolbar* toolbar,
                   int index, IRawElementProviderFragment** result);
HRESULT MakeAddress(UiaRoot* root, const std::shared_ptr<const UiaChromeHost>& host, UiaToolbar* toolbar,
                    IRawElementProviderFragment** result);

class UiaToolbar final : public ChromeFragment<IUiaFragmentExtension>
{
  public:
    HRESULT RuntimeClassInitialize(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host)
    {
        Init(root, std::move(host));
        return S_OK;
    }

    IFACEMETHODIMP HitTest(double x, double y, IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        const D2D1_POINT_2F point = m_host->fromScreen(x, y);
        if (const auto button = m_host->toolbar->ButtonAt(point))
        {
            return MakeButton(m_root.Get(), m_host, this, static_cast<int>(*button), result);
        }
        if (!m_host->address->IsEditing() && Contains(m_host->address->FieldRect(), point))
        {
            return MakeAddress(m_root.Get(), m_host, this, result);
        }
        return Return(static_cast<IRawElementProviderFragment*>(this), result);
    }
    IFACEMETHODIMP FocusedDescendant(IRawElementProviderFragment** result) override
    {
        return Return(static_cast<IRawElementProviderFragment*>(this), result);
    }

  protected:
    [[nodiscard]] int ControlType() const override
    {
        return UIA_ToolBarControlTypeId;
    }
    [[nodiscard]] std::wstring Name() const override
    {
        return m_host->toolbarName;
    }
    [[nodiscard]] D2D1_RECT_F Dip() const override
    {
        return m_host->toolbarBounds ? m_host->toolbarBounds() : D2D1_RECT_F{};
    }
    [[nodiscard]] std::vector<int> RuntimeParts() const override
    {
        return {kRuntimeIdToolbar};
    }
    HRESULT NavigateTo(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        if (direction == NavigateDirection_FirstChild)
        {
            return MakeButton(m_root.Get(), m_host, this, 0, result);
        }
        if (direction == NavigateDirection_LastChild)
        {
            // While editing, the native EDIT is the Address element (named by AddressBar with Dynamic
            // Annotation).
            return m_host->address->IsEditing()
                       ? MakeButton(m_root.Get(), m_host, this, static_cast<int>(Toolbar::kButtonCount) - 1,
                                    result)
                       : MakeAddress(m_root.Get(), m_host, this, result);
        }
        return RootNavigate(direction, result);
    }
};

class UiaToolbarButton final : public ChromeFragment<IInvokeProvider>
{
  public:
    HRESULT RuntimeClassInitialize(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                                   UiaToolbar* toolbar, int index)
    {
        Init(root, std::move(host));
        m_toolbar = toolbar;
        m_index = index;
        return S_OK;
    }

    IFACEMETHODIMP Invoke() override
    {
        RETURN_HR_IF(kNotAvailable, !Available());
        RETURN_HR_IF(kNotEnabled, !Enabled());
        if (m_host->invokeButton)
        {
            m_host->invokeButton(static_cast<Toolbar::Button>(m_index));
        }
        return S_OK;
    }

  protected:
    // "Back (Alt+Left Arrow)": the name before the parenthesis, the keys inside it.
    [[nodiscard]] const std::wstring& Text() const
    {
        const Toolbar::Strings& texts = m_host->toolbar->Texts();
        const std::wstring* all[] = {&texts.back, &texts.forward, &texts.up, &texts.refresh};
        return *all[m_index];
    }
    [[nodiscard]] int ControlType() const override
    {
        return UIA_ButtonControlTypeId;
    }
    [[nodiscard]] std::wstring Name() const override
    {
        const std::wstring& text = Text();
        const std::size_t open = text.find(L" (");
        return open == std::wstring::npos ? text : text.substr(0, open);
    }
    [[nodiscard]] D2D1_RECT_F Dip() const override
    {
        return m_host->toolbar->ButtonRect(static_cast<Toolbar::Button>(m_index));
    }
    [[nodiscard]] std::vector<int> RuntimeParts() const override
    {
        return {kRuntimeIdToolbarButton, m_index};
    }
    [[nodiscard]] bool Enabled() const override
    {
        return m_host->toolbar->IsEnabled(static_cast<Toolbar::Button>(m_index));
    }
    void ExtraProperty(PROPERTYID propertyId, VARIANT* result) override
    {
        const std::wstring& text = Text();
        const std::size_t open = text.find(L" (");
        if (propertyId == UIA_AcceleratorKeyPropertyId && open != std::wstring::npos && text.back() == L')')
        {
            uia::SetString(result, text.substr(open + 2, text.size() - open - 3));
        }
        else if (propertyId == UIA_HelpTextPropertyId)
        {
            uia::SetString(result, text);
        }
    }
    IUnknown* Pattern(PATTERNID patternId) override
    {
        return patternId == UIA_InvokePatternId ? static_cast<IInvokeProvider*>(this) : nullptr;
    }
    HRESULT NavigateTo(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        switch (direction)
        {
        case NavigateDirection_Parent:
            return Return(static_cast<IRawElementProviderFragment*>(m_toolbar.Get()), result);
        case NavigateDirection_NextSibling:
            return m_index + 1 < static_cast<int>(Toolbar::kButtonCount)
                       ? MakeButton(m_root.Get(), m_host, m_toolbar.Get(), m_index + 1, result)
                   : m_host->address->IsEditing()
                       ? S_OK
                       : MakeAddress(m_root.Get(), m_host, m_toolbar.Get(), result);
        case NavigateDirection_PreviousSibling:
            return m_index > 0 ? MakeButton(m_root.Get(), m_host, m_toolbar.Get(), m_index - 1, result)
                               : S_OK;
        default:
            return S_OK;
        }
    }

  private:
    ComPtr<UiaToolbar> m_toolbar;
    int m_index = 0;
};

// The Edit "Address" in display mode, with the Value pattern. While the user edits, this
// element steps aside and the native EDIT is the Address element, named by AddressBar with
// Dynamic Annotation, with its own Value and Text patterns.
class UiaAddress final : public ChromeFragment<IValueProvider>
{
  public:
    HRESULT RuntimeClassInitialize(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                                   UiaToolbar* toolbar)
    {
        Init(root, std::move(host));
        m_toolbar = toolbar;
        return S_OK;
    }

    IFACEMETHODIMP SetValue(LPCWSTR text) override
    {
        RETURN_HR_IF(kNotAvailable, !Available());
        RETURN_HR_IF_NULL(E_INVALIDARG, text);
        if (m_host->navigateToText)
        {
            m_host->navigateToText(text);
        }
        return S_OK;
    }
    IFACEMETHODIMP get_Value(BSTR* result) override
    {
        *result = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        *result = SysAllocString(m_host->address->DisplayText().c_str());
        return S_OK;
    }
    IFACEMETHODIMP get_IsReadOnly(BOOL* readOnly) override
    {
        *readOnly = FALSE;
        return S_OK;
    }

  protected:
    [[nodiscard]] bool Editing() const
    {
        return m_host->address->IsEditing() && m_host->address->Edit();
    }
    [[nodiscard]] int ControlType() const override
    {
        return UIA_EditControlTypeId;
    }
    [[nodiscard]] std::wstring Name() const override
    {
        return m_host->addressName;
    }
    [[nodiscard]] D2D1_RECT_F Dip() const override
    {
        return m_host->address->FieldRect();
    }
    [[nodiscard]] std::vector<int> RuntimeParts() const override
    {
        return {kRuntimeIdAddress};
    }
    [[nodiscard]] bool Focusable() const override
    {
        return true; // Ctrl+L, Alt+D, F4
    }
    [[nodiscard]] bool HasFocus() const override
    {
        return Editing() && GetFocus() == m_host->address->Edit();
    }
    void ExtraProperty(PROPERTYID propertyId, VARIANT* result) override
    {
        if (propertyId == UIA_AutomationIdPropertyId)
        {
            uia::SetString(result, L"AddressBar");
        }
        else if (propertyId == UIA_AcceleratorKeyPropertyId)
        {
            uia::SetString(result, L"Ctrl+L");
        }
    }
    IUnknown* Pattern(PATTERNID patternId) override
    {
        return patternId == UIA_ValuePatternId && !Editing() ? static_cast<IValueProvider*>(this) : nullptr;
    }
    HRESULT NavigateTo(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        switch (direction)
        {
        case NavigateDirection_Parent:
            return Return(static_cast<IRawElementProviderFragment*>(m_toolbar.Get()), result);
        case NavigateDirection_PreviousSibling:
            return MakeButton(m_root.Get(), m_host, m_toolbar.Get(),
                              static_cast<int>(Toolbar::kButtonCount) - 1, result);
        default:
            return S_OK;
        }
    }

  private:
    ComPtr<UiaToolbar> m_toolbar;
};

HRESULT MakeButton(UiaRoot* root, const std::shared_ptr<const UiaChromeHost>& host, UiaToolbar* toolbar,
                   int index, IRawElementProviderFragment** result)
{
    *result = nullptr;
    ComPtr<UiaToolbarButton> button;
    RETURN_IF_FAILED(
        Microsoft::WRL::MakeAndInitialize<UiaToolbarButton>(&button, root, host, toolbar, index));
    *result = button.Detach();
    return S_OK;
}

HRESULT MakeAddress(UiaRoot* root, const std::shared_ptr<const UiaChromeHost>& host, UiaToolbar* toolbar,
                    IRawElementProviderFragment** result)
{
    *result = nullptr;
    ComPtr<UiaAddress> address;
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<UiaAddress>(&address, root, host, toolbar));
    *result = address.Detach();
    return S_OK;
}

// ---------------------------------------------------------------------------
// Tree "Navigation pane" (T090; was a List in T078): one TreeItem per row, nested as the
// folder tree is, each with ExpandCollapse while it can have subfolders.
// ---------------------------------------------------------------------------
class UiaPane;
HRESULT MakePaneItem(UiaRoot* root, const std::shared_ptr<const UiaChromeHost>& host, UiaPane* pane,
                     std::size_t index, IRawElementProviderFragment** result);

class UiaPane final : public ChromeFragment<ISelectionProvider, IUiaFragmentExtension>
{
  public:
    HRESULT RuntimeClassInitialize(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host)
    {
        Init(root, std::move(host));
        return S_OK;
    }

    IFACEMETHODIMP GetSelection(SAFEARRAY** selection) override
    {
        *selection = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        std::vector<ComPtr<IRawElementProviderFragment>> current;
        if (const auto index = m_host->pane->CurrentIndex())
        {
            ComPtr<IRawElementProviderFragment> item;
            RETURN_IF_FAILED(MakePaneItem(m_root.Get(), m_host, this, *index, &item));
            current.push_back(std::move(item));
        }
        return uia::MakeProviderArray(current, selection);
    }
    IFACEMETHODIMP get_CanSelectMultiple(BOOL* result) override
    {
        *result = FALSE;
        return S_OK;
    }
    IFACEMETHODIMP get_IsSelectionRequired(BOOL* result) override
    {
        *result = FALSE;
        return S_OK;
    }

    IFACEMETHODIMP HitTest(double x, double y, IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        if (const auto index = m_host->pane->EntryAt(m_host->fromScreen(x, y)))
        {
            return MakePaneItem(m_root.Get(), m_host, this, *index, result);
        }
        return Return(static_cast<IRawElementProviderFragment*>(this), result);
    }
    IFACEMETHODIMP FocusedDescendant(IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        RETURN_HR_IF(kNotAvailable, !Available());
        if (const auto index = m_host->pane->FocusIndex())
        {
            return MakePaneItem(m_root.Get(), m_host, this, *index, result);
        }
        return Return(static_cast<IRawElementProviderFragment*>(this), result);
    }

  protected:
    [[nodiscard]] int ControlType() const override
    {
        return UIA_TreeControlTypeId;
    }
    [[nodiscard]] std::wstring Name() const override
    {
        return m_host->paneName;
    }
    [[nodiscard]] D2D1_RECT_F Dip() const override
    {
        return m_host->pane->Bounds();
    }
    [[nodiscard]] std::vector<int> RuntimeParts() const override
    {
        return {kRuntimeIdPane};
    }
    [[nodiscard]] bool Focusable() const override
    {
        return true;
    }
    [[nodiscard]] bool HasFocus() const override
    {
        return m_host->paneHasFocus && m_host->paneHasFocus();
    }
    void TakeFocus() override
    {
        if (m_host->focusPane)
        {
            m_host->focusPane();
        }
    }
    IUnknown* Pattern(PATTERNID patternId) override
    {
        return patternId == UIA_SelectionPatternId ? static_cast<ISelectionProvider*>(this) : nullptr;
    }
    HRESULT NavigateTo(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        const auto& entries = m_host->pane->Entries();
        if (direction == NavigateDirection_FirstChild)
        {
            return entries.empty() ? S_OK : MakePaneItem(m_root.Get(), m_host, this, 0, result);
        }
        if (direction == NavigateDirection_LastChild)
        {
            // The last top-level row.
            for (std::size_t i = entries.size(); i-- > 0;)
            {
                if (entries[i].depth == 0)
                {
                    return MakePaneItem(m_root.Get(), m_host, this, i, result);
                }
            }
            return S_OK;
        }
        return RootNavigate(direction, result);
    }
};

// A row of the tree. Keyed by its node id: the same folder can appear twice (Desktop at the
// top and under This PC). After a refresh, or when its parent collapses, it is no longer
// available.
class UiaPaneItem final
    : public ChromeFragment<ISelectionItemProvider, IInvokeProvider, IExpandCollapseProvider>
{
  public:
    HRESULT RuntimeClassInitialize(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host, UiaPane* pane,
                                   std::uint64_t nodeId, const ShellLocation& location)
    {
        Init(root, std::move(host));
        m_pane = pane;
        m_nodeId = nodeId;
        m_location = location;
        return S_OK;
    }

    // ISelectionItemProvider: selecting a place navigates to it, as a click does.
    IFACEMETHODIMP Select() override
    {
        return Go();
    }
    IFACEMETHODIMP AddToSelection() override
    {
        RETURN_HR_IF(kNotAvailable, !Index());
        return uia::kInvalidOperation; // one place at a time
    }
    IFACEMETHODIMP RemoveFromSelection() override
    {
        RETURN_HR_IF(kNotAvailable, !Index());
        return uia::kInvalidOperation;
    }
    IFACEMETHODIMP get_IsSelected(BOOL* selected) override
    {
        const auto index = Index();
        RETURN_HR_IF(kNotAvailable, !index);
        *selected = m_host->pane->CurrentIndex() == index ? TRUE : FALSE;
        return S_OK;
    }
    IFACEMETHODIMP get_SelectionContainer(IRawElementProviderSimple** container) override
    {
        *container = nullptr;
        RETURN_HR_IF(kNotAvailable, !Index());
        *container = static_cast<IRawElementProviderSimple*>(m_pane.Get());
        (*container)->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP Invoke() override
    {
        return Go();
    }

    // IExpandCollapseProvider (T090): posted, so the tree changes after the call returns.
    IFACEMETHODIMP Expand() override
    {
        const auto index = Index();
        RETURN_HR_IF(kNotAvailable, !index);
        RETURN_HR_IF(uia::kInvalidOperation, !m_host->pane->Entries()[*index].expandable);
        if (m_host->expandPlace)
        {
            m_host->expandPlace(m_nodeId, true);
        }
        return S_OK;
    }
    IFACEMETHODIMP Collapse() override
    {
        const auto index = Index();
        RETURN_HR_IF(kNotAvailable, !index);
        RETURN_HR_IF(uia::kInvalidOperation, !m_host->pane->Entries()[*index].expandable);
        if (m_host->expandPlace)
        {
            m_host->expandPlace(m_nodeId, false);
        }
        return S_OK;
    }
    IFACEMETHODIMP get_ExpandCollapseState(ExpandCollapseState* state) override
    {
        const auto index = Index();
        RETURN_HR_IF(kNotAvailable, !index);
        const NavigationPane::Entry& entry = m_host->pane->Entries()[*index];
        *state = !entry.expandable ? ExpandCollapseState_LeafNode
                 : entry.expanded
                     ? (entry.loading ? ExpandCollapseState_PartiallyExpanded : ExpandCollapseState_Expanded)
                     : ExpandCollapseState_Collapsed;
        return S_OK;
    }

    // Overridden so a vanished row reports "not available" everywhere.
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override
    {
        result->vt = VT_EMPTY;
        RETURN_HR_IF(kNotAvailable, !Index());
        return ChromeFragment::GetPropertyValue(propertyId, result);
    }

  protected:
    [[nodiscard]] std::optional<std::size_t> Index() const
    {
        return Available() ? m_host->pane->IndexOfNode(m_nodeId) : std::nullopt;
    }
    HRESULT Go()
    {
        RETURN_HR_IF(kNotAvailable, !Index());
        if (m_host->navigateToPlace)
        {
            m_host->navigateToPlace(m_location);
        }
        return S_OK;
    }
    [[nodiscard]] int ControlType() const override
    {
        return UIA_TreeItemControlTypeId;
    }
    [[nodiscard]] std::wstring Name() const override
    {
        return m_location.DisplayName();
    }
    [[nodiscard]] D2D1_RECT_F Dip() const override
    {
        const auto index = Index();
        return index ? m_host->pane->EntryRect(*index) : D2D1_RECT_F{};
    }
    [[nodiscard]] std::vector<int> RuntimeParts() const override
    {
        return {kRuntimeIdPaneItem, static_cast<int>(m_nodeId & 0x7FFFFFFF)};
    }
    [[nodiscard]] bool Focusable() const override
    {
        return true;
    }
    [[nodiscard]] bool HasFocus() const override
    {
        return m_host->paneHasFocus && m_host->paneHasFocus() && m_host->pane->FocusIndex() == Index();
    }
    void TakeFocus() override
    {
        if (m_host->focusPane)
        {
            m_host->focusPane();
        }
    }
    IUnknown* Pattern(PATTERNID patternId) override
    {
        const auto index = Index();
        if (!index)
        {
            return nullptr;
        }
        switch (patternId)
        {
        case UIA_SelectionItemPatternId:
            return static_cast<ISelectionItemProvider*>(this);
        case UIA_InvokePatternId:
            return static_cast<IInvokeProvider*>(this);
        case UIA_ExpandCollapsePatternId:
            return m_host->pane->Entries()[*index].expandable ? static_cast<IExpandCollapseProvider*>(this)
                                                              : nullptr;
        default:
            return nullptr;
        }
    }
    HRESULT NavigateTo(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        const auto index = Index();
        RETURN_HR_IF(kNotAvailable, !index);
        const NavigationPane& pane = *m_host->pane;
        std::optional<std::size_t> target;
        switch (direction)
        {
        case NavigateDirection_Parent:
            target = pane.ParentOf(*index);
            if (!target)
            {
                return Return(static_cast<IRawElementProviderFragment*>(m_pane.Get()), result);
            }
            break;
        case NavigateDirection_NextSibling:
            target = pane.NextSiblingOf(*index);
            break;
        case NavigateDirection_PreviousSibling:
            target = pane.PreviousSiblingOf(*index);
            break;
        case NavigateDirection_FirstChild:
            target = pane.FirstChildOf(*index);
            break;
        case NavigateDirection_LastChild:
            target = pane.LastChildOf(*index);
            break;
        default:
            break;
        }
        return target ? MakePaneItem(m_root.Get(), m_host, m_pane.Get(), *target, result) : S_OK;
    }

  private:
    ComPtr<UiaPane> m_pane;
    std::uint64_t m_nodeId = 0;
    ShellLocation m_location;
};

HRESULT MakePaneItem(UiaRoot* root, const std::shared_ptr<const UiaChromeHost>& host, UiaPane* pane,
                     std::size_t index, IRawElementProviderFragment** result)
{
    *result = nullptr;
    RETURN_HR_IF(E_INVALIDARG, index >= host->pane->Entries().size());
    const NavigationPane::Entry& entry = host->pane->Entries()[index];
    ComPtr<UiaPaneItem> item;
    RETURN_IF_FAILED(
        Microsoft::WRL::MakeAndInitialize<UiaPaneItem>(&item, root, host, pane, entry.id, entry.location));
    *result = item.Detach();
    return S_OK;
}

// ---------------------------------------------------------------------------
// StatusBar: its name is the status text; a polite live region.
// ---------------------------------------------------------------------------
class UiaStatus final : public ChromeFragment<>
{
  public:
    HRESULT RuntimeClassInitialize(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host)
    {
        Init(root, std::move(host));
        return S_OK;
    }

  protected:
    [[nodiscard]] int ControlType() const override
    {
        return UIA_StatusBarControlTypeId;
    }
    [[nodiscard]] std::wstring Name() const override
    {
        return m_host->status->LeftText();
    }
    [[nodiscard]] D2D1_RECT_F Dip() const override
    {
        return m_host->status->Bounds();
    }
    [[nodiscard]] std::vector<int> RuntimeParts() const override
    {
        return {kRuntimeIdStatus};
    }
    void ExtraProperty(PROPERTYID propertyId, VARIANT* result) override
    {
        if (propertyId == UIA_LiveSettingPropertyId)
        {
            uia::SetInt(result, Polite);
        }
        else if (propertyId == UIA_FullDescriptionPropertyId)
        {
            uia::SetString(result, m_host->status->RightText()); // the appearance summary
        }
        else if (propertyId == UIA_AutomationIdPropertyId)
        {
            uia::SetString(result, L"StatusBar");
        }
    }
    HRESULT NavigateTo(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        return RootNavigate(direction, result);
    }
};

template <class T>
HRESULT MakeTopLevel(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                     ComPtr<IRawElementProviderFragment>* result)
{
    RETURN_HR_IF(E_INVALIDARG, !root || !host || !host->toScreen || !host->fromScreen);
    ComPtr<T> provider;
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<T>(&provider, root, std::move(host)));
    return provider.As(result);
}

} // namespace

HRESULT MakeUiaToolbar(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                       ComPtr<IRawElementProviderFragment>* result)
{
    RETURN_HR_IF(E_INVALIDARG, !host || !host->toolbar || !host->address);
    return MakeTopLevel<UiaToolbar>(root, std::move(host), result);
}

HRESULT MakeUiaNavigationPane(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                              ComPtr<IRawElementProviderFragment>* result)
{
    RETURN_HR_IF(E_INVALIDARG, !host || !host->pane);
    return MakeTopLevel<UiaPane>(root, std::move(host), result);
}

HRESULT MakeUiaStatusBar(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                         ComPtr<IRawElementProviderFragment>* result)
{
    RETURN_HR_IF(E_INVALIDARG, !host || !host->status);
    return MakeTopLevel<UiaStatus>(root, std::move(host), result);
}

} // namespace te
