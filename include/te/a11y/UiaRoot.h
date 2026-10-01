#pragma once

// The UI Automation fragment root of the main window (T074; research R-09; UI contract
// §5). The window answers WM_GETOBJECT(UiaRootObjectId) with this object; its children
// are the providers of the custom-drawn components (T075–T078), in tree order.

#include <windows.h>

#include <objbase.h>

#include <UIAutomation.h>

#include <wrl/implements.h>

#include <functional>
#include <vector>

namespace te
{

// Optional app-defined interface of a child fragment, for what the root delegates.
MIDL_INTERFACE("6F4D8E2B-3C1A-4B7E-9D55-2A8C0E7F1B63")
IUiaFragmentExtension : public IUnknown
{
  public:
    // The deepest element at a screen point (physical pixels) within this fragment, or the
    // fragment itself; S_FALSE with null when the point is outside it.
    virtual HRESULT STDMETHODCALLTYPE HitTest(double x, double y, IRawElementProviderFragment** result) = 0;
    // The descendant with the keyboard focus, or the fragment itself.
    virtual HRESULT STDMETHODCALLTYPE FocusedDescendant(IRawElementProviderFragment * *result) = 0;
};

class UiaRoot final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IRawElementProviderSimple, IRawElementProviderFragment,
                                          IRawElementProviderFragmentRoot, IRawElementProviderHwndOverride>
{
  public:
    // The children in tree order (non-owning; null entries are skipped): title-bar button,
    // toolbar, navigation pane, file list, status bar.
    using ChildrenProvider = std::function<std::vector<IRawElementProviderFragment*>()>;
    // The child with the keyboard focus, or null when the window itself has it.
    using FocusProvider = std::function<IRawElementProviderFragment*()>;

    UiaRoot() = default;
    // A fragment that takes the place of a child window in the tree (T078: the address
    // field's EDIT while editing), or null to leave the window where it is.
    using HwndOverride = std::function<Microsoft::WRL::ComPtr<IRawElementProviderSimple>(HWND child)>;

    HRESULT RuntimeClassInitialize(HWND hwnd, ChildrenProvider children, FocusProvider focus,
                                   HwndOverride hwndOverride = {});

    // WM_DESTROY: every later call fails with UIA_E_ELEMENTNOTAVAILABLE, and clients are
    // told to release the provider (UiaDisconnectProvider).
    void Disconnect();
    [[nodiscard]] bool IsConnected() const noexcept
    {
        return m_hwnd != nullptr;
    }
    [[nodiscard]] HWND Hwnd() const noexcept
    {
        return m_hwnd;
    }

    // For children: their NextSibling / PreviousSibling in the root's order.
    HRESULT Sibling(IRawElementProviderFragment* child, NavigateDirection direction,
                    IRawElementProviderFragment** result);

    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override;
    IFACEMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** pattern) override;
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override;
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override;

    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override;
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override;
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override;
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override;
    IFACEMETHODIMP SetFocus() override;
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override;

    // IRawElementProviderFragmentRoot
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y,
                                            IRawElementProviderFragment** result) override;
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** result) override;

    // IRawElementProviderHwndOverride: without it UIA would also list such a child window
    // under the main window, so it would appear twice.
    IFACEMETHODIMP GetOverrideProviderForHwnd(HWND child, IRawElementProviderSimple** result) override;

  private:
    [[nodiscard]] std::vector<IRawElementProviderFragment*> Children() const;

    HWND m_hwnd = nullptr;
    ChildrenProvider m_children;
    FocusProvider m_focus;
    HwndOverride m_hwndOverride;
};

} // namespace te
