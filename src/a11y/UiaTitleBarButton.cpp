#include <te/a11y/UiaTitleBarButton.h>

#include <UIAutomationCoreApi.h>

#include <wil/result.h>

#include <utility>

namespace te
{

namespace
{

constexpr HRESULT kNotAvailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);

void SetBool(VARIANT* value, bool flag)
{
    value->vt = VT_BOOL;
    value->boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE;
}

void SetString(VARIANT* value, const std::wstring& text)
{
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocString(text.c_str());
}

} // namespace

HRESULT UiaTitleBarButton::RuntimeClassInitialize(UiaRoot* root, Host host)
{
    RETURN_HR_IF_NULL(E_INVALIDARG, root);
    m_root = root;
    m_host = std::move(host);
    return S_OK;
}

void UiaTitleBarButton::Disconnect()
{
    if (!m_root)
    {
        return;
    }
    m_root.Reset();
    m_host = {};
    LOG_IF_FAILED(UiaDisconnectProvider(this));
}

void UiaTitleBarButton::NotifyOpenChanged(bool open)
{
    if (!Available() || !UiaClientsAreListening())
    {
        return;
    }
    VARIANT before{};
    before.vt = VT_I4;
    before.lVal = open ? ExpandCollapseState_Collapsed : ExpandCollapseState_Expanded;
    VARIANT after{};
    after.vt = VT_I4;
    after.lVal = open ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    LOG_IF_FAILED(UiaRaiseAutomationPropertyChangedEvent(
        this, UIA_ExpandCollapseExpandCollapseStatePropertyId, before, after));
}

// ---------------------------------------------------------------------------
// IRawElementProviderSimple
// ---------------------------------------------------------------------------

IFACEMETHODIMP UiaTitleBarButton::get_ProviderOptions(ProviderOptions* options)
{
    RETURN_HR_IF_NULL(E_POINTER, options);
    *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::GetPatternProvider(PATTERNID patternId, IUnknown** pattern)
{
    RETURN_HR_IF_NULL(E_POINTER, pattern);
    *pattern = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    if (patternId == UIA_InvokePatternId)
    {
        *pattern = static_cast<IInvokeProvider*>(this);
    }
    else if (patternId == UIA_ExpandCollapsePatternId)
    {
        *pattern = static_cast<IExpandCollapseProvider*>(this);
    }
    if (*pattern)
    {
        (*pattern)->AddRef();
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::GetPropertyValue(PROPERTYID propertyId, VARIANT* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    result->vt = VT_EMPTY;
    RETURN_HR_IF(kNotAvailable, !Available());
    switch (propertyId)
    {
    case UIA_ControlTypePropertyId:
        result->vt = VT_I4;
        result->lVal = UIA_ButtonControlTypeId;
        break;
    case UIA_NamePropertyId:
        SetString(result, m_host.name);
        break;
    case UIA_AutomationIdPropertyId:
        SetString(result, L"AppearanceButton");
        break;
    case UIA_AcceleratorKeyPropertyId:
        SetString(result, m_host.acceleratorKey);
        break;
    case UIA_IsEnabledPropertyId:
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        SetBool(result, true);
        break;
    case UIA_IsKeyboardFocusablePropertyId:
        SetBool(result, true); // in the F6 focus ring (T080)
        break;
    case UIA_HasKeyboardFocusPropertyId:
        SetBool(result, m_host.hasFocus && m_host.hasFocus());
        break;
    default:
        break;
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::get_HostRawElementProvider(IRawElementProviderSimple** host)
{
    RETURN_HR_IF_NULL(E_POINTER, host);
    *host = nullptr; // a fragment inside the window, not a window itself
    return S_OK;
}

// ---------------------------------------------------------------------------
// IRawElementProviderFragment
// ---------------------------------------------------------------------------

IFACEMETHODIMP UiaTitleBarButton::Navigate(NavigateDirection direction, IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    switch (direction)
    {
    case NavigateDirection_Parent:
        *result = m_root.Get();
        (*result)->AddRef();
        return S_OK;
    case NavigateDirection_NextSibling:
    case NavigateDirection_PreviousSibling:
        return m_root->Sibling(this, direction, result);
    default:
        return S_OK; // no children
    }
}

IFACEMETHODIMP UiaTitleBarButton::GetRuntimeId(SAFEARRAY** runtimeId)
{
    RETURN_HR_IF_NULL(E_POINTER, runtimeId);
    *runtimeId = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    int ids[] = {UiaAppendRuntimeId, kRuntimeIdPart};
    *runtimeId = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(std::size(ids)));
    RETURN_IF_NULL_ALLOC(*runtimeId);
    for (LONG i = 0; i < static_cast<LONG>(std::size(ids)); ++i)
    {
        RETURN_IF_FAILED(SafeArrayPutElement(*runtimeId, &i, &ids[i]));
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::get_BoundingRectangle(UiaRect* rect)
{
    RETURN_HR_IF_NULL(E_POINTER, rect);
    *rect = UiaRect{};
    RETURN_HR_IF(kNotAvailable, !Available());
    if (m_host.screenRect)
    {
        const RECT screen = m_host.screenRect();
        *rect = UiaRect{static_cast<double>(screen.left), static_cast<double>(screen.top),
                        static_cast<double>(screen.right - screen.left),
                        static_cast<double>(screen.bottom - screen.top)};
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::GetEmbeddedFragmentRoots(SAFEARRAY** roots)
{
    RETURN_HR_IF_NULL(E_POINTER, roots);
    *roots = nullptr;
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::SetFocus()
{
    RETURN_HR_IF(kNotAvailable, !Available());
    if (m_host.setFocus)
    {
        m_host.setFocus();
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::get_FragmentRoot(IRawElementProviderFragmentRoot** root)
{
    RETURN_HR_IF_NULL(E_POINTER, root);
    *root = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    *root = m_root.Get();
    (*root)->AddRef();
    return S_OK;
}

// ---------------------------------------------------------------------------
// IInvokeProvider, IExpandCollapseProvider
// ---------------------------------------------------------------------------

IFACEMETHODIMP UiaTitleBarButton::Invoke()
{
    RETURN_HR_IF(kNotAvailable, !Available());
    if (m_host.requestToggle)
    {
        m_host.requestToggle();
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::Expand()
{
    RETURN_HR_IF(kNotAvailable, !Available());
    if (!Open() && m_host.requestToggle)
    {
        m_host.requestToggle();
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::Collapse()
{
    RETURN_HR_IF(kNotAvailable, !Available());
    if (Open() && m_host.requestToggle)
    {
        m_host.requestToggle();
    }
    return S_OK;
}

IFACEMETHODIMP UiaTitleBarButton::get_ExpandCollapseState(ExpandCollapseState* state)
{
    RETURN_HR_IF_NULL(E_POINTER, state);
    RETURN_HR_IF(kNotAvailable, !Available());
    *state = Open() ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    return S_OK;
}

} // namespace te
