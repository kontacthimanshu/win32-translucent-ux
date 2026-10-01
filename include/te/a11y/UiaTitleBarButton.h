#pragma once

// UI Automation provider for the title bar's appearance picker button (T075; research
// R-09; UI contract §5): a Button named "Appearance and color" with Invoke and
// ExpandCollapse (Expanded while the popup is open). A child of UiaRoot.

#include <te/a11y/UiaRoot.h>

#include <functional>
#include <string>

namespace te
{

class UiaTitleBarButton final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IRawElementProviderSimple, IRawElementProviderFragment,
                                          IInvokeProvider, IExpandCollapseProvider>
{
  public:
    struct Host
    {
        std::wstring name = L"Appearance and color"; // IDS_A11Y_PICKER_NAME
        std::wstring acceleratorKey = L"Alt+Shift+C";
        std::function<RECT()> screenRect; // physical pixels
        std::function<bool()> isOpen;
        // Opens or closes the popup. Called from Invoke / Expand / Collapse; it should post
        // rather than act inside the UIA call.
        std::function<void()> requestToggle;
        // Keyboard focus (the F6 ring, T080).
        std::function<bool()> hasFocus;
        std::function<void()> setFocus;
    };

    UiaTitleBarButton() = default;
    HRESULT RuntimeClassInitialize(UiaRoot* root, Host host);

    // The popup opened or closed: raises the ExpandCollapseState property change.
    void NotifyOpenChanged(bool open);
    // WM_DESTROY: every later call fails with UIA_E_ELEMENTNOTAVAILABLE.
    void Disconnect();

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

    // IInvokeProvider
    IFACEMETHODIMP Invoke() override;

    // IExpandCollapseProvider
    IFACEMETHODIMP Expand() override;
    IFACEMETHODIMP Collapse() override;
    IFACEMETHODIMP get_ExpandCollapseState(ExpandCollapseState* state) override;

    static constexpr int kRuntimeIdPart = 1; // unique among the root's children

  private:
    [[nodiscard]] bool Available() const noexcept
    {
        return m_root && m_root->IsConnected();
    }
    [[nodiscard]] bool Open() const
    {
        return m_host.isOpen && m_host.isOpen();
    }

    Microsoft::WRL::ComPtr<UiaRoot> m_root;
    Host m_host;
};

} // namespace te
