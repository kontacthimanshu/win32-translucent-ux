#include <te/a11y/UiaRoot.h>

#include <UIAutomationCoreApi.h>

#include <wil/com.h>
#include <wil/result.h>

#include <algorithm>
#include <utility>

namespace te
{

namespace
{

// UIA_E_ELEMENTNOTAVAILABLE is a plain integer literal; wil wants an HRESULT.
constexpr HRESULT kNotAvailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);

template <class T> HRESULT Return(T* object, T** result)
{
    *result = object;
    if (object)
    {
        object->AddRef();
    }
    return S_OK;
}

bool Contains(const UiaRect& rect, double x, double y)
{
    return x >= rect.left && y >= rect.top && x < rect.left + rect.width && y < rect.top + rect.height;
}

} // namespace

HRESULT UiaRoot::RuntimeClassInitialize(HWND hwnd, ChildrenProvider children, FocusProvider focus,
                                        HwndOverride hwndOverride)
{
    RETURN_HR_IF(E_INVALIDARG, !hwnd);
    m_hwnd = hwnd;
    m_children = std::move(children);
    m_focus = std::move(focus);
    m_hwndOverride = std::move(hwndOverride);
    return S_OK;
}

void UiaRoot::Disconnect()
{
    if (!m_hwnd)
    {
        return;
    }
    m_hwnd = nullptr;
    m_children = nullptr;
    m_focus = nullptr;
    m_hwndOverride = nullptr;
    // Tells clients to drop their references to this provider (and its fragments).
    LOG_IF_FAILED(UiaDisconnectProvider(this));
}

std::vector<IRawElementProviderFragment*> UiaRoot::Children() const
{
    std::vector<IRawElementProviderFragment*> children = m_children ? m_children() : decltype(children){};
    std::erase(children, nullptr);
    return children;
}

HRESULT UiaRoot::Sibling(IRawElementProviderFragment* child, NavigateDirection direction,
                         IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    const auto children = Children();
    const auto it = std::find(children.begin(), children.end(), child);
    if (it == children.end())
    {
        return S_OK;
    }
    if (direction == NavigateDirection_NextSibling && it + 1 != children.end())
    {
        return Return(*(it + 1), result);
    }
    if (direction == NavigateDirection_PreviousSibling && it != children.begin())
    {
        return Return(*(it - 1), result);
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// IRawElementProviderSimple
// ---------------------------------------------------------------------------

IFACEMETHODIMP UiaRoot::get_ProviderOptions(ProviderOptions* options)
{
    RETURN_HR_IF_NULL(E_POINTER, options);
    // Calls arrive on the UI thread (COM threading), which owns the components.
    *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

IFACEMETHODIMP UiaRoot::GetPatternProvider(PATTERNID, IUnknown** pattern)
{
    RETURN_HR_IF_NULL(E_POINTER, pattern);
    *pattern = nullptr;
    return S_OK;
}

IFACEMETHODIMP UiaRoot::GetPropertyValue(PROPERTYID, VARIANT* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    result->vt = VT_EMPTY; // the host provider supplies the window's name and control type
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    return S_OK;
}

IFACEMETHODIMP UiaRoot::get_HostRawElementProvider(IRawElementProviderSimple** host)
{
    RETURN_HR_IF_NULL(E_POINTER, host);
    *host = nullptr;
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    return UiaHostProviderFromHwnd(m_hwnd, host);
}

// ---------------------------------------------------------------------------
// IRawElementProviderFragment
// ---------------------------------------------------------------------------

IFACEMETHODIMP UiaRoot::Navigate(NavigateDirection direction, IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    // Parent and siblings of the root come from the host window.
    if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)
    {
        const auto children = Children();
        if (!children.empty())
        {
            return Return(direction == NavigateDirection_FirstChild ? children.front() : children.back(),
                          result);
        }
    }
    return S_OK;
}

IFACEMETHODIMP UiaRoot::GetRuntimeId(SAFEARRAY** runtimeId)
{
    RETURN_HR_IF_NULL(E_POINTER, runtimeId);
    *runtimeId = nullptr; // the host window's runtime ID is used
    return S_OK;
}

IFACEMETHODIMP UiaRoot::get_BoundingRectangle(UiaRect* rect)
{
    RETURN_HR_IF_NULL(E_POINTER, rect);
    *rect = UiaRect{}; // the host window's rectangle is used
    return S_OK;
}

IFACEMETHODIMP UiaRoot::GetEmbeddedFragmentRoots(SAFEARRAY** roots)
{
    RETURN_HR_IF_NULL(E_POINTER, roots);
    *roots = nullptr;
    return S_OK;
}

IFACEMETHODIMP UiaRoot::SetFocus()
{
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    ::SetFocus(m_hwnd);
    return S_OK;
}

IFACEMETHODIMP UiaRoot::get_FragmentRoot(IRawElementProviderFragmentRoot** root)
{
    RETURN_HR_IF_NULL(E_POINTER, root);
    return Return(static_cast<IRawElementProviderFragmentRoot*>(this), root);
}

// ---------------------------------------------------------------------------
// IRawElementProviderFragmentRoot
// ---------------------------------------------------------------------------

IFACEMETHODIMP UiaRoot::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    for (IRawElementProviderFragment* child : Children())
    {
        UiaRect bounds{};
        if (FAILED(child->get_BoundingRectangle(&bounds)) || !Contains(bounds, x, y))
        {
            continue;
        }
        // The child may know a deeper element (a row in the file list).
        if (wil::com_ptr<IUiaFragmentExtension> extension = wil::try_com_query<IUiaFragmentExtension>(child))
        {
            wil::com_ptr<IRawElementProviderFragment> deeper;
            if (SUCCEEDED(extension->HitTest(x, y, &deeper)) && deeper)
            {
                *result = deeper.detach();
                return S_OK;
            }
        }
        return Return(child, result);
    }
    return S_OK; // the root itself (UIA uses the host)
}

IFACEMETHODIMP UiaRoot::GetFocus(IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    IRawElementProviderFragment* const focused = m_focus ? m_focus() : nullptr;
    if (!focused)
    {
        return S_OK; // the window itself
    }
    if (wil::com_ptr<IUiaFragmentExtension> extension = wil::try_com_query<IUiaFragmentExtension>(focused))
    {
        wil::com_ptr<IRawElementProviderFragment> deeper;
        if (SUCCEEDED(extension->FocusedDescendant(&deeper)) && deeper)
        {
            *result = deeper.detach();
            return S_OK;
        }
    }
    return Return(focused, result);
}

IFACEMETHODIMP UiaRoot::GetOverrideProviderForHwnd(HWND child, IRawElementProviderSimple** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !m_hwnd);
    if (m_hwndOverride)
    {
        *result = m_hwndOverride(child).Detach();
    }
    return S_OK;
}

} // namespace te
